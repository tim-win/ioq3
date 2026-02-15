/*
 * g_parkour_test.c -- Server-side parkour mechanics validation
 *
 * Provides RCON command "test_parkour <test> [client]" to test each
 * parkour mechanic with frame-accurate input sequences. Each test
 * teleports the player, injects inputs for precise durations, and
 * asserts expected outcomes (flags, speed, position).
 *
 * The test sequencer overrides the client's usercmd each frame and
 * checks assertions at step boundaries.
 *
 * Tests: doublejump, wallrun, walljump, slide, vault, ledgegrab, all
 */

#include "g_local.h"
#include "bg_local.h"

// =====================================================
// Step flags
// =====================================================
#define PKS_TELEPORT      (1<<0)   // Set origin at step start
#define PKS_SET_VEL       (1<<1)   // Set velocity at step start
#define PKS_OVERRIDE_CMD  (1<<2)   // Override usercmd during step
#define PKS_SET_YAW       (1<<3)   // Set view yaw at step start
#define PKS_CHECK_FLAG    (1<<4)   // Assert pm_flags has expectFlag
#define PKS_CHECK_NOFLAG  (1<<5)   // Assert pm_flags lacks expectNoFlag
#define PKS_CHECK_SPEED   (1<<6)   // Assert horiz speed >= minHorizSpeed
#define PKS_CHECK_Z       (1<<7)   // Assert origin[2] >= minZ
#define PKS_CHECK_AIR     (1<<8)   // Assert player is airborne
#define PKS_CHECK_GROUND  (1<<9)   // Assert player is on ground
#define PKS_NOCLIP_OFF    (1<<10)  // Disable noclip at step start

#define PKT_MAX_STEPS 32

typedef struct {
	int         flags;
	int         duration_ms;
	// Positioning
	vec3_t      origin;
	float       yaw;
	vec3_t      velocity;
	// Input override
	int         forwardmove;
	int         rightmove;
	int         upmove;
	// Assertions (checked at end of step)
	int         expectFlag;       // PMF_ that should be set
	int         expectNoFlag;     // PMF_ that should NOT be set
	float       minHorizSpeed;    // -1 = don't check
	float       minZ;             // -1 = don't check
	// Description
	const char  *desc;
} pkTestStep_t;

typedef struct {
	qboolean        active;
	int             clientNum;
	int             currentStep;
	int             stepStartTime;  // level.time when step began
	qboolean        stepInitialized; // have we run the step-start actions?
	int             numSteps;
	pkTestStep_t    steps[PKT_MAX_STEPS];
	int             passed;
	int             failed;
	float           lockedYaw;      // view yaw to enforce every frame
	qboolean        yawLocked;
	char            testName[64];
} pkTestState_t;

static pkTestState_t testState;

// =====================================================
// Helper: teleport player cleanly (no velocity kick, no knockback)
// =====================================================
static void PKT_Teleport( gentity_t *ent, vec3_t origin, float yaw ) {
	vec3_t angles;

	VectorClear( angles );
	angles[YAW] = yaw;

	// Unlink for safe repositioning
	trap_UnlinkEntity( ent );

	VectorCopy( origin, ent->client->ps.origin );
	ent->client->ps.origin[2] += 1;

	// Zero velocity (tests set it explicitly if needed)
	VectorClear( ent->client->ps.velocity );

	// Clear movement flags that might interfere
	ent->client->ps.pm_flags &= ~(PMF_WALLRUN | PMF_DOUBLEJUMP |
		PMF_JUMP_HELD | PMF_TIME_KNOCKBACK | PMF_TIME_LAND);
	ent->client->ps.pm_time = 0;
	ent->client->ps.generic1 = 0;

	// Set view angles
	SetClientViewAngle( ent, angles );

	// Toggle teleport bit for client
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT;

	// Sync entity state
	BG_PlayerStateToEntityState( &ent->client->ps, &ent->s, qtrue );
	VectorCopy( ent->client->ps.origin, ent->r.currentOrigin );

	// Disable noclip
	ent->client->noclip = qfalse;

	trap_LinkEntity( ent );
}

