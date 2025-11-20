#pragma once

typedef struct Entity Entity;

typedef void (*gslQueuedReplyFunc)(Entity *pEnt, void *userdata);
void gslEditorOncePerFrame();
void gslEditorQueueAutoCommandReply(Entity *pEnt, gslQueuedReplyFunc func, void *userdata);