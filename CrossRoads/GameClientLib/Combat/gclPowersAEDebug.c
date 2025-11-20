#include "gclPowersAEDebug.h"
#include "PowersAEDebug.h"
#include "Entity.h"
#include "gclEntity.h"
#include "EntityMovementManager.h"
#include "Capsule.h"
#include "Character.h"

#include "Character_combat.h"
#include "CombatDebugViewer.h"
#include "gclDebugDrawPrimitives.h"


// -------------------------------------------------------
typedef struct AEPowersDebugData
{
	EntityRef	erEntity;
	S32			bDrawSourceEnt;
	S32			bDrawHitEnts;

	DDrawPrimHandle	*eaLastDrawPrimitives;
	DDrawPrimHandle	*eaClientLastDrawPrimitives;

	S32			bEnableClientSideDebug;
} AEPowersDebugData; 
#define FADE_TIME	1.5f

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------------
static AEPowersDebugData g_aePowersDebug = {0};

static Color s_red = {255, 0, 0, 255};
static Color s_yellow = {255, 255, 0, 255};
static Color s_green = {0, 255, 0, 255};
static Color s_blue = {0, 0, 255, 255};
static Color s_black = {0, 0, 0, 255};
static Color s_white = {255, 255, 255, 255};
static Color s_gray = {175, 175, 175, 255};