// =====================================================
// Assertion checking
// =====================================================
static void PKT_CheckAssertions( pkTestStep_t *step, gentity_t *ent ) {
	playerState_t *ps = &ent->client->ps;
	float hspeed;

	if ( step->flags & PKS_CHECK_FLAG ) {
		if ( ps->pm_flags & step->expectFlag ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] flag 0x%x is set\n", step->desc, step->expectFlag );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] expected flag 0x%x set, got pm_flags=0x%x\n",
				step->desc, step->expectFlag, ps->pm_flags );
		}
	}

	if ( step->flags & PKS_CHECK_NOFLAG ) {
		if ( !( ps->pm_flags & step->expectNoFlag ) ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] flag 0x%x is clear\n", step->desc, step->expectNoFlag );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] expected flag 0x%x clear, got pm_flags=0x%x\n",
				step->desc, step->expectNoFlag, ps->pm_flags );
		}
	}

	if ( step->flags & PKS_CHECK_SPEED ) {
		hspeed = sqrt( ps->velocity[0] * ps->velocity[0] +
		               ps->velocity[1] * ps->velocity[1] );
		if ( hspeed >= step->minHorizSpeed ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] speed %.0f >= %.0f\n",
				step->desc, hspeed, step->minHorizSpeed );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] speed %.0f < %.0f\n",
				step->desc, hspeed, step->minHorizSpeed );
		}
	}

	if ( step->flags & PKS_CHECK_Z ) {
		if ( ps->origin[2] >= step->minZ ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] z=%.0f >= %.0f\n",
				step->desc, ps->origin[2], step->minZ );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] z=%.0f < %.0f\n",
				step->desc, ps->origin[2], step->minZ );
		}
	}

	if ( step->flags & PKS_CHECK_AIR ) {
		if ( ps->groundEntityNum == ENTITYNUM_NONE ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] airborne\n", step->desc );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] expected airborne, groundEnt=%d\n",
				step->desc, ps->groundEntityNum );
		}
	}

	if ( step->flags & PKS_CHECK_GROUND ) {
		if ( ps->groundEntityNum != ENTITYNUM_NONE ) {
			testState.passed++;
			G_Printf( "  PASS: [%s] on ground\n", step->desc );
		} else {
			testState.failed++;
			G_Printf( "  FAIL: [%s] expected on ground\n", step->desc );
		}
	}
}

// =====================================================
// Test definitions
// =====================================================

// Parkour map coordinates (from generate_parkour_map.py):
// Spawn room: (-256,0) to (256,512), floor at z=0
// Section 1 (wall run): walls at x=-272/-256 and x=256/272, y=512 to y=1280
//   Entry floor: y=512 to y=640, exit floor: y=1152 to y=1280
//   Pit in between: z=-192
// Section 7 (vault): obstacles at ~40 units height
//   Y starts at ~6144 (calculated from section layout)

static int PKT_BuildDoubleJump( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;

	// Step 0: Teleport to spawn room center
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_NOCLIP_OFF;
	s->duration_ms = 200;
	s->origin[0] = 0; s->origin[1] = 256; s->origin[2] = 24;
	s->yaw = 90;
	s->desc = "Teleport to spawn room";

	// Step 1: Run forward briefly to get some speed
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 300;
	s->forwardmove = 127;
	s->desc = "Run forward";

	// Step 2: Jump (press jump)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->forwardmove = 127;
	s->upmove = 127;
	s->desc = "First jump";

	// Step 3: Release jump, still in air
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_AIR;
	s->duration_ms = 250;
	s->forwardmove = 127;
	s->upmove = 0;
	s->desc = "Release jump, airborne";

	// Step 4: Press jump again (double jump)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->forwardmove = 127;
	s->upmove = 127;
	s->desc = "Press jump again";

	// Step 5: Check double jump flag is set
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_FLAG | PKS_CHECK_AIR;
	s->duration_ms = 100;
	s->forwardmove = 127;
	s->expectFlag = PMF_DOUBLEJUMP;
	s->desc = "Double jump triggered";

	// Step 6: Let it play out
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->duration_ms = 500;
	s->desc = "Cool down";

	return n;
}

