#include "gclReticle.h"
#include "ClientTargeting.h"
#include "Entity.h"
#include "gclControlScheme.h"
#include "gclEntity.h"
#include "gclPlayerControl.h"
#include "GfxPrimitive.h"
#include "GraphicsLib.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "inputMouse.h"
#include "Powers.h"
#include "gclReticle_h_ast.h"
#include "gclReticle_c_ast.h"
#include "textparser.h"
#include "TextParserSimpleInheritance.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define HORIZONTAL_BIAS_MAGNITUDE 4.0f
#define VERTICAL_BIAS_MAGNITUDE 4.0f

AUTO_STRUCT;
typedef struct ClientTargetingReticle
{
	EClientReticleShape	eReticleShape;

	F32			fReticleScreenNormOffsetX;

	F32			fReticleScreenNormOffsetY;

	F32			fReticleScreenNormRadius;

	// for box type reticules
	F32			fReticleRectWidthNorm;

	F32			fReticleRectHeightNorm;

	F32			fReticleRectHeightOffsetNorm;

	F32			fTargetInnerCircleNormRadius;
	
} ClientTargetingReticle;

AUTO_STRUCT;
typedef struct ClientReticleTweenDef 
{
	// in pixels/second, target vertical resolution (1050)
	// the speed the reticle travels to the target entity
	F32		fToTargetSpeed;
	
	// in pixels/second, target vertical resolution (1050)
	// the speed the reticle travels back to the reticle position
	F32		fBackToReticleSpeed;
	
	// the ratio on the target entity's primary capsule screen projected box 
	// that the reticle will travel to
	F32		fReticlePlacementVerticalRatio;

	// above this screen ratio that the reticle speed will scale
	F32		fDistanceRatioSpeedScale;
	
} ClientReticleTweenDef;

AUTO_STRUCT;
typedef struct ClientReticleDef 
{
	// reticle def that this inherits from - applied in order, so later ones override earlier
	const char *pchName;							AST(STRUCTPARAM POOL_STRING KEY)

	ClientReticleTweenDef	*pTweenReticleDef;			


	// Optional combat entity targeting reticle
	ClientTargetingReticle	*pCombatEntityTargetingReticle;

	// Optional targeting reticle for everything but combat entities.
	// If this is defined and no CombatEntityTargetingReticle is defined,
	// this will be used for combat entities as well.
	ClientTargetingReticle	*pTargetingReticle;
	
} ClientReticleDef;


AUTO_STRUCT;
typedef struct ClientReticleSetDef
{
	// list of named reticles
	ClientReticleDef		**eaReticles;		AST(NAME("Reticle"))
		
	const char *pchDefaultDef;					AST(POOL_STRING)

} ClientReticleSetDef;


typedef struct TweenReticleInstance
{
	Vec2		vReticlePos;
	Vec2		vTargetReticlePos;
	EntityRef	erCurrentEntity;

	U32			bSnapToTargetPos : 1;
} TweenReticleInstance;


// a flag that is used via UIGens to check if it should draw the reticle cursor texture
static S32 s_disableDrawReticleRef = 0;


// the current reticle def being used
static ClientReticleDef s_currentReticleDef = {0};

static ClientReticleSetDef s_clientReticleSet = {0};

static TweenReticleInstance s_tweenReticleInstance = {0};


// --------------------------------------------------------------------------------------------------------------------
ClientReticleDef* gclReticle_FindReticleDef(const char *pchName);
void gclReticle_UseReticleDef(const ClientReticleDef *pDef);


