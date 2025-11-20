#ifndef AIFCEXPRFUNC_H
#define AIFCEXPRFUNC_H

#include "ExpressionMinimal.h"

typedef struct Entity Entity;
typedef struct ExprContext ExprContext;


void exprFuncAnimListSet(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_DICT(AIAnimList) const char* animList);
void aiSayExternMessageVar(Entity *e, ExprContext* context,
						   const char* category, const char* name, F32 duration);
void aiSayVoiceMessage(Entity *e, ExprContext* context, const char* msg_suffix);
void aiSayMessageInternal(Entity* e, Entity *target, ExprContext* context,
							const char* messageKey, const char* pChatBubbleName, F32 duration);

#define FSMLD_UNTARGETABLE_KEY (U64)1 << 33
#define FSMLD_UNATTACKABLE_KEY (U64)2 << 33
#define FSMLD_FAKECOMBAT_KEY   (U64)3 << 33
#define FSMLD_INVULNERABLE_KEY (U64)4 << 33

#endif