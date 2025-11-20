#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct UrlArgument UrlArgument;
typedef struct UrlArgumentList UrlArgumentList;

typedef void(*suCallback)(Entity *ent, const char *response, int response_code, void *userdata);
typedef void(*suTimeout)(Entity *ent, void *userdata);

char *suXmlParse(const char *data, const char *tag);
char *suXmlParseAttr(const char *data, const char *tag, const char *attr);
void suPrintCB(Entity *ent, const char *response, int response_code, void *userdata);

void suRequest(Entity *ent, UrlArgumentList *args, suCallback cb, suTimeout timeout_cb, void *userdata);