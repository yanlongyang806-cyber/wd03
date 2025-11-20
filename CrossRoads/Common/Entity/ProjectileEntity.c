#include "ProjectileEntity.h"
#include "entCritter.h"
#include "error.h"
#include "mathutil.h"
#include "ProjectileEntity_h_ast.h"
#include "ResourceManager.h"
#include "ReferenceSystem.h"
#include "Quat.h"
#include "file.h"

#if GAMESERVER
#include "gslProjectileEntity.h"
#endif

DictionaryHandle g_hProjectileDefDict;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// -------------------------------------------------------------------------------------------------------------------
S32 projectileEntityDefValidate(ProjectileEntityDef *def)
{
	S32 bReturn = true;
	if (def->mmSettings.fWorldCollisionRadius == 0.f && 
		def->mmSettings.fEntityCollisionRadius == 0.f &&
		def->mmSettings.fRange == 0.f && 
		def->mmSettings.fLifetime == 0.f)
	{
		ErrorFilenamef(def->pchFilename, "%s: Projectile def found to not have world collision, projectile collisio, a range and a valid lifetime. This projectile will not do anything.", def->pchName);
		bReturn = false;
	}

	// Validate that we've got a power def
	if(	!REF_STRING_FROM_HANDLE(def->hPowerOnExpire)  &&
		!REF_STRING_FROM_HANDLE(def->hPowerOnCollideWorld) &&
		!REF_STRING_FROM_HANDLE(def->hPowerOnHitEnt) && 
		!REF_STRING_FROM_HANDLE(def->hPowerOnHitGotoEnt) &&
		!REF_STRING_FROM_HANDLE(def->hPowerOnDeath))
	{
		ErrorFilenamef(def->pchFilename,"%s: has no powers defined. Must have at least one: PowerOnExpire, PowerOnCollideWall, PowerOnCollideScenery, PowerOnHitGotoEnt, PowerOnDeath", def->pchName);
		bReturn = false;
	}

	if(REF_STRING_FROM_HANDLE(def->hPowerOnExpire) && !GET_REF(def->hPowerOnExpire))
	{
		ErrorFilenamef(def->pchFilename,"%s: has PowerOnExpire defined but it is invalid (currently %s)", 
						def->pchName, REF_STRING_FROM_HANDLE(def->hPowerOnExpire) );
		bReturn = false;
	}
	if(REF_STRING_FROM_HANDLE(def->hPowerOnCollideWorld) && !GET_REF(def->hPowerOnCollideWorld))
	{
		ErrorFilenamef(def->pchFilename,"%s: has PowerOnCollideWorld defined but it is invalid (currently %s)", 
						def->pchName, REF_STRING_FROM_HANDLE(def->hPowerOnCollideWorld) );
		bReturn = false;
	}
	if(REF_STRING_FROM_HANDLE(def->hPowerOnHitEnt) && !GET_REF(def->hPowerOnHitEnt))
	{
		ErrorFilenamef(def->pchFilename,"%s: has PowerOnHitEnt defined but it is invalid (currently %s)", 
						def->pchName, REF_STRING_FROM_HANDLE(def->hPowerOnHitEnt) );
		bReturn = false;
	}
	if(REF_STRING_FROM_HANDLE(def->hPowerOnHitGotoEnt) && !GET_REF(def->hPowerOnHitGotoEnt))
	{
		ErrorFilenamef(def->pchFilename,"%s: has PowerOnHitGotoEnt defined but it is invalid (currently %s)", 
			def->pchName, REF_STRING_FROM_HANDLE(def->hPowerOnHitGotoEnt) );
		bReturn = false;
	}
	if(REF_STRING_FROM_HANDLE(def->hPowerOnDeath) && !GET_REF(def->hPowerOnDeath))
	{
		ErrorFilenamef(def->pchFilename,"%s: has PowerOnDeath defined but it is invalid (currently %s)", 
			def->pchName, REF_STRING_FROM_HANDLE(def->hPowerOnDeath) );
		bReturn = false;
	}
	if(REF_STRING_FROM_HANDLE(def->hCostume) && !GET_REF(def->hCostume))
	{
		ErrorFilenamef(def->pchFilename,"%s: has Costume defined but it is invalid (currently %s)", 
						def->pchName, REF_STRING_FROM_HANDLE(def->hCostume) );
		bReturn = false;
	}

	

	return bReturn;
}

