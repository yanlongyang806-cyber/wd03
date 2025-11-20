#ifndef ENTITYCIVILIAN_H_
#define ENTITYCIVILIAN_H_
GCC_SYSTEM


typedef struct Entity Entity;


void gclCivilian_Initialize(Entity *e);
void gclCivilian_CleanUp(Entity *e);
void gclCivilian_Tick(Entity *e);
bool gclCivilian_IsCivilian(Entity *e);

// note: first time calling this function will not return the data as it requests data from the server.
// subsequent calls will contain the correct information.
const char** gclCivilian_GetLegDefNames();

#endif
