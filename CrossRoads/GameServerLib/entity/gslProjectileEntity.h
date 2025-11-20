#ifndef _GSLPROJECTILE_ENTITY
#define _GSLPROJECTILE_ENTITY


typedef struct Entity Entity;
typedef struct ProjectileEntityDef ProjectileEntityDef;
typedef struct MovementRequester MovementRequester;
typedef U32 EntityRef;

typedef struct ProjCreateParams
{
	// required.
	ProjectileEntityDef *pDef;
	
	// required.
	F32 *pvStartPos;

	// required if erTarget is NOT defined
	F32 *pvDirection;

	// optional
	F32 *pqRot;

	// optional.
	EntityRef erTarget;

	// optional.
	F32 *pvTargetPos;

	// optional
	F32 *pvTrajectorySourcePos;
	F32 *pvTrajectorySourceDir;

	// optional 
	F32 *pvCreateOffset;

	// optional.
	Entity *pOwner;

	F32 fHue;

	S32 bSnapToGround;

} ProjCreateParams;


Entity* gslProjectile_CreateProjectile(S32 iPartitionIdx, ProjCreateParams *pCreateParams);

void gslProjectile_UpdateProjectile(F32 fDTime, SA_PARAM_NN_VALID Entity *pEnt);

void gslProjectile_Destroy(Entity *pEnt);

void gslProjectile_HitEntitiesToFG(SA_PARAM_NN_VALID Entity *eProjectile, EntityRef *eaiHitEntities);

void gslProjectile_Expire(Entity *pEnt, bool bIgnoreExpireEventExpression);

// when reported from the requester that we hit scenery
void gslProjectile_HitSceneryToFG(SA_PARAM_NN_VALID Entity *pEnt);

bool gslProjectile_ShouldDestroy(SA_PARAM_NN_VALID Entity *pEnt);

void gslProjectile_AutoRun();

void gslProjectile_StaticInit();

bool gslProjectileDef_FixupExpressions(ProjectileEntityDef *pDef);


#endif