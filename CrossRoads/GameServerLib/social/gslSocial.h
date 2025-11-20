#pragma once
GCC_SYSTEM

#include "SocialCommon.h"

typedef struct ItemDef ItemDef;
typedef struct StoredCredentials StoredCredentials;

typedef struct ActivityDataScreenshot
{
	char *title;
	char *data;
	U32 len;
} ActivityDataScreenshot;

typedef struct ActivityDataBlog
{
	char *title;
	char *text;
} ActivityDataBlog;

typedef struct ActivityDataItem
{
	char *name;
	char *def_name;
	ItemDef *def;
} ActivityDataItem;

typedef struct Entity Entity;

typedef void (*gslSocialInvokeCB)(Entity *ent, const char *service, ActivityType type, StoredCredentials *creds, ActivityVerbosity verbosity, void *data);
typedef void (*gslSocialEnrollCB)(Entity *ent, const char *service, U32 state, const char *userdata, const char *input);

// Plugin registration
void gslSocialRegister(const char *service, ActivityType type, gslSocialInvokeCB invoke_cb);
void gslSocialRegisterEnrollment(const char *service, gslSocialEnrollCB enroll_cb);

void gslSocialUpdateEnrollment(Entity *ent, const char *service, const U32 state, const char *userdata);

// Broadcast an activity item
void gslSocialActivity(Entity *ent, ActivityType type, void *data);

const char *gslGetCurrentActivity(Entity *ent);
// Set the current activity summary string
void gslSetCurrentActivity(Entity *ent, const char *string);

// Check if a given service is registered for a given activity
bool gslSocialServiceRegistered(const char *service, ActivityType type);

// Fill in a SocialServices struct for a given activity
void gslSocialServicesRegistered(SocialServices *services, ActivityType type);