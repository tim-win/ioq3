/*
 * g_titan.c -- Titan hitbox child entity management
 *
 * Spawns/despawns child entities around a titan player for per-part
 * damage detection. Each child has CONTENTS_TITANPART so weapon traces
 * hit them. The parent titan uses CONTENTS_PLAYERCLIP instead of
 * CONTENTS_BODY so shots pass through to children.
 */

#include "g_local.h"
#include "bg_titan_parts.h"

static vmCvar_t	titan_damage_log;

/*
 * TitanPartPain -- pain callback for child hitbox entities.
 * Fires on EVERY damage event (unlike die which only fires at health<=0).
 * Routes damage to parent player with the part's multiplier applied.
 */
static void TitanPartPain( gentity_t *self, gentity_t *attacker, int damage ) {
	gentity_t		*parent;
	const titanPartDef_t *def;
	int				scaled;

	if ( !self->parent || !self->parent->client ) {
		return;
	}

	parent = self->parent;
	def = &titanParts[self->titanPartType];
	scaled = (int)( damage * def->damageMultiplier );
	if ( scaled < 1 ) {
		scaled = 1;
	}

	if ( titan_damage_log.integer ) {
		G_Printf( "TITAN HIT: %s -> %s [%s] raw=%d mult=%.1f final=%d\n",
			attacker && attacker->client ? attacker->client->pers.netname : "world",
			parent->client->pers.netname,
			def->name,
			damage, def->damageMultiplier, scaled );
	}

	// Route damage to parent
	G_Damage( parent, NULL, attacker, NULL, NULL, scaled,
			  DAMAGE_NO_ARMOR, MOD_UNKNOWN );

	// Keep child alive — it has no health pool of its own
	self->health = 999;
}

/*
 * RotatePointAroundYaw -- rotate a point's X/Y by yaw angle (degrees).
 * Input: offset = (forward, left, up) in local space
 * Output: result = world-space offset
 */
static void RotatePointAroundYaw( const vec3_t offset, float yaw, vec3_t result ) {
	float	rad, s, c;

	rad = DEG2RAD( yaw );
	s = sin( rad );
	c = cos( rad );

	// forward (offset[0]) maps to world X/Y via cos/sin of yaw
	// left (offset[1]) maps to world X/Y perpendicular to forward
	result[0] = offset[0] * c - offset[1] * s;
	result[1] = offset[0] * s + offset[1] * c;
	result[2] = offset[2];
}

/*
 * G_SpawnTitanParts -- create child hitbox entities for all parts.
 * Called when a player enters titan mode.
 */
void G_SpawnTitanParts( gentity_t *ent ) {
	gclient_t			*client = ent->client;
	int					i;
	gentity_t			*child;
	const titanPartDef_t *def;
	vec3_t				worldOffset;
	float				yaw;

	if ( !client ) {
		return;
	}

	// Despawn any existing parts first (safety)
	G_DespawnTitanParts( ent );

	// Reset animation state
	client->titanCrouchFrac = 0.0f;
	client->titanWalkPhase = 0.0f;

	yaw = ent->client->ps.viewangles[YAW];

	for ( i = 0; i < NUM_TITAN_PARTS; i++ ) {
		def = &titanParts[i];

		child = G_Spawn();
		if ( !child ) {
			G_Printf( "G_SpawnTitanParts: G_Spawn failed for part %s\n", def->name );
			break;
		}

		child->classname = "titan_part";
		child->titanPartType = def->type;
		child->parent = ent;

		// Set bounding box from part definition
		VectorCopy( def->mins, child->r.mins );
		VectorCopy( def->maxs, child->r.maxs );

		// Position: parent origin + yaw-rotated offset
		RotatePointAroundYaw( def->offset, yaw, worldOffset );
		VectorAdd( ent->r.currentOrigin, worldOffset, child->r.currentOrigin );
		VectorCopy( child->r.currentOrigin, child->s.pos.trBase );
		child->s.pos.trType = TR_STATIONARY;

		// Collision: hit by weapon traces, ignored by movement
		child->r.contents = CONTENTS_TITANPART;
		child->r.ownerNum = ent->s.number;  // prevent self-damage
		child->clipmask = 0;  // child doesn't move, no clipmask needed

		// Damage handling
		child->takedamage = qtrue;
		child->health = 999;  // never dies on its own
		child->pain = TitanPartPain;

		// Entity state for client-side rendering
		child->s.eType = ET_TITAN_PART;
		child->s.generic1 = def->type;  // tell cgame which part this is
		child->s.otherEntityNum = ent->s.number;  // tell cgame which player owns this part

		// Always send to all clients (PVS check can miss nearby entities)
		child->r.svFlags |= SVF_BROADCAST;

		// Link into the world
		trap_LinkEntity( child );

		client->titanParts[i] = child;
	}

	client->numTitanParts = i;

	// Change parent contents: PLAYERCLIP for movement collision,
	// remove BODY so weapon traces pass through to children
	ent->r.contents = CONTENTS_PLAYERCLIP;
	trap_LinkEntity( ent );
}