static int PKT_BuildWallRun( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;

	// Teleport near the right wall of Section 1
	// Right wall is at x=256, player needs to be within 32 units
	// Entry floor at y=512 to y=640, facing +Y (yaw=90)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_NOCLIP_OFF;
	s->duration_ms = 300;
	s->origin[0] = 230; s->origin[1] = 560; s->origin[2] = 24;
	s->yaw = 90;
	s->desc = "Teleport near right wall";

	// Give initial velocity (need > 200 for wall run)
	// Use small negative z to ensure player is grounded, not briefly airborne
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_SET_VEL | PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->velocity[0] = 0; s->velocity[1] = 300; s->velocity[2] = -10;
	s->forwardmove = 127;
	s->desc = "Set forward velocity";

	// Jump to get airborne (wall run requires airborne)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->forwardmove = 127;
	s->upmove = 127;
	s->desc = "Jump near wall";

	// Release jump, keep moving forward along wall
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_AIR;
	s->duration_ms = 200;
	s->forwardmove = 127;
	s->desc = "Airborne near wall";

	// Check wall run engaged
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_FLAG;
	s->duration_ms = 300;
	s->forwardmove = 127;
	s->expectFlag = PMF_WALLRUN;
	s->desc = "Wall run active";

	// Continue wall running
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 500;
	s->forwardmove = 127;
	s->desc = "Continue wall run";

	// Release forward to end wall run
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_CHECK_NOFLAG;
	s->duration_ms = 200;
	s->expectNoFlag = PMF_WALLRUN;
	s->desc = "Wall run ended";

	return n;
}

static int PKT_BuildWallJump( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;

	// Teleport near right wall, give velocity, jump into wall run
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_NOCLIP_OFF;
	s->duration_ms = 300;
	s->origin[0] = 230; s->origin[1] = 560; s->origin[2] = 24;
	s->yaw = 90;
	s->desc = "Teleport near right wall";

	// Set velocity and jump
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_SET_VEL | PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->velocity[0] = 0; s->velocity[1] = 300; s->velocity[2] = -10;
	s->forwardmove = 127;
	s->upmove = 127;
	s->desc = "Jump with velocity";

	// Release jump, wall run should start
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 300;
	s->forwardmove = 127;
	s->desc = "Wall running";

	// Verify wall run is active
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_FLAG;
	s->duration_ms = 100;
	s->forwardmove = 127;
	s->expectFlag = PMF_WALLRUN;
	s->desc = "Wall run confirmed";

	// Press jump to wall jump off the wall
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->forwardmove = 127;
	s->upmove = 127;
	s->desc = "Jump off wall";

	// Wall jump should have cleared WALLRUN and pushed away
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_NOFLAG | PKS_CHECK_AIR;
	s->duration_ms = 200;
	s->forwardmove = 127;
	s->expectNoFlag = PMF_WALLRUN;
	s->desc = "Wall jump: no longer wall running";

	// Cool down
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->duration_ms = 500;
	s->desc = "Cool down";

	return n;
}

