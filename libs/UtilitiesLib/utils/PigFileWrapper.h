#pragma once
GCC_SYSTEM
/***************************************************************************
 
 
 
 *
 * Module Description:
 *   Wrapper for piglib functions to support fopen, fread, fseek, etc
 * 
 ***************************************************************************/
#ifndef _PIGFILEWRAPPER_H
#define _PIGFILEWRAPPER_H

#include "piglib.h"

typedef struct PigFileHandle PigFileHandle;

void *pig_fopen(const char *name,const char *how);
void *pig_fopen_pfd(PigFileDescriptor *pfd,const char *how);
int pig_fclose(PigFileHandle *handle);
int pig_fseek(PigFileHandle *handle,long dist,int whence);
long pig_ftell(PigFileHandle *handle);
long pig_fread(PigFileHandle *handle, void *buf, long size MEM_DBG_PARMS);
char *pig_fgets(PigFileHandle *handle,char *buf,int len MEM_DBG_PARMS);
int pig_setvbuf(PigFileHandle *handle, char *buffer, int mode, size_t size);
int pig_fgetc(PigFileHandle *handle MEM_DBG_PARMS);
#define pig_getc(handle) pig_fgetc(handle MEM_DBG_PARMS_INIT)
//long pig_fwrite(const void *buf,long size1,long size2,FileWrapper *fw)=NULL;
//void pig_fflush(FileWrapper *fw)=NULL;
//int pig_fprintf (FileWrapper *fw, FORMAT_STR const char *format, ...);
//int pig_fscanf(FileWrapper* fw, FORMAT_STR const char* format, ...);
//int pig_fputc(int c,FileWrapper* fw);

void *pig_lockRealPointer(PigFileHandle *handle);
void pig_unlockRealPointer(PigFileHandle *handle);

void *pig_duphandle(PigFileHandle *handle); // Returns a Win32 file handle

long pig_filelength(PigFileHandle *handle);

void initPigFileHandles(void);

void pig_freeOldBuffers(void); // Frees buffers on handles that haven't been accessed in a long time
void pig_freeBuffer(PigFileHandle *handle); // Frees a buffer, if there is one, on a zipped file handle

#endif