/*
 * G_DespawnTitanParts -- free all child hitbox entities.
 * Called when a player exits titan mode, disconnects, or dies.
 */
void G_DespawnTitanParts( gentity_t *ent ) {
	gclient_t	*client = ent->client;
	int			i;

	if ( !client ) {
		return;
	}

	for ( i = 0; i < client->numTitanParts; i++ ) {
		if ( client->titanParts[i] ) {
			G_FreeEntity( client->titanParts[i] );
			client->titanParts[i] = NULL;
		}
	}
	client->numTitanParts = 0;

	// Restore parent contents to normal BODY
	if ( ent->inuse ) {
		ent->r.contents = CONTENTS_BODY;
		trap_LinkEntity( ent );
	}
}

// Crouch lerp speed: 0 to 1 in ~125ms
#define TITAN_CROUCH_RATE		8.0f

// Walk animation tuning
#define TITAN_WALK_PHASE_RATE	0.008f	// radians per unit-speed per millisecond
#define TITAN_WALK_LEG_AMP		40.0f	// max forward/back leg swing (units)
#define TITAN_WALK_ARM_AMP		25.0f	// max forward/back arm counter-swing

/*
 * TitanCrouchScale -- compute Z scale factor from crouch fraction.
 * 0.0 crouch fraction = 1.0 (full height), 1.0 = crouchRatio (~0.627)
 */
static float TitanCrouchScale( float crouchFrac ) {
	float standH = (float)( TITAN_HEIGHT - TITAN_MINS_Z );	// 370
	float crouchH = (float)( TITAN_CROUCH_HEIGHT - TITAN_MINS_Z );	// 232
	float ratio = crouchH / standH;
	return 1.0f - crouchFrac * ( 1.0f - ratio );
}

/*
 * G_UpdateTitanParts -- reposition all children to track parent.
 * Called every server frame for titan players.
 * Handles crouch compression and walk animation.
 */
