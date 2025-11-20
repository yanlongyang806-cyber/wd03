#if !PLATFORM_CONSOLE
#include "winInclude.h"
#endif

#include "wlTerrainSource.h"
#include "wlTerrainErode.h"
#include "rand.h"

#ifndef NO_EDITORS

U32 hydro_timer_1 = 0;
U32 hydro_timer_2 = 0;

#define MAX_ERODE_STEPS 100

int getNextMove( TerrainEditorSource *source, U32* seed, F32 part_pos[2], F32 slopes[9], HeightMapCache *cache )
{
	int i;
	F32 neighbors[9];
	int next_move = -1;
	F32 diff_sum = 0;
	F32 diff_count = 0;
	F32 map_scale = 1<<source->visible_lod;
	F32 r = randomPositiveF32Seeded( seed, RandType_LCG);
	assert( r >= 0.0f && r < 1.0f );

	for (i = 0; i < 9; i++)
	{
		F32 offx = part_pos[0] + ((i%3) - 1)*map_scale;
		F32 offz = part_pos[1] + ((i/3) - 1)*map_scale;

		if (!terrainSourceGetHeight( source, offx, offz, &neighbors[i], cache))
			neighbors[i] = FLT_MAX;
	}

	for (i = 0; i < 9; i++)
	{
		F32 rise, run;
		switch(i)
		{
		//Going from 4 to 2 or 6 crosses a poly edge.
		//There are 3 possible slops to take, the closest, the furthest or 
		//the one that ignores the the fact that there is a poly edge in the way.
		//Using the most uphill slop between the "nearest" and the "ignore" seems to give best results.
		case 2:
			rise = neighbors[4] - (neighbors[5]+neighbors[1])/2.0f;
			rise = MIN(rise, neighbors[4] - neighbors[i]);
			run = map_scale*SQRT2*0.5f;
			break;
		case 6:
			rise = neighbors[4] - (neighbors[7]+neighbors[3])/2.0f;
			rise = MIN(rise, neighbors[4] - neighbors[i]);
			run = map_scale*SQRT2*0.5f;
			break;
		//0 and 8 are slightly further away than map_scale
		case 0:
		case 8:
			rise = neighbors[4] - neighbors[i];
			run = map_scale*SQRT2;
			break;
		default:
			rise = neighbors[4] - neighbors[i];
			run = map_scale;
			break;
		}
		slopes[i] = MAX(0, rise/run);
		diff_sum += slopes[i];
	}
	r *= diff_sum;

	for (i = 0; i < 9; i++)
	{
		if (r > diff_count && r < diff_count + slopes[i])
		{
			next_move = i;
			break;
		}
		diff_count += slopes[i];
	}

	return next_move;
}


__forceinline static void moveToNextMove( S32 local_pos[2], int next_move, int lod )
{
	int deltax = next_move%3 - 1;
	int deltay = next_move/3 - 1;
	
	local_pos[0] += deltax << lod;
	local_pos[1] += deltay << lod;
}

#define MAX_ERODE_LINES 50
#define ERODE_LINES_UPDATE 10
#define ERODE_REMOVE 0
#define ERODE_DEPOSIT 1
typedef struct ErodeLineInfo
{
	int length;
	Vec3 points[MAX_ERODE_STEPS];
	U8 colors[MAX_ERODE_STEPS];		//ERODE_REMOVE, ERODE_DEPOSIT
} ErodeLineInfo;
static ErodeLineInfo erode_lines[MAX_ERODE_LINES];
static int update_line = -1;

