/*
 * bg_titan_parts.c -- Titan part table definition
 *
 * Shared between game and cgame modules. Offsets are in local space:
 *   offset[0] = forward, offset[1] = left, offset[2] = up
 * Forward/left are rotated by parent yaw at runtime.
 *
 * The titan is ~224 units tall (ground to head). Player origin is 24
 * above ground (mins_z = -24), so offsets are relative to that origin.
 * The rendered player model floats at TITAN_RENDER_OFFSET (168) above
 * ground, inside the cockpit area.
 *
 * Layout (units above ground -> offset from origin):
 *   Ground:    0   -> -24
 *   Knees:    50   ->  26
 *   Waist:   100   ->  76
 *   Chest:   140   -> 116
 *   Cockpit: 180   -> 156  (player model is here, at 168 above ground)
 *   Head:    220   -> 196
 */

#include "../qcommon/q_shared.h"
#include "bg_titan_parts.h"

const titanPartDef_t titanParts[NUM_TITAN_PARTS] = {
	// TORSO — large center body, chest to waist
	{
		TITAN_PART_TORSO, "torso",
		{ 0, 0, 96 },				// centered at chest height (120 above ground)
		{ -20, -24, -40 },			// 40 x 48 x 80
		{ 20, 24, 40 },
		1.0f,
		{ 0, 200, 0, 100 }			// green
	},
	// COCKPIT — where the pilot sits, 2x damage weak point
	{
		TITAN_PART_COCKPIT, "cockpit",
		{ 10, 0, 156 },			// forward, high up (180 above ground)
		{ -12, -16, -20 },			// 24 x 32 x 40
		{ 12, 16, 20 },
		2.0f,
		{ 255, 0, 0, 100 }			// red
	},
	// ARM_L — left side, shoulder to elbow height
	{
		TITAN_PART_ARM_L, "arm_l",
		{ 0, 36, 96 },				// left, chest height
		{ -14, -10, -36 },			// 28 x 20 x 72
		{ 14, 10, 36 },
		0.5f,
		{ 0, 128, 255, 100 }		// blue
	},
	// ARM_R — right side, shoulder to elbow height
	{
		TITAN_PART_ARM_R, "arm_r",
		{ 0, -36, 96 },			// right, chest height
		{ -14, -10, -36 },			// 28 x 20 x 72
		{ 14, 10, 36 },
		0.5f,
		{ 0, 128, 255, 100 }		// blue
	},
	// LEG_L — left leg, ground to waist
	{
		TITAN_PART_LEG_L, "leg_l",
		{ 0, 16, 26 },				// left, centered at knee height (50 above ground)
		{ -14, -10, -50 },			// 28 x 20 x 100
		{ 14, 10, 50 },
		0.3f,
		{ 255, 255, 0, 100 }		// yellow
	},
	// LEG_R — right leg, ground to waist
	{
		TITAN_PART_LEG_R, "leg_r",
		{ 0, -16, 26 },			// right, centered at knee height
		{ -14, -10, -50 },			// 28 x 20 x 100
		{ 14, 10, 50 },
		0.3f,
		{ 255, 255, 0, 100 }		// yellow
	},
	// VENT — back upper, critical weak point
	{
		TITAN_PART_VENT, "vent",
		{ -18, 0, 130 },			// behind, upper back (154 above ground)
		{ -12, -16, -20 },			// 24 x 32 x 40
		{ 12, 16, 20 },
		3.0f,
		{ 255, 128, 0, 100 }		// orange
	}
};