void G_UpdateTitanParts( gentity_t *ent ) {
	gclient_t			*client = ent->client;
	int					i;
	gentity_t			*child;
	const titanPartDef_t *def;
	vec3_t				adjOffset, worldOffset;
	float				yaw, dt, crouchTarget, crouchScale;
	float				speed, walkAmp, walkOffset;

	if ( !client || client->numTitanParts == 0 ) {
		return;
	}

	dt = ( level.time - level.previousTime ) * 0.001f;
	if ( dt <= 0.0f ) {
		dt = 0.05f;	// fallback to 20fps
	}

	yaw = client->ps.viewangles[YAW];

	// --- Crouch lerp ---
	crouchTarget = ( client->ps.pm_flags & PMF_DUCKED ) ? 1.0f : 0.0f;
	if ( client->titanCrouchFrac < crouchTarget ) {
		client->titanCrouchFrac += TITAN_CROUCH_RATE * dt;
		if ( client->titanCrouchFrac > crouchTarget )
			client->titanCrouchFrac = crouchTarget;
	} else if ( client->titanCrouchFrac > crouchTarget ) {
		client->titanCrouchFrac -= TITAN_CROUCH_RATE * dt;
		if ( client->titanCrouchFrac < crouchTarget )
			client->titanCrouchFrac = crouchTarget;
	}
	crouchScale = TitanCrouchScale( client->titanCrouchFrac );

	// --- Walk animation phase ---
	speed = sqrt( client->ps.velocity[0] * client->ps.velocity[0]
				+ client->ps.velocity[1] * client->ps.velocity[1] );
	client->titanWalkPhase += speed * dt * TITAN_WALK_PHASE_RATE;
	if ( client->titanWalkPhase > 2.0f * M_PI ) {
		client->titanWalkPhase -= 2.0f * M_PI;
	}
	// Amplitude normalized by default run speed, clamped to [0,1]
	walkAmp = speed / 320.0f;
	if ( walkAmp > 1.0f ) walkAmp = 1.0f;

	for ( i = 0; i < client->numTitanParts; i++ ) {
		child = client->titanParts[i];
		if ( !child || !child->inuse ) {
			continue;
		}

		def = &titanParts[child->titanPartType];

		// Start from table offset
		VectorCopy( def->offset, adjOffset );

		// Apply crouch Z compression: remap offset Z within the titan's height
		adjOffset[2] = TITAN_MINS_Z + ( def->offset[2] - TITAN_MINS_Z ) * crouchScale;

		// Apply walk animation (forward/back oscillation)
		walkOffset = 0;
		if ( def->type == TITAN_PART_LEG_L ) {
			walkOffset = sin( client->titanWalkPhase ) * TITAN_WALK_LEG_AMP * walkAmp;
		} else if ( def->type == TITAN_PART_LEG_R ) {
			walkOffset = -sin( client->titanWalkPhase ) * TITAN_WALK_LEG_AMP * walkAmp;
		} else if ( def->type == TITAN_PART_ARM_L ) {
			walkOffset = -sin( client->titanWalkPhase ) * TITAN_WALK_ARM_AMP * walkAmp;
		} else if ( def->type == TITAN_PART_ARM_R ) {
			walkOffset = sin( client->titanWalkPhase ) * TITAN_WALK_ARM_AMP * walkAmp;
		}
		adjOffset[0] += walkOffset;

		// Rotate and position in world space
		RotatePointAroundYaw( adjOffset, yaw, worldOffset );
		VectorAdd( ent->r.currentOrigin, worldOffset, child->r.currentOrigin );
		VectorCopy( child->r.currentOrigin, child->s.pos.trBase );

		// Apply crouch Z compression to bounding box
		VectorCopy( def->mins, child->r.mins );
		VectorCopy( def->maxs, child->r.maxs );
		child->r.mins[2] *= crouchScale;
		child->r.maxs[2] *= crouchScale;

		// Communicate crouch fraction to client for debug rendering (0-100)
		child->s.frame = (int)( client->titanCrouchFrac * 100.0f );

		trap_LinkEntity( child );
	}
}

/*
 * G_EnterTitanMode -- transition a player into titan mode.
 * Sets health, spawns hitbox parts, notifies client.
 */
void G_EnterTitanMode( gentity_t *ent ) {
	if ( !ent->client || ent->client->titanMode ) {
		return;
	}

	ent->client->titanMode = qtrue;
	ent->health = TITAN_HEALTH;
	ent->client->ps.stats[STAT_HEALTH] = TITAN_HEALTH;
	ent->client->ps.stats[STAT_MAX_HEALTH] = TITAN_HEALTH;
	G_SpawnTitanParts( ent );
	trap_SendServerCommand( ent - g_entities, "print \"TITAN MODE ACTIVATED\n\"" );
}

/*
 * G_ExitTitanMode -- transition a player out of titan mode.
 * Despawns hitbox parts, restores pilot health cap.
 * Does NOT teleport or set position — caller handles that.
 */
void G_ExitTitanMode( gentity_t *ent ) {
	if ( !ent->client || !ent->client->titanMode ) {
		return;
	}

	G_DespawnTitanParts( ent );
	ent->client->titanMode = qfalse;
	ent->client->ps.stats[STAT_MAX_HEALTH] = ent->client->pers.maxHealth;
	if ( ent->health > ent->client->ps.stats[STAT_MAX_HEALTH] ) {
		ent->health = ent->client->ps.stats[STAT_MAX_HEALTH];
		ent->client->ps.stats[STAT_HEALTH] = ent->health;
	}
	trap_SendServerCommand( ent - g_entities, "print \"TITAN MODE DEACTIVATED\n\"" );
}

/*
 * G_FindEjectPosition -- find a clear position to eject a pilot to.
 * Tries 4 cardinal directions (relative to yaw), then straight up,
 * then falls back to titan origin. Returns qtrue if a good spot was found.
 */