static int PKT_BuildSlide( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;

	// Teleport to spawn room
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_NOCLIP_OFF;
	s->duration_ms = 200;
	s->origin[0] = 0; s->origin[1] = 128; s->origin[2] = 24;
	s->yaw = 90;
	s->desc = "Teleport to spawn room";

	// Set high velocity (above slide threshold of 400)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_SET_VEL | PKS_OVERRIDE_CMD;
	s->duration_ms = 50;
	s->velocity[0] = 0; s->velocity[1] = 500; s->velocity[2] = 0;
	s->forwardmove = 127;
	s->desc = "Set high velocity";

	// Crouch while moving fast — should trigger slide (reduced friction)
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_GROUND | PKS_CHECK_FLAG;
	s->duration_ms = 300;
	s->forwardmove = 127;
	s->upmove = -127;
	s->expectFlag = PMF_DUCKED;
	s->desc = "Crouching while fast (slide)";

	// After 300ms of sliding, check speed is above zero
	// With slide friction (3.0 vs normal 6.0), speed decays ~50% slower
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_SPEED;
	s->duration_ms = 200;
	s->forwardmove = 127;
	s->upmove = -127;
	s->minHorizSpeed = 50;  // still sliding, not stopped
	s->desc = "Slide preserves speed";

	// Stand up
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->duration_ms = 300;
	s->desc = "Cool down";

	return n;
}

static int PKT_BuildVault( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;
	float startZ;

	// Vault section (Section 7) obstacles are 40 units high
	// Section 7 Y start: calculated from sections 0-6 layout
	// Spawn(512) + S1(768) + corr(128) + S2(768) + corr(128) +
	// S3(1024) + corr(128) + S4(1024) + corr(128) + S5(768) + corr(128) +
	// S6(512) + corr(128) = 6144
	// First obstacle at ~sec_y1 + 128 + spacing = ~6144 + 230 = ~6374
	// Obstacle: y=6358 to y=6390, z=0 to z=40
	// BUT there's a floor at z=0 in the vault section

	// Actually, it's simpler to create a test obstacle scenario.
	// Instead, test vault in the tuning arena (Section 9) or spawn room.
	// OR, better: teleport in front of a known vault obstacle.

	// For now, position just before the first vault obstacle
	// The vault obstacles are at x=(-vault_w/4, vault_w/4) = (-96, 96)
	// with y centered around sec_y1 + 128 + spacing intervals
	// Height = 40, which is < pm_vaultMaxHeight (48)

	// Vault section starts at y=6400. First obstacle at y=6554-6586, z=0 to 40.
	// Teleport a bit before the first obstacle
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_NOCLIP_OFF;
	s->duration_ms = 200;
	s->origin[0] = 0; s->origin[1] = 6500; s->origin[2] = 24;
	s->yaw = 90;
	s->desc = "Teleport before vault obstacle";

	// Run forward into obstacle at full speed
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_SET_VEL;
	s->duration_ms = 300;
	s->velocity[0] = 0; s->velocity[1] = 320; s->velocity[2] = 0;
	s->forwardmove = 127;
	s->desc = "Run toward obstacle";

	// Continue running — vault should have lifted us over
	// The obstacle is 40 units high so after vault z should be ~41
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_Z;
	s->duration_ms = 500;
	s->forwardmove = 127;
	s->minZ = 35;  // above the obstacle top (40 units, minus some tolerance)
	s->desc = "Vaulted over obstacle (higher Z)";

	// Cool down
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->duration_ms = 300;
	s->desc = "Cool down";

	return n;
}

static int PKT_BuildLedgeGrab( pkTestStep_t *steps ) {
	int n = 0;
	pkTestStep_t *s;

	// Section 6 first ledge (right side): brush_box(32, 5568, 72, 160, 5664, 88)
	// Front face at y=5568, top at z=88, bottom at z=72.
	// Ledge grab requires: airborne, vel[2]<=50, forward trace hits wall,
	// ledgeHeight > vaultMax(48) and <= grabHeight+32(80).
	// With player origin at z=52: feet=28, waist trace=28+48=76 (hits ledge face z=72-88).
	// Top trace finds z=88. ledgeHeight = 88 - 28 = 60. 60>48 and <=80 → grab.

	// Step 1: Teleport airborne in front of ledge at correct height.
	// Ledge face at y=5568. Forward trace reaches 47 units.
	// At origin y=5520, trace end = 5567 (just misses). y=5522 → 5569 (hits).
	// Height: z=52 (origin), feet=28, waist trace=76 (hits ledge z=72-88).
	// Player has ~250ms before falling to ground from z=52.
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_TELEPORT | PKS_SET_YAW | PKS_SET_VEL | PKS_NOCLIP_OFF;
	s->duration_ms = 50;
	s->origin[0] = 96; s->origin[1] = 5520; s->origin[2] = 52;
	s->yaw = 90;
	s->velocity[0] = 0; s->velocity[1] = 50; s->velocity[2] = 0;
	s->desc = "Teleport airborne near ledge";

	// Step 2: Hold forward — trace should detect ledge face.
	// Ledge grab gives climb velocity, pushing player up.
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD;
	s->duration_ms = 500;
	s->forwardmove = 127;
	s->desc = "Approach ledge face";

	// Step 3: Ledge grab should have given upward velocity.
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->flags = PKS_OVERRIDE_CMD | PKS_CHECK_Z;
	s->duration_ms = 500;
	s->forwardmove = 127;
	s->minZ = 55;  // player should have climbed above teleport height
	s->desc = "Gained height from ledge grab";

	// Cool down
	s = &steps[n++];
	memset( s, 0, sizeof(*s) );
	s->duration_ms = 300;
	s->desc = "Cool down";

	return n;
}

// =====================================================
// Sequencer core
// =====================================================

/*
 * G_ParkourTestStart - Start a named test for a client
 */
void G_ParkourTestStart( const char *testName, int clientNum ) {
	gentity_t *ent;
	int numSteps = 0;

	if ( clientNum < 0 || clientNum >= level.maxclients ) {
		G_Printf( "test_parkour: bad client %d\n", clientNum );
		return;
	}
	ent = &g_entities[clientNum];
	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
		G_Printf( "test_parkour: client %d not connected\n", clientNum );
		return;
	}

	// Cancel any running test
	testState.active = qfalse;

	memset( &testState, 0, sizeof(testState) );
	testState.clientNum = clientNum;

	if ( Q_stricmp( testName, "doublejump" ) == 0 ) {
		numSteps = PKT_BuildDoubleJump( testState.steps );
	} else if ( Q_stricmp( testName, "wallrun" ) == 0 ) {
		numSteps = PKT_BuildWallRun( testState.steps );
	} else if ( Q_stricmp( testName, "walljump" ) == 0 ) {
		numSteps = PKT_BuildWallJump( testState.steps );
	} else if ( Q_stricmp( testName, "slide" ) == 0 ) {
		numSteps = PKT_BuildSlide( testState.steps );
	} else if ( Q_stricmp( testName, "vault" ) == 0 ) {
		numSteps = PKT_BuildVault( testState.steps );
	} else if ( Q_stricmp( testName, "ledgegrab" ) == 0 ) {
		numSteps = PKT_BuildLedgeGrab( testState.steps );
	} else {
		G_Printf( "test_parkour: unknown test '%s'\n", testName );
		G_Printf( "  Available: doublejump, wallrun, walljump, slide, vault, ledgegrab\n" );
		return;
	}

	if ( numSteps <= 0 ) {
		G_Printf( "test_parkour: test '%s' has no steps\n", testName );
		return;
	}

	testState.numSteps = numSteps;
	testState.currentStep = 0;
	testState.stepStartTime = level.time;
	testState.stepInitialized = qfalse;
	testState.active = qtrue;
	Q_strncpyz( testState.testName, testName, sizeof(testState.testName) );

	G_Printf( "=== PARKOUR TEST: %s (client %d, %d steps) ===\n",
		testName, clientNum, numSteps );
}

/*
 * G_ParkourTestFrame - Called from G_RunFrame each server frame.
 * Handles step initialization, timing, assertions, and transitions.
 */
