/***************************************************************************



***************************************************************************/

#ifndef _WORLDBOUNDS_H_
#define _WORLDBOUNDS_H_
GCC_SYSTEM

// This files defines important boundaries, limits, and characteristics of the world system.

// Some values are still defined in the WorldGrid header.

#include "WorldGrid.h"

C_DECLARATIONS_BEGIN

// long-standing semi-arbitrary limit, as defined by current code, possibly chosen as world limit less-than 16,384 where we have sufficient floating-point precision.
#define MAX_PLAYABLE_COORDINATE					15000
// the factor of 2.f used here had previously been introduced elsewhere to relax the condition in space
#define MAX_PLAYABLE_COORDINATE_IN_SPACE		(MAX_PLAYABLE_COORDINATE * 2.f)

// Maximum world-space distance to origin for moving static world pivots, dynamic moving objects, etc
// = ||<MAX_PLAYABLE_COORDINATE, MAX_PLAYABLE_COORDINATE, MAX_PLAYABLE_COORDINATE>||^2
#define MAX_PLAYABLE_DIST_ORIGIN_SQR			(3*MAX_PLAYABLE_COORDINATE*MAX_PLAYABLE_COORDINATE)
#define MAX_PLAYABLE_DIST_ORIGIN_SQR_IN_SPACE	(3*MAX_PLAYABLE_COORDINATE_IN_SPACE*MAX_PLAYABLE_COORDINATE_IN_SPACE)

// The furthest that the animation system is allowed to offset bones in an animation in a space region, not currently enforced outside of DynNode position checking
// note: this is ridiculously high to accommodate starship warping in STO (as of 11/26/2012 for the Borg Cube in particular, might need to be nudged a bit more)
#define MAX_ANIMATION_BONE_OFFSET_IN_SPACE		100000.f

// Maximum world-space distance to origin for dynNodes that also accommodates for the position of animation bones in space
#define MAX_PLAYABLE_DIST_ORIGIN_SQR_WITH_ANIMATION_IN_SPACE	(powf((sqrtf(MAX_PLAYABLE_DIST_ORIGIN_SQR_IN_SPACE) + MAX_ANIMATION_BONE_OFFSET_IN_SPACE),2))

// Maximum terrain or grid block number before overflowing 15K position limit
// = floor(MAX_PLAYABLE_COORDINATE / BLOCK_SIZE)
#define MAX_PLAYABLE_GRID_BLOCK					58


#define MAX_PLAYABLE_GRID_BLOCK_COORDINATE		(58 * GRID_BLOCK_SIZE)


#define FLT_MAX_WORLD 5.3251161693118645675e+18F

C_DECLARATIONS_END

#endif //_WORLDBOUNDS_H_