qboolean G_FindEjectPosition( gentity_t *ent, vec3_t result ) {
	float		yaw;
	int			i;
	float		dist;
	vec3_t		start, end, mins, maxs;
	trace_t		tr;

	yaw = ent->client->ps.viewangles[YAW];
	dist = TITAN_WIDTH + PLAYER_WIDTH + 16;

	VectorSet( mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z );
	VectorSet( maxs, PLAYER_WIDTH, PLAYER_WIDTH, DEFAULT_HEIGHT );

	// Try 4 cardinal directions relative to player yaw
	for ( i = 0; i < 4; i++ ) {
		float angle = DEG2RAD( yaw + i * 90.0f );
		float dx = cos( angle ) * dist;
		float dy = sin( angle ) * dist;

		VectorCopy( ent->r.currentOrigin, start );
		start[0] += dx;
		start[1] += dy;

		// Trace from proposed position down to find floor
		VectorCopy( start, end );
		end[2] -= 128;

		trap_Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID );

		if ( tr.fraction < 1.0f && !tr.startsolid && !tr.allsolid ) {
			// Found a floor — place player on it
			VectorCopy( tr.endpos, result );
			result[2] += 1;  // slight offset above ground
			return qtrue;
		}
	}

	// Try straight up
	VectorCopy( ent->r.currentOrigin, start );
	start[2] += TITAN_HEIGHT + 32;
	VectorCopy( start, end );
	end[2] -= 256;
	trap_Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_PLAYERSOLID );
	if ( tr.fraction < 1.0f && !tr.startsolid && !tr.allsolid ) {
		VectorCopy( tr.endpos, result );
		result[2] += 1;
		return qtrue;
	}

	// Fallback: titan origin (will telefrag but at least doesn't fail)
	VectorCopy( ent->r.currentOrigin, result );
	return qfalse;
}

/*
 * TitanPodThink -- per-frame think for descending titan pod.
 * Traces downward to detect ground. On landing: snap to ground,
 * go stationary, become solid, notify owner.
 */
static void TitanPodThink( gentity_t *self ) {
	trace_t		tr;
	vec3_t		prevPos, currentPos;

	self->nextthink = level.time + FRAMETIME;

	// Previous position (where we were last frame)
	VectorCopy( self->r.currentOrigin, prevPos );

	// Evaluate current position based on trajectory
	BG_EvaluateTrajectory( &self->s.pos, level.time, currentPos );

	// Trace from previous position to current — detects ground passage
	trap_Trace( &tr, prevPos, self->r.mins, self->r.maxs,
				currentPos, self->s.number, MASK_SOLID );

	if ( tr.fraction < 1.0f && !tr.allsolid ) {
		// Hit ground — land at contact point
		VectorCopy( tr.endpos, self->s.pos.trBase );
		self->s.pos.trType = TR_STATIONARY;
		self->s.pos.trTime = level.time;
		VectorClear( self->s.pos.trDelta );

		VectorCopy( tr.endpos, self->r.currentOrigin );
		self->r.contents = CONTENTS_SOLID;
		trap_LinkEntity( self );

		// Notify owner
		if ( self->parent && self->parent->client ) {
			trap_SendServerCommand( self->parent - g_entities,
				"print \"Titan ready. Use /embark to enter.\n\"" );
		}

		// Stop thinking — pod is landed
		self->think = NULL;
		self->nextthink = 0;
		return;
	}

	// Update position
	VectorCopy( currentPos, self->r.currentOrigin );
	trap_LinkEntity( self );
}

/*
 * Cmd_CallTitan_f -- spawn a titan pod entity that descends from the sky.
 */