void terrainErosionDrawHydraulicErosion( TerrainEditorSource *source, U32 lod, S32 x, S32 y, S32 width, S32 height, 
										F32 flow, ErodeBrushData erode_options, draw_line_function draw_cb )
{
	static U32 seed;
	int particle;
	int max_particles = (width*height)*flow;
	int step;
	HeightMapCache cache = { 0 };
	U32 lod_diff = lod - source->editing_lod;
	int map_scale = (1 << source->visible_lod);	
	F32 rock_remove_rate = erode_options.rock_removal_rate;
	F32 soil_remove_rate = erode_options.soil_removal_rate;
	F32 deposit_rate = erode_options.deposit_rate;
	F32 carrying_const = erode_options.carrying_const;
	F32 remove_multi = erode_options.remove_multi;
	F32 deposit_multi = erode_options.deposit_multi;

	//Clear our draw lines the first time
	if(update_line == -1)
	{
		memset(erode_lines, 0 , MAX_ERODE_LINES*sizeof(ErodeLineInfo));
		update_line = 0;		
	}

	for(particle = 0; particle < max_particles; particle++)
	{
		F32 slopes[9];
		S32 partical_pos[2];
		F32 sediment = 0;

		partical_pos[0] = x + randomF32Seeded(&seed, RandType_LCG)*width;
		partical_pos[1] = y + randomF32Seeded(&seed, RandType_LCG)*height;
		partical_pos[0] = (partical_pos[0] >> lod_diff) << lod_diff;
		partical_pos[1] = (partical_pos[1] >> lod_diff) << lod_diff;

		for(step=0; step < MAX_ERODE_STEPS; step++)
		{
			int next_move;
			F32 world_pos[2];
			F32 soil_depth=0;
			
			// calculate the next move for the particle
			// randomly choose next move according to difference in height
			world_pos[0] = partical_pos[0]<<source->editing_lod;
			world_pos[1] = partical_pos[1]<<source->editing_lod;
			next_move = getNextMove( source, &seed, world_pos, slopes, &cache );

			if(!terrainSourceGetSoilDepth(source, world_pos[0], world_pos[1], &soil_depth, &cache ))
				break;

			// calculate carry_sediment deposit / removal
			//If we are moving locations
			if (next_move >= 0)
			{
				F32 sediment_capacity;
				F32 soil_diff;

				sediment_capacity = carrying_const * slopes[next_move];
				soil_diff = sediment - sediment_capacity;

				if(soil_diff >= 0)
				{
					F32 deposit_amount = deposit_rate * soil_diff;

					//If we are drawing lines
					if (draw_cb && particle < ERODE_LINES_UPDATE)
					{
						F32 cur_height=0;
						terrainSourceGetHeight(source, world_pos[0], world_pos[1], &cur_height, &cache ); 
						setVec3(erode_lines[update_line].points[erode_lines[update_line].length], world_pos[0], cur_height + 1.0f, world_pos[1]);
						erode_lines[update_line].colors[erode_lines[update_line].length] = ERODE_DEPOSIT;
						erode_lines[update_line].length++;
					}

					//If we are not just drawing lines
					if (!draw_cb)
					{
						terrainSourceDrawHeight( source, world_pos[0], world_pos[1], lod, deposit_amount * deposit_multi, &cache );
						terrainSourceDrawSoilDepth( source, world_pos[0], world_pos[1], lod, deposit_amount, &cache );
					}
					sediment -= deposit_amount;
				}
				else
				{
					F32 remove_amount;
					//Removing rock
					if(soil_depth < 0.1)
					{
						remove_amount = rock_remove_rate * soil_diff;
					}
					//Removing soil
					else
					{
						remove_amount = soil_remove_rate * soil_diff;
						//If this pushes us into rock
						if(soil_depth + remove_amount < 0.f)
						{
							//Remove all soil, and % of rock
							soil_diff += soil_depth;//Remove soil from the equation
							remove_amount = (rock_remove_rate * soil_diff) - soil_depth;//Find amount and add the negative soil depth back in
						}
					}

					//If we are drawing lines
					if (draw_cb && particle < ERODE_LINES_UPDATE)
					{
						F32 cur_height=0;
						terrainSourceGetHeight(source, world_pos[0], world_pos[1], &cur_height, &cache ); 
						setVec3(erode_lines[update_line].points[erode_lines[update_line].length], world_pos[0], cur_height + 1.0f, world_pos[1]);
						erode_lines[update_line].colors[erode_lines[update_line].length] = ERODE_REMOVE;
						erode_lines[update_line].length++;
					}

					//If we are not just drawing lines
					if (!draw_cb)
					{
						terrainSourceDrawHeight( source, world_pos[0], world_pos[1], lod, remove_amount * remove_multi, &cache );
						terrainSourceDrawSoilDepth( source, world_pos[0], world_pos[1], lod, remove_amount, &cache );
					}
					sediment -= remove_amount;
				}

				//Move to the next point
				moveToNextMove( partical_pos, next_move, lod_diff );
			}
			//Otherwise if we are staying put
			else
			{
				F32 deposit_amount = deposit_rate;
				//If we are not just drawing lines
				if (!draw_cb)
				{
					terrainSourceDrawHeight( source, world_pos[0], world_pos[1], lod, MIN(sediment, deposit_amount) * deposit_multi, &cache );
					terrainSourceDrawSoilDepth( source, world_pos[0], world_pos[1], lod, MIN(sediment, deposit_amount), &cache );
				}
				sediment -= deposit_amount;
				if(sediment <= 0.f)
					break;
			}
		}

#if !PLATFORM_CONSOLE
        Sleep(0);
#endif

		//If we are drawing lines
		if (draw_cb && particle < ERODE_LINES_UPDATE)
		{
			//Change which line we are updating
			update_line++;
			if(update_line >= MAX_ERODE_LINES)
				update_line = 0;
			erode_lines[update_line].length = 0;
		}
	}

	//Draw Lines
	if (draw_cb)
	{
		int deposit_color = (255 << 24) | (255 << 16) | (0 << 8) | 0;//ARGB
		int	remove_color = (255 << 24) | (0 << 16) | (0 << 8) | 255;//ARGB
		int	static_color = (255 << 24) | (0 << 16) | (0 << 8) | 0;//ARGB
		for(particle = 0; particle < MAX_ERODE_LINES; particle++)
		{
			for(step=0; step < erode_lines[particle].length - 1 ; step++)
			{
				draw_cb(erode_lines[particle].points[step],
										erode_lines[particle].points[step+1],
										erode_lines[particle].colors[step] == ERODE_REMOVE ? remove_color : deposit_color);
			}
		}
    }
}

