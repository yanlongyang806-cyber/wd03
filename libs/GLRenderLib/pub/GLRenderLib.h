#ifndef _GLRENDERLIB_H_
#define _GLRENDERLIB_H_

#include "../pub/RenderLib.h"


RdrDevice *rdrCreateDeviceWinGL(WindowCreateParams *params, HINSTANCE hInstance, bool compatible_cursor, bool threaded);


#endif //_GLRENDERLIB_H_