void Cmd_CallTitan_f( gentity_t *ent ) {
	gentity_t	*pod;
	vec3_t		spawnPos;

	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Can't call titan while dead\n\"" );
		return;
	}
	if ( ent->client->titanMode ) {
		trap_SendServerCommand( ent - g_entities, "print \"Already in titan mode\n\"" );
		return;
	}
	if ( ent->client->titanPod ) {
		trap_SendServerCommand( ent - g_entities, "print \"Titan pod already active\n\"" );
		return;
	}
	if ( ent->client->titanCooldownTime > level.time ) {
		trap_SendServerCommand( ent - g_entities,
			va( "print \"Titan on cooldown: %d seconds\n\"",
				( ent->client->titanCooldownTime - level.time ) / 1000 + 1 ) );
		return;
	}

	// Spawn pod high above player
	VectorCopy( ent->r.currentOrigin, spawnPos );
	spawnPos[2] += TITAN_POD_DROP_HEIGHT;

	pod = G_Spawn();
	if ( !pod ) {
		trap_SendServerCommand( ent - g_entities, "print \"Failed to spawn titan pod\n\"" );
		return;
	}

	pod->classname = "titan_pod";
	pod->parent = ent;
	pod->s.eType = ET_GENERAL;
	pod->s.generic1 = TITAN_POD_TAG;

	// Bounding box
	VectorSet( pod->r.mins, -TITAN_POD_WIDTH, -TITAN_POD_WIDTH, 0 );
	VectorSet( pod->r.maxs, TITAN_POD_WIDTH, TITAN_POD_WIDTH, TITAN_POD_HEIGHT );

	// Trajectory: linear descent
	VectorCopy( spawnPos, pod->s.pos.trBase );
	pod->s.pos.trType = TR_LINEAR;
	pod->s.pos.trTime = level.time;
	VectorSet( pod->s.pos.trDelta, 0, 0, -TITAN_POD_SPEED );

	VectorCopy( spawnPos, pod->r.currentOrigin );

	// Not solid while descending — becomes solid on landing
	pod->r.contents = 0;
	pod->clipmask = MASK_SOLID;
	pod->r.svFlags |= SVF_BROADCAST;

	// Think function for ground detection
	pod->think = TitanPodThink;
	pod->nextthink = level.time + FRAMETIME;

	trap_LinkEntity( pod );

	ent->client->titanPod = pod;

	trap_SendServerCommand( ent - g_entities, "print \"Titan inbound!\n\"" );
}

/*
 * Cmd_Embark_f -- enter a landed titan pod.
 */
void Cmd_Embark_f( gentity_t *ent ) {
	gentity_t	*pod;
	vec3_t		diff;
	float		dist;

	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Can't embark while dead\n\"" );
		return;
	}
	if ( ent->client->titanMode ) {
		trap_SendServerCommand( ent - g_entities, "print \"Already in titan mode\n\"" );
		return;
	}

	pod = ent->client->titanPod;
	if ( !pod || !pod->inuse ) {
		trap_SendServerCommand( ent - g_entities, "print \"No titan pod available\n\"" );
		ent->client->titanPod = NULL;
		return;
	}

	// Check pod has landed (TR_STATIONARY means landed)
	if ( pod->s.pos.trType != TR_STATIONARY ) {
		trap_SendServerCommand( ent - g_entities, "print \"Titan pod hasn't landed yet\n\"" );
		return;
	}

	// Check distance
	VectorSubtract( pod->r.currentOrigin, ent->r.currentOrigin, diff );
	dist = VectorLength( diff );
	if ( dist > TITAN_EMBARK_RANGE ) {
		trap_SendServerCommand( ent - g_entities,
			va( "print \"Too far from titan pod (%.0f / %d)\n\"", dist, TITAN_EMBARK_RANGE ) );
		return;
	}

	// Teleport player to pod position
	VectorCopy( pod->r.currentOrigin, ent->client->ps.origin );
	VectorCopy( pod->r.currentOrigin, ent->r.currentOrigin );
	VectorCopy( pod->r.currentOrigin, ent->s.pos.trBase );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;

	// Free the pod
	G_FreeEntity( pod );
	ent->client->titanPod = NULL;

	// Enter titan mode
	G_EnterTitanMode( ent );
}

/*
 * Cmd_Disembark_f -- exit titan mode as a pilot.
 */
void Cmd_Disembark_f( gentity_t *ent ) {
	vec3_t	ejectPos;

	if ( ent->client->ps.stats[STAT_HEALTH] <= 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Can't disembark while dead\n\"" );
		return;
	}
	if ( !ent->client->titanMode ) {
		trap_SendServerCommand( ent - g_entities, "print \"Not in titan mode\n\"" );
		return;
	}

	G_FindEjectPosition( ent, ejectPos );
	G_ExitTitanMode( ent );

	// Teleport to eject position
	VectorCopy( ejectPos, ent->client->ps.origin );
	VectorCopy( ejectPos, ent->r.currentOrigin );
	VectorCopy( ejectPos, ent->s.pos.trBase );
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;

	// Set pilot health
	ent->health = TITAN_PILOT_HEALTH;
	ent->client->ps.stats[STAT_HEALTH] = TITAN_PILOT_HEALTH;

	trap_LinkEntity( ent );
}