void terrainErosionDrawThermalErosion( TerrainEditorSource *source, S32 x, S32 z, int draw_lod, F32 rate, F32 soil_angle, F32 rock_angle, HeightMapCache *cache)
{
	U32 random_seed;
	int k;
	F32 alt, neighbor_alt;
	int vis_scale = (1<<draw_lod);
	F32 rock_slide_const = tan(rock_angle * 3.14159 / 180.0) * vis_scale;
	F32 soil_slide_const = tan(soil_angle * 3.14159 / 180.0) * vis_scale;
	int offset = (int)(randomPositiveF32Seeded( &random_seed, RandType_LCG ) * 9.0);
	F32 soil_depth = 0;
	rate = CLAMP(rate, 0.0f, 1.0f);

	if (	terrainSourceGetHeight( source, x, z, &alt, cache)	&&
			terrainSourceGetSoilDepth( source, x, z, &soil_depth, cache)	)
	{
		for (k = 0; k < 9; k++)
		{
			F32 diff;
			F32 soil_slide_amt = 0;
			F32 rock_slide_amt = 0;
			int index = (k+offset)%9;
			F32 offx = x + ((index%3) - 1) * vis_scale;
			F32 offz = z + ((index/3) - 1) * vis_scale;
			if (terrainSourceGetHeight( source, offx, offz, &neighbor_alt, cache))
			{
				diff = (alt - neighbor_alt);
				if (index == 0 || index == 2 || index == 6 || index == 8)
					diff *= 0.707f;
				
				if(diff < 0)
					continue;

				// calculate fall of the soil first
				if(diff > soil_slide_const)
				{
					soil_slide_amt = (diff - soil_slide_const) * rate;
					soil_slide_amt = MIN( soil_slide_amt, soil_depth );
					diff -= soil_slide_amt;
				}
				// now the rock material, if necessary
				if (diff > rock_slide_const)
				{
					rock_slide_amt = (diff - rock_slide_const) * rate;
				}

				if (rock_slide_amt > 0 || soil_slide_amt > 0)
				{
					F32 slide_amt = rock_slide_amt + soil_slide_amt;

					terrainSourceDrawHeight( source, x, z, draw_lod, -slide_amt, cache );
					terrainSourceDrawHeight( source, offx, offz, draw_lod, slide_amt, cache );

					terrainSourceDrawSoilDepth( source, x, z, draw_lod, -soil_slide_amt, cache );
					terrainSourceDrawSoilDepth( source, offx, offz, draw_lod, slide_amt, cache );
				}
			}
		}
	}
}

#endif
