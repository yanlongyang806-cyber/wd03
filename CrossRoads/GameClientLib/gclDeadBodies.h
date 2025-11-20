#ifndef GCL_DEAD_BODIES_H
#define GCL_DEAD_BODIES_H


typedef struct Entity Entity;

void gclDeadBodies_Initialize();
void gclDeadBodies_Shutdown();
void gclDeadBodies_OncePerFrame();
void gclDeadBodies_EntityTick(Entity *e);

#endif