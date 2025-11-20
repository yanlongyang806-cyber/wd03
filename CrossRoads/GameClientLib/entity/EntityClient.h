/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYCLIENT_H_
#define ENTITYCLIENT_H_
GCC_SYSTEM

#define FC_MAX_CHAT_BUBBLES 3
#define FC_CHAT_BUBBLE_MIN_Z 2.f
#define FC_CHAT_BUBBLE_MAX_Z 3.f

typedef struct ClientOnlyEntity ClientOnlyEntity;
typedef struct Entity Entity;
typedef struct EntityClientDamageFXData EntityClientDamageFXData;

void gclDrawStuffOverEntities(void);
void gclSetUserEntities(void);
void gclClearUserEntities(void);
bool ChatBubble_DrawFor(Entity *pEnt, F32 fZ);
void ChatBubbleStack_Process(Entity *pEnt, F32 fElapsed);

S32 gclEntityTick(const Vec3 vSourcePos, Entity *e, ClientOnlyEntity* coe, F32 elapsed);
S32 gclExternEntityDoorSequenceTick(Entity *pEntity, F32 fElapsed);
void gclExternEntityDetectable(Entity *ePlayer, Entity *eTarget);
bool gclExternEntityTargetable(Entity *ePlayer, Entity *eTarget);

typedef struct WorldVolume WorldVolume;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;

void entClientEnteredFXVolume(WorldVolume* pVolume, WorldVolumeQueryCache* pCache);
void entClientExitedFXVolume(WorldVolume* pVolume, WorldVolumeQueryCache* pCache);

void entClientRegionRules(Entity *pEnt);

extern int entClientMaxCostumeSkeletonCreationsPerFrame;
extern int entClientCostumeSkeletonCreationsThisFrame;

#define entClientCreateSkeleton(e) entClientCreateSkeletonEx((e),false)
void entClientCreateSkeletonEx(Entity* e, bool bKeepOldSkeleton);

EntityClientDamageFXData* entClientCreateDamageFxData();
void entClientFreeDamageFxData(EntityClientDamageFXData* pData);

void gclPet_UpdateLocalPlayerPetInfo();
void gclPet_DestroyRallyPoints(Entity* pPlayerEnt);


LATELINK;
void gclEntityTick_GameSpecific(Entity* pEnt);


#endif

