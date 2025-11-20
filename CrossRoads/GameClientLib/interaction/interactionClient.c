/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "interactionClient.h"

#include "wlInteraction.h"
#include "GfxCamera.h"
#include "GraphicsLib.h"
#include "CBox.h"

F32 objGetScreenDist(WorldInteractionNode *pNode)
{
	int w, h;
	GfxCameraView *view = gfxGetActiveCameraView();
	Vec3 t, pos3d;

	gfxGetActiveSurfaceSize(&w, &h);
	wlInteractionNodeGetWorldMid(pNode,t);
	mulVecMat4(t, view->frustum.viewmat, pos3d);

	return view->frustum.znear - pos3d[2];
}

void objGetScreenBoundingBox(WorldInteractionNode *pNode, CBox *pBox, F32 *pfDistance, bool bClipToScreen, bool bIgnoreDimensionsAndUseCenterPoint)
{
	GfxCameraView *pView = gfxGetActiveCameraView();
	S32 iWidth;
	S32 iHeight;
	Vec3 v3Min;
	Vec3 v3Max;
	Vec3 v3Mid;
	Vec2 v2Min;
	Vec2 v2Max;
	//S32 i = 0;
	gfxGetActiveSurfaceSize(&iWidth, &iHeight);
	wlInteractionNodeGetWorldBounds(pNode,v3Min,v3Max,v3Mid);

	if (bIgnoreDimensionsAndUseCenterPoint)
	{
		copyVec3(v3Mid, v3Min);
		copyVec3(v3Mid, v3Max);
	}

	if(!gfxGetScreenExtents(&pView->frustum, 
							pView->projection_matrix, 
							NULL, 
							v3Min, 
							v3Max, 
							v2Min, 
							v2Max, 
							pfDistance, 
							bClipToScreen))
	{
		ZeroStruct(pBox);
		*pfDistance = -1;
	}
	else
	{
		pBox->lx = v2Min[0] * iWidth;
		pBox->hx = v2Max[0] * iWidth;
		pBox->ly = iHeight - v2Min[1] * iHeight;
		pBox->hy = iHeight - v2Max[1] * iHeight;
		CBoxNormalize(pBox);
	}
}

#include "StaticWorld/WorldCellEntry.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"
#include "StringCache.h"

// Upon further reflection, this approach is flawed.  I will be taking this function back out
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(SendFXMessageToNamed) ACMD_CLIENTCMD ACMD_PRIVATE;
void SendFXMessageToNamed(const char * pcNodeName, const char * pchMessage)
{
	WorldInteractionEntry *pInteractionEntry = RefSystem_ReferentFromString(ENTRY_DICTIONARY, pcNodeName);
	if (pInteractionEntry)
	{
		int i;
		for (i=0;i<eaSize(&pInteractionEntry->child_entries);i++)
		{
			if (pInteractionEntry->child_entries[i]->type == WCENT_FX)
			{
				WorldFXEntry * pFXEntry = (WorldFXEntry*)pInteractionEntry->child_entries[i];
				if (pFXEntry->fx_manager)
				{
					DynFxManager* pFxMan = dynFxManFromGuid(pFXEntry->fx_manager);
					dynFxManBroadcastMessage(pFxMan, allocFindString(pchMessage));
				}
			}
		}
	}
}