#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef GROUPPROPERTIES_H
#define GROUPPROPERTIES_H

typedef struct GroupDef GroupDef;
typedef struct Packet Packet;

SA_RET_OP_STR const char *groupDefFindProperty(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_OP_STR const char* propName);
int groupHasPropertySet(SA_PARAM_OP_VALID GroupDef *def, SA_PARAM_OP_STR const char *propName);
void groupDefAddProperty(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_OP_STR const char* propName, SA_PARAM_NN_STR const char* propValue);
void groupDefAddPropertyEx(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_OP_STR const char* propName, SA_PARAM_NN_STR const char* propValue, bool ignore_depreciated);
void groupDefRemoveProperty(SA_PARAM_OP_VALID GroupDef *def, SA_PARAM_OP_STR const char *propName);

void groupDefAddVolumeType(SA_PARAM_OP_VALID GroupDef *def, SA_PARAM_OP_STR const char *volType);
void groupDefRemoveVolumeType(SA_PARAM_OP_VALID GroupDef *def, SA_PARAM_OP_STR const char *volType);
bool groupDefIsVolumeType(SA_PARAM_OP_VALID GroupDef *def, SA_PARAM_OP_STR const char *volType);


__forceinline static void groupDefAddPropertyBool(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_NN_STR const char* propName, bool propValue)
{
	if (propValue)
		groupDefAddProperty(def, propName, "1");
	else
		groupDefAddProperty(def, propName, "0");
}

__forceinline static void groupDefAddPropertyS32(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_NN_STR const char* propName, S32 propValue)
{
	char tempstr[1024];
	sprintf(tempstr, "%d", propValue);
	groupDefAddProperty(def, propName, tempstr);
}

__forceinline static void groupDefAddPropertyF32(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_NN_STR const char* propName, F32 propValue)
{
	char tempstr[1024];
	sprintf(tempstr, "%f", propValue);
	groupDefAddProperty(def, propName, tempstr);
}

__forceinline static void groupDefAddPropertyVec3(SA_PARAM_OP_VALID GroupDef* def, SA_PARAM_NN_STR const char* propName, SA_PRE_NN_RELEMS(3) const Vec3 propValue)
{
	char tempstr[1024];
	sprintf(tempstr, "%f %f %f", propValue[0], propValue[1], propValue[2]);
	groupDefAddProperty(def, propName, tempstr);
}

__forceinline static F32 groupDefGetPropertyF32(GroupDef *def, const char *name, F32 default_value)
{
	F32 ret = default_value;
	const char *prop = groupDefFindProperty(def, name);
	if (prop) sscanf(prop, "%f", &ret);
	return ret;
}

__forceinline static S32 groupDefGetPropertyS32(GroupDef *def, const char *name, S32 default_value)
{
	S32 ret = default_value;
	const char *prop = groupDefFindProperty(def, name);
	if (prop) sscanf(prop, "%d", &ret);
	return ret;
}

#endif
