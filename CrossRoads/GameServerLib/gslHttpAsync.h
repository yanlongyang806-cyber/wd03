#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct UrlArgument UrlArgument;
typedef struct UrlArgumentList UrlArgumentList;

typedef void(*gslhaCallback)(Entity *ent, const char *response, int len, int response_code, void *userdata);
typedef void(*gslhaTimeout)(Entity *ent, void *userdata);

void gslhaPrintCB(Entity *ent, const char *response, int len, int response_code, void *userdata);

void gslhaRequest(Entity *ent, UrlArgumentList *args, gslhaCallback cb, gslhaTimeout timeout_cb, int timeout, void *userdata);