// --------------------------------------------------------------------------------------------------------------------
static void gclReticle_PostLoad()
{
	if (s_currentReticleDef.pchName)
	{
		ClientReticleDef *pDef = gclReticle_FindReticleDef(s_currentReticleDef.pchName);

		if (pDef)
		{
			StructCopyAll(parse_ClientReticleDef, pDef, &s_currentReticleDef);
		}
	}
	
	if (!s_currentReticleDef.pchName)
	{
		ClientReticleDef *pDef = NULL;
		// try to find the default set by the file, otherwise use the first one
		if (s_clientReticleSet.pchDefaultDef)
			pDef = gclReticle_FindReticleDef(s_clientReticleSet.pchDefaultDef);

		if (!pDef)
			pDef = eaGet(&s_clientReticleSet.eaReticles, 0);

		if (pDef)
			gclReticle_UseReticleDef(pDef);
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void ReticleDefReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading ReticleDef...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	StructInit(parse_ClientReticleSetDef, &s_clientReticleSet);

	ParserLoadFiles(NULL,
					"defs/config/Reticle.def",
					"Reticle.bin",
					PARSER_OPTIONALFLAG,
					parse_ClientReticleSetDef,
					&s_clientReticleSet);
	
	loadend_printf(" done.");

	gclReticle_PostLoad();
}


// --------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(Reticle);
void gclReticle_Startup(void)
{
	ParserLoadFiles(NULL,
					"defs/config/Reticle.def",
					"Reticle.bin",
					PARSER_OPTIONALFLAG,
					parse_ClientReticleSetDef,
					&s_clientReticleSet);
	
	gclReticle_PostLoad();

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/Reticle.def", ReticleDefReload);
	}
	
}

// --------------------------------------------------------------------------------------------------------------------
bool gclReticle_HasValidSmartReticleDef()
{
	return (s_currentReticleDef.pTweenReticleDef != NULL);
}

