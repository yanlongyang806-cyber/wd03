
#include "entworldcoll.h"
#include <string.h>
#include "mathutil.h"
#include "collide.h"
#include "WorldColl.h"
#include "timing.h"



static void collInitFromMotion(	CollInfo* coll,
								const MotionState *motion)
{
	if(	!wcCellGetWorldColl(motion->wcCell,
							&coll->wc)
		||
		!wcCellGetSceneAndOffset(	motion->wcCell,
									&coll->psdkScene,
									coll->sceneOffset))
	{
		coll->wc = NULL;
		coll->psdkScene = NULL;
	}

	coll->filterBits = motion->filterBits;
	coll->actorIgnoredCB = motion->actorIgnoredCB;
	coll->userPointer = motion->userPointer;
	copyVec3(motion->vel, coll->motion_dir);
}

static int SlideWall(MotionState *motion,Vec3 top,Vec3 bot)
{
	int			i;
	CollInfo	coll;
	F32			d,h;
	const F32	rad = ENT_WORLDCAP_DEFAULT_RADIUS;
	PERFINFO_AUTO_START_FUNC();
	
	collInitFromMotion(&coll, motion);
	coll.motion_dir[1] = 0.f;

	motion->hit_ground = 0;
	motion->hit_surface = 0;

	h = bot[1];
	for(i=0;i<3;i++){
		Vec3 dv;
		Vec3 pt1;
		Vec3 pt2;
		
		copyVec3(bot,pt1);
		copyVec3(bot,pt2);
		pt1[1] += ENT_WORLDCAP_DEFAULT_HEIGHT_OFFUP;
		pt2[1] += motion->step_height;
		if(!collideWithAccelerator(pt1,pt2,&coll,rad,COLL_DISTFROMCENTER | COLL_CYLINDER | COLL_BOTHSIDES)){
			copyVec3(top,pt1);
			copyVec3(bot,pt2);
			pt1[1] += 3.5f;
			pt2[1] += 3.5f;

			subVec3(top,bot,dv);
			if(	lengthVec3Squared(dv) < 1.f)
			{
								copyVec3(bot,top);
				PERFINFO_AUTO_STOP();
				return 0;
			}
			if (!collideWithAccelerator(pt1,pt2,&coll,rad * 0.667f,COLL_DISTFROMCENTER))
			{
				copyVec3(bot,top);
				PERFINFO_AUTO_STOP();
				return 0;
			}else{
				PERFINFO_AUTO_STOP();
				return 1;
			}
		}

		subVec3(coll.mat[3],bot,dv);
		motion->hit_surface = 1;

		if(	dv[1] <= 0.0f ||
			coll.mat[1][1] >= -.7f ||
			dotVec3(motion->vel, coll.mat[1]) >= 0)
		{
			dv[1] = 0.0f;
			d = rad + 0.05f - normalVec3XZ(dv);
			
			negateVec3(dv, motion->surface_normal);
			
			scaleVec3(dv,d,dv);
			subVec3(bot,dv,bot);
		}else{
			Vec3	vBoxExents;
			F32		dotDv;
			F32		dotExtent;

			vBoxExents[1] = ENT_WORLDCAP_DEFAULT_HEIGHT_OFFUP;
			vBoxExents[0] = (dv[0] < 0.f) ? -rad : rad; 
			vBoxExents[2] = (dv[2] < 0.f) ? -rad : rad; 
			dotExtent = -dotVec3(coll.mat[1], vBoxExents);
			
			dotDv = -dotVec3(coll.mat[1], dv);
			d = dotExtent + 0.05f - dotDv;

			if(SQR(d) > lengthVec3Squared(motion->vel)+0.05f){
				dv[1] = 0.0f;
				d = rad + 0.05f - normalVec3(dv);
				
				negateVec3(dv, motion->surface_normal);
				
				scaleVec3(dv,d,dv);
				subVec3(bot,dv,bot);
			}else{
				copyVec3(coll.mat[1], motion->surface_normal);
				assert(FINITEVEC3(motion->surface_normal));

				scaleVec3(coll.mat[1],d,dv);
				addVec3(bot, dv, bot);
			}
		}
	}

	if(motion->vel[1] > 0){
		motion->vel[1] = 0;
	}
	
	PERFINFO_AUTO_STOP();
	return 2;
}

