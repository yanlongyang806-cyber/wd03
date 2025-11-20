#ifndef GCL_SMART_RETICLE_H
#define GCL_SMART_RETICLE_H

#include "Cbox.h"

AUTO_ENUM;
typedef enum EClientReticleShape
{
	EClientReticleShape_NONE = 0,	EIGNORE
	EClientReticleShape_CIRCLE,
	EClientReticleShape_BOX,
} EClientReticleShape;


typedef struct ClientReticle
{
	EClientReticleShape	eReticleShape;

	CBox	reticleBox;
	S32		iReticlePosX;
	S32		iReticlePosY;
	S32		iReticleRadius;
	S32		iReticleInnerRadius;

} ClientReticle;


void gclReticle_OncePerFrame(F32 fElapsedTime);

void gclReticle_DisableDrawReticle();
void gclReticle_EnableDrawReticle();

void gclReticle_GetReticle(ClientReticle *pReticule, bool bCombatEntityTargeting);
void gclReticle_GetReticlePosition(bool bCombatEntityTargeting, S32 *pXOut, S32 *pYOut);

void gclReticle_GetReticleNormOffset(bool bCombatEntityTargeting, F32 *pfOffsetX, F32 *pfOffsetY);

void gclReticle_DebugDraw(ClientReticle *pReticule);
F32 gclReticle_GetTargetingBias(const ClientReticle *pReticule, const CBox *pBox, bool bCombatEntityTargeting);
bool gclReticle_IsTargetInReticle(ClientReticle *pReticle, const CBox *pTargetBox);

void gclReticle_UseReticleDefByName(const char *pchName);

#endif