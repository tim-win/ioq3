/*
 * bg_titan_parts.h -- Titan part definitions shared between game and cgame
 *
 * Each titan part is a child entity spawned around the parent player when
 * they enter titan mode. Parts have:
 *   - Local offset (forward, left, up) rotated by parent yaw
 *   - Bounding box dimensions (mins/maxs)
 *   - Damage multiplier (applied when routing damage to parent)
 *   - Debug color (RGBA for cg_titanDebug visualization)
 *
 * Collision strategy:
 *   Parent titan entity uses CONTENTS_PLAYERCLIP (not CONTENTS_BODY) so
 *   weapon traces (MASK_SHOT) pass through the parent bbox. Child parts
 *   use CONTENTS_TITANPART which IS in MASK_SHOT, so weapons hit parts
 *   directly. Movement collision (MASK_PLAYERSOLID) still works because
 *   CONTENTS_PLAYERCLIP is in MASK_PLAYERSOLID.
 */

#ifndef BG_TITAN_PARTS_H
#define BG_TITAN_PARTS_H

// MAX_TITAN_PARTS is defined in bg_public.h

typedef enum {
	TITAN_PART_TORSO,		// center body — 1.0x damage
	TITAN_PART_COCKPIT,		// upper front — 2.0x damage (weak point)
	TITAN_PART_ARM_L,		// left arm — 0.5x damage
	TITAN_PART_ARM_R,		// right arm — 0.5x damage
	TITAN_PART_LEG_L,		// left leg — 0.3x damage
	TITAN_PART_LEG_R,		// right leg — 0.3x damage
	TITAN_PART_VENT,		// back vents — 3.0x damage (critical)
	NUM_TITAN_PARTS
} titanPartType_t;

typedef struct {
	titanPartType_t	type;
	const char		*name;

	// Local offset from parent origin (forward, left, up)
	// Forward/left are rotated by parent yaw; up is absolute
	vec3_t			offset;

	// Bounding box relative to part origin
	vec3_t			mins;
	vec3_t			maxs;

	// Damage multiplier when routing hits to parent
	float			damageMultiplier;

	// Debug visualization color (R, G, B, A) 0-255
	byte			color[4];
} titanPartDef_t;

// Part table — defined in bg_titan_parts.c, indexed by titanPartType_t
extern const titanPartDef_t titanParts[NUM_TITAN_PARTS];

#endif // BG_TITAN_PARTS_H
