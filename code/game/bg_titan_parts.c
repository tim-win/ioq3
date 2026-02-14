/*
 * bg_titan_parts.c -- Titan part table definition
 *
 * Shared between game and cgame modules. Offsets are in local space:
 *   offset[0] = forward, offset[1] = left, offset[2] = up
 * Forward/left are rotated by parent yaw at runtime.
 */

#include "../qcommon/q_shared.h"
#include "bg_titan_parts.h"

const titanPartDef_t titanParts[NUM_TITAN_PARTS] = {
	// TORSO — large center body, base damage
	{
		TITAN_PART_TORSO, "torso",
		{ 0, 0, 18 },				// centered, chest height
		{ -12, -14, -14 },			// 24 x 28 x 28
		{ 12, 14, 14 },
		1.0f,
		{ 0, 200, 0, 80 }			// green
	},
	// COCKPIT — upper front, 2x damage weak point
	{
		TITAN_PART_COCKPIT, "cockpit",
		{ 6, 0, 40 },				// forward, high up
		{ -6, -8, -6 },			// 12 x 16 x 12
		{ 6, 8, 6 },
		2.0f,
		{ 255, 0, 0, 80 }			// red
	},
	// ARM_L — left side
	{
		TITAN_PART_ARM_L, "arm_l",
		{ 0, 20, 16 },				// left, chest height
		{ -8, -6, -12 },			// 16 x 12 x 24
		{ 8, 6, 12 },
		0.5f,
		{ 0, 128, 255, 80 }		// blue
	},
	// ARM_R — right side
	{
		TITAN_PART_ARM_R, "arm_r",
		{ 0, -20, 16 },			// right, chest height
		{ -8, -6, -12 },			// 16 x 12 x 24
		{ 8, 6, 12 },
		0.5f,
		{ 0, 128, 255, 80 }		// blue
	},
	// LEG_L — lower left
	{
		TITAN_PART_LEG_L, "leg_l",
		{ 0, 10, -10 },			// left, below origin
		{ -8, -6, -14 },			// 16 x 12 x 28
		{ 8, 6, 14 },
		0.3f,
		{ 255, 255, 0, 80 }		// yellow
	},
	// LEG_R — lower right
	{
		TITAN_PART_LEG_R, "leg_r",
		{ 0, -10, -10 },			// right, below origin
		{ -8, -6, -14 },			// 16 x 12 x 28
		{ 8, 6, 14 },
		0.3f,
		{ 255, 255, 0, 80 }		// yellow
	},
	// VENT — back upper, critical weak point
	{
		TITAN_PART_VENT, "vent",
		{ -10, 0, 32 },			// behind, upper back
		{ -6, -8, -8 },			// 12 x 16 x 16
		{ 6, 8, 8 },
		3.0f,
		{ 255, 128, 0, 80 }		// orange
	}
};