void G_ParkourTestFrame( void ) {
	pkTestStep_t *step;
	gentity_t *ent;
	int elapsed;

	if ( !testState.active ) {
		return;
	}

	ent = &g_entities[testState.clientNum];
	if ( !ent->client || ent->client->pers.connected != CON_CONNECTED ) {
		G_Printf( "test_parkour: client disconnected, aborting\n" );
		testState.active = qfalse;
		return;
	}

	step = &testState.steps[testState.currentStep];

	// Initialize step on first frame
	if ( !testState.stepInitialized ) {
		testState.stepInitialized = qtrue;
		testState.stepStartTime = level.time;

		G_Printf( "  Step %d/%d: %s (%dms)\n",
			testState.currentStep + 1, testState.numSteps,
			step->desc, step->duration_ms );

		// Apply step-start actions
		if ( step->flags & PKS_NOCLIP_OFF ) {
			ent->client->noclip = qfalse;
		}

		if ( step->flags & PKS_TELEPORT ) {
			PKT_Teleport( ent, step->origin, step->yaw );
			testState.lockedYaw = step->yaw;
			testState.yawLocked = qtrue;
		} else if ( step->flags & PKS_SET_YAW ) {
			vec3_t angles;
			VectorClear( angles );
			angles[YAW] = step->yaw;
			SetClientViewAngle( ent, angles );
			testState.lockedYaw = step->yaw;
			testState.yawLocked = qtrue;
		}

		if ( step->flags & PKS_SET_VEL ) {
			VectorCopy( step->velocity, ent->client->ps.velocity );
		}
	}

	// Check if step duration has elapsed
	elapsed = level.time - testState.stepStartTime;
	if ( elapsed >= step->duration_ms ) {
		// Run assertions for this step
		PKT_CheckAssertions( step, ent );

		// Log state for debugging
		G_Printf( "    state: pos=(%.0f,%.0f,%.0f) vel=(%.0f,%.0f,%.0f) pm_flags=0x%x ground=%d\n",
			ent->client->ps.origin[0], ent->client->ps.origin[1], ent->client->ps.origin[2],
			ent->client->ps.velocity[0], ent->client->ps.velocity[1], ent->client->ps.velocity[2],
			ent->client->ps.pm_flags, ent->client->ps.groundEntityNum );

		// Advance to next step
		testState.currentStep++;
		if ( testState.currentStep >= testState.numSteps ) {
			// Test complete
			G_Printf( "=== PARKOUR TEST COMPLETE: %s — %d passed, %d failed ===\n",
				testState.testName, testState.passed, testState.failed );
			testState.active = qfalse;
			return;
		}

		testState.stepInitialized = qfalse;
	}
}

/*
 * G_ParkourTestOverrideCmd - Called from ClientThink_real before Pmove.
 * Overrides usercmd fields when a test step has PKS_OVERRIDE_CMD.
 */
void G_ParkourTestOverrideCmd( gentity_t *ent ) {
	pkTestStep_t *step;

	if ( !testState.active ) {
		return;
	}

	if ( ent->s.clientNum != testState.clientNum ) {
		return;
	}

	if ( !testState.stepInitialized ) {
		return;
	}

	step = &testState.steps[testState.currentStep];

	if ( step->flags & PKS_OVERRIDE_CMD ) {
		ent->client->pers.cmd.forwardmove = step->forwardmove;
		ent->client->pers.cmd.rightmove = step->rightmove;
		ent->client->pers.cmd.upmove = step->upmove;
	}

	// Enforce locked yaw every frame by adjusting usercmd angles
	// This prevents the client's random view angles from interfering
	if ( testState.yawLocked ) {
		vec3_t angles;
		VectorClear( angles );
		angles[YAW] = testState.lockedYaw;
		SetClientViewAngle( ent, angles );
	}
}

/*
 * G_ParkourTestActive - Returns qtrue if a test is currently running.
 */
qboolean G_ParkourTestActive( void ) {
	return testState.active;
}