// ------------------------------------------------------------------------------------------------
void gclPowersAEDebug_Init()
{
	g_aePowersDebug.bDrawSourceEnt = true;
	g_aePowersDebug.bDrawHitEnts = true;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void PowersDebugAEDrawSourceEnt(int bOn)
{
	g_aePowersDebug.bDrawSourceEnt = bOn;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void PowersDebugAEDrawHitEnts(int bOn)
{
	g_aePowersDebug.bDrawHitEnts = bOn;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void PowersDebugAEDrawAll(int bOn)
{
	g_aePowersDebug.bDrawSourceEnt = bOn;
	g_aePowersDebug.bDrawHitEnts = bOn;
}

static void _powersDebugAEClear(DDrawPrimHandle	**peaPrimitivesList)
{
	S32 i;

	for (i = eaiSize(peaPrimitivesList) - 1; i >= 0; --i)
	{
		DDrawPrimHandle h = (*peaPrimitivesList)[i];
		gclDebugDrawPrimitive_Kill(h, FADE_TIME);
	}
	eaiClear(peaPrimitivesList);

}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void PowersDebugAEClear()
{
	_powersDebugAEClear(&g_aePowersDebug.eaLastDrawPrimitives);
	_powersDebugAEClear(&g_aePowersDebug.eaClientLastDrawPrimitives);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void PowersAEDebug_TurnOffAEDebug()
{
	_powersDebugAEClear(&g_aePowersDebug.eaLastDrawPrimitives);
}

// ------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(PowersDebugAEClient) ACMD_ACCESSLEVEL(7);
void PowersDebugAEEnt(Entity* e, int enabled)
{
	enabled = !!enabled;
	g_aePowersDebug.bEnableClientSideDebug = enabled;
	g_powersDebugAEOn = enabled;

	if (!enabled)
	{
		_powersDebugAEClear(&g_aePowersDebug.eaClientLastDrawPrimitives);
	}
}

Entity* gclPowersAEDebug_GetDebuggingEnt()
{
	if (g_aePowersDebug.bEnableClientSideDebug)
		return entActivePlayerPtr();
	return NULL;
}

// ------------------------------------------------------------------------------------------------
static void getCapsuleStartDir(Entity *pEnt, const Capsule* pCap, const Vec3 vBasePos, Vec3 vStartOut, Vec3 vDirOut)
{
	Quat	qRot;
	if (!mmDoesCapsuleOrientationUseRotation(pEnt->mm.movement))
	{
		Vec3 pyr;
		entGetFacePY(pEnt, pyr);

		// Ignore pitch if not flying
		if (pEnt->pChar && pEnt->pChar->pattrBasic->fFlight <= 0.f)
		{
			pyr[0] = 0.f;
		}		

		// No roll
		pyr[2] = 0.f;

		PYRToQuat(pyr, qRot);
	}
	else
	{
		entGetRot(pEnt, qRot);
	}

	quatRotateVec3(qRot, pCap->vStart, vStartOut);
	addVec3(vBasePos, vStartOut, vStartOut);
	quatRotateVec3(qRot, pCap->vDir, vDirOut);
}

// ------------------------------------------------------------------------------------------------
static bool addNewDebugDraw(DDrawPrimHandle	**peaPrimitivesList, PowerDebugAE *pAE)
{
	//if (!g_powersDebugAEOn)
	//	return false;
	//if (g_aePowersDebug.erEntity != -1 && g_aePowersDebug.erEntity != (e ? entGetRef(e) : 0))
	//{	// debugging particular entity, not the right entity
	//	return false;
	//}
	
	_powersDebugAEClear(peaPrimitivesList);
		

	if (g_aePowersDebug.bDrawSourceEnt)
	{
		Entity *e = pAE->erEnt ? entFromEntityRefAnyPartition(pAE->erEnt) : NULL;
		const Capsule* cap = e ? entGetPrimaryCapsule(e) : NULL;
		Color *pcolor = NULL;

		if (pAE->isClient)
			pcolor = &s_gray;
		else
			pcolor = &s_green;
			

		if (cap) 
		{
			Vec3 vCapPos, vCapDir;
			DDrawPrimHandle h;

			getCapsuleStartDir(e, cap, pAE->vCasterPos, vCapPos, vCapDir);

			h = gclDebugDrawPrimitive_AddCapsule(vCapPos, vCapDir, cap->fLength, cap->fRadius, pcolor, -1);
			if (h) eaiPush(peaPrimitivesList, h);
		}
	}

	if (g_aePowersDebug.bDrawHitEnts)
	{
		Color *pclr = NULL;
		if (pAE->isClient)
			pclr = &s_gray;
		else
			pclr = &s_red;
			
		FOR_EACH_IN_EARRAY(pAE->eaHitEnts, AEPowersDebugHit, phit)
		{
			Entity *ptarget = phit->erEnt ? entFromEntityRefAnyPartition(phit->erEnt) : NULL;

			if (ptarget && ptarget->pChar && ptarget->pChar->pEntParent)
			{
				const Capsule* cap = entGetPrimaryCapsule(ptarget->pChar->pEntParent);
				

				if (cap) 
				{
					Vec3 vCapPos, vCapDir;
					DDrawPrimHandle h;
					getCapsuleStartDir(ptarget, cap, phit->vPos, vCapPos, vCapDir);
					h = gclDebugDrawPrimitive_AddCapsule(vCapPos, vCapDir, cap->fLength, cap->fRadius, pclr, -1);
					if (h) eaiPush(peaPrimitivesList, h);
				}
			}
		}
		FOR_EACH_END

	}

	return true;
}


// ----------------------------------------------------------------------
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE;
void PowersAEDebug_AddAEDebug(PowerDebugAE *debugData)
{
	Color *pcolor = NULL;
	DDrawPrimHandle h;
	DDrawPrimHandle	**peaPrimitivesList = NULL;

	if (debugData->isClient)
	{
		pcolor = &s_white;
		peaPrimitivesList = &g_aePowersDebug.eaClientLastDrawPrimitives;
	}
	else
	{
		pcolor = &s_red;
		peaPrimitivesList = &g_aePowersDebug.eaLastDrawPrimitives;
	}

	if (!addNewDebugDraw(peaPrimitivesList, debugData))
	{
		return;
	}
	
	switch (debugData->eType)
	{
		case kEffectArea_Character:
		case kEffectArea_Location:
		{
			h = gclDebugDrawPrimitive_AddLocation(debugData->vCastLoc, pcolor, -1);
			if (h) eaiPush(peaPrimitivesList, h);
		}
		xcase kEffectArea_Cylinder:
		{
			h = gclDebugDrawPrimitive_AddCylinder(debugData->vCastLoc, debugData->vTargetLoc, debugData->fRadius, pcolor, -1);
			if (h) eaiPush(peaPrimitivesList, h);
		}
		xcase kEffectArea_Cone:
		{
			h = gclDebugDrawPrimitive_AddCone(debugData->vCastLoc, debugData->vTargetLoc, debugData->fArc, 
												debugData->fLength, debugData->fRadius, pcolor, -1);
			if (h) eaiPush(peaPrimitivesList, h);
		}
		xcase kEffectArea_Sphere:
		{
			h = gclDebugDrawPrimitive_AddSphere(debugData->vCastLoc, debugData->fRadius, pcolor, -1);
			if (h) eaiPush(peaPrimitivesList, h);
		}
			
	}
}