// -------------------------------------------------------------------------------------------------------------------
void projectileEntityDefFixup(ProjectileEntityDef *def)
{
	if (def->mmSettings.fWorldCollisionRadius < 0)
	{
		def->mmSettings.fWorldCollisionRadius = 0.f;
	}
	if (def->mmSettings.fEntityCollisionRadius < 0)
	{
		def->mmSettings.fWorldCollisionRadius = 0.f;
	}
	if (def->mmSettings.fAcceleration < 0)
	{
		def->mmSettings.fAcceleration = -def->mmSettings.fAcceleration;
	}

	if (def->eProjectileType == EProjectileType_BEAM)
	{	// these settings are ignored for beam projectile types
		def->mmSettings.fWorldCollisionRadius = 0.f;
		def->mmSettings.fSpeed = 0.f;
		def->mmSettings.fTargetSpeed = 0.f;
		def->mmSettings.fGravity = 0.f;
		def->mmSettings.fWorldCollisionRadius = 0.f;
		def->mmSettings.fSurfaceObstructAngle = 0.f;
		def->mmSettings.fStepHeight = 0.f;
		def->mmSettings.bHugGround = 0;
		def->mmSettings.bWallsObstruct = 0;
	}

	if (def->fResetHitEntsTimer)
		def->fResetHitEntsTimer = MAX(def->fResetHitEntsTimer, 0.2f);

#if GAMESERVER
	gslProjectileDef_FixupExpressions(def);
#endif
	
}


