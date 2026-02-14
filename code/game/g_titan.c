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
 * TitanPartDie -- damage callback for child hitbox entities.
 * Routes damage to parent player with the part's multiplier applied.
 */
static void TitanPartDie( gentity_t *self, gentity_t *inflictor,
						  gentity_t *attacker, int damage, int mod ) {
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
	G_Damage( parent, inflictor, attacker, NULL, NULL, scaled,
			  DAMAGE_NO_ARMOR, mod );

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
		child->die = TitanPartDie;

		// Entity state for client-side rendering
		child->s.eType = ET_TITAN_PART;
		child->s.generic1 = def->type;  // tell cgame which part this is

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

/*
 * G_UpdateTitanParts -- reposition all children to track parent.
 * Called every server frame for titan players.
 */
void G_UpdateTitanParts( gentity_t *ent ) {
	gclient_t			*client = ent->client;
	int					i;
	gentity_t			*child;
	const titanPartDef_t *def;
	vec3_t				worldOffset;
	float				yaw;

	if ( !client || client->numTitanParts == 0 ) {
		return;
	}

	yaw = client->ps.viewangles[YAW];

	for ( i = 0; i < client->numTitanParts; i++ ) {
		child = client->titanParts[i];
		if ( !child || !child->inuse ) {
			continue;
		}

		def = &titanParts[child->titanPartType];

		// Rotate offset by parent yaw
		RotatePointAroundYaw( def->offset, yaw, worldOffset );
		VectorAdd( ent->r.currentOrigin, worldOffset, child->r.currentOrigin );
		VectorCopy( child->r.currentOrigin, child->s.pos.trBase );

		// Update bbox (in case we ever support dynamic sizing)
		VectorCopy( def->mins, child->r.mins );
		VectorCopy( def->maxs, child->r.maxs );

		trap_LinkEntity( child );
	}
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