static int GroundHeight(MotionState *motion,CollInfo *coll)
{
	Vec3			top;
	Vec3			bot;
	int				ret;

	copyVec3(motion->pos,top);
	copyVec3(motion->pos,bot);
	top[1] += 2.5f - motion->vel[1];
	bot[1] += motion->vel[1] - 0.5f;
	coll->coll_count = 0;
	coll->coll_max = 0;
	ret = collideWithAccelerator(top,bot,coll,ENT_WORLDCAP_DEFAULT_RADIUS,COLL_DISTFROMSTART|COLL_IGNOREONEWAY);// |jj COLL_GATHERTRIS);
	if(	ret &&
		coll->mat[1][1] < 0.7f)
	{
		ret = collideWithAccelerator(top,bot,coll,ENT_WORLDCAP_DEFAULT_RADIUS, COLL_DISTFROMSTART|COLL_DISTFROMSTARTEXACT|COLL_IGNOREONEWAY);// | COLL_GATHERTRIS);
	}

	if(!ret){
		F32 minHeight = -15000;//bfixme
		
		coll->mat[3][1] = minHeight;
		
		if(bot[1] < minHeight){
			static CTri bottom_tri;
	
			coll->mat[1][0] = 0;
			coll->mat[1][1] = 1;
			coll->mat[1][2] = 0;

			bottom_tri.norm[0] = 0;
			bottom_tri.norm[1] = 1;
			bottom_tri.norm[2] = 0;
			
			coll->ctri = &bottom_tri;

			ret = 1;
		}
	}
	return ret;
}

static void CheckFeet(MotionState *motion)
{
	F32				new_height;
	F32				plr_height;
	CollInfo		coll;
	int				first = 1;
	Vec3			dv;
	int				hit;
	F32				heightDiff;
	
	collInitFromMotion(&coll, motion);

retry:
	hit = GroundHeight(motion,&coll);

	new_height = coll.mat[3][1];
	plr_height = motion->pos[1];

	// Is the entity more than 0.05 feet above the ground?
	heightDiff = new_height - plr_height;

	if (heightDiff < -0.3f)
	{
		zeroVec3(motion->ground_normal);
		return;
	}

	// The entity is less than 0.3 feet above the ground.  The entity
	// should be considered as "not falling."
	// If the entity thinks it's falling, it means it's time to make the entity
	// land.

	copyVec3(	coll.mat[1],
				motion->ground_normal);
	
	if (heightDiff > motion->step_height)
	{
		if (first)
		{
			Vec3 newpos;
			subVec3(motion->pos,motion->last_pos,dv);
			scaleVec3(dv,0.5,dv);
			addVec3(motion->last_pos,dv,newpos);
			if (new_height - plr_height > 40){
				vecY(newpos) = new_height;
			}
			copyVec3(newpos,motion->pos);
			first = 0;
			goto retry;
		}

		copyVec3(motion->last_pos,motion->pos);
		return;
	}
		
	
	if ((motion->use_sticky_ground || heightDiff >= 0.f) && 
		heightDiff <= motion->step_height)
	{
		vecY(motion->pos) = new_height;
	}
	motion->hit_ground = 1;
}

static int CheckHead(MotionState *motion,int ok_to_move)
{
	Vec3			top;
	Vec3			bot;
	int				ret;
	CollInfo		coll = {0};
	F32				diff;

	collInitFromMotion(&coll, motion);
	
	copyVec3(motion->pos,top);
	copyVec3(motion->pos,bot);
	top[1] += 6.0;
	bot[1] += 1.5;
	ret = collideWithAccelerator(bot,top,&coll,ENT_WORLDCAP_DEFAULT_RADIUS,COLL_DISTFROMSTART | COLL_CYLINDER);
	if (ret)
	{
		if (!ok_to_move)
			return 1;
		diff = motion->pos[1] + 6 - coll.mat[3][1];
		if (diff > 1.5)
		{
			copyVec3(motion->last_pos,motion->pos);
		}
		else
		{
			Vec3 newpos;
			copyVec3(motion->pos, newpos);
			vecY(newpos) += diff;
			copyVec3(newpos,motion->pos);
		}
		return 1;
	}
	return 0;
}

void worldMoveMe(MotionState *motion)
{
	Vec3 top;
	Vec3 bot;
	Vec3 test;

	if (motion->step_height <= 0.f)
	{
		motion->step_height = motion->is_player ?
								ENT_WORLDCAP_DEFAULT_PLAYER_STEP_HEIGHT :
								ENT_WORLDCAP_DEFAULT_CRITTER_STEP_HEIGHT;
	}

	copyVec3(motion->last_pos, top);
	copyVec3(motion->pos, bot);
	if(SlideWall(motion, top, bot)){
		motion->stuck_head = STUCK_COMPLETELY;
		copyVec3(motion->last_pos,motion->pos);
	}else{
		motion->stuck_head = 0;
		copyVec3(top,motion->pos);
	}

	copyVec3(motion->pos, test);

 	CheckFeet(motion);

	if (fabs(motion->pos[1] - test[1]) > 0.00001)
	{
		if (CheckHead(motion,0))
		{
			if (motion->vel[1] > 0)
			{
				motion->vel[1] = 0;
			}
			motion->stuck_head = STUCK_SLIDE;
			copyVec3(motion->last_pos,motion->pos);
		}
	}
	else if (motion->stuck_head == STUCK_COMPLETELY)
	{
		if (CheckHead(motion,1))
		{
			copyVec3(motion->pos,test);

			CheckFeet(motion);

			if (fabs(motion->pos[1] - test[1]) > 0.00001)
			{
				copyVec3(motion->last_pos,motion->pos);
			}
		}
	}
}

