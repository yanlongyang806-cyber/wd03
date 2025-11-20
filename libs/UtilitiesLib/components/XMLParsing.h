#pragma once

#ifndef _XBOX
//no xbox lib

#ifdef _FULLDEBUG
#ifdef _WIN64
#pragma comment(lib, "libexpatX64MTdebug.lib")
#else
#pragma comment(lib, "libexpatMTdebug.lib")
#endif
#else
#ifdef _WIN64
#pragma comment(lib, "libexpatX64MT.lib")
#else
#pragma comment(lib, "libexpatMT.lib")
#endif
#endif

//Global macro for linking expat
#define XML_LARGE_SIZE 1
#define XML_STATIC 1
#include "../../3rdparty/expat-2.0.1/lib/expat.h"
#include "../../3rdparty/expat-2.0.1/xmlwf/xmlfile.h"

#endif
