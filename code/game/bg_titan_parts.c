/*
 * bg_titan_parts.c -- Titan part table definition
 *
 * Shared between game and cgame modules. Offsets are in local space:
 *   offset[0] = forward, offset[1] = left, offset[2] = up
 * Forward/left are rotated by parent yaw at runtime.
 *
 * The titan is ~377 units tall (ground to head). Player origin is 24
 * above ground (mins_z = -24), so offsets are relative to that origin.
 * The rendered player model floats at TITAN_RENDER_OFFSET (225) above
 * ground, inside the upper chest area.
 *
 * Layout (units above ground -> offset from origin):
 *   Ground:    0   -> -24
 *   Knees:    87   ->  63
 *   Waist:   173   -> 149
 *   Chest:   243   -> 219
 *   Cockpit: 312   -> 288  (player model is here, at ~249 above ground)
 *   Head:    377   -> 353
 */

#include "../qcommon/q_shared.h"
#include "bg_titan_parts.h"

const titanPartDef_t titanParts[NUM_TITAN_PARTS] = {
	// TORSO — large center body, chest to waist
	{
		TITAN_PART_TORSO, "torso",
		{ 0, 0, 166 },				// centered at chest height (190 above ground)
		{ -35, -42, -69 },			// 70 x 84 x 138
		{ 35, 42, 69 },
		1.0f,
		{ 0, 200, 0, 100 }			// green
	},
	// COCKPIT — where the pilot sits, 2x damage weak point
	{
		TITAN_PART_COCKPIT, "cockpit",
		{ 17, 0, 270 },			// forward, high up (294 above ground)
		{ -21, -27, -35 },			// 42 x 54 x 70
		{ 21, 27, 35 },
		2.0f,
		{ 255, 0, 0, 100 }			// red
	},
	// ARM_L — left side, shoulder to elbow height
	{
		TITAN_PART_ARM_L, "arm_l",
		{ 0, 62, 166 },			// left, chest height
		{ -25, -17, -62 },			// 50 x 34 x 124
		{ 25, 17, 62 },
		0.5f,
		{ 0, 128, 255, 100 }		// blue
	},
	// ARM_R — right side, shoulder to elbow height
	{
		TITAN_PART_ARM_R, "arm_r",
		{ 0, -62, 166 },			// right, chest height
		{ -25, -17, -62 },			// 50 x 34 x 124
		{ 25, 17, 62 },
		0.5f,
		{ 0, 128, 255, 100 }		// blue
	},
	// LEG_L — left leg, ground to waist
	{
		TITAN_PART_LEG_L, "leg_l",
		{ 0, 27, 46 },				// left, centered at knee height (70 above ground)
		{ -25, -17, -87 },			// 50 x 34 x 174
		{ 25, 17, 87 },
		0.3f,
		{ 255, 255, 0, 100 }		// yellow
	},
	// LEG_R — right leg, ground to waist
	{
		TITAN_PART_LEG_R, "leg_r",
		{ 0, -27, 46 },			// right, centered at knee height
		{ -25, -17, -87 },			// 50 x 34 x 174
		{ 25, 17, 87 },
		0.3f,
		{ 255, 255, 0, 100 }		// yellow
	},
	// VENT — back upper, critical weak point
	{
		TITAN_PART_VENT, "vent",
		{ -31, 0, 225 },			// behind, upper back (249 above ground)
		{ -21, -27, -35 },			// 42 x 54 x 70
		{ 21, 27, 35 },
		3.0f,
		{ 255, 128, 0, 100 }		// orange
	}
};