/*
 * G_TitanDestroyed -- called when a titan reaches 0 HP.
 * Awards kill credit, ejects pilot alive, starts cooldown.
 */
void G_TitanDestroyed( gentity_t *self, gentity_t *attacker ) {
	vec3_t		ejectPos;
	gentity_t	*obit;

	// Award kill credit
	if ( attacker && attacker->client && attacker != self ) {
		AddScore( attacker, self->r.currentOrigin, 1 );
	}

	// Broadcast obituary: "X destroyed Y's titan"
	obit = G_TempEntity( self->r.currentOrigin, EV_OBITUARY );
	obit->s.eventParm = MOD_UNKNOWN;
	obit->s.otherEntityNum = self->s.number;
	obit->s.otherEntityNum2 = attacker ? attacker->s.number : ENTITYNUM_WORLD;
	obit->r.svFlags = SVF_BROADCAST;

	G_LogPrintf( "TitanDestroyed: %s killed %s's titan\n",
		attacker && attacker->client ? attacker->client->pers.netname : "world",
		self->client->pers.netname );

	// Find eject position before exiting titan mode
	G_FindEjectPosition( self, ejectPos );

	// Exit titan mode (despawns parts, restores normal state)
	G_ExitTitanMode( self );

	// Teleport pilot to eject position
	VectorCopy( ejectPos, self->client->ps.origin );
	VectorCopy( ejectPos, self->r.currentOrigin );
	VectorCopy( ejectPos, self->s.pos.trBase );
	self->client->ps.eFlags ^= EF_TELEPORT_BIT;

	// Set pilot health
	self->health = TITAN_PILOT_HEALTH;
	self->client->ps.stats[STAT_HEALTH] = TITAN_PILOT_HEALTH;

	// Start cooldown
	self->client->titanCooldownTime = level.time + TITAN_COOLDOWN;

	trap_SendServerCommand( self - g_entities,
		"print \"Titan destroyed! Pilot ejected.\n\"" );

	trap_LinkEntity( self );
}

/*
 * Cmd_TitanParts_f -- debug command: print all child entity state.
 */
void Cmd_TitanParts_f( gentity_t *ent ) {
	gclient_t			*client = ent->client;
	int					i;
	gentity_t			*child;
	const titanPartDef_t *def;

	if ( !client ) {
		return;
	}

	if ( !client->titanMode ) {
		trap_SendServerCommand( ent - g_entities,
			"print \"Not in titan mode\n\"" );
		return;
	}

	trap_SendServerCommand( ent - g_entities,
		va( "print \"Titan parts: %d children\n\"", client->numTitanParts ) );

	for ( i = 0; i < client->numTitanParts; i++ ) {
		child = client->titanParts[i];
		if ( !child || !child->inuse ) {
			trap_SendServerCommand( ent - g_entities,
				va( "print \"  [%d] FREED\n\"", i ) );
			continue;
		}

		def = &titanParts[child->titanPartType];
		trap_SendServerCommand( ent - g_entities,
			va( "print \"  [%d] %s ent#%d pos(%.0f %.0f %.0f) "
				"mins(%.0f %.0f %.0f) maxs(%.0f %.0f %.0f) dmg=%.1fx\n\"",
				i, def->name, child->s.number,
				child->r.currentOrigin[0], child->r.currentOrigin[1], child->r.currentOrigin[2],
				child->r.mins[0], child->r.mins[1], child->r.mins[2],
				child->r.maxs[0], child->r.maxs[1], child->r.maxs[2],
				def->damageMultiplier ) );
	}
}

/*
 * G_InitTitanCvars -- register titan-related cvars.
 * Call from G_InitGame.
 */
void G_InitTitanCvars( void ) {
	trap_Cvar_Register( &titan_damage_log, "titan_damage_log", "0", 0 );
}