// -------------------------------------------------------------------------------------------------------------------
static int projectileEntityDefValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ProjectileEntityDef *pProjDef, U32 userID)
{
	switch (eType)
	{	
//		xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
			//resFixPooledFilename(&pCritter->pchFileName, CRITTER_BASE_DIR, pCritter->pchScope, pCritter->pchName, CRITTER_EXTENSION);

		xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
			projectileEntityDefFixup(pProjDef);
			return VALIDATE_HANDLED;	
//		xcase RESVALIDATE_POST_BINNING: // Called on all objects in dictionary after any load/reload of this dictionary
	
		xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
			projectileEntityDefValidate(pProjDef);
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
int RegisterProjectileDefDict(void)
{
#if GAMESERVER
	gslProjectile_AutoRun();
#endif
	g_hProjectileDefDict = RefSystem_RegisterSelfDefiningDictionary("ProjectileEntityDef",false, parse_ProjectileEntityDef, true, true, NULL);
		
	resDictManageValidation(g_hProjectileDefDict, projectileEntityDefValidateCB);
	resDictSetDisplayName(g_hProjectileDefDict, "Projectile", "Projectiles", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hProjectileDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) 
		{
			resDictMaintainInfoIndex(g_hProjectileDefDict, ".Name", NULL, NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hProjectileDefDict, 8, false, resClientRequestSendReferentCommand);
	}

	// resDictProvideMissingRequiresEditMode(g_hProjectileDefDict);	

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(Projectiles) ASTRT_DEPS(Powers);
void Projectile_LoadDefs(void)
{
	if(!IsClient())
	{
#if GAMESERVER
		gslProjectile_StaticInit();
#endif
		resLoadResourcesFromDisk(g_hProjectileDefDict, "defs/projectiles", ".projectile", "projectiles.bin",  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}

}


// -------------------------------------------------------------------------------------------------------------------
// Returns true if the ray hits
S32 RayVsInfiniteCylinder(	const Vec3 vRaySt, 
							const Vec3 vRayDir, 
							const Vec3 vCylinderStPos, 
							const Vec3 vCylinderDir, 
							F32 fCylinderRadius, 
							F32 *pfHitTimeOut,
							S32 *bInside)
{
	Vec3 RayPtToCylinder, d0_d0_V_V, p0_O_V_V;
	F32 a,b,c, time_neg, time_pos;

	/*d0_d0_V_V = D0 - (Do.V)V */
	c = dotVec3(vRayDir, vCylinderDir);

	scaleAddVec3(vCylinderDir, -c, vRayDir, d0_d0_V_V);
	
	//A = [ D0 - (Do.V)V ]^2
	a = dotVec3( d0_d0_V_V, d0_d0_V_V );

	if( a == 0.f )
		return false;//ray is parallel to cylinder
	
	//get the vector from the ray's origin to the cylinder origin
	//RayPtToCylinder = p0-O
	subVec3( vRaySt, vCylinderStPos, RayPtToCylinder );

	//p0_O_V_V = (p0-0) - [(p0-O).V]V
	c = dotVec3(RayPtToCylinder, vCylinderDir);
	scaleAddVec3(vCylinderDir, -c, RayPtToCylinder, p0_O_V_V);
	
	//
	b = 2 * dotVec3(p0_O_V_V, d0_d0_V_V);
	c = dotVec3( p0_O_V_V, p0_O_V_V) - SQR(fCylinderRadius);

	/*store the discriminent in c*/
	c = (b*b) - (4.f*a*c);

	if( c < 0.f ) /*NO SOLUTION*/
		return false;

	//do some precalculations
	a = 2.f*a;
	c = sqrtf(c);

	//calculate positive time
	time_pos = (-b + c)/a;
	if( time_pos < 0.f )//case 1
		return 0; // NONE; intersection before the ray start

	time_neg = (-b - c)/a;
	if( time_neg < 0.f )
	{//ray starts inside the cylinder, use time_pos;
		if (pfHitTimeOut)
			*pfHitTimeOut = time_pos;
		if (bInside) 
			*bInside = true;
		return true;
	}

	if (pfHitTimeOut)
		*pfHitTimeOut = time_neg;
	
	if (bInside) 
		*bInside = false;
		
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
S32 RayVsSphere(const Vec3 vRaySt, 
				const Vec3 vRayDir, 
				const Vec3 vSphereCenter, 
				F32 fSphereRadius, 
				F32 *pfHitTime,
				S32 *bInside)
{
	F32	a, b, c, time_neg, time_pos;
	Vec3 vRayPt_Center; //doubles as vector and time variables to save memory


	//get the direction from the ray's origin to the sphere center
	subVec3( vRaySt, vSphereCenter, vRayPt_Center );

	//get each value for the
	a = dotVec3( vRayDir, vRayDir );
	b = 2.f * ( dotVec3( vRayPt_Center, vRayDir ) );
	c = dotVec3( vRayPt_Center, vRayPt_Center ) - SQR(fSphereRadius);

	//store the discriminant in c
	c = (b*b) - (4.f*a*c);

	if( c < 0.f ) //NO SOLUTION
		return false;

	a = 2.f*a;
	c = sqrtf(c);

	//calculate positive time
	time_pos = (-b + c)/a;
	if( time_pos < 0.f ) 
		return false;

	time_neg = (-b - c)/a;
	if( time_neg < 0.f )
	{// inside the sphere
		if (pfHitTime)
			*pfHitTime = time_pos;
		if (bInside) 
			*bInside = true;
		return true;	
	}
	
	assert( (0.f <= time_neg) && (time_neg <= time_pos) );

	if (pfHitTime)
		*pfHitTime = time_neg;
	if (bInside) 
		*bInside = false;
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
S32 LineSegmentVsCapsule(	const Capsule *pcap, 
							const Vec3 vCapPos, 
							const Quat qCapRot,
							const Vec3 vLineStart,
							const Vec3 vLineEnd,
							F32 *pfHitTime)
{
	Vec3 vCapStart, vCapEnd;
	Vec3 vCapDir;
	Vec3 vLineDirLen;
	F32 fMinHitTime = FLT_MAX;
	F32 fHitTime;
	S32 bInside = false;

	quatRotateVec3(qCapRot, pcap->vStart, vCapStart);
	addVec3(vCapStart, vCapPos, vCapStart);
	quatRotateVec3(qCapRot, pcap->vDir, vCapDir);
	scaleAddVec3(vCapDir, pcap->fLength, vCapStart, vCapEnd);

	subVec3(vLineEnd, vLineStart, vLineDirLen);

	if (RayVsSphere(vLineStart, vLineDirLen, vCapStart, pcap->fRadius, &fHitTime, &bInside))
	{
		if (fHitTime <= 1.f)
			fMinHitTime = fHitTime;
		else if (bInside)
		{	// we're inside the sphere- hitTime is 0
			if (pfHitTime) 
				*pfHitTime = 0.f;
			return true;
		}
	}
	if (RayVsSphere(vLineStart, vLineDirLen, vCapEnd, pcap->fRadius, &fHitTime, &bInside))
	{
		if (fHitTime <= 1.f && fHitTime < fMinHitTime)
			fMinHitTime = fHitTime;
		else if (bInside)
		{	// we're inside the sphere- hitTime is 0
			if (pfHitTime) 
				*pfHitTime = 0.f;
			return true;
		}
	}
	if (RayVsInfiniteCylinder(vLineStart, vLineDirLen, vCapStart, vCapDir, pcap->fRadius, &fHitTime, &bInside))
	{
		if (fHitTime <= 1.f && fHitTime < fMinHitTime)
		{
			Vec3 vHitPoint, vCylinderToHitPt;
			F32 fDot;
			// validate that the point is within the finite part of they cylinder
			scaleAddVec3(vLineDirLen, fHitTime, vLineStart, vHitPoint);
			subVec3(vHitPoint, vCapStart, vCylinderToHitPt);
			fDot = dotVec3(vHitPoint, vCapDir);
			if (fDot >= 0.f && fDot < pcap->fLength)
			{
				fMinHitTime = fHitTime;

				if (bInside)
				{	// we're inside the sphere- hitTime is 0
					if (pfHitTime) 
						*pfHitTime = 0.f;
					return true;
				}
			}
		}
	}

	if (fMinHitTime != FLT_MAX)
	{
		if (pfHitTime)
			*pfHitTime = fMinHitTime;
		return true;
	}

	return false;
}


// -------------------------------------------------------------------------------------------------------------------
S32 SphereSweepVsCapsule(	const Capsule *pcap, 
							const Vec3 vCapPos, 
							const Quat qCapRot,	
							const Vec3 vSphereSt,
							const Vec3 vSphereEnd,
							F32 fSphereRadius,
							F32 *pfHitTime)
{
	if (!sameVec3(vSphereSt, vSphereEnd))
	{
		Capsule	tmpCap;
		tmpCap = *pcap;
		tmpCap.fRadius += fSphereRadius;
		return (LineSegmentVsCapsule(&tmpCap, vCapPos, qCapRot, vSphereSt, vSphereEnd, pfHitTime));
	}
	else
	{
		Vec3 vCapStart, vCapEnd;
		Vec3 vCapDir;
		Mat4 mtx;
		quatRotateVec3(qCapRot, pcap->vStart, vCapStart);
		addVec3(vCapStart, vCapPos, vCapStart);
		quatRotateVec3(qCapRot, pcap->vDir, vCapDir);
		scaleAddVec3(vCapDir, pcap->fLength, vCapStart, vCapEnd);

		identityMat4(mtx);

		if (capsuleSphereCollision((F32*)vCapStart, (F32*)vCapDir, pcap->fLength, pcap->fRadius, mtx, (F32*)vSphereSt, fSphereRadius))
		{
			if (pfHitTime) *pfHitTime = 0.f;
			return true;
		}
	}
	
	return false;
}

#include "ProjectileEntity_h_ast.c"
