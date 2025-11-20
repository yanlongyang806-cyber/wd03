#pragma once
GCC_SYSTEM

typedef struct UrlArgumentList UrlArgumentList;

typedef void JpegLibrary_ReturnJpegCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, void *pUserData);


//the http jpeg library is a way to serve up bitmaps for servermonitoring. Basically, a request
//will show up on a server with a name of the form DICTNAME_FOOBAR.jpg. Based on the DICTNAME, the
//jpeg library will dispatch the request off to one of various registered callbacks. For instance,
//FILE_FILENAME.jpg means a local file.
typedef void JpegLibrary_GetJpegCB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData);




//When you call this function, nothing may happen for a while (for instance, requesting a client
//screenshot JPEG on a gameserver will take many frames). When the request JPEG is ready,
//your callback will be called
void JpegLibrary_GetJpeg(char *pInName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData);

void JpegLibrary_RegisterCB(char *pPrefix, JpegLibrary_GetJpegCB *pCB);

//takes a local file name and converts it to the prefixed and server-specified
void JpegLibrary_GetFixedUpJpegFileName(char **ppEstrOut, char *pIn);