// --------------------------------------------------------------------------------------------------------------------
// sets the current reticle def to be used. 
ClientReticleDef* gclReticle_FindReticleDef(const char *pchName)
{
	FOR_EACH_IN_EARRAY(s_clientReticleSet.eaReticles, ClientReticleDef, pDef)
	{
		if (!stricmp(pDef->pchName, pchName))
		{
			return pDef;
		}
	}
	FOR_EACH_END

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------
void gclReticle_UseReticleDef(const ClientReticleDef *pDef)
{
	if (pDef)
	{
		// pooled string compare
		if (pDef->pchName == s_currentReticleDef.pchName)
		{
			return;
		}
				
		StructCopyAll(parse_ClientReticleDef, pDef, &s_currentReticleDef);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// sets the current reticle def to be used. 
void gclReticle_UseReticleDefByName(const char *pchName)
{
	if (!pchName)
		pchName = s_clientReticleSet.pchDefaultDef;

	if (!stricmp(s_currentReticleDef.pchName, pchName))
	{	// same def
		return;
	}

	{
		ClientReticleDef *pDef = gclReticle_FindReticleDef(pchName);
		if (pDef)
		{
			StructCopyAll(parse_ClientReticleDef, pDef, &s_currentReticleDef);
			return;
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
static F32 gclReticle_GetMouseLookHardTargetRadius(void)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && entIsAiming(pEnt))
	{
		return g_CurrentScheme.fMouseLookHardTargetRadiusAim;
	}
	return g_CurrentScheme.fMouseLookHardTargetRadius;
}


// -----------------------------------------------------------------------------------------------------------------------
static F32 scaleForCurrentResolution(S32 iScreenWidth, S32 iScreenHeight)
{
#define TARGET_RESOLUTION_Y		1050.f
	
	
	return (F32)iScreenHeight / TARGET_RESOLUTION_Y;
}

// --------------------------------------------------------------------------------------------------------------------
void gclReticle_OncePerFrame(F32 fElapsedTime)
{
	Entity *pEntPlayer;
	Entity *pFoeEnt = NULL;
	ClientReticleTweenDef *pReticleDef;
	if (!gclReticle_HasValidSmartReticleDef())
		return;

	pReticleDef = s_currentReticleDef.pTweenReticleDef;

	// todo: need a function to get friendly and enemy entities
	// right now we're just getting whatever is under the mouse
	pEntPlayer = entActivePlayerPtr();

	if (clientTarget_IsTargetHard())
	{
		const ClientTargetDef *pClientTargetDef = clientTarget_GetCurrentHardTarget();
		if (pClientTargetDef)
		{
			pFoeEnt = entFromEntityRefAnyPartition(pClientTargetDef->entRef);
		}
		
	}

	if (!pFoeEnt)
		pFoeEnt = target_SelectUnderMouse(	pEntPlayer, 
											g_CurrentScheme.bMouseLookHardTargetExcludeCorpses ? kTargetType_Alive : 0,
											0,
											NULL,
											true,
											false,
											false);

	if (pFoeEnt)
	{
		CBox entityBox = {0};
		F32 fDistance;
		EntityRef erEntity = entGetRef(pFoeEnt);

		if (erEntity != s_tweenReticleInstance.erCurrentEntity)
		{
			s_tweenReticleInstance.bSnapToTargetPos = false;
			s_tweenReticleInstance.erCurrentEntity = erEntity;
		}
		
		if (entGetPrimaryCapsuleScreenBoundingBox(pFoeEnt, &entityBox, &fDistance))
		{
			// interpolate to the entity's center box position
			s_tweenReticleInstance.vTargetReticlePos[0] = (entityBox.left + entityBox.right) * 0.5f;
			s_tweenReticleInstance.vTargetReticlePos[1] = interpF32(pReticleDef->fReticlePlacementVerticalRatio, 
																	entityBox.bottom, 
																	entityBox.top);
		}
		else
		{
			pFoeEnt = NULL;
		}
	}
	

	if (!pFoeEnt)
	{
		ClientReticle	reticle = {0};
		S32 iReticlePosX, iReticlePosY;

		gclReticle_GetReticlePosition(true, &iReticlePosX, &iReticlePosY);

		s_tweenReticleInstance.bSnapToTargetPos = false;
		s_tweenReticleInstance.erCurrentEntity = 0;
		s_tweenReticleInstance.vTargetReticlePos[0] = (F32)iReticlePosX;
		s_tweenReticleInstance.vTargetReticlePos[1] = (F32)iReticlePosY;
	}


	// linear interpolation to the target position
	if (!s_tweenReticleInstance.bSnapToTargetPos)
	{
		Vec2 vDirection;
		F32 fLen, fStep, fSpeed;
		S32 iScreenHeight, iScreenWidth;

		gfxGetActiveDeviceSize(&iScreenWidth, &iScreenHeight);

		subVec2(s_tweenReticleInstance.vTargetReticlePos, s_tweenReticleInstance.vReticlePos, vDirection);
		
		fLen = normalVec2(vDirection);

		if (pFoeEnt)
		{
			fSpeed = pReticleDef->fToTargetSpeed;
		}
		else
		{
			fSpeed = pReticleDef->fBackToReticleSpeed;
		}
		
		// scale the speed based on the resolution 
		fSpeed *= scaleForCurrentResolution(iScreenWidth, iScreenHeight);
		
		if (fLen > iScreenWidth * pReticleDef->fDistanceRatioSpeedScale)
		{
			F32 fDistanceScale = fLen / (iScreenWidth * pReticleDef->fDistanceRatioSpeedScale);
			fSpeed *= fDistanceScale;
		}

		fStep = fSpeed * fElapsedTime;

		if (fStep >= fLen)
		{
			fStep = fLen;
			s_tweenReticleInstance.bSnapToTargetPos = true;
		}

		scaleAddVec2(vDirection, fStep, s_tweenReticleInstance.vReticlePos, s_tweenReticleInstance.vReticlePos);
	}
	else
	{
		copyVec2(s_tweenReticleInstance.vTargetReticlePos, s_tweenReticleInstance.vReticlePos);
	}


}

// --------------------------------------------------------------------------------------------------------------------
void gclReticle_GetReticle(ClientReticle *pReticle, bool bCombatEntityTargeting)
{
	if(g_CurrentScheme.bMouseLookHardTarget && gclPlayerControl_IsMouseLooking())
	{
		S32 iScreenX, iScreenY, iScreenNormBasis;

		gfxGetActiveSurfaceSize(&iScreenX, &iScreenY);

		iScreenNormBasis = MIN(iScreenX, iScreenY);
				
		if (s_currentReticleDef.pTargetingReticle || (bCombatEntityTargeting && s_currentReticleDef.pCombatEntityTargetingReticle))
		{
			ClientTargetingReticle *pReticleInfo;

			pReticleInfo = (bCombatEntityTargeting && s_currentReticleDef.pCombatEntityTargetingReticle) ? 
									s_currentReticleDef.pCombatEntityTargetingReticle : s_currentReticleDef.pTargetingReticle;

			pReticle->eReticleShape = pReticleInfo->eReticleShape;

			pReticle->iReticlePosX = iScreenX * pReticleInfo->fReticleScreenNormOffsetX;
			pReticle->iReticlePosY = iScreenY * pReticleInfo->fReticleScreenNormOffsetY;

			pReticle->iReticleInnerRadius = iScreenNormBasis * pReticleInfo->fTargetInnerCircleNormRadius;

			if (pReticleInfo->eReticleShape == EClientReticleShape_BOX)
			{
				S32 iReticleHeightOffset = iScreenY * pReticleInfo->fReticleRectHeightOffsetNorm;
				S32 iWidth, iHeight;

				iWidth = (iScreenNormBasis * pReticleInfo->fReticleRectWidthNorm);
				iHeight = (iScreenNormBasis * pReticleInfo->fReticleRectHeightNorm);

				pReticle->iReticleRadius = sqrtf(SQR(iWidth) + SQR(iHeight));

				BuildCBoxFromCenter(&pReticle->reticleBox, 
									pReticle->iReticlePosX,
									(pReticle->iReticlePosY + iReticleHeightOffset), 
									iWidth,
									iHeight);
			}
			else
			{
				pReticle->iReticleRadius = iScreenNormBasis * pReticleInfo->fReticleScreenNormRadius;
			}
		}
		else
		{
			F32 fTargetRadius = gclReticle_GetMouseLookHardTargetRadius();

			pReticle->eReticleShape = EClientReticleShape_CIRCLE;
			pReticle->iReticleRadius = iScreenNormBasis * fTargetRadius;
			pReticle->iReticlePosX = iScreenX * g_CurrentScheme.fMouseLookHardTargetX;
			pReticle->iReticlePosY = iScreenY * g_CurrentScheme.fMouseLookHardTargetY;
			pReticle->iReticleInnerRadius = 0;
		}
	}
	else
	{
		pReticle->eReticleShape = EClientReticleShape_NONE;
		mousePos(&pReticle->iReticlePosX, &pReticle->iReticlePosY);
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclReticle_GetReticlePosition(bool bCombatEntityTargeting, S32 *pXOut, S32 *pYOut)
{
	ClientReticle reticle = {0};
	gclReticle_GetReticle(&reticle, bCombatEntityTargeting);

	*pXOut = reticle.iReticlePosX;
	*pYOut = reticle.iReticlePosY;
}

// -------------------------------------------------------------------------------------------------------------------------
void gclReticle_GetReticleNormOffset(bool bCombatEntityTargeting, F32 *pfOffsetX, F32 *pfOffsetY)
{
	if (s_currentReticleDef.pTargetingReticle || (bCombatEntityTargeting && s_currentReticleDef.pCombatEntityTargetingReticle))
	{
		ClientTargetingReticle *pReticleInfo;
		pReticleInfo = (bCombatEntityTargeting && s_currentReticleDef.pCombatEntityTargetingReticle) ? 
						s_currentReticleDef.pCombatEntityTargetingReticle : s_currentReticleDef.pTargetingReticle;

		if (pfOffsetX)
			*pfOffsetX = pReticleInfo->fReticleScreenNormOffsetX;
		if (pfOffsetY)
			*pfOffsetY = pReticleInfo->fReticleScreenNormOffsetY;
	}
	else
	{
		if (pfOffsetX)
			*pfOffsetX = g_CurrentScheme.fMouseLookHardTargetX;
		if (pfOffsetY)
			*pfOffsetY = g_CurrentScheme.fMouseLookHardTargetY;
	}

}


// -------------------------------------------------------------------------------------------------------------------------
bool gclReticle_IsTargetInReticle(ClientReticle *pReticle, const CBox *pTargetBox)
{
	switch (pReticle->eReticleShape)
	{
		xcase EClientReticleShape_NONE:
			return point_cbox_clsn(pReticle->iReticlePosX, pReticle->iReticlePosY, pTargetBox);
		xcase EClientReticleShape_CIRCLE:
			return CBoxIntersectsCircle(pTargetBox, pReticle->iReticlePosX, pReticle->iReticlePosY, pReticle->iReticleRadius);
		xcase EClientReticleShape_BOX:
			return CBoxIntersects(pTargetBox, &pReticle->reticleBox);
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------------
F32 gclReticle_GetTargetingBias(const ClientReticle *pReticule, 
								const CBox *pBox,
								bool bCombatEntityTargeting)
{
	F32 fVertBias = 0.f;
	F32 fHorizBias = 0.f;
	F32 fWeight = 0.f;
	F32 fCX, fCY;
	F32 fWidth, fHeight;
	F32 fRatio;
		
	CBoxGetCenter( pBox, &fCX, &fCY ); 

	fWidth = pBox->hx - pBox->lx;
	fHeight = pBox->hy - pBox->ly;

	if (fWidth == 0 || fHeight == 0)
	{
		fRatio = 0.0f;
	}
	else if (fWidth >= fHeight)
	{
		fRatio = fHeight / fWidth;
	}
	else
	{
		fRatio = fWidth / fHeight;
	}

	if (fWidth != 0.0f)
		fHorizBias = HORIZONTAL_BIAS_MAGNITUDE * ( 1.f - fabsf((F32)pReticule->iReticlePosX - fCX) / (fWidth * 0.5f) );
	if (fHeight != 0.0f)
		fVertBias = VERTICAL_BIAS_MAGNITUDE * ( 1.f - fabsf((F32)pReticule->iReticlePosY - fCY) / (fHeight * 0.5f) );

	//Scale up the smaller dimension so that it is easier to hit skinny targets
	if (fWidth >= fHeight)
	{
		fVertBias *= (1.0f - fRatio) * 3.0f;
	}
	else
	{
		fHorizBias *= (1.0f - fRatio) * 3.0f;
	}

	fWeight = fHorizBias + fVertBias;
		
	if (CBoxIntersectsCircle(pBox, pReticule->iReticlePosX, pReticule->iReticlePosY, (F32)pReticule->iReticleInnerRadius))
	{	// if the reticle inner radius is touching the actual box, we are prioritizing it
		fWeight *= 2.f;
	}
	
	return fWeight;
}

// -------------------------------------------------------------------------------------------------------------------------
void gclReticle_DebugDraw(ClientReticle *pReticule)
{
	gfxDrawBox(	pReticule->iReticlePosX-5, 
				pReticule->iReticlePosY-5, 
				pReticule->iReticlePosX+5, 
				pReticule->iReticlePosY+5, 0.0f, colorFromRGBA(0x00FF00FF));

	switch (pReticule->eReticleShape)
	{
		xcase EClientReticleShape_CIRCLE:
			gfxDrawEllipse(	pReticule->iReticlePosX - pReticule->iReticleRadius,
							pReticule->iReticlePosY - pReticule->iReticleRadius,
							pReticule->iReticlePosX + pReticule->iReticleRadius,
							pReticule->iReticlePosY + pReticule->iReticleRadius,
							0, 25, colorFromRGBA(0xFF0000FF));

		xcase EClientReticleShape_BOX:
			gfxDrawCBox(&pReticule->reticleBox, 0.0f, colorFromRGBA(0xFF0000FF));
	}

	if (pReticule->iReticleInnerRadius)
	{
		gfxDrawEllipse(	pReticule->iReticlePosX - pReticule->iReticleInnerRadius,
						pReticule->iReticlePosY - pReticule->iReticleInnerRadius,
						pReticule->iReticlePosX + pReticule->iReticleInnerRadius,
						pReticule->iReticlePosY + pReticule->iReticleInnerRadius,
						0, 25, colorFromRGBA(0x0000FFFF));
	}
	
}


// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTargetingReticleXPos");
F32 exprGetTargetingReticleXPos(void)
{
	ClientReticle reticle = { 0 };

	// Get the reticle (if calling reticule_GetReticle separately for X and Y positions
	// become costly we might want to re-factor this function.
	gclReticle_GetReticle(&reticle, false);

	return reticle.iReticlePosX;
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTargetingReticleYPos");
F32 exprGetTargetingReticleYPos(void)
{
	ClientReticle reticle = { 0 };

	// Get the reticle (if calling reticule_GetReticle separately for X and Y positions
	// become costly we might want to re-factor this function.
	gclReticle_GetReticle(&reticle, false);

	return reticle.iReticlePosY;
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ReticleGetSmartXPos");
F32 exprReticleGetSmartXPos()
{
	return s_tweenReticleInstance.vReticlePos[0];
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ReticleGetSmartYPos");
F32 exprReticleGetSmartYPos()
{
	return s_tweenReticleInstance.vReticlePos[1];
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTargetReticleSize");
F32 clientTargetExprGetTargetReticleSize(void)
{
	if (g_CurrentScheme.bMouseLookHardTarget && gclPlayerControl_IsMouseLooking())
	{
		int iWidth, iHeight;
		gfxGetActiveSurfaceSize(&iWidth, &iHeight);
		return MIN(iWidth, iHeight) * gclReticle_GetMouseLookHardTargetRadius();
	}
	return 0.0f;
}


// --------------------------------------------------------------------------------------------------------------------
void gclReticle_DisableDrawReticle()
{
	s_disableDrawReticleRef = true;
}

// --------------------------------------------------------------------------------------------------------------------
void gclReticle_EnableDrawReticle()
{
	s_disableDrawReticleRef = false;
}

// --------------------------------------------------------------------------------------------------------------------
// todo: remove the below hacks or make an actual feature - intended for some quick prototyping in NW
// --------------------------------------------------------------------------------------------------------------------
static bool s_bHideReticleHack = false;

AUTO_COMMAND ACMD_NAME(HideReticlePrototype);
void HideReticleHack(bool bReticleHack)
{
	s_bHideReticleHack = bReticleHack;
}

// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ReticleShouldHide");
bool exprReticleShouldHide()
{
	return s_bHideReticleHack || s_disableDrawReticleRef;
}


// --------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ReticleIsEntityOverEntity");
bool exprReticleIsEntityOverEntity(SA_PARAM_OP_VALID Entity *pEntity, bool bCombatEntityTargeting)
{
	if (pEntity)
	{
		F32 fSBDistance;
		CBox entBox = {0};

		if (entGetScreenBoundingBox(pEntity, &entBox, &fSBDistance, true))
		{
			ClientReticle reticle = {0};
			gclReticle_GetReticle(&reticle, bCombatEntityTargeting);
			return gclReticle_IsTargetInReticle(&reticle, &entBox);
		}
	}

	return false;
}

#include "gclReticle_c_ast.c"
#include "gclReticle_h_ast.c"
