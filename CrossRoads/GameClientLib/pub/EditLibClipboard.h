#pragma once
GCC_SYSTEM
#ifndef __EDITLIBCLIPBOARD_H__
#define __EDITLIBCLIPBOARD_H__

#ifndef NO_EDITORS

typedef struct EMPanel EMPanel;

#endif
AUTO_ENUM;
typedef enum EcbClipTypeEnum
{
	ECB_EXPRESSION = 1,
	ECB_EXPR_LINE,
	ECB_STATE,
	ECB_STATES
} EcbClipTypeEnum;
#ifndef NO_EDITORS

typedef void (*EcbMakeTextCallback)(void*, char**);

void ecbRegisterClipType(EcbClipTypeEnum type, EcbMakeTextCallback displayNameFunc, ParseTable *pti);
const char *ecbClipTypeToString(EcbClipTypeEnum type);

EMPanel *ecbLoad(void);
void ecbSave(void);

SA_RET_NN_VALID EMPanel *ecbGetPanel(void);
int ecbAddClip(SA_PARAM_NN_VALID void *obj, EcbClipTypeEnum type);
EcbClipTypeEnum ecbGetClipByIndex(int i, SA_PARAM_NN_VALID void **obj);
void ecbRemoveClip(SA_PRE_OP_VALID SA_POST_P_FREE void *obj);
void ecbRemoveClipByIndex(int i);

#endif // NO_EDITORS

#endif // __EDITLIBCLIPBOARD_H__