/*
	EventSystem manages the event_system and fmod_system objects in order to abstract
	away the C++-ness of them.  It adds a little bit of function call overhead, but not much.

	TODO(am):	Fix loading intelligence
				Fix project management?
*/

//there's a #define of the CRT function remove() in memcheck.h which screws things up here if we don't turn it off because
//FMOD DSP has a member function called remove()
#include "memcheck.h"
#undef remove

#ifndef STUB_SOUNDLIB

// Timing.h must not be included in an extern "C" block.
#include "timing.h"

#include "mathutil.h"

#include "math.h"
#include "string.h"
#include "file.h"

#include "error.h"
#include "utils.h"
#include "MemoryMonitor.h"
#include "strings_opt.h"
#include "wininclude.h"
#include "endian.h"
#include <stdlib.h>
#include "stashtable.h"
#include "MemoryMonitor.h"
#include "MemoryPool.h"

#include "GfxConsole.h"
#include "StashTable.h"
#include "earray.h"

#include "SoundLib.h"
#include "sndLibPrivate.h"
#include "event_sys.h"

#include "sndSource.h"

extern "C" {
#include "EString.h"
#include "fileLoader.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TimedCallback.h"
#include "trivia.h"
}

#include "fmod.hpp"
#include "fmod_event.hpp"
#include "fmod_event_net.h"
#include "fmod_event_net.hpp"




#define _FMOD_SETTINGS_H
#define FMOD_DEBUG

//#define DEBUG_SOUND_TIME

#ifdef DEBUG_SOUND_TIME
#define OSTS		int __start, __time1, __time2;
#define T0			__time2 = __start = timeGetTime(); \
					__time1 = __time2;
#define TI			__time2 = timeGetTime(); \
					conPrintfUpdate("%s %d: %d", __FUNCTION__, __LINE__, __time2-__time1); \
					__time1 = __time2;
#define TN			__time2 = timeGetTime(); \
					conPrintfUpdate("%s %d: %d", __FUNCTION__, __LINE__, __time2-__time1); \
					conPrintfUpdate("Total %s: %d", __FUNCTION__, __time2 - __start);
#else
#define OSTS		
#define T0			
#define TI			
#define TN
#endif

#define REBUILD_ME 8
#define FMOD_DEBUG_LIBS
 
#if _PS3
    // Use the same sound media path as the win32 version
	#define SOUND_MEDIA_PATH "sound/win32/"
#elif _XBOX
	#define SOUND_MEDIA_PATH "sound/xbox360/"
	#pragma comment(lib, "xaudio2.lib")
	#pragma comment(lib, "xmp.lib")
	#ifdef FMOD_DEBUG_LIBS
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_ex_xbox360DEBUG.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_net_xbox360DEBUG.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_xbox360DEBUG.lib")
	#else
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_ex_xbox360.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_xbox360.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_net_xbox360.lib")
	#endif
#else
	#define SOUND_MEDIA_PATH "sound/win32/"
	#pragma comment(lib, "msacm32.lib")
	#ifdef FMOD_DEBUG_LIBS
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_ex_win32DEBUG.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_win32DEBUG.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_net_win32DEBUG.lib")
	#else
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_ex_win32.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_win32.lib")
		#pragma comment(lib, "../../3rdparty/fmod/lib/fmod_event_net_win32.lib")
	#endif
#endif

#if _PS3
#elif _XBOX
	#define SOUND_MEDIA_PATH "sound/xbox360/"
#else
	#define SOUND_MEDIA_PATH "sound/win32/"
#endif


typedef struct DriverInfo
{
	int driverid; 
	int minfreq;
	int maxfreq;
	char drivername[50];
	FMOD_CAPS caps;
	FMOD_SPEAKERMODE speakermode;	
} DriverInfo;

typedef struct FMODCallbackData
{
	sndEventCallback cb;
	void* ud;
} FMODCallbackData;

#define MAX_CALLBACKS FMOD_EVENT_CALLBACKTYPE_EVENTGET+2
#define FMOD_EVENT_CALLBACKTYPE_EVENTGET FMOD_EVENT_CALLBACKTYPE_SOUNDDEF_SELECTINDEX+1
#define FMOD_EVENT_ACTIVE (FMOD_EVENT_STATE_PLAYING|FMOD_EVENT_STATE_CHANNELSACTIVE)

typedef struct CallbackInfo
{
	FMODCallbackData **callbacks[MAX_CALLBACKS];
} CallbackInfo;

typedef struct FileInfo
{
	char name[CRYPTIC_MAX_PATH];
	U32 filesize;
	FILE *fp;
	U32 refcount;
}FileInfo;

typedef struct VirtualFP
{
	U32 pos;
	FileInfo *fi;
	FILE *fp;

	U32 closed : 1;
} VirtualFP;

typedef struct MemEntry {
	int size;
	int count;
	const char *key;
} MemEntry;

static const char *s_pcBootupLanguage = NULL;

FMOD::EventSystem *event_system = NULL;
FMOD::System *fmod_system = NULL;
#ifdef FMOD_SUPPORT_MEMORYTRACKER
FMOD::MemoryTracker static_tracker;
#endif
FMOD_EVENT_WAVEBANKINFO *wavebankinfos = NULL;

VirtualFP** fileArray = 0;
void ** oncePerMapList = 0;
StashTable fpStash = 0;
StashTable cbStash = 0;
StashTable memStash = 0;
CallbackInfo cbMasters;
CallbackInfo cbMastersFinal;

typedef FMOD_RESULT (F_CALLBACK *FMODCallback)(FMOD::Event *event, FMOD_EVENT_CALLBACKTYPE type, void *p1, void *p2, void *ud);

static FMOD_RESULT F_CALLBACK fmodMasterCallback(FMOD_EVENT *e, FMOD_EVENT_CALLBACKTYPE type, void *p1, void *p2, void *userdata);

#define FILE_BLOCK_SIZE				2048

// The ratio of units used by the sound system, defaults to meters
#define DISTANCEFACTOR 1.0f 

static StashTable complainFiles = 0;

MP_DEFINE(VirtualFP);
CRITICAL_SECTION fmod_file_crit;

FMOD_RESULT F_CALLBACK cbopen(const char *name, int unicode, unsigned int *filesize, void **handle, void **userdata)
{
	char tempname[1000];

	EnterCriticalSection(&fmod_file_crit);

	if (name)
	{
		FILE *fp = NULL;
		FileInfo *fi;
		VirtualFP *vfp = NULL;

		if(!stashFindPointer(fpStash, name, (void**)&fi))
		{
			if (!fileLocateRead(name,tempname))
			{
				if(!complainFiles)
				{
					complainFiles = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
				}
				if(!stashFindInt(complainFiles, name, NULL))
				{
					Vec3 pos;
					sndGetListenerPosition(pos);
					triviaPrintf("Pos:", "%f %f %f", vecParamsXYZ(pos));
					ErrorFilenamef(name, "Unable to find file: %s", name);
					triviaRemoveEntry("Pos:");
					stashAddInt(complainFiles, name, 1, 1);
				}

				LeaveCriticalSection(&fmod_file_crit);
				return FMOD_ERR_FILE_NOTFOUND;
			}

			if(!strcmp(tempname, ""))
			{
				LeaveCriticalSection(&fmod_file_crit);
				return FMOD_ERR_FILE_NOTFOUND;
			}

			if(g_audio_state.sound_developer)
			{
				if(!strstr(tempname, "hogg"))
				{
					// Copy file and open that instead
					char copyname[MAX_PATH];

					strcpy(copyname, tempname);
					strcat(copyname, ".bak");

					if(fileNewerAbsolute(copyname, tempname))
					{
						//printf("Copying %s to %s\n", tempname, copyname);
						fileCopy(tempname, copyname);
					}
					//printf("Opening %s\n", copyname);
					fp = fopen(copyname, "rb");
				}
				else
				{
					//printf("Opening %s\n", tempname);
					fp = fopen(tempname, "rb");
				}
			}
			else
			{
				//printf("Opening %s\n", tempname);
				fp = fopen(tempname, "rb");
			}

			if (!fp)
			{
				LeaveCriticalSection(&fmod_file_crit);
				return FMOD_ERR_FILE_NOTFOUND;
			}
			fi = callocStruct(FileInfo);
			strcpy(fi->name, name);
			devassertmsg(strlen(name)<260, "Filename too long");
			fi->fp = fp;
			fi->filesize = fileSize(name);
			fi->refcount = 0;

			stashAddPointer(fpStash, fi->name, fi, 1);
		}

		MP_CREATE(VirtualFP, 10);

		*filesize = fi->filesize;

		fi->refcount++;
		vfp = MP_ALLOC(VirtualFP);
		vfp->fp = fi->fp;
		vfp->pos = 0;
		vfp->fi = fi;
		*filesize = fi->filesize;
		*userdata = vfp;
		*handle = (void*)((intptr_t)eaPush((cEArrayHandle*)&fileArray, vfp)+1);
	}
	
	LeaveCriticalSection(&fmod_file_crit);

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK cbclose(void *handle, void *userdata)
{
	int index = ((intptr_t)handle)-1;
	VirtualFP *vfp;
	FileInfo *fi;

	EnterCriticalSection(&fmod_file_crit);

	if (!handle)
	{
		LeaveCriticalSection(&fmod_file_crit);
		return FMOD_ERR_INVALID_PARAM;
	}

	vfp = fileArray[index];
	devassertmsg(vfp==userdata, "Bad userdata for file.\n");
	devassert(!vfp->closed);
	fi = vfp->fi;

	fi->refcount--;

	if(fi->refcount==0
#if !_PS3
        && fi->fp->iomode==IO_WINIO
#endif
    ) {
		fclose(fi->fp);

		stashRemovePointer(fpStash, fi->name, NULL);

		free(fi);
	}

	vfp->closed = 1;

	if(g_audio_state.debug_level < 3)
	{
		MP_FREE(VirtualFP, vfp);
		fileArray[index] = NULL;
	}

	LeaveCriticalSection(&fmod_file_crit);

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK cbread(void *handle, void *buffer, unsigned int sizebytes, unsigned int *bytesread, void *userdata)
{
	int index = ((intptr_t)handle)-1;
	VirtualFP *vfp;

	EnterCriticalSection(&fmod_file_crit);

	if (!handle)
	{
		LeaveCriticalSection(&fmod_file_crit);
		return FMOD_ERR_INVALID_PARAM;
	}

	vfp = fileArray[index];
	devassertmsg(vfp==userdata, "Bad userdata for file.\n");
	assert(!vfp->closed);

	if (bytesread)
	{
		fseek(vfp->fp, vfp->pos, SEEK_SET);
		*bytesread = (int)fread(buffer, 1, sizebytes, vfp->fp);
		vfp->pos += *bytesread;

		if (*bytesread < sizebytes)
		{
			LeaveCriticalSection(&fmod_file_crit);
			return FMOD_ERR_FILE_EOF;
		}
	}

	LeaveCriticalSection(&fmod_file_crit);
	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK cbseek(void *handle, unsigned int pos, void *userdata)
{
	int index = ((intptr_t)handle)-1;
	VirtualFP *vfp;

	EnterCriticalSection(&fmod_file_crit);

	if (!handle)
	{
		LeaveCriticalSection(&fmod_file_crit);
		return FMOD_ERR_INVALID_PARAM;
	}

	vfp = fileArray[index];
	devassertmsg(vfp==userdata, "Bad userdata for file.\n");
	assert(!vfp->closed);
	vfp->pos = pos;

	LeaveCriticalSection(&fmod_file_crit);
	return FMOD_OK;
}

S32 fmodClearFileStash(void)
{
	int i;
	StashTableIterator iter;
	StashElement elem;

	EnterCriticalSection(&fmod_file_crit);

	stashGetIterator(fpStash, &iter);
	while(stashGetNextElement(&iter, &elem))
	{
		FileInfo *fi = (FileInfo*)stashElementGetPointer(elem);

		fclose(fi->fp);

		free(fi);
	}

	stashTableClear(fpStash);

	for(i=0; i<eaSize((cEArrayHandle*)&fileArray); i++)
	{
		VirtualFP *file = fileArray[i];
		MP_FREE(VirtualFP, file);
	}
	eaClear((cEArrayHandle*)&fileArray);

	LeaveCriticalSection(&fmod_file_crit);

	return 1;
}

void *audioMemory = NULL;

typedef struct FileLine
{
	char *origstr;
	char *file;
	int line;
} FileLine;

StashTable g_fmodSrcStrLookup = NULL;

FileLine* fmodFileLineFromSrcStr(const char* str)
{
	char* ptr = NULL;
	FileLine *fileline = NULL;

	if(!g_fmodSrcStrLookup)
		g_fmodSrcStrLookup = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);

	if(!stashFindPointer(g_fmodSrcStrLookup, str, (void**)&fileline))
	{
		char* fileptr;
		char* intptr;
		char* closeparen;
		fileline = callocStruct(FileLine);

		fileline->origstr = strdup(str);
		fileptr = strrchr(fileline->origstr, '\\');
		if(!fileptr) fileptr = strrchr(fileline->origstr, '/');
		if(fileptr)
			fileline->file = fileptr+1;
		else
			fileline->file = fileline->origstr;
		intptr = strchr(fileline->file, '(');
		intptr[0] = 0;
		intptr++;

		closeparen = strchr(intptr, ')');
		closeparen[0] = 0;

		fileline->line = atoi(intptr);

		stashAddPointer(g_fmodSrcStrLookup, str, fileline, true);
	}

	return fileline;
}

void * F_CALLBACK fmodMemAlloc(unsigned int size, FMOD_MEMORY_TYPE type, const char* srcstr)
{
	void* result;
	FileLine *fileline = fmodFileLineFromSrcStr(srcstr);

	result = _malloc_dbg(size, _NORMAL_BLOCK, fileline->file, fileline->line);

	return result;
}

void * F_CALLBACK fmodMemRealloc(void *ptr, unsigned int size, FMOD_MEMORY_TYPE type, const char* srcstr)
{
	void *result = NULL;
	FileLine *fileline = fmodFileLineFromSrcStr(srcstr);

	
	
	result = _realloc_dbg(ptr, size, _NORMAL_BLOCK, fileline->file, fileline->line);

	return result;
}

void F_CALLBACK fmodFree(void *ptr, FMOD_MEMORY_TYPE type, const char* srcstr)
{
	free(ptr);
}

void fmodInitSystem(void)
{
	FMOD_RESULT result;

	if (g_audio_state.noaudio)
		return;

	if (!audioMemory)
	{
#if _PS3
		#if PS3_USE_VRAM
		audioMemory = vram_malloc(soundBufferSize,128,"VideoMemory:Sound");
		#else
		audioMemory = malloc_aligned(soundBufferSize,128,"Sound-PhysicalAlloc");
		FMOD_Memory_Initialize(audioMemory,soundBufferSize,NULL,NULL,NULL,0);
		#endif
#elif _XBOX
		audioMemory = physicalmalloc(soundBufferSize,0, "Sound-PhysicalAlloc");
		FMOD_Memory_Initialize(physicalptr(audioMemory),soundBufferSize,NULL,NULL,NULL,0);
#else
		// fixed mem pool

		if(!snd_enable_debug_mem)
		{
			audioMemory = malloc(soundBufferSize);
			FMOD_Memory_Initialize(audioMemory,soundBufferSize,NULL,NULL,NULL,0);
		}
		else
		{
			FMOD_Memory_Initialize(NULL, 0, fmodMemAlloc, fmodMemRealloc, fmodFree, FMOD_MEMORY_NORMAL);
		}
#endif
	}

    if(!event_system)
	{
		result = FMOD::EventSystem_Create(&event_system);
		
		if(!event_system)
		{
			Errorf("Unable to create FMOD EventSystem.  Blame Adam.");
			g_audio_state.noaudio = 1;
			if (audioMemory)
			{
				ANALYSIS_ASSUME(audioMemory);
#if _PS3
#if PS3_USE_VRAM
				vram_free(audioMemory,"VideoMemory:Sound");
#else
				free_aligned(audioMemory,"Sound-PhysicalAlloc");
#endif
#elif _XBOX
				physicalfree(audioMemory, "Sound-PhysicalAlloc");
#else
				free(audioMemory);
#endif
				audioMemory = NULL;
			}
			return;
		}
		result = event_system->getSystemObject(&fmod_system);
		FMOD_ErrCheck(result);

		if (s_pcBootupLanguage) {
			fmodSetLanguage(s_pcBootupLanguage);
			s_pcBootupLanguage = NULL;
		}
	}
}

void fmodInitDriverInfo(void)
{
	if(g_audio_state.noaudio)
		return;

	fmodInitSystem();

	fmodUpdateDriverInfo();
}

void fmodUpdateDriverInfo(void)
{
	int i, numdrivers = 0;

	if(g_audio_state.noaudio)
		return;

	eaDestroyEx((EArrayHandle*)&g_audio_state.soundDrivers, NULL);

	fmod_system->getNumDrivers(&numdrivers);

	for(i=0; i<numdrivers; i++)
	{
		SoundDriverInfo *sdi = (SoundDriverInfo*)calloc(1, sizeof(SoundDriverInfo));

#ifdef UNICODE
		{
			wchar_t driverNameWide[100];
			fmod_system->getDriverInfoW(i, (short*)driverNameWide, 100, NULL);
			WideToUTF8StrConvert(driverNameWide, sdi->drivername, ARRAY_SIZE(sdi->drivername));
		}
#else
		{
			char drivernameACP[100];
	
			fmod_system->getDriverInfo(i, SAFESTR(drivernameACP), NULL);
			ACPToUTF8(drivernameACP, SAFESTR(sdi->drivername));
		}
#endif
		fmod_system->getDriverCaps(	i, 
									&sdi->caps, 
									NULL,
									&sdi->speakermode);

		eaPush((cEArrayHandle*)&g_audio_state.soundDrivers, sdi);
	}

	g_audio_state.curDriver = AUDIO_DEFAULT_DEVICE_ID; // force the default system device
	FMOD_SetDriver(g_audio_state.curDriver);
}

void FMOD_SetDriver(int driverIndex)
{
	if(fmod_system)
	{
		fmod_system->setDriver(driverIndex);
		g_audio_state.curDriver = driverIndex;
	}
}

FMOD_RESULT F_CALLBACK FMOD_SystemCallback(FMOD_SYSTEM *system, FMOD_SYSTEM_CALLBACKTYPE type, void *commanddata1, void *commanddata2)
{
	if (type == FMOD_SYSTEM_CALLBACKTYPE_DEVICELISTCHANGED) {
		sndDeviceListsChanged();
	}
	return FMOD_OK;
}

S32 FMOD_EventSystem_ReInit(void)
{
	unsigned int version;
	FMOD_RESULT result;
	FMOD_ADVANCEDSETTINGS settings = {0};
	char *extra_driver_data = NULL;
#if _PS3
    FMOD_PS3_EXTRADRIVERDATA ps3extradriverdata;
#endif

	if (g_audio_state.noaudio)
		return 1;

	fmodInitSystem();
	if(!fmod_system)
	{
		g_audio_state.noaudio = 1;
		return FMOD_ERR_INTERNAL;
	}

	//FMOD_DEBUG_ALL & ~FMOD_DEBUG_TYPE_FILE & ~FMOD_DEBUG_LEVEL_HINT & ~FMOD_DEBUG_TYPE_THREAD
	FMOD::Debug_SetLevel(0);

    result = fmod_system->getVersion(&version);
	g_audio_state.fmod_version = version;
	assert(result==FMOD_OK);
	if (version < FMOD_VERSION)
	{
		Alertf("Error!  You are using an old version of FMOD %08x.  This program requires %08x.  Getting latest should fix this.  If not, blame Adam.\n", version, FMOD_VERSION);
		g_audio_state.noaudio = 1;
		
#if _PS3
		#if PS3_USE_VRAM
		vram_free(audioMemory, "VideoMemory:Sound");
		#else
        free_aligned(audioMemory, "Sound-PhysicalAlloc");
		#endif
#elif _XBOX
        physicalfree(audioMemory, "Sound-PhysicalAlloc");
#else
		free(audioMemory);
#endif
        audioMemory = NULL;
		return result;
	}
	settings.cbsize = sizeof(settings);
	settings.eventqueuesize = 256;
	settings.maxMPEGcodecs = 64;
	fmod_system->setAdvancedSettings(&settings);

	if(!g_audio_state.noSoundCard && eaSize(&g_audio_state.soundDrivers))
	{
#if !_PS3 // PS3 only supports FMOD_SPEAKERMODE_7POINT1 so speaker mode can't be changed
		FMOD_ErrCheck(fmod_system->setSpeakerMode(g_audio_state.soundDrivers[g_audio_state.curDriver]->speakermode));
#endif
	}
	else
	{
		fmod_system->setOutput(FMOD_OUTPUTTYPE_NOSOUND);
	}

	// we've been instructed to write the output to file
	if(g_audio_state.outputFilePath)
	{
		fmod_system->setOutput(FMOD_OUTPUTTYPE_WAVWRITER);
		extra_driver_data = g_audio_state.outputFilePath;
	}

#if _PS3
	assert(audioMemory != NULL);
    memset(&ps3extradriverdata, 0, sizeof(FMOD_PS3_EXTRADRIVERDATA));

    CellSpurs* spurs = GetPs3SpursInstance();
    ps3extradriverdata.spurs = spurs;                                /* Using SPURS */
    ps3extradriverdata.spursmode = FMOD_PS3_SPURSMODE_CREATECONTEXT; // default: FMOD_PS3_SPURSMODE_NOCONTEXT

    static const uint8_t spurs_taskset_priorities[8] = {0,2,2,};
    ps3extradriverdata.spurs_taskset_priorities = const_cast<uint8_t*>(spurs_taskset_priorities);

	#if PS3_USE_VRAM
	ps3extradriverdata.rsx_pool         = ((NiNode*)audioMemory)->ptr;	/* Pointer to RSX memory pool */
	ps3extradriverdata.rsx_pool_size    = soundBufferSize;				/* Size of RSX memory pool */
	#endif

    extra_driver_data = (char*)&ps3extradriverdata;
#endif

	result = event_system->init(MAX_CHANNELS, 
								g_audio_state.d_useNetListener ? 
									FMOD_INIT_ENABLE_PROFILE : 
									FMOD_INIT_NORMAL/*FMOD_INIT_VOL0_BECOMES_VIRTUAL*/, 
								extra_driver_data, 
								FMOD_EVENT_INIT_FAIL_ON_MAXSTREAMS);

	if(result == FMOD_ERR_NET_SOCKET_ERROR && g_audio_state.d_useNetListener)
	{
		result = event_system->init(MAX_CHANNELS, 
									FMOD_INIT_NORMAL, 
									extra_driver_data, 
									FMOD_EVENT_INIT_FAIL_ON_MAXSTREAMS);
	}

	memMonitorTrackUserMemory("Fmod untracked", 1, 750000, MM_ALLOC);
	if(result!=FMOD_OK)
	{
		event_system = NULL;
		g_audio_state.noaudio = 1;

#if _PS3
		#if PS3_USE_VRAM
		vram_free(audioMemory, "VideoMemory:Sound");
		#else
		free_aligned(audioMemory, "Sound-PhysicalAlloc");
		#endif
#elif _XBOX
        physicalfree(audioMemory, "Sound-PhysicalAlloc");
#else
		free(audioMemory);
#endif
        audioMemory = NULL;
		return 0;
	}

	if(g_audio_state.audition)
	{
		result = FMOD::NetEventSystem_Init(event_system);

		if(FMOD_ErrIsFatal(result))
		{
			return 0;
		}
	}

	if (g_audio_state.surround)
	{	
		result = fmod_system->set3DSettings(1.0, DISTANCEFACTOR, 1.0f);
		if (FMOD_ErrIsFatal(result))
		{
			g_audio_state.surroundFailed = 1;
			g_audio_state.surround = 0;
		}
	}

	result = fmod_system->setFileSystem(cbopen,cbclose,cbread,cbseek,NULL,NULL,FILE_BLOCK_SIZE);
	if (FMOD_ErrIsFatal(result))
	{
		Alertf("Unable to initialize FMOD Filesystem.");
		g_audio_state.noaudio = 1;
		return result;
	}
	
	result = fmod_system->setCallback(FMOD_SystemCallback);
	if (FMOD_ErrIsFatal(result))
	{
		Alertf("Unable to set the FMOD system callback.h");
		g_audio_state.noaudio = 1;
		return result;
	}

	event_system->setMediaPath(SOUND_MEDIA_PATH);

	//fmod_system->setGeometrySettings(10000);

	g_audio_state.inited = 1;

	return FMOD_OK;
}

//FMOD_RESULT FMOD_EventSystem_Init(void)
S32 fmodEventSystemInit(void)
{
	InitializeCriticalSection(&fmod_file_crit);
	FMOD_RESULT result = (FMOD_RESULT)FMOD_EventSystem_ReInit();
	if(FMOD_ErrIsFatal(result))
	{
		return FMOD_ERR_INTERNAL;
	}
	wavebankinfos = (FMOD_EVENT_WAVEBANKINFO*)calloc(NUM_WAVEBANK_INFOS, sizeof(FMOD_EVENT_WAVEBANKINFO));
	fpStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	cbStash = stashTableCreateAddress(100);
	memStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);

	return FMOD_OK;
}

void fmodSetLanguage(const char *language)
{
	if (event_system) {
		if (!language || !strcmpi(language,"English")) {
			event_system->setLanguage("");
		//Uncomment this if we ever need to support multiple chinese text languages (simplified & traditional) with a single audio language
		//} else if (strstri(language,"Chinese")) {
		//	event_system->setLanguage("Chinese");
		} else {
			event_system->setLanguage(language);
		}
	}
	else
	{
		s_pcBootupLanguage = language;
	}
}

const char *fmodGetLanguage(void)
{
	static char language[64];
	
	if (event_system) {
		event_system->getLanguage(language);
		if (language[0] == '\0') {
			strcpy(language, "English");
		}
	} else {
		strcpy(language, "FMOD is missing event_system");
	}

	return language;
}

const char* fmodGetMediaPath(void)
{
	return SOUND_MEDIA_PATH;
}

FMOD_RESULT FMOD_EventSystem_LoadBank(const char* bank, const char* full_name)
{
	FMOD::EventGroup *group = NULL;
	FMOD::EventProject *project = NULL;
	FMOD_EVENT_PROJECTINFO projinfo;
	FMOD_RESULT result;

	result = event_system->load(bank, NULL, &project);
	
	if(!project)
	{
		return result;
	}

	result = project->getInfo(&projinfo);
	stashAddPointer(sndProjectStash, projinfo.name, strdup(full_name), 1);

	// check for errors related to building & checking in the data if this is someone on the audio team
	if(g_audio_state.loadtimeErrorCheck)
	{
		S32 iNumEvents;

		project->getNumEvents(&iNumEvents);

		for (S32 i = 0; i < iNumEvents; i++)
		{
			FMOD::Event *pEvent;

			result = project->getEventByProjectID(i, FMOD_EVENT_DEFAULT|FMOD_EVENT_INFOONLY, &pEvent);

			if (result == FMOD_OK)
			{
				FMOD_EVENT_INFO eventInfo;
				char *pcEventName;

				eventInfo.maxwavebanks = NUM_WAVEBANK_INFOS;
				eventInfo.wavebankinfo = wavebankinfos;
				result = pEvent->getInfo(NULL, &pcEventName, &eventInfo);

				if (result == FMOD_OK)
				{
					result = project->getEventByProjectID(i, FMOD_EVENT_DEFAULT, &pEvent);

					if (result == FMOD_OK)
					{
						FMOD::EventGroup *pParent = NULL;
						FMOD_RESULT freeResult = FMOD_OK;

						FMOD_EventSystem_GetParentGroup(pEvent, (void**)&pParent);

						if(!pParent)
						{
							Errorf("Failed to free event %s from project %s due to lack of parent during %s\n",  pcEventName, full_name, __FUNCTION__);
						}
						else if (FMOD_OK != pParent->freeEventData(pEvent, true))
						{
							Errorf("Failed to free event %s from project %s due to error during %s\n", pcEventName, full_name, __FUNCTION__);
						}
					}
					else if (result == FMOD_ERR_EVENT_MISMATCH)
					{
						ErrorFilenamef(full_name, "FMOD Error during test load of event %s - FSB data mismatches the FEV it was compiled with OR the stream/sample mode it was meant to be created with was different OR the FEV was built for a different platform.", pcEventName);
					}
				}
			}
		}
	}

	return result;
}

FMOD_RESULT FMOD_EventSystem_UnloadAll()
{
	if(!event_system)
	{
		return FMOD_OK;
	}
	return event_system->unload();
}

FMOD_RESULT FMOD_EventSystem_ClearAll(void)
{
	FMOD_EventSystem_UnloadAll();

	fmodClearFileStash();
	
	return FMOD_OK;
}

/*
FMOD_RESULT FMOD_EventSystem_PrintDebug(int which, ESPrintFunc print)
{
	static char buff1[50], buff2[20];
	if(which & EVENT_DBGPRT_DRIVER)
	{
		if(g_driver_info.speakermode == FMOD_SPEAKERMODE_RAW)
		{
			sprintf_s(buff2, 20, "Raw");
		}
		if(g_driver_info.speakermode == FMOD_SPEAKERMODE_MONO)
		{
			sprintf_s(buff2, 20, "Mono");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_STEREO)
		{
			sprintf_s(buff2, 20, "Stereo");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_QUAD)
		{
			sprintf_s(buff2, 20, "Quad");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_SURROUND)
		{
			sprintf_s(buff2, 20, "Surround");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_5POINT1)
		{
			sprintf_s(buff2, 20, "5.1");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_7POINT1)
		{
			sprintf_s(buff2, 20, "7.1");
		}
		else if(g_driver_info.speakermode == FMOD_SPEAKERMODE_PROLOGIC)
		{
			sprintf_s(buff2, 20, "Prologic");
		}
		else 
		{
			sprintf_s(buff2, 20, "Error 1");
		}

		print("Sound Driver Information: Name: %s - MinF: %d - MaxF: %d - Mode: %s",
						g_driver_info.drivername, g_driver_info.minfreq, g_driver_info.maxfreq, buff2);
		print("\tCapabilities:");
		if(g_driver_info.caps & FMOD_CAPS_HARDWARE)
		{
			print("\t\tDevice supports hardware mixing.");
		}
		if(g_driver_info.caps & FMOD_CAPS_HARDWARE_EMULATED)
		{
			print("\t\tDevice supports FMOD_HARDWARE buf mixing will be done on CPU by kernel.");
		}
		if(g_driver_info.caps & FMOD_CAPS_OUTPUT_MULTICHANNEL)
		{
			print("\t\tDevice can do multichannel output.");
		}
		if(g_driver_info.caps & FMOD_CAPS_OUTPUT_FORMAT_PCM8)
		{
			print("\t\tDevice can output to 8bit integer PCM.");
		}
		if(g_driver_info.caps & FMOD_CAPS_OUTPUT_FORMAT_PCM16)
		{
			print("\t\tDevice can output to 16bit integer PCM.");
		}
		if(g_driver_info.caps & FMOD_CAPS_OUTPUT_FORMAT_PCM24)
		{
			print("\t\tDevice can output to 24bit integer PCM.");
		}
		if(g_driver_info.caps & FMOD_CAPS_OUTPUT_FORMAT_PCM32)
		{
			print("\t\tDevice can output to 32bit integer PCM.");
		}
		if(g_driver_info.caps & FMOD_CAPS_REVERB_EAX2)
		{
			print("\t\tDevice supports EAX2 reverb.");
		}
		if(g_driver_info.caps & FMOD_CAPS_REVERB_EAX3)
		{
			print("\t\tDevice supports EAX3 reverb.");
		}
		if(g_driver_info.caps & FMOD_CAPS_REVERB_EAX4)
		{
			print("\t\tDevice supports EAX4 reverb.");
		}
		if(g_driver_info.caps & FMOD_CAPS_REVERB_I3DL2)
		{
			print("\t\tDevice supports I3DL2 reverb.");
		}
		if(g_driver_info.caps & FMOD_CAPS_REVERB_LIMITED)
		{
			print("\t\tDevice supports some form of limited hardware reverb.");
		}
	}
	if(which & EVENT_DBGPRT_FORMAT)
	{
		int samplerate, outputchannels, inputchannels, bits;
		FMOD_SOUND_FORMAT format;
		FMOD_DSP_RESAMPLER resampler;

		fmod_system->getSoftwareFormat(&samplerate, &format, &outputchannels, &inputchannels, &resampler, &bits);
		if(format == FMOD_SOUND_FORMAT_NONE)
		{
			strcpy_s(buff1, 50, "Unitialized / unknown");
		}
		if(format == FMOD_SOUND_FORMAT_PCM8)
		{
			strcpy_s(buff1, 50, "8I PCM");
		}
		if(format == FMOD_SOUND_FORMAT_PCM16)
		{
			strcpy_s(buff1, 50, "16I PCM");
		}
		if(format == FMOD_SOUND_FORMAT_PCM24)
		{
			strcpy_s(buff1, 50, "24I PCM");
		}
		if(format == FMOD_SOUND_FORMAT_PCM32)
		{
			strcpy_s(buff1, 50, "32I PCM");
		}
		if(format == FMOD_SOUND_FORMAT_PCMFLOAT)
		{
			strcpy_s(buff1, 50, "32F PCM");
		}
		if(format == FMOD_SOUND_FORMAT_GCADPCM)
		{
			strcpy_s(buff1, 50, "GCADPCM");
		}
		if(format == FMOD_SOUND_FORMAT_IMAADPCM)
		{
			strcpy_s(buff1, 50, "IMA ADPCM.");
		}
		if(format == FMOD_SOUND_FORMAT_VAG)
		{
			strcpy_s(buff1, 50, "VAG");
		}
		if(format == FMOD_SOUND_FORMAT_XMA)
		{
			strcpy_s(buff1, 50, "XMA");
		}
		if(format == FMOD_SOUND_FORMAT_MPEG)
		{
			strcpy_s(buff1, 50, "MP3/2");
		}
		if(format == FMOD_SOUND_FORMAT_MAX)
		{
			strcpy_s(buff1, 50, "Max?");
		}
		
		if(resampler == FMOD_DSP_RESAMPLER_NOINTERP)
		{
			strcpy_s(buff2, 20, "None");
		}
		if(resampler == FMOD_DSP_RESAMPLER_LINEAR)
		{
			strcpy_s(buff2, 20, "Linear");
		}
		if(resampler == FMOD_DSP_RESAMPLER_CUBIC)
		{
			strcpy_s(buff2, 20, "Cubic");
		}
		if(resampler == FMOD_DSP_RESAMPLER_SPLINE)
		{
			strcpy_s(buff2, 20, "5-Spline");
		}
		if(resampler == FMOD_DSP_RESAMPLER_MAX)
		{
			strcpy_s(buff2, 20, "Max?");
		}

		print("Software Format: SampleRate: %d - Format: %s", samplerate, buff1);
		print("   #OChannels: %d - #IChannels: %d - Resampler: %s - BPS: %d", outputchannels, inputchannels, buff2, bits);
	}
	if(which & EVENT_DBGPRT_DSPBUF)
	{
		unsigned int bufferlen;
		int numbuffers;
		fmod_system->getDSPBufferSize(&bufferlen, &numbuffers);
		print("DSP Buffer Size: Bufferlength: %d - NumBuffers: %d", bufferlen, numbuffers);
	}
	if(which & EVENT_DBGPRT_VERSION)
	{
		unsigned int version;
		fmod_system->getVersion(&version);
		print("FMOD Version: %x", version);
	}

	return FMOD_OK;
}
*/

//void fmodEventSystemSetDSPAllocCallback(FMOD_DSP_ALLOCCALLBACK func)
//{
//	fmod_system->setDSPAllocCallback(func);
//}
//
//void fmodEventSystemSetDSPFreeCallback(FMOD_DSP_FREECALLBACK func)
//{
//	fmod_system->setDSPFreeCallback(func);
//}
//
//void fmodEventSystemSetDSPExecuteCallback(FMOD_DSP_EXECUTECALLBACK func)
//{
//	fmod_system->setDSPExecuteCallback(func);
//}
//
//void fmodEventSystemSetDSPConnectionAllocCallback(FMOD_DSPCONNECTION_ALLOCCALLBACK func)
//{
//	fmod_system->setDSPConnectionAllocCallback(func);
//}
//
//void fmodEventSystemSetDSPConnectionFreeCallback(FMOD_DSPCONNECTION_FREECALLBACK func)
//{
//	fmod_system->setDSPConnectionFreeCallback(func);
//}

FMOD_RESULT FMOD_EventSystem_set3DListenerAttributes(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up)
{
	return event_system->set3DListenerAttributes(0, (FMOD_VECTOR*)pos, (FMOD_VECTOR*)vel, (FMOD_VECTOR*)forward, (FMOD_VECTOR*)up);
}

FMOD_RESULT FMOD_EventSystem_get3DListenerAttributes(Vec3 pos, Vec3 vel, Vec3 forward, Vec3 up)
{
	return event_system->get3DListenerAttributes(0, (FMOD_VECTOR*)pos, (FMOD_VECTOR*)vel, (FMOD_VECTOR*)forward, (FMOD_VECTOR*)up);
}

//FMOD_RESULT FMOD_EventSystem_Update(void)
S32 fmodEventSystemUpdate(void)
{
	FMOD_RESULT result;
	PERFINFO_AUTO_START("sndUpdate", 1);

	if(!event_system)
	{
		PERFINFO_AUTO_STOP();
		return FMOD_OK;
	}

	if(g_audio_state.noaudio)
	{
		PERFINFO_AUTO_STOP();
		return FMOD_OK;
	}

	result = event_system->update();
	if(result != FMOD_OK)
	{
		PERFINFO_AUTO_STOP();
		return result;
	}
	
	if(g_audio_state.audition)
	{
		result = FMOD::NetEventSystem_Update();
	}
	else
	{
		result = event_system->update();
	}

	PERFINFO_AUTO_STOP();

	return result;
}
  
FMOD_RESULT FMOD_EventSystem_PlayEvent(void *event)
{
	FMOD::Event *e = (FMOD::Event*)(event);
	FMOD_RESULT result;
	void *data = NULL;
	char *name = NULL;

	if(NULL == e)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	FMOD_EventSystem_GetName(event, &name);
	fmodEventGetUserData(event, &data);

	//sndPrintf(1, SNDDBG_EVENT, "Playing source: %s %p\n", name, data);
	result = e->start();

	FMOD_ErrCheckRetF(result);

	return result;
}

FMOD_RESULT FMOD_EventSystem_StopEvent(void *event, bool immediate)
{
	FMOD::Event *e = (FMOD::Event*)event;
	char *name = NULL;
	void *data = NULL;

	if(NULL == e)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	FMOD_EventSystem_GetName(event, &name);
	fmodEventGetUserData(event, &data);

	//sndPrintf(1, SNDDBG_EVENT, "Stop Event: %s %p - %d\n", name, data, immediate);

	return e->stop(immediate);
}

FMOD_RESULT FMOD_EventSystem_EventSetMute(void *event, U32 mute)
{
	FMOD::Event *e = (FMOD::Event*)event;
	char *name = NULL;
	
	if(NULL == e)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	FMOD_EventSystem_GetName(event, &name);
	if(!!mute)
	{
		//sndPrintf(2, SNDDBG_EVENT, "Event: Muting %p - %s\n", event, name);
	}
	else
	{
		//sndPrintf(2, SNDDBG_EVENT, "Event: Unmuting %p - %s\n", event, name);
	}

	return e->setVolume(!mute);

	return e->setMute(!!mute);
}

FMOD_RESULT FMOD_EventSystem_CreateGeometry(void **geometry, S32 num_tris)
{
	FMOD_RESULT result = fmod_system->createGeometry(num_tris, num_tris*3, (FMOD::Geometry**)geometry);
	return result;
}

FMOD_RESULT FMOD_EventSystem_FreeGeometry(void **geometry)
{
	FMOD::Geometry* geo;
	
	if(!geometry)
	{
		return FMOD_ERR_INTERNAL;
	}
	
	geo = (FMOD::Geometry*)*geometry;

	if(!geo)
	{
		return FMOD_ERR_INTERNAL;
	}

	//geo->release();

	*geometry = NULL;

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_AddPolygon(void *geometry, Vec3 p1, Vec3 p2, Vec3 p3, F32 occlude, F32 reverb_occlude)
{
	FMOD_VECTOR tri[3] = {*((FMOD_VECTOR*)p1), *((FMOD_VECTOR*)p2), *((FMOD_VECTOR*)p3)};

	((FMOD::Geometry*)geometry)->addPolygon(occlude, reverb_occlude, 1, 3, (FMOD_VECTOR*)tri, NULL); 

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_DestroyGeometry(void **geometry)
{
	return FMOD_OK;
}

// values are ints:
//  0 = done or doesn't need patching
//  n = n files left to patch
// Only modified in the main thread, not in patch callbacks
StashTable stPatchingEvents;

// This callback gets called in the main thread
static void fmodPatchingFinishedCallbackMainThread(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int patching_value;
	const char *event_name = (const char*)userData;
	if (!stashFindInt(stPatchingEvents, event_name, &patching_value))
		assert(0);
	patching_value--;
	assert(patching_value>=0);
	stashAddInt(stPatchingEvents, event_name, patching_value, true);
}

// This callback gets called in the fileLoaderThread
static void fmodPatchingFinishedCallback(const char *filename, void *userData)
{
	PERFINFO_AUTO_START_FUNC();
	TimedCallback_Run(fmodPatchingFinishedCallbackMainThread, userData, 0);
	PERFINFO_AUTO_STOP();
}

FMOD_RESULT fmodEventCheckNeedsPatching(const char *event_name)
{
	FMOD_RESULT res;
	// Check if the event is loadable before trying to load
	// Get the event's filename
	FMOD_EVENT_INFO info;
	FMOD_EVENT *fmod_event;
	int isPatching;

	if (!isPatchStreamingOn())
		return FMOD_OK;

	if (!stPatchingEvents)
		stPatchingEvents = stashTableCreateWithStringKeys(64, StashDefault);
	if (stashFindInt(stPatchingEvents, event_name, &isPatching))
	{
		assert(isPatching>=0);
		if (isPatching == 0)
			return FMOD_OK;
		return FMOD_ERR_NET_CONNECT;
	}

	assert(fileloader_patch_needs_callback);
	ZeroStruct(&info);
	res = FMOD_EventSystem_GetEventEx(event_name, FMOD_EVENT_DEFAULT|FMOD_EVENT_INFOONLY, (void**)&fmod_event, NULL);

	if (!res)
	{
		info.maxwavebanks = NUM_WAVEBANK_INFOS;
		info.wavebankinfo = wavebankinfos;
		FMOD_Event_GetInfo(fmod_event, NULL, NULL, &info);
		if(info.maxwavebanks)
		{
			int i = 0;
			char fullfsbname[MAX_PATH];

			for(i=0; i<info.maxwavebanks; i++)
			{
				FMOD_EVENT_WAVEBANKINFO *wbinfo = &wavebankinfos[i];
				int stash_add_value;
				if (!stashFindInt(stPatchingEvents, event_name, &stash_add_value))
					stash_add_value = 0;

				event_name = allocAddString(event_name);

				sprintf(fullfsbname, "%s/%s.fsb", fmodGetMediaPath(), wbinfo->name);

				if (fileloader_patch_needs_callback(fullfsbname))
				{
					// start a fileLoader event to ensure it gets patched
					stash_add_value++;
					fileLoaderRequestAsyncExec(allocAddFilename(fullfsbname), FILE_MEDIUM_HIGH_PRIORITY, false, fmodPatchingFinishedCallback, (void*)event_name);
					res = FMOD_ERR_NET_CONNECT;
				} else {
					// Already good
				}
				stashAddInt(stPatchingEvents, event_name, stash_add_value, true);
			}
		}
	}
	return res;
}

//FMOD_RESULT FMOD_EventSystem_GetEvent(const char *event_name, void **event)
S32 fmodEventSystemGetEvent(const char *event_name, void **event, void *userdata)
{
	FMOD_RESULT res;
	if (res = fmodEventCheckNeedsPatching(event_name))
		return res;
	return FMOD_EventSystem_GetEventEx(event_name, FMOD_EVENT_DEFAULT|FMOD_EVENT_NONBLOCKING, event, userdata);
}

FMOD_RESULT FMOD_EventSystem_GetEventInfoOnly(const char *event_name, void **event)
{
	return FMOD_EventSystem_GetEventEx(event_name, FMOD_EVENT_DEFAULT|FMOD_EVENT_INFOONLY, event, NULL);
}

S32 fmodEventSystemGetEventBySystemID(int sysid, void **event, void *userdata)
{
	FMOD::Event *e = NULL;

	PERFINFO_AUTO_START_FUNC();

	// sysids are given out as index+1, since stash tables can't use 0 based ints as keys
	event_system->getEventBySystemID(sysid-1, FMOD_EVENT_DEFAULT|FMOD_EVENT_INFOONLY, (FMOD::Event**)&e);

	if(e)
	{
		*event = (void*)e;
	}

	PERFINFO_AUTO_STOP();

	return !!e;
}

FMOD_RESULT FMOD_EventSystem_DelCallback(void *event, int master, int final, int type, sndEventCallback cb)
{
	CallbackInfo *cbinfo = NULL;
	FMODCallbackData *cbd = NULL;
	int i;
	assert(type>=0 && type<MAX_CALLBACKS);

	if(master)
	{
		// NULL event means master list
		cbinfo = &cbMasters;
		if(final)
		{
			cbinfo = &cbMastersFinal;
		}
	}
	else if(!stashAddressFindPointer(cbStash, event, (void**)&cbinfo))
	{
	}

	if(!cbinfo)
	{
		return FMOD_ERR_INVALID_PARAM;  // Assert()?
	}

	for(i=0; i<eaSize((cEArrayHandle*)&cbinfo->callbacks[type]); i++)
	{
		cbd = cbinfo->callbacks[type][i];

		if(cbd->cb == cb)
		{
			break;
		}
	}

	if(cbd && i<eaSize((cEArrayHandle*)&cbinfo->callbacks[type]))
	{
		eaRemoveFast((cEArrayHandle*)&cbinfo->callbacks[type], i);
		free(cbd);
	}

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_AddCallback(void *event, int master, int final, int type, sndEventCallback cb, void *data)
{
	CallbackInfo *cbinfo;
	FMODCallbackData *cbd;
	assert(type>=0 && type<MAX_CALLBACKS);

	if(!master && !event)
	{
		return FMOD_ERR_INVALID_PARAM;  // ADAM TODO: FIGURE OUT WHY SOME EVENTS ARE NULL
	}

	assert(isCrashed() || g_audio_state.main_thread_id==GetCurrentThreadId());

	if(master)
	{
		// NULL event means master list
		cbinfo = &cbMasters;

		if(final)
		{
			cbinfo = &cbMastersFinal;
		}
	}
	else if(!stashAddressFindPointer(cbStash, event, (void**)&cbinfo))
	{
		cbinfo = callocStruct(CallbackInfo);
		stashAddressAddPointer(cbStash, event, cbinfo, 1);
	}

	cbd = callocStruct(FMODCallbackData);
	cbd->cb = cb;
	cbd->ud = data;
	eaPush((cEArrayHandle*)&cbinfo->callbacks[type], cbd);

	return FMOD_OK;
}

void fmodManualCallback(void *e, int type)
{
	fmodMasterCallback((FMOD_EVENT*)e, (FMOD_EVENT_CALLBACKTYPE)type, NULL, NULL, NULL);
}

//typedef FMOD_RESULT (F_CALLBACK *sndEventCallback)(void *e, int type, void *p1, void *p2, void *ud);
FMOD_RESULT F_CALLBACK fmodMasterCallback(FMOD_EVENT *e, FMOD_EVENT_CALLBACKTYPE type, void *p1, void *p2, void *userdata)
{
	int i;
	CallbackInfo *cbinfo;
	char *name = NULL;
	void *e_userdata = NULL;
	FMOD_RESULT fmodResult;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(g_audio_state.d_reloading)
	{
		PERFINFO_AUTO_STOP();
		return FMOD_OK;
	}

	fmodEventGetUserData(e, &e_userdata);

	fmodResult = FMOD_EventSystem_GetName(e, &name);

	if(fmodResult == FMOD_OK)
	{
		if(	0 && (type==FMOD_EVENT_CALLBACKTYPE_STOLEN ||
			type==FMOD_EVENT_CALLBACKTYPE_EVENTFINISHED ||
			type==FMOD_EVENT_CALLBACKTYPE_EVENTSTARTED ||
			type==FMOD_EVENT_CALLBACKTYPE_SOUNDDEF_START ||
			type==FMOD_EVENT_CALLBACKTYPE_SOUNDDEF_END))
		{
			printf("Event ");

			switch(type)
			{
				xcase FMOD_EVENT_CALLBACKTYPE_STOLEN: 
			printf("stolen");
			xcase FMOD_EVENT_CALLBACKTYPE_EVENTFINISHED:
			printf("finished");
			xcase FMOD_EVENT_CALLBACKTYPE_EVENTSTARTED:
			printf("started");
			xcase FMOD_EVENT_CALLBACKTYPE_SOUNDDEF_START:
			printf("sd_start");
			xcase FMOD_EVENT_CALLBACKTYPE_SOUNDDEF_END:
			printf("sd_end");
			}

			printf(": %p %p %s\n", e_userdata, e, name);
		}

		for(i=0; i<eaSize((cEArrayHandle*)&cbMasters.callbacks[type]); i++)
		{
			FMODCallbackData *cbd = cbMasters.callbacks[type][i];
			cbd->cb(e, type, p1, p2, cbd->ud);
		}

		assert(isCrashed() || g_audio_state.main_thread_id==GetCurrentThreadId());

		if(stashAddressFindPointer(cbStash, e, (void**)&cbinfo))
		{
			for(i=0; i<eaSize((cEArrayHandle*)&cbinfo->callbacks[type]); i++)
			{
				FMODCallbackData *cbd = cbinfo->callbacks[type][i];
				cbd->cb(e, type, p1, p2, cbd->ud);
			}

			if(type==FMOD_EVENT_CALLBACKTYPE_EVENTFINISHED)  // NOTE: STOLEN happens BEFORE finished
			{
				for(i=0; i<MAX_CALLBACKS; i++)
				{
					eaDestroyEx((EArrayHandle*)&cbinfo->callbacks[i], NULL);
				}

				stashAddressRemovePointer(cbStash, e, NULL);
				free(cbinfo);
			}
		} 

		for(i=0; i<eaSize((cEArrayHandle*)&cbMastersFinal.callbacks[type]); i++)
		{
			FMODCallbackData *cbd = cbMastersFinal.callbacks[type][i];
			cbd->cb(e, type, p1, p2, cbd->ud);
		}
	}
	
	PERFINFO_AUTO_STOP();

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK fmodStolenCallback(void *e, int type, void *p1, void *p2, void *userdata)
{
	return FMOD_OK;
}

static void fmodSetupCallbacks(FMOD::Event *e, U32 flags)
{
	FMOD::EventGroup *parent;
	if(e && !(flags & FMOD_EVENT_INFOONLY))
	{
		e->setCallback(fmodMasterCallback, NULL);
	}

	if(e && !(flags & FMOD_EVENT_INFOONLY))
	{
		e->getParentGroup(&parent);

		if(fmodGroupIsExclusive(parent))
		{
			FMOD_EventSystem_AddCallback(e, 0, 0, fmodGetStlCBType(), fmodStolenCallback, NULL);
		}
	}
}

FMOD_RESULT FMOD_EventSystem_FindEventGroupByName(const char *eventGroupName, void **eventGroup)
{
	FMOD::EventGroup *eg;
	FMOD_RESULT result = FMOD_ERR_INVALID_PARAM;
	
	*eventGroup = NULL;
	
	if(!eventGroupName || !eventGroupName[0])
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	if(event_system)
	{
		int proji, numproj;
		event_system->getNumProjects(&numproj);
		for(proji = 0; proji<numproj; proji++)
		{
			FMOD::EventProject *project;
			event_system->getProjectByIndex(proji, &project);

			result = project->getGroup(eventGroupName, false, &eg);
			if(result == FMOD_OK || result==FMOD_ERR_MEMORY)
			{
				*eventGroup = eg;
				break;
			}
		}
	}

	return result;
}

FMOD_RESULT FMOD_EventSystem_GetEventEx(const char *event_name, U32 flags, void **event, void *userdata)
{
	FMOD::Event *pTstEvent = NULL;
	FMOD::Event *pEventFromProject = NULL;
	FMOD_RESULT result = FMOD_ERR_INVALID_PARAM;
	FMOD::EventProject *pProject;
	static char *clean_name = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!event_name || !event_name[0])
	{
		PERFINFO_AUTO_STOP();
		return FMOD_ERR_INVALID_PARAM;
	}

	estrPrintf(&clean_name, "%s", event_name);
	estrTrimLeadingAndTrailingWhitespace(&clean_name);

	// VERY IMPORTANT IF YOU WANT TO BE ABLE TO ADD GROUPS (eg. fmodChannelGroupAddGroup will fail if DSP_ONLY)
	// no DSP head node will be created!
	// 
	if(!(flags & FMOD_EVENT_INFOONLY))
		flags |= FMOD_EVENT_USERDSP;

	if(event_system)
	{
		// important!
		// some events are loading into the system even though their origins are projects
		result = event_system->getEvent(clean_name, flags, &pEventFromProject);

		if(result==FMOD_ERR_EVENT_NOTFOUND || result==FMOD_ERR_INVALID_PARAM)
		{
			const char *pool = allocFindString(clean_name);

			if(pool && stashAddressFindPointer(gEventToProjectStash, pool, (void**)&pProject))
			{
				result = pProject->getEvent(pool, flags, &pEventFromProject);
			}

			if(result!=FMOD_OK)
			{
				const char* evt = strchr(clean_name, '/');

				if(evt)
				{
					pool = allocFindString(evt+1);

					if(pool && stashAddressFindPointer(gEventToProjectStash, pool, (void**)&pProject))
						result = pProject->getEvent(pool, flags, &pEventFromProject);
				}
			}
		}

		if(result == FMOD_OK)
		{
			fmodSetupCallbacks(pEventFromProject, flags);
		}
	}

	if(event && pEventFromProject)
	{
		*event = (void*)pEventFromProject;

		if(!(flags & FMOD_EVENT_INFOONLY))
		{
			devassertmsg(userdata, "Trying to get event without userdata");
			fmodEventSetUserData(pEventFromProject, userdata);

			fmodManualCallback(pEventFromProject, fmodGetGetCBType());
		}
	}
	
	PERFINFO_AUTO_STOP();

	return result;
}

// Just a note from Event->set3DAttributes: orientation is by default null as is vel.
FMOD_RESULT FMOD_EventSystem_Get3DEventAttributes(void *event, Vec3 pos, Vec3 vel, Vec3 orientation)
{
	FMOD::Event *e = (FMOD::Event*)event;

	if(NULL == e)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	return e->get3DAttributes((FMOD_VECTOR*)pos, (FMOD_VECTOR*)vel, (FMOD_VECTOR*)orientation);
}

#define removeDENVec3(v) (v)[0] = fabs((v)[0])<0.0001 ? 0 : (v)[0]; (v)[1] = fabs((v)[1])<0.0001 ? 0 : (v)[1]; (v)[2] = fabs((v)[2])<0.0001 ? 0 : (v)[2];

// Just a note from Event->set3DAttributes: orientation is by default null as is vel.
FMOD_RESULT FMOD_EventSystem_Set3DEventAttributes(void *event, const Vec3 pos, const Vec3 vel, Vec3 orientation)
{
	FMOD::Event *e = (FMOD::Event*)event;
	Vec3 p, v, o;

	if(NULL == e)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	if(pos)
	{
		copyVec3(pos, p);

		removeDENVec3(p);
	}

	if(vel)
	{
		copyVec3(vel, v);

		removeDENVec3(v);
	}

	if(orientation)
	{
		copyVec3(orientation, o);

		removeDENVec3(o);
	}

	return e->set3DAttributes((FMOD_VECTOR*)(pos ? p : NULL), (FMOD_VECTOR*)(vel ? v : NULL), (FMOD_VECTOR*)(orientation ? o : NULL));
}

void FMOD_EventSystem_GetMemStats(int *current, int *max)
{
	FMOD::Memory_GetStats(current, max);
}

void freeMemEntry(void *entry)
{
	free(entry);
}

int memEntryCompare(const void *right, const void *left)
{
	MemEntry **r = (MemEntry**)right;
	MemEntry **l = (MemEntry**)left;
	return (*r)->size - (*l)->size;
}

F32 fmodGetCPUUsage(void)
{
	F32 total = 0;
	fmod_system->getCPUUsage(NULL, NULL, NULL, NULL, &total);

	return total;
}

FMOD_RESULT FMOD_EventSystem_GetVolume(void *event, F32 *vol)
{
	return ((FMOD::Event*)event)->getVolume(vol);
}

FMOD_RESULT fmodDSPGetMemoryInfo(void *dsp, unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details)
{
	FMOD::DSP *d = (FMOD::DSP*)dsp;

	return d->getMemoryInfo(FMOD_MEMBITS_ALL, FMOD_EVENT_MEMBITS_ALL, memUsed, details);
}

FMOD_RESULT fmodEventGetMemoryInfo(void *event, unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details)
{
	FMOD::Event *e = (FMOD::Event*)event;
	
	return e->getMemoryInfo(FMOD_MEMBITS_ALL, FMOD_EVENT_MEMBITS_ALL, memUsed, details);
}

FMOD_RESULT fmodEventSystemGetMemoryInfo(unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details)
{
	if(!event_system)
	{
		return FMOD_ERR_EVENT_NOTFOUND;
	}

	return event_system->getMemoryInfo(FMOD_MEMBITS_ALL, FMOD_EVENT_MEMBITS_ALL, memUsed, details);
}

FMOD_RESULT fmodSystemGetMemoryInfo(unsigned int *memUsed, FMOD_MEMORY_USAGE_DETAILS *details)
{
	if(!fmod_system)
	{
		return FMOD_ERR_EVENT_NOTFOUND;
	}

	return fmod_system->getMemoryInfo(FMOD_MEMBITS_ALL, FMOD_EVENT_MEMBITS_ALL, memUsed, details);
}


F32 fmodEventSystemGetVolumeProperty(void *event)
{
	F32 vol = 0;
	FMOD::Event *e = (FMOD::Event*)event;

	if(e)
	{
		e->getPropertyByIndex(FMOD_EVENTPROPERTY_VOLUME, &vol, 1);
	}

	return vol;
}

void fmodEventSetVolumeProperty(void *event, F32 value)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setPropertyByIndex(FMOD_EVENTPROPERTY_VOLUME, &value, 0);
}

S32 fmodEventGetPriority(void *event)
{
	S32 pri = 0;
	FMOD::Event *e = (FMOD::Event*)event;
	if(e)
		e->getPropertyByIndex(FMOD_EVENTPROPERTY_PRIORITY, &pri, 1);
	return pri;
}

U32 fmodEventIsPlaying(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD_EVENT_STATE state;
	FMOD_RESULT result;

	result = e->getState(&state);

	if(result!=FMOD_OK)
	{
		return 0;
	}

	return !!(state&FMOD_EVENT_STATE_PLAYING);
}

U32 fmodEventIsActive(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD_EVENT_STATE state;
	FMOD_RESULT result;

	assert(e);

	result = e->getState(&state);

	if(result!=FMOD_OK)
	{
		
	}

	return !!((state&FMOD_EVENT_ACTIVE)==FMOD_EVENT_ACTIVE);
}

FMOD_RESULT FMOD_EventSystem_GetSoundByIndex(void *event, int index, void **sound)
{
	FMOD::Event* e = (FMOD::Event*)event;
	FMOD::ChannelGroup *group;
	FMOD::Channel *channel;
	FMOD_RESULT result;
	// Could this be any slower?
	result = e->getChannelGroup(&group);
	if(result) return result;
	result = group->getChannel(index, &channel);
	if(result) return result;
	result = channel->getCurrentSound(((FMOD::Sound**)sound));
	return result;
}

FMOD_RESULT FMOD_EventSystem_SetVolume(void *event, F32 volume)
{
	return ((FMOD::Event*)event)->setVolume(volume);
}

FMOD_RESULT FMOD_EventSystem_GetName(void *event, char **name)
{
	return ((FMOD::Event*)event)->getInfo(NULL, name, NULL);
}

FMOD_RESULT FMOD_EventSystem_GetParentGroup(void *event, void **parent)
{
	if(event)
		return ((FMOD::Event*)event)->getParentGroup(((FMOD::EventGroup**)parent));
	else
		return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT FMOD_EventSystem_GetGroupName(void *group, char **name)
{
	return ((FMOD::EventGroup*)group)->getInfo(NULL, name);
}

S32 fmodEventSystemGetGroup(char *group_name, void **group)
{
	FMOD::EventGroup *g = NULL;
	FMOD_RESULT result = FMOD_ERR_INVALID_PARAM;

	if(!group_name || !group_name[0])
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	if(event_system)
	{
		result = event_system->getGroup(group_name, 0, &g);
		if(result==FMOD_ERR_INVALID_PARAM || result==FMOD_ERR_EVENT_NOTFOUND)
		{
			int proji, numproj;
			event_system->getNumProjects(&numproj);
			for(proji = 0; proji<numproj; proji++)
			{
				FMOD::EventProject *project;
				event_system->getProjectByIndex(proji, &project);

				result = project->getGroup(group_name, 0, &g);

				if(result==FMOD_OK || result==FMOD_ERR_MEMORY)
				{
					break;
				}
			}
		}
	}
	else
	{
		g = NULL;
	}

	if(group)
	{
		*group = (void*)g;
	}

	return result;
}

FMOD_RESULT FMOD_EventSystem_GroupRun(void *group, sndEventRunFunc func, void *userdata)
{
	int num_events, index, result;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;
	FMOD::Event *event;

	result = g->getNumEvents(&num_events);
	
	for(index = 0; index < num_events; index++)
	{
		result = g->getEventByIndex(index, FMOD_EVENT_DEFAULT, &event);
		result = func(event, userdata);
	}

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_GroupGetNumEvents(void *group, int *event_count)
{
	int num_events;
	FMOD_RESULT result;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	result = g->getNumEvents(&num_events);

	if(event_count)
	{
		*event_count = num_events;
	}

	return result;
}
FMOD_RESULT FMOD_EventSystem_GroupGetEventByIndex(void *group, int event_index, void **event)
{
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;
	FMOD_RESULT result;

	result = g->getEventByIndex(event_index, FMOD_EVENT_INFOONLY, (FMOD::Event**)event);

	if(*event)
	{
		fmodSetupCallbacks((FMOD::Event*)*event, FMOD_EVENT_INFOONLY);
	}

	return result;
}

FMOD_RESULT FMOD_EventSystem_EventIsLooping(void *event, int *loop)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD_RESULT result;
	result = e->getPropertyByIndex(FMOD_EVENTPROPERTY_ONESHOT, loop);
	*loop = !*loop;
	FMOD_ErrCheckRetF(result);
	return FMOD_OK;
}

U32 fmodEventIsLooping(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD_RESULT result;
	int loop = 0;
	
	result = e->getPropertyByIndex(FMOD_EVENTPROPERTY_ONESHOT, &loop);

	return !loop;
}

U32 fmodEventHasRanPastEndWhileLoading(void *event)
{
	if (!fmodEventIsLooping(event))
	{
		FMOD_EVENT_INFO info;
		FMOD::Event *e = (FMOD::Event*)event;
		e->getInfo(NULL, NULL, &info);
		if (0 < info.lengthms && info.lengthms < info.positionms)
		{
			FMOD_EVENT_STATE state;
			FMOD_RESULT result;
			result = e->getState(&state);
			return state & FMOD_EVENT_STATE_LOADING;
		}
	}
	return 0;
}

U32 fmodEventHasFevFsbMismatch(void *event)
{
	FMOD::Event* e = (FMOD::Event*)event;
	FMOD_EVENT_STATE state;
	FMOD_RESULT result;

	//this lies about the event mismatch if you have more than one bank and a good bank was the most recently loaded
	result = e->getState(&state);

	if (result == FMOD_ERR_EVENT_MISMATCH) {
		return 1;
	}

	return 0;
}

FMOD_RESULT FMOD_EventSystem_GetSystemID(void *event, int *id)
{
	FMOD_RESULT result;
	FMOD::Event *e = (FMOD::Event*)event;

	FMOD_EVENT_INFO a;
	ZeroStruct(&a);

	result = e->getInfo(NULL, NULL, &a);
	if(result != FMOD_OK)
	{
		return result;
	}
	*id = a.systemid+1;  // To go from 0 index to 1 index for stash table usage

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_GetSystemIDStr(const char* event_name, int *id)
{
	FMOD::Event *e;
	FMOD_RESULT result;

	result = FMOD_EventSystem_GetEventInfoOnly(event_name, (void**)&e);
	FMOD_ErrCheckRetF(result);

	return FMOD_EventSystem_GetSystemID(e, id);
}

FMOD_RESULT F_CALLBACK MarkerCallback(FMOD_EVENT *event, FMOD_EVENT_CALLBACKTYPE type, void *p1, void *p2, void *ud)
{
 	if(type == FMOD_EVENT_CALLBACKTYPE_SYNCPOINT)
	{
		// Here we want to insta-fade to ud
		// printf("Something!!!!  Please see this!!!");
	}

	return FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_SetMarkerCallback(void *event, void *ud)
{
	FMOD::Event *e = (FMOD::Event*)event;
	return e->setCallback(MarkerCallback, ud);
}

FMOD_RESULT FMOD_EventSystem_GetWaveData(float *data, int num_data, int channel)
{
	return fmod_system->getWaveData(data, num_data, channel);
}

FMOD_RESULT FMOD_EventSystem_EventGetRadius(void *event, float *radius)
{
	return ((FMOD::Event*)event)->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_MAXDISTANCE, radius);
}

bool FMOD_EventSystem_CBEnd(int type)
{
	return type==FMOD_EVENT_CALLBACKTYPE_EVENTFINISHED;
}

bool FMOD_EventSystem_CBMod(int type)
{
	return type==FMOD_EVENT_CALLBACKTYPE_NET_MODIFIED;
}

U32 fmodEventHasProperty(void *event, char *property)
{
	int value;
	return ((FMOD::Event*)event)->getProperty(property, &value)==FMOD_OK;
}

FMOD_RESULT FMOD_EventSystem_GetProperty(void *event, char *property, void *value)
{
	return ((FMOD::Event*)event)->getProperty(property, value);
}

FMOD_RESULT FMOD_EventSystem_GroupGetProperty(void *group, char *property, void *value)
{
	return ((FMOD::EventGroup*)group)->getProperty(property, value);
}

FMOD_RESULT FMOD_EventSystem_SetMute(bool mute) 
{
	FMOD::EventCategory *master;
	FMOD_RESULT result;
	
	if(!event_system)
	{
		return FMOD_OK;
	}

	result = event_system->getCategoryByIndex(-1, &master);
	
	if(FMOD_ErrIsFatal(result))
	{
		return FMOD_ERR_INTERNAL;
	}

	result = master->setMute(mute);
	return result;
}

FMOD_RESULT FMOD_EventSystem_SetMasterVolume(F32 volume)
{
	FMOD::EventCategory *master;
	FMOD_RESULT result;
	result = event_system->getCategoryByIndex(-1, &master);
	FMOD_ErrCheckRetF(result);
	result = master->setVolume(volume);
	return result;
}

FMOD_RESULT FMOD_EventSystem_StopAllEvents(void)
{
	FMOD::EventCategory *master;
	FMOD_RESULT result;
	result = event_system->getCategoryByIndex(-1, &master);
	FMOD_ErrCheckRetF(result);
	result = master->stopAllEvents();
	return result;
}

FMOD_RESULT FMOD_EventSystem_SetVolumeByCategory(char *category, F32 vol)
{
	FMOD::EventCategory *cat;
	FMOD_RESULT r;
	r = event_system->getCategory(category, &cat);
	r = cat->setVolume(vol);
	return r;
}

FMOD_RESULT FMOD_EventSystem_ShutDown(void)
{
	if(FMOD_EventSystem_ProjectNotLoaded())
	{
		return FMOD_OK;
	}
	
	event_system->release();

	if(g_audio_state.audition)
		FMOD::NetEventSystem_Shutdown();
	
	event_system = NULL;
	fmod_system = NULL;

    if(audioMemory) {
#if _PS3
		#if PS3_USE_VRAM
        vram_free(audioMemory, "VideoMemory:Sound");
		#else
		free_aligned(audioMemory, "Sound-PhysicalAlloc");
		#endif
#elif _XBOX
	    physicalfree(audioMemory, "Sound-PhysicalAlloc");
#else
		free(audioMemory);
#endif
	    audioMemory = NULL;
    }

	return FMOD_OK;
}

bool FMOD_EventSystem_ProjectNotLoaded(void)
{
	return !event_system;
}

U32 FMOD_EventSystem_GetEventListHelper(char ***earrayOut, FMOD::EventGroup* grp, const char* grpname)
{
	char *myname;
	char myfullname[MAX_PATH];
	int i;
	int size;
	FMOD::EventGroup *subgrp = NULL;
	FMOD::Event *e;
	char *ename;
	char efullname[MAX_PATH];
	char newgroup[MAX_PATH];
	char *dupd;

	assert(grp);
	assert(grpname);

	grp->getNumGroups(&size);
	grp->getInfo(NULL, &myname);
	strcpy(myfullname, grpname);
	if(strcmp(grpname, ""))
	{
		strcat(myfullname, "/");
	}
	else
	{
		strcat(myfullname, myname);
		strcat(myfullname, "/");
	}

	if(size)
	{
		for(i=0; i<size; i++)
		{
			grp->getGroupByIndex(i, 0, &subgrp);
			subgrp->getInfo(NULL, (char**)&grpname);
			strcpy(newgroup, myfullname);
			strcat(newgroup, grpname);
			FMOD_EventSystem_GetEventListHelper(earrayOut, subgrp, newgroup);
		}
	}

	size = 0;
	grp->getNumEvents(&size);

	if(size)
	{
		for(i=0; i<size; i++)
		{
			e = NULL;
			grp->getEventByIndex(i, FMOD_EVENT_INFOONLY, &e);
			if(e)
			{
				e->getInfo(NULL, &ename, NULL);

				strcpy(efullname, myfullname);
				strcat(efullname, ename);

				dupd = strdup(efullname);

				eaPush(((cEArrayHandle*)earrayOut), dupd);
			}
		}
	}

	return 1;
}


U32 FMOD_EventSystem_TreeTraverseHelper(void* p_c_g_e, SoundTreeType type, sndTreeIterator iter, void *parent, void *userData)
{
	char* name = NULL;
	void* new_parent = NULL;
	FMOD_RESULT fresult = FMOD_OK;
	U32 result = 0;

	switch(type)
	{
		case STT_PROJECT:{
			FMOD::EventProject *project = (FMOD::EventProject*)p_c_g_e;
			FMOD_EVENT_PROJECTINFO projinfo;
			fresult = project->getInfo(&projinfo);
			name = projinfo.name;
		}
		xcase STT_CATEGORY:{
			FMOD::EventCategory *category = (FMOD::EventCategory*)p_c_g_e;
			fresult = category->getInfo(NULL, &name);
		}
		xcase STT_GROUP:{
			FMOD::EventGroup *group = (FMOD::EventGroup*)p_c_g_e;
			fresult = group->getInfo(NULL, &name);
		}
		xcase STT_EVENT:{
			FMOD::Event *event = (FMOD::Event*)p_c_g_e;
			fresult = event->getInfo(NULL, &name, NULL);
		}	
	}

	if (fresult != FMOD_OK) {
		ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(fresult));
		Errorf("%s : FMOD Error when attempting to getInfo", __FUNCTION__);
	}

	result = iter(name, p_c_g_e, type, parent, userData, &new_parent);

	if(!result)
	{
		return 0;
	}

	switch(type)
	{
		case STT_PROJECT:{
			FMOD::EventProject *project = (FMOD::EventProject*)p_c_g_e;
			int max, i;

			project->getNumGroups(&max);

			for(i=0; i<max; i++)
			{
				FMOD::EventGroup *g;

				// precache 0
				fresult = project->getGroupByIndex(i, 0, &g);

				if (fresult != FMOD_OK) {
					ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(fresult));
					Errorf("%s : FMOD Error when attempting to getGroupByIndex", __FUNCTION__);
				}

				if(!FMOD_EventSystem_TreeTraverseHelper(g, STT_GROUP, iter, new_parent, userData))
				{
					return 0;
				}
			}
		}
		xcase STT_CATEGORY:{
			FMOD::EventCategory *category = (FMOD::EventCategory*)p_c_g_e;
			int max, i;

			category->getNumCategories(&max);

			for(i=0; i<max; i++)
			{
				FMOD::EventCategory *c;

				fresult = category->getCategoryByIndex(i, &c);

				if (fresult != FMOD_OK) {
					ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(fresult));
					Errorf("%s : FMOD Error when attempting to getCategoryByIndex", __FUNCTION__);
				}

				if(!FMOD_EventSystem_TreeTraverseHelper(c, STT_CATEGORY, iter, new_parent, userData))
				{
					return 0;
				}
			}
		}
		xcase STT_GROUP:{
			FMOD::EventGroup *group = (FMOD::EventGroup*)p_c_g_e;
			int max, i;

			group->getNumGroups(&max);

			for(i=0; i<max; i++)
			{
				FMOD::EventGroup *g;

				// No precache
				fresult = group->getGroupByIndex(i, 0, &g);

				if (fresult != FMOD_OK) {
					ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(fresult));
					Errorf("%s : FMOD Error when attempting to getGroupByIndex", __FUNCTION__);
				}

				if(!FMOD_EventSystem_TreeTraverseHelper(g, STT_GROUP, iter, new_parent, userData))
				{
					return 0;
				}
			}

			group->getNumEvents(&max);

			for(i=0; i<max; i++)
			{
				FMOD::Event *e;

				fresult = group->getEventByIndex(i, FMOD_EVENT_INFOONLY, &e);

				if (fresult != FMOD_OK) {
					ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(fresult));
					Errorf("%s : FMOD Error when attempting to getEventByIndex", __FUNCTION__);
				}

				if(!FMOD_EventSystem_TreeTraverseHelper(e, STT_EVENT, iter, new_parent, userData))
				{
					return 0;
				}
			}
		}
		xcase STT_EVENT:{
			break;
		}	
	}

	return 1;
}

U32 FMOD_EventSystem_TreeTraverse(SoundTreeType type, sndTreeIterator iter, void* root, void *userData)
{
	if(FMOD_EventSystem_ProjectNotLoaded())
	{
		return 0;
	}

	if(type==STT_CATEGORY)
	{
		int i, max;

		event_system->getNumCategories(&max);

		for(i=0; i<max; i++)
		{
			FMOD::EventCategory *c;
			FMOD_RESULT result;

			result = event_system->getCategoryByIndex(i, &c);

			if (result != FMOD_OK) {
				ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(result));
				Errorf("%s : FMOD Error when attempting to getCategoryByIndex", __FUNCTION__);
			}

			FMOD_EventSystem_TreeTraverseHelper(c, STT_CATEGORY, iter, root, userData);
		}
	}
	else
	{
		int i, max;

		event_system->getNumProjects(&max);

		for(i=0; i<max; i++)
		{
			FMOD::EventProject *p;
			FMOD_RESULT result;

			result = event_system->getProjectByIndex(i, &p);

			if (result != FMOD_OK) {
				ErrorDetailsf("FMOD Result = %s", fmodGetErrorText(result));
				Errorf("%s : FMOD Error when attempting to getProjectByIndex", __FUNCTION__);
			}

			FMOD_EventSystem_TreeTraverseHelper(p, STT_PROJECT, iter, root, userData);
		}
	}

	return 1;
}

bool fmodEventGetProjectFilename(void *event, char **projectFilename)
{
	bool found = false;
	FMOD_EVENTGROUP *group = NULL;
	FMOD_EVENTPROJECT *project = NULL;
	EventMetaData *emd = NULL;
	FMOD_EVENT_INFO info;// = {0};
	FMOD_EVENT *fmod_event = (FMOD_EVENT *)event;

	FMOD_Event_GetInfo(fmod_event, NULL, NULL, &info);
	FMOD_Event_GetParentGroup(fmod_event, &group);

	if(group)
	{
		FMOD_EventGroup_GetParentProject(group, &project);
		if(project)
		{
			FMOD_EVENT_PROJECTINFO projinfo;

			FMOD_EventProject_GetInfo(project, &projinfo);
			
			found = stashFindPointer(sndProjectStash, projinfo.name, (void**)projectFilename);
		}
	}
	return found;
}

bool fmodProjectGetFilename(void *project, char **projectFilename)
{
	FMOD_EVENTPROJECT *fmod_project = (FMOD_EVENTPROJECT*)project;
	FMOD_EVENT_PROJECTINFO projinfo;
	FMOD_EventProject_GetInfo(fmod_project, &projinfo);
	return stashFindPointer(sndProjectStash, projinfo.name, (void**)projectFilename);
}

void fmodEventGetFullName(char **estrOut, void *event, bool includeProject)
{
	char *names[200];
	int count = 0, i;
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::EventGroup *grp = NULL;
	FMOD::EventGroup *lastgrp = NULL;
	FMOD::EventProject *proj = NULL;
	
	e->getInfo(NULL, &names[0], NULL);
	e->getParentGroup(&grp);
	if(!grp)
		return;
	while(grp && count<ARRAY_SIZE(names)-1)
	{
		count++;
		grp->getInfo(NULL, &names[count]);
		lastgrp = grp;
		grp->getParentGroup(&grp);
	}

	estrClear(estrOut);
	if(includeProject)
	{
		FMOD_EVENT_PROJECTINFO projinfo;
		lastgrp->getParentProject(&proj);
		proj->getInfo(&projinfo);

		estrPrintf(estrOut, "%s/", projinfo.name);
	}

	for(i=count; i>=0; i--)
	{
		if(i!=count)
			estrConcatf(estrOut, "/");
		estrConcatf(estrOut, "%s", names[i]);
	}
}

U32 FMOD_EventSystem_GetEventList(char*** earrayOut, char* group)
{
	FMOD::EventGroup *grp;
	int size, i, status;
	char name[MAX_PATH];

	if(group)
	{
		status = event_system->getGroup(group, 0, &grp);
		if(status != FMOD_OK)
		{
			return 0;
		}
	}
	else
	{
		grp = NULL;
	}

	if(grp)
	{
		strcpy(name, group);
		status = FMOD_EventSystem_GetEventListHelper(earrayOut, grp, group);
	}
	else
	{
		int proji, numproj;
		status = 1;
		if(!event_system) return 0;
		event_system->getNumProjects(&numproj);
		for(proji=0; proji<numproj; proji++)
		{
			FMOD::EventProject *project;
			event_system->getProjectByIndex(proji, &project);
			assert(project);
			if(project->getNumGroups(&size)==FMOD_OK)
			{
				for(i=0; i<size; i++)
				{
					project->getGroupByIndex(i, 0, &grp);
					assert(grp);
					FMOD_EventSystem_GetEventListHelper(earrayOut, grp, "");
				}
			}
		}
	}

	return status;
}

S32 fmodGetGetCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_EVENTGET;
}

S32 fmodGetStartCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_EVENTSTARTED;
}

S32 fmodGetEndCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_EVENTFINISHED;
}

S32 fmodGetStlCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_STOLEN;
}

S32 fmodGetMkrCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_SYNCPOINT;
}

S32 fmodGetModCBType(void)
{
	return FMOD_EVENT_CALLBACKTYPE_NET_MODIFIED;
}

U32 fmodEventExists(const char* event_name)
{
	FMOD_RESULT result;
	if(g_audio_state.noaudio || FMOD_EventSystem_ProjectNotLoaded())
	{
		return 1;
	}

	result = FMOD_EventSystem_GetEventInfoOnly(event_name, NULL);

	return result==FMOD_OK || result==FMOD_ERR_MEMORY;
}

U32 fmodEventCanPlay(void *event_info)
{
	int value = 1;
	FMOD::Event *ei = (FMOD::Event*)event_info;

	PERFINFO_AUTO_START(__FUNCTION__, 1);
	ei->getProperty("OncePerMap", (void*)&value, 0);
	PERFINFO_AUTO_STOP();
	return value;
}

U32 fmodEventSetCanPlay(void *event_info, int value)
{
	FMOD::Event *ei = (FMOD::Event*)event_info;

	ei->setProperty("OncePerMap", (void*)&value, 0);

	return 1;
}


U32 fmodEventIs2D(void *fmod_event)
{
	FMOD::Event *e = (FMOD::Event*)fmod_event;
	FMOD_EVENT_MODE mode = FMOD_3D;
	int result;

	result = e->getPropertyByIndex(FMOD_EVENTPROPERTY_MODE, &mode);

	if(!result)
	{
		return mode==FMOD_2D;
	}

	return 0;
}

U32 fmodGroupIsExclusive(void *group)
{
	int dummy;
	return FMOD_EventSystem_GroupGetProperty(group, "Exclusive", &dummy)==FMOD_OK;	
}

U32 fmodEventSystemGroupHasPlayingEvent(void *group, void **event2)
{
	int i;
	FMOD_EVENT *events[1000];
	FMOD::EventGroup *parent;
	FMOD_EVENT_SYSTEMINFO info;

	info.numplayingevents = 1000;
	info.playingevents = events;

	event_system->getInfo(&info);

	for(i=0; i<info.numplayingevents; i++)
	{
		FMOD::Event *e = (FMOD::Event*)info.playingevents[i];

		e->getParentGroup(&parent);

		if(parent==group)
		{
			if(event2)
			{
				*event2 = e;
			}
			return 1;
		}
	}

	return 0;
}

U32 fmodEventSetUserData(void *event, void *data)
{
	FMOD::Event *e = (FMOD::Event*)event;

	return e->setUserData(data);
}

U32 fmodEventGetUserData(void *event, void **data)
{
	FMOD::Event *e = (FMOD::Event*)event;

	return e->getUserData(data);
}

void fmodEventSetVolume(void *event, F32 volume)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setVolume(volume);
}

F32 fmodEventGetVolume(void *event)
{
	F32 volume = 0;

	if(event)
	{
		FMOD::Event *e = (FMOD::Event*)event;

		e->getVolume(&volume);
	}

	return volume;
}

void fmodEventGet3DInfo(void *event, Fmod3DRolloffType *rolloff, F32 *minDistance, F32 *maxDistance)
{
	FMOD::Event *e = (FMOD::Event*)event;
	U32 mode;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_ROLLOFF, &mode);
	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_MINDISTANCE, minDistance);
	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_MAXDISTANCE, maxDistance);

	switch(mode) {
		/*
		case FMOD_3D_LOGROLLOFF:
			*rolloff = FMOD_3DROLLOFF_LOG;
			break;
		*/
		case FMOD_3D_LINEARROLLOFF:
			*rolloff = FMOD_3DROLLOFF_LINEAR;
			break;
		default:
			*rolloff = FMOD_3DROLLOFF_CUSTOM;
			break;
	}
}

int fmodEventGetFadeInTime(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	int value;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_FADEIN, &value);

	return value;
}

void fmodEventSetFadeInTime(void *event, int val)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setPropertyByIndex(FMOD_EVENTPROPERTY_FADEIN, &val);
}

int fmodEventGetFadeOutTime(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	int value = 0;

	if (e) {
		e->getPropertyByIndex(FMOD_EVENTPROPERTY_FADEOUT, &value);
	}

	return value;
}

void fmodEventSetFadeOutTime(void *event, int val)
{
	FMOD::Event *e = (FMOD::Event*)event;

	if (e) {
		e->setPropertyByIndex(FMOD_EVENTPROPERTY_FADEOUT, &val);
	}
}

void fmodEventSystemGetPlaying(void ***events)
{
	int i;
	FMOD_EVENT_SYSTEMINFO info;
	FMOD_EVENT *playing[1000];

	info.numplayingevents = 1000;
	info.playingevents = playing;

	event_system->getInfo(&info);

	for(i=0; i<info.numplayingevents; i++)
	{
		eaPush((cEArrayHandle*)events, (void*)info.playingevents[i]);
	}	
}

U32 fmodEventSystemGetNumPlaying(void)
{
	FMOD_EVENT_SYSTEMINFO info;
	FMOD_EVENT *playing[100];

	info.numplayingevents = 100;
	info.playingevents = playing;

	event_system->getInfo(&info);

	return info.numplayingevents;
}

FMOD_RESULT fmodEventSystemGetNumEvents(int *numEvents, int *numInstances, int *numPlaying)
{
	FMOD_EVENT_SYSTEMINFO info;
	FMOD_EVENT *playing[100];
	FMOD_RESULT result;

	if(!event_system)
	{
		return FMOD_ERR_NOTREADY;
	}
	 
	info.numplayingevents = 100;
	info.playingevents = playing;

	result = event_system->getInfo(&info);

	*numEvents = info.numevents;
	*numInstances = info.numinstances;
	*numPlaying = info.numplayingevents;
	

	return result;
}

U32 fmodEventHasParam(void *event, char *param)
{
	FMOD_RESULT result;
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::EventParameter *fmod_param = NULL;

	result = e->getParameter(param, &fmod_param);

	return result==FMOD_OK;
}

void fmodEventSetParam(void *event, char *param, F32 value)
{
	FMOD_RESULT result;
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::EventParameter *fmod_param = NULL;

	result = e->getParameter(param, &fmod_param);
	if(result==FMOD_OK)
	{
		fmod_param->setValue(value);
	}
}

int fmodEventGetMaxPlaybacks(void *info_event)
{
	int mp = 0;
	FMOD::Event *e = (FMOD::Event*)info_event;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_MAX_PLAYBACKS, &mp);

	return mp;
}

void fmodEventSetMaxPlaybacks(void *info_event, int maxPlaybacks)
{
	FMOD::Event *e = (FMOD::Event*)info_event;

	e->setPropertyByIndex(FMOD_EVENTPROPERTY_MAX_PLAYBACKS, &maxPlaybacks);
}


U32 fmodEventIsPanLevel0(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value = 1;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_PANLEVEL, &value, 0);

	return value <= 0.01;
}

void fmodEventSetPanLevel(void *event, F32 value)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setPropertyByIndex(FMOD_EVENTPROPERTY_3D_PANLEVEL, &value, 0);
}

F32 fmodEventGetPanLevel(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value = 1;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_PANLEVEL, &value, 0);

	return value;
}

void fmodEventSetDopplerScale(void *event, F32 value)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setPropertyByIndex(FMOD_EVENTPROPERTY_3D_DOPPLERSCALE, &value, 0);
}

F32 fmodEventGetDopplerScale(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value = 1;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_DOPPLERSCALE, &value, 0);

	return value;
}

F32 fmodEventGetMinRadius(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_MINDISTANCE, &value, 0);

	return value;
}

F32 fmodEventGetMaxRadius(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_3D_MAXDISTANCE, &value, 0);

	return value;
}

U32 fmodEventSystemGetEventMemUsage(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD_EVENT_INFO info;// = {0};

	if(!e)
	{
		return 0;
	}

	e->getInfo(NULL, NULL, &info);

	return info.memoryused;
}

void fmodValidateBanks(void)
{
	if (event_system &&
		g_audio_state.loadtimeErrorCheck)
	{
		const char** eaUniqueBankNames = NULL;
		const char** eaSharedBankNames = NULL;
		int iNumProjects;
		int i;

		event_system->getNumProjects(&iNumProjects);
		eaCreate((cEArrayHandle*)&eaUniqueBankNames);
		eaCreate((cEArrayHandle*)&eaSharedBankNames);

		for (i = 0; i < iNumProjects; i++)
		{
			FMOD::EventProject *project;
			FMOD_EVENT_PROJECTINFO projectInfo;// = {0};
			int j;

			event_system->getProjectByIndex(i, &project);
			projectInfo.maxwavebanks = NUM_WAVEBANK_INFOS;
			projectInfo.wavebankinfo = wavebankinfos;
			project->getInfo(&projectInfo);

			assert(projectInfo.maxwavebanks <= NUM_WAVEBANK_INFOS);

			for (j = 0; j < projectInfo.maxwavebanks; j++)
			{
				FMOD_EVENT_WAVEBANKINFO *bankinfo = &wavebankinfos[j];
				const char *pcBankName = allocAddString(bankinfo->name);

				//U32 stream = !!(bankinfo->mode & FMOD_CREATESTREAM);
				//U32 comram = !!(bankinfo->mode & FMOD_CREATECOMPRESSEDSAMPLE);
			
				// 0 = stream from disk, 1 = load into memory, 2 = decompress into memory.
				U32 stream = bankinfo->type == 0;
				U32 comram = bankinfo->type == 1;

				if(!stream && !comram)
				{
					char bankFilename[256];
					sprintf(bankFilename, "%s%s.fsb", SOUND_MEDIA_PATH, pcBankName);
					ErrorFilenamef(bankFilename, "Bank: %s mode set as Decompress into Memory!  Change to Load into Memory!", pcBankName);
				}

				if (0 < eaFind((cEArrayHandle*)&eaUniqueBankNames, pcBankName)) {
					eaPushUnique((cEArrayHandle*)&eaSharedBankNames, pcBankName);
				} else {
					eaPush((cEArrayHandle*)&eaUniqueBankNames, pcBankName);
				}
			}
		}

		for (i = 0; i < iNumProjects; i++)
		{
			FMOD::EventProject *project;
			FMOD_EVENT_PROJECTINFO projectInfo;// = {0};
			int j;

			event_system->getProjectByIndex(i, &project);
			projectInfo.maxwavebanks = NUM_WAVEBANK_INFOS;
			projectInfo.wavebankinfo = wavebankinfos;
			project->getInfo(&projectInfo);

			assert(projectInfo.maxwavebanks <= NUM_WAVEBANK_INFOS);

			for(j = 0; j < projectInfo.maxwavebanks; j++)
			{
				FMOD_EVENT_WAVEBANKINFO *bankinfo = &wavebankinfos[j];
				const char *pcBankName = allocAddString(bankinfo->name);

				if (0 < eaFind(&eaSharedBankNames, pcBankName))
				{
					char bankFilename[256];
					sprintf(bankFilename, "%s%s.fsb", SOUND_MEDIA_PATH, pcBankName);
					ErrorFilenamef(bankFilename, "Bank %s uses the same name in multiple projects including project %s", pcBankName, projectInfo.name);
				}
			}
		}

		eaDestroy((cEArrayHandle*)&eaUniqueBankNames);
		eaDestroy((cEArrayHandle*)&eaSharedBankNames);
	}
}

void fmodEventSystemCheckWavebanks(void)
{
	if (event_system &&
		g_audio_state.sound_developer) // wasn't working before, so just run it for sound developers now since it's every frame
	{
		int iNumProjects;
		int i;

		event_system->getNumProjects(&iNumProjects);

		for (i = 0; i < iNumProjects; i++)
		{
			FMOD::EventProject *project;
			FMOD_EVENT_PROJECTINFO projectInfo;// = {0};
			int j;

			event_system->getProjectByIndex(i, &project);
			projectInfo.maxwavebanks = NUM_WAVEBANK_INFOS;
			projectInfo.wavebankinfo = wavebankinfos;
			project->getInfo(&projectInfo);

			assert(projectInfo.maxwavebanks <= NUM_WAVEBANK_INFOS);

			for (j = 0; j < projectInfo.maxwavebanks; j++)
			{
				FMOD_EVENT_WAVEBANKINFO *bankinfo = &projectInfo.wavebankinfo[j];
				if(bankinfo->streamsinuse > bankinfo->maxstreams)
				{
					Errorf("Exceeded max streams on project %s bank %s with limit %d", projectInfo.name, bankinfo->name, bankinfo->maxstreams);
				}
			}
		}
	}
}

bool fmodEventSystemFreeEventData(void *event)
{
	FMOD::EventGroup *parent = NULL;  //This is really stupid

	PERFINFO_AUTO_START_FUNC();

	FMOD_EventSystem_GetParentGroup(event, (void**)&parent);

	if(!parent)
	{
		PERFINFO_AUTO_STOP();
		//badness;
		return true;
	}

	FMOD_RESULT res = parent->freeEventData((FMOD::Event*)event, false);

	PERFINFO_AUTO_STOP();
	return res != FMOD_ERR_NOTREADY;
}
   
void FMOD_EventSystem_EventPause(void *event, U32 pause)
{
	FMOD::Event *e = (FMOD::Event*)event;

	e->setPaused(!!pause);
}

//const char* fmodMemoryTrackerGetOriginStr(int origin)
//{
//#define CASE(orig)  xcase MEMORIG_##orig: return #orig
//	
//	switch(origin)
//	{
//		CASE(UNKNOWN);
//		CASE(EVENTINSTANCE);
//		CASE(CHANNELGROUP);
//		CASE(DSPUNIT);
//		CASE(FEV);
//		CASE(EVENTSYSTEM);
//		CASE(SYSTEM);
//		CASE(MUSICSYSTEM);
//		CASE(CHANNELI);
//		CASE(OUTPUTMODULE);
//		CASE(SAMPLE);
//		CASE(SOUNDBANKCLASS);
//		CASE(STREAMINSTANCE);
//		CASE(SOUNDI);
//		CASE(REVERBDEF);
//		CASE(EVENTREVERB);
//		CASE(REVERBI);
//		CASE(MEMORYFSB);
//		CASE(EVENTPROJECT);
//		CASE(SOUNDDEFPOOL);
//		CASE(EVENTGROUPI);
//		CASE(SOUNDDEFCLASS);
//		CASE(SOUNDDEFDEFCLASS);
//		CASE(SOUNDBANKLIST);
//		CASE(REVERBCHANNELPROPS);
//		CASE(EVENTINSTANCE_COMPLEX);
//		CASE(EVENTINSTANCE_SIMPLE);
//		CASE(EVENTINSTANCE_LAYER);
//		CASE(EVENTINSTANCE_SOUND);
//		CASE(EVENTENVELOPE);
//		CASE(EVENTENVELOPEDEF);
//		CASE(DSPCONNECTION);
//		CASE(DSPI);
//		CASE(EVENTCATEGORY);
//		CASE(EVENTENVELOPEPOINT);
//	}
//#undef CASE
//
//	return "";
//}

U32 fmodGetEventInfo(void *event, FMOD_EVENT_INFO *info)
{
	FMOD::Event *e = (FMOD::Event*)event;

	if(!info)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	return e->getInfo(NULL, NULL, info);
}

U32 fmodGetFmodEventInfo(void *event, FmodEventInfo *info)
{
	FMOD::Event *e = (FMOD::Event*)event;
	U32 result;

	FMOD_EVENT_INFO origInfo;

	if(!info)
	{
		return FMOD_ERR_INVALID_PARAM;
	}

	// copy potential input params
	origInfo.instances = (FMOD_EVENT**)info->instances;
	origInfo.numinstances = info->numinstances;
	origInfo.maxwavebanks = info->maxwavebanks;
	origInfo.wavebankinfo = (FMOD_EVENT_WAVEBANKINFO*)info->wavebankinfo;

	// get the info
	result = e->getInfo(NULL, NULL, &origInfo);
	
	// make a copy to our wrapper
	info->audibility = origInfo.audibility;
	info->channelsplaying = origInfo.channelsplaying;
	info->instances = (void**)origInfo.instances;
	info->instancesactive = origInfo.instancesactive;
	info->lengthms = origInfo.lengthms;
	//info->lengthmsnoloop = origInfo.lengthmsnoloop;
	info->maxwavebanks = origInfo.maxwavebanks;
	info->memoryused = origInfo.memoryused;
	info->numinstances = origInfo.numinstances;
	info->positionms = origInfo.positionms;
	info->projectid = origInfo.projectid;
	info->systemid = origInfo.systemid;
	info->wavebankinfo = (void*)origInfo.wavebankinfo;

	return result;
}

F32 fmodGetEventAudibility(void *event)
{
	FMOD_EVENT_INFO info;// = {0};
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::ChannelGroup *channelgroup = NULL;

	e->getInfo(NULL, NULL, &info);
	e->getChannelGroup(&channelgroup);
	if(channelgroup)
	{
		F32 volume = -1;
		channelgroup->getVolume(&volume);
		return volume;
	}

	return info.audibility;
}

U32 fmodEventIsValid(void *event)
{
	FMOD_EVENT_STATE state;
	FMOD::Event *e = (FMOD::Event*)event;

	return e->getState(&state)==FMOD_OK;
}

F32 fmodEventGetLength(void *event)
{
	FMOD_EVENT_INFO info;// = {0};
	FMOD::Event *e = (FMOD::Event*)event;

	e->getInfo(NULL, NULL, &info);
	return (F32)info.lengthms/1000;
}

//U32 fmodEventGetMemoryUsage(void *event)
//{
//	FMOD_EVENT_INFO info;// = {0};
//	FMOD::Event *e = (FMOD::Event*)event;
//	FMOD::MemoryTracker tracker;
//
//	e->getMemoryUsed(0);
//	e->getMemoryUsed(&tracker);
//	e->getMemoryUsed(0);
//
//	return tracker.getTotal();
//}

void* fmodEventGetChannelGroup(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::ChannelGroup *cg = NULL;

	if(!e)
	{
		return NULL;
	}

	e->getChannelGroup(&cg);

	return cg;
}

void* fmodChannelGroupCreate(void *userdata, const char* name)
{
	if(event_system)
	{
		FMOD::ChannelGroup *channel_group = NULL;
		
		fmod_system->createChannelGroup(name, &channel_group);

		channel_group->setUserData(userdata);

		//sndPrintf(1, SNDDBG_MEMORY, "Created CG: %s %p\n", name, channel_group);

		return channel_group;
	}

	return NULL;
}

void fmodChannelGroupFixup(void *channel_group)
{
	FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;

	if(cg)
	{
		static FMOD::DSP *hackyFun = NULL;
		if(!hackyFun)
			fmod_system->createDSPByType(FMOD_DSP_TYPE_MIXER, &hackyFun);

		cg->addDSP(hackyFun, NULL);
		hackyFun->remove();
	}
}

void fmodChannelGroupDestroy(void **channel_group)
{
	if(channel_group)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)(*channel_group);
		char name[MAX_PATH];

		if(cg)
		{
			cg->getName(SAFESTR(name));

			//sndPrintf(1, SNDDBG_MEMORY, "Destroyed CG: %s %p\n", name, cg);

			cg->release();
		}

		*channel_group = NULL;
	}
}

int fmodChannelGroupAddGroup(void *channel_group, void *add_group)
{
	FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;
	FMOD::ChannelGroup *add = (FMOD::ChannelGroup*)add_group;
	PERFINFO_AUTO_START_FUNC();

	if(!cg || !add)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	cg->addGroup(add);

	PERFINFO_AUTO_STOP();
	return 1;
}

int fmodChannelGroupDisconnect(void *del_group)
{
	FMOD::ChannelGroup *del = (FMOD::ChannelGroup*)del_group;
	FMOD::DSP *dsp = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!del)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	del->getDSPHead(&dsp);

	// this is assuming the channel group isn't linking other things together
	dsp->disconnectAll(0,1);

	PERFINFO_AUTO_STOP();
	return 1;
}

void fmodChannelGroupSetVolume(void *channel_group, F32 volume)
{
	if(channel_group)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;

		//cg->setVolume(volume);
		cg->overrideVolume(volume);
	}
}

F32 fmodChannelGroupGetVolume(void *channel_group)
{
	F32 volume = 0;

	if(channel_group)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;

		//cg->setVolume(volume);
		cg->getVolume(&volume);
	}

	return volume;
}

void fmodChannelGroupSetMute(void *channel_group, bool mute)
{
	if(channel_group)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;
		cg->setMute(mute);
	}
}

U32 fmodChannelGroupAddDSP(void *channel_group, void* dsp)
{
	if(channel_group && dsp)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;
		FMOD::DSP *d = (FMOD::DSP*)dsp;
		FMOD::DSP *head = NULL;

		cg->getDSPHead(&head);

		if(head)
		{
			int i;
			int size = 0;
			head->getNumInputs(&size);

			for(i=0; i<size; i++)
			{
				FMOD::DSP *other = NULL;

				head->getInput(i, &other, NULL);

				if(other && other==d)
				{
					return 0;
				}
			}

			cg->addDSP(d, NULL);
		}
	}

	fmodFlushDSPConnectionRequests();

	return 1;
}

U32 fmodChannelGroupGetNumGroups(void *channel_group)
{
	if(channel_group)
	{
		int groups = 0;
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;

		cg->getNumGroups(&groups);

		return groups;
	}

	return 0;
}

void* fmodChannelGroupGetDSPHead(void *channel_group)
{
	if(channel_group)
	{
		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;
		FMOD::DSP *dsp = NULL;

		cg->getDSPHead(&dsp);		

		return dsp;
	}

	return NULL;
}

void fmodDSPSetBypass(void *dsp, bool val)
{
	if(dsp)
	{
		FMOD::DSP *fmod_dsp = (FMOD::DSP*)dsp;
		fmod_dsp->setBypass(val);
	}
}

void fmodDSPGetBypass(void *dsp, bool *val)
{
	if(dsp)
	{
		FMOD::DSP *fmod_dsp = (FMOD::DSP*)dsp;
		fmod_dsp->getBypass(val);
	}
}

//void* fmodChannelGroupGetMemoryTracker(void *channel_group)
//{
//	if(channel_group)
//	{
//		FMOD::ChannelGroup *cg = (FMOD::ChannelGroup*)channel_group;
//
//		static_tracker.clear();
//		cg->getMemoryUsage(NULL);
//		cg->getMemoryUsage(&static_tracker);
//		cg->getMemoryUsage(NULL);
//
//		return &static_tracker;
//	}
//	return NULL;
//}

static void fmodDSPSetupDistortion(DSP_Distortion *distortion, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_DISTORTION_LEVEL, distortion->distortion_level);
}

static void fmodDSPSetupHighpass(DSP_HighPass *hp, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_HIGHPASS_CUTOFF, hp->highpass_cutoff);
	dsp->setParameter(FMOD_DSP_HIGHPASS_RESONANCE, hp->highpass_resonance);
}

static void fmodDSPSetupEcho(DSP_Echo *echo, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_ECHO_DELAY, echo->echo_delay);
	dsp->setParameter(FMOD_DSP_ECHO_DECAYRATIO, echo->echo_decayratio);
	dsp->setParameter(FMOD_DSP_ECHO_MAXCHANNELS, echo->echo_maxchannels);
	dsp->setParameter(FMOD_DSP_ECHO_DRYMIX, echo->echo_drymix);
	dsp->setParameter(FMOD_DSP_ECHO_WETMIX, echo->echo_wetmix);
}

static void fmodDSPSetupChorus(DSP_Chorus *ch, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_CHORUS_DRYMIX, ch->chorus_drymix);
	dsp->setParameter(FMOD_DSP_CHORUS_WETMIX1, ch->chorus_wetmix1);
	dsp->setParameter(FMOD_DSP_CHORUS_WETMIX2, ch->chorus_wetmix2);
	dsp->setParameter(FMOD_DSP_CHORUS_WETMIX3, ch->chorus_wetmix3);
	dsp->setParameter(FMOD_DSP_CHORUS_DELAY, ch->chorus_delay);
	dsp->setParameter(FMOD_DSP_CHORUS_RATE, ch->chorus_rate);
	dsp->setParameter(FMOD_DSP_CHORUS_DEPTH, ch->chorus_depth);
}

static void fmodDSPSetupCompressor(DSP_Compressor *com, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_COMPRESSOR_THRESHOLD, com->compressor_threshold);
	dsp->setParameter(FMOD_DSP_COMPRESSOR_ATTACK, com->compressor_attack);
	dsp->setParameter(FMOD_DSP_COMPRESSOR_RELEASE, com->compressor_release);
	dsp->setParameter(FMOD_DSP_COMPRESSOR_GAINMAKEUP, com->compressor_gainmakeup);
}

static void fmodDSPSetupFlange(DSP_Flange *f, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_FLANGE_DRYMIX, f->flange_drymix);
	dsp->setParameter(FMOD_DSP_FLANGE_WETMIX, f->flange_wetmix);
	dsp->setParameter(FMOD_DSP_FLANGE_DEPTH, f->flange_depth);
	dsp->setParameter(FMOD_DSP_FLANGE_RATE, f->flange_rate);
}


static void fmodDSPSetupLowpass(DSP_Lowpass *lp, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_LOWPASS_CUTOFF, lp->lowpass_cutoff);
	dsp->setParameter(FMOD_DSP_LOWPASS_RESONANCE, lp->lowpass_resonance);
}

static void fmodDSPSetupSlowpass(DSP_SLowpass *lp, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_LOWPASS_SIMPLE_CUTOFF, lp->lowpass_cutoff);
}

static void fmodDSPSetupNormalize(DSP_Normalize *norm, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_NORMALIZE_FADETIME, norm->normalize_fadetime);
	dsp->setParameter(FMOD_DSP_NORMALIZE_THRESHHOLD, norm->normalize_threshold);
	dsp->setParameter(FMOD_DSP_NORMALIZE_MAXAMP, norm->normalize_maxamp);
}

static void fmodDSPSetupParamEQ(DSP_ParamEQ *peq, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_PARAMEQ_CENTER, peq->parameq_center);
	dsp->setParameter(FMOD_DSP_PARAMEQ_BANDWIDTH, peq->parameq_bandwidth);
	dsp->setParameter(FMOD_DSP_PARAMEQ_GAIN, peq->parameq_gain);
}

static void fmodDSPSetupPitchshift(DSP_Pitchshift *ps, FMOD::DSP *dsp)
{
	dsp->setParameter(FMOD_DSP_PITCHSHIFT_PITCH, ps->pitchshift_pitch);
	dsp->setParameter(FMOD_DSP_PITCHSHIFT_FFTSIZE, ps->pitchshift_fftsize);
	dsp->setParameter(FMOD_DSP_PITCHSHIFT_MAXCHANNELS, ps->pitchshift_maxchannels);
}

void fmodDSPSetupSfxReverb(DSP_SfxReverb *sfx, void *fmodDSP)
{
	FMOD::DSP *dsp = (FMOD::DSP *)fmodDSP;

	dsp->setParameter(FMOD_DSP_SFXREVERB_DRYLEVEL, sfx->sfxreverb_drylevel);
	dsp->setParameter(FMOD_DSP_SFXREVERB_ROOM, sfx->sfxreverb_room);
	dsp->setParameter(FMOD_DSP_SFXREVERB_ROOMHF, sfx->sfxreverb_roomhf);
	//dsp->setParameter(FMOD_DSP_SFXREVERB_ROOMROLLOFFFACTOR, sfx->sfxreverb_roomrollofffactor);
	dsp->setParameter(FMOD_DSP_SFXREVERB_DECAYTIME, sfx->sfxreverb_decaytime);
	dsp->setParameter(FMOD_DSP_SFXREVERB_DECAYHFRATIO, sfx->sfxreverb_decayhfratio);
	dsp->setParameter(FMOD_DSP_SFXREVERB_REFLECTIONSLEVEL, sfx->sfxreverb_reflectionslevel);
	dsp->setParameter(FMOD_DSP_SFXREVERB_REFLECTIONSDELAY, sfx->sfxreverb_reflectionsdelay);
	dsp->setParameter(FMOD_DSP_SFXREVERB_REVERBLEVEL, sfx->sfxreverb_reverblevel);
	dsp->setParameter(FMOD_DSP_SFXREVERB_REVERBDELAY, sfx->sfxreverb_reverbdelay);
	dsp->setParameter(FMOD_DSP_SFXREVERB_DIFFUSION, sfx->sfxreverb_diffusion);
	dsp->setParameter(FMOD_DSP_SFXREVERB_DENSITY, sfx->sfxreverb_density);
	dsp->setParameter(FMOD_DSP_SFXREVERB_HFREFERENCE, sfx->sfxreverb_hfreference);
	dsp->setParameter(FMOD_DSP_SFXREVERB_ROOMLF, sfx->sfxreverb_roomlf);
	dsp->setParameter(FMOD_DSP_SFXREVERB_LFREFERENCE, sfx->sfxreverb_lfreference);
}

void fmodDSPSetValuesByInfo(SoundDSP *dspinfo, void *dsp)
{
	FMOD::DSP *d = (FMOD::DSP*)dsp;

	if(d)
	{
		switch(dspinfo->type)
		{
			xcase DSPDISTORTION: {
				return fmodDSPSetupDistortion(&dspinfo->distortion, d);
			}
			xcase DSPHIGHPASS: {
				return fmodDSPSetupHighpass(&dspinfo->highpass, d);
			}
			xcase DSPECHO: {
				return fmodDSPSetupEcho(&dspinfo->echo, d);
			}
			xcase DSPCOMPRESSOR: {
				return fmodDSPSetupCompressor(&dspinfo->compressor, d);
			}
			xcase DSPFLANGE: {
				return fmodDSPSetupFlange(&dspinfo->flange, d);
			}
			xcase DSPLOWPASS: {
				return fmodDSPSetupLowpass(&dspinfo->lowpass, d);
			}
			xcase DSPSLOWPASS: {
				return fmodDSPSetupSlowpass(&dspinfo->slowpass, d);
			}
			xcase DSPNORMALIZE: {
				return fmodDSPSetupNormalize(&dspinfo->normalize, d);
			}
			xcase DSPPARAMEQ: {
				return fmodDSPSetupParamEQ(&dspinfo->parameq, d);
			}
			xcase DSPPITCHSHIFT: {
				return fmodDSPSetupPitchshift(&dspinfo->pitchshift, d);
			}
			xcase DSPSFXREVERB: {
				return fmodDSPSetupSfxReverb(&dspinfo->sfxreverb, d);
			}
			xdefault: {
				devassertmsg(0, "Unrecognized or unimplemented dsp type.");
			}
		}
	}	
}

FMOD_DSP_TYPE fmodDSPTypeFromInfoType(SoundDSP *dspinfo)
{
	switch(dspinfo->type)
	{
		xcase DSPDISTORTION: {
			return FMOD_DSP_TYPE_DISTORTION;
		}
		xcase DSPHIGHPASS: {
			return FMOD_DSP_TYPE_HIGHPASS;
		}
		xcase DSPECHO: {
			return FMOD_DSP_TYPE_ECHO;
		}
		xcase DSPCOMPRESSOR: {
			return FMOD_DSP_TYPE_COMPRESSOR;
		}
		xcase DSPFLANGE: {
			return FMOD_DSP_TYPE_FLANGE;
		}
		xcase DSPLOWPASS: {
			return FMOD_DSP_TYPE_LOWPASS;
		}
		xcase DSPSLOWPASS: {
			return FMOD_DSP_TYPE_LOWPASS_SIMPLE;
		}
		xcase DSPNORMALIZE: {
			return FMOD_DSP_TYPE_NORMALIZE;
		}
		xcase DSPPARAMEQ: {
			return FMOD_DSP_TYPE_PARAMEQ;
		}
		xcase DSPPITCHSHIFT: {
			return FMOD_DSP_TYPE_PITCHSHIFT;
		}
		xcase DSPSFXREVERB: {
			return FMOD_DSP_TYPE_SFXREVERB;
		}
		xdefault: {
			devassertmsg(0, "Unrecognized or unimplemented dsp type.");
		}
	}
	
	return FMOD_DSP_TYPE_MIXER;	
}

U32 fmodDSPCreateWithType(int type, void **dspOut, void *userdata)
{
	FMOD_RESULT result;
	FMOD::DSP *dsp = NULL;

	*dspOut = NULL; // default result

	if(!fmod_system)
	{
		return FMOD_OK;
	}

	result = fmod_system->createDSPByType((FMOD_DSP_TYPE)type, &dsp);
	if(dsp)
	{
		dsp->setUserData(userdata);
		*dspOut = dsp;
	}
	
	return result;
}

U32 fmodSetDSPUserData(void *dsp, void *userdata)
{
	FMOD::DSP *fmodDSP = (FMOD::DSP *)dsp;
	if(dsp)
	{
		fmodDSP->setUserData(userdata);
	}

	return FMOD_OK;
}

U32 fmodSystemAddDSP(void *dsp, void **dspConnection)
{
	FMOD_RESULT result;

	if(!fmod_system)
	{
		return FMOD_OK;
	}

	result = fmod_system->addDSP((FMOD::DSP*)dsp, (FMOD::DSPConnection**)dspConnection);

	return result;
}


void* fmodDSPCreateFromInfo(SoundDSP *dspinfo, void *inst_userdata)
{
	FMOD::DSP *dsp = NULL;
	
	fmod_system->createDSPByType(fmodDSPTypeFromInfoType(dspinfo), &dsp);

	if(dsp)
	{
		SoundDSPInstance *inst = NULL;
		SoundDSPInstanceList *list = NULL;
		fmodDSPSetValuesByInfo(dspinfo, (void*)dsp);
		
		if(!stashFindPointer(dspInstances, dspinfo->name, (void**)&list))
		{
			list = callocStruct(SoundDSPInstanceList);

			stashAddPointer(dspInstances, dspinfo->name, list, 1);

			SET_HANDLE_FROM_STRING(space_state.dsp_dict, dspinfo->name, list->dspRef);
		}

		inst = callocStruct(SoundDSPInstance);
		inst->list = list;
		inst->fmod_dsp = dsp;
		inst->user_data = inst_userdata;
		eaPush((cEArrayHandle*)&list->instances, inst);
		dsp->setUserData(inst);
	}

	//sndPrintf(1, SNDDBG_DSP|SNDDBG_MEMORY, "Created dsp: %p\n", dsp);
	
	return dsp;
}

void* fmodDSPCreateCopy(void *orig, void *inst_userdata)
{
	FMOD::DSP* odsp = (FMOD::DSP*)orig;

	if(odsp)
	{
		SoundDSPInstance *inst = NULL;
		SoundDSPInstanceList *list = NULL;

		odsp->getUserData((void**)&inst);

		if(inst && inst->list)
		{
			SoundDSP *dspinfo = GET_REF(inst->list->dspRef);
			if(dspinfo)
			{
				return fmodDSPCreateFromInfo(dspinfo, inst_userdata);
			}
		}
	}

	return NULL;
}

void* fmodDSPGetSystemHead(void)
{
	FMOD::DSP *dsp = NULL;

	fmod_system->getDSPHead(&dsp);

	return dsp;
}

//void* fmodDSPGetSystemCGMixTarget(void)
//{
//	FMOD::DSP *dsp = NULL;
//
//	fmod_system->getDSPChannelGroupTarget(&dsp);
//
//	return dsp;
//}

void fmodDSPFree(void **dsp, bool bHasInstanceUserData)
{
	FMOD::DSP *d = (FMOD::DSP*)(*dsp);

	if(d)
	{
		int iNumInputs;
		int iNumOutputs;

		if (bHasInstanceUserData)
		{
			SoundDSPInstance *inst = NULL;

			d->getUserData((void**)&inst);
			if(!inst)
				return;  // Something is wrong
			if(inst && inst->list)
			{
				int r =eaFindAndRemoveFast((cEArrayHandle*)&inst->list->instances, inst);

				devassertmsg(r!=-1, "Trying to free DSP that wasn't tracked... blame Adam");
			}
		}

		fmodFlushDSPConnectionRequests();
		d->getNumInputs(&iNumInputs);
		d->getNumOutputs(&iNumOutputs);
		if (iNumInputs <= 1 && iNumOutputs <= 1) {
			d->remove();
		} else {
			Errorf(	"Found DSP with %i input%s and %i output%s during %s, these won't be reconnected correctly (see 'FMOD_DSP_Remove' and 'FMOD_DSP_DisconnectAll' function comments in FMOD's source code)",
					iNumInputs,  iNumInputs  != 1 ? "s" : "",
					iNumOutputs, iNumOutputs != 1 ? "s" : "",
					__FUNCTION__);
			d->disconnectAll(1, 1);
		}
		d->release();
		fmodFlushDSPConnectionRequests();

		//sndPrintf(1, SNDDBG_DSP | SNDDBG_MEMORY, "Freeing DSP: %p\n", d);

		*dsp = NULL;
	}
}

void fmodDSPConnectToDSP(void *source, void *target)
{
	FMOD::DSP *s = (FMOD::DSP*)source;
	FMOD::DSP *t = (FMOD::DSP*)target;

	t->addInput(s, NULL);
}

U32 fmodDSPFindDSP(void *target, void *source, void **connOut)
{
	FMOD::DSP *s = (FMOD::DSP*)source;
	FMOD::DSP *t = (FMOD::DSP*)target;
	int inputs = 0;
	int i;

	t->getNumInputs(&inputs);

	for(i=0; i<inputs; i++)
	{
		FMOD::DSP *dsp = NULL;
		
		t->getInput(i, &dsp, (FMOD::DSPConnection**)connOut);
		if(dsp==source)
		{
			return 1;
		}
	}

	if(connOut)
		*connOut = NULL;

	return 0;
}

void fmodDSPAddDSP(void *target, void *source, void **connOut)
{
	FMOD::DSP *s = (FMOD::DSP*)source;
	FMOD::DSP *t = (FMOD::DSP*)target;

	t->addInput(s, (FMOD::DSPConnection**)connOut);

	fmodFlushDSPConnectionRequests();
}

void fmodDSPGetChildren(void *dsp, void ***dsps)
{
	int i, size = 0;
	FMOD::DSP *d = (FMOD::DSP*)dsp;

	d->getNumInputs(&size);

	for(i=0; i<size; i++)
	{
		FMOD::DSP *input = NULL;
		d->getInput(i, &input, NULL);

		if(input)
		{
			eaPush((cEArrayHandle*)dsps, input);
		}
	}
}

void fmodDSPGetOutputs(void *dsp, void ***dsps, void ***conns)
{
	int i, size = 0;
	FMOD::DSP *d = (FMOD::DSP*)dsp;

	d->getNumOutputs(&size);

	for(i=0; i<size; i++)
	{
		FMOD::DSP *output = NULL;
		FMOD::DSPConnection *conn = NULL;
		d->getOutput(i, &output, &conn);

		if(output && dsps)
			eaPush((cEArrayHandle*)dsps, output);
		if(conn && conns)
			eaPush((cEArrayHandle*)conns, conn);
	}
}

void* fmodDSPGetOutputConn(void *dsp)
{
	FMOD::DSP* d = (FMOD::DSP*)dsp;
	FMOD::DSP* out = NULL;
	FMOD::DSPConnection *out_conn = NULL;

	d->getOutput(0, &out, &out_conn);

	return out_conn;
}

void fmodDSPConnectionSetVolumeEx(void *dsp_conn, F32 vol, char *file, int line)
{
	FMOD::DSPConnection *conn = (FMOD::DSPConnection*)dsp_conn;

	if(conn)
	{
		conn->setMix(vol);//, file, line);
	}
}

F32 fmodDSPConnectionGetVolume(void *dsp_conn)
{
	FMOD::DSPConnection *conn = (FMOD::DSPConnection*)dsp_conn;
	F32 vol = 0;

	if(conn)
	{
		conn->getMix(&vol);
	}

	return vol;
}


void* fmodDSPConnectionGetOutput(void *dsp_conn)
{
	FMOD::DSPConnection *conn = (FMOD::DSPConnection*)dsp_conn;
	FMOD::DSP *dsp_out = NULL;

	conn->getOutput(&dsp_out);

	return dsp_out;
}

void fmodDSPConnectionDisconnect(void *dsp_conn)
{
	FMOD::DSP *input = NULL;
	FMOD::DSP *output = NULL;
	FMOD::DSPConnection *conn = (FMOD::DSPConnection*)dsp_conn;

	conn->getInput(&input);
	conn->getOutput(&output);

	if(input && output)
	{
		output->disconnectFrom(input);
	}
}

void fmodFlushDSPConnectionRequests(void)
{
	FMOD::DSP *dsp = NULL;
	int num = 0;
	fmod_system->getDSPHead(&dsp);

	dsp->getNumInputs(&num);
}

void fmodEventConnectToDSP(void *event, void *dsp)
{
	FMOD::DSP *t = (FMOD::DSP*)dsp;
	FMOD::Event* e = (FMOD::Event*)event;
	FMOD::ChannelGroup *cg = NULL;

	e->getChannelGroup(&cg);

	if(cg)
	{
		FMOD::DSP *cgdsp = NULL;

		cg->getDSPHead(&cgdsp);
		t->addInput(cgdsp, NULL);
	}
}

U32 fmodGetProjects(void ***projects)
{
	int i;
	int project_count = 0;

	if(!event_system)
	{
		return 0;
	}

	event_system->getNumProjects(&project_count);
	for(i=0; i<project_count; i++)
	{
		FMOD::EventProject *project = NULL;

		event_system->getProjectByIndex(i, &project);
		if(project)
		{
			eaPush((cEArrayHandle*)projects, project);
		}
	}

	return 1;
}

U32 fmodProjectByName(const char *projectName, void **project)
{
	// find project by name
	U32 result = 0;
	int i, numProjects;
	FMOD_RESULT fmodResult;

	if(!event_system)
	{
		return result;
	}

	fmodResult = event_system->getNumProjects(&numProjects);
	if( fmodResult == FMOD_OK)
	{
		for(i = 0; i < numProjects; i++)
		{
			FMOD::EventProject *p;
			char *name;
			fmodResult = event_system->getProjectByIndex(i, &p);

			if( fmodResult == FMOD_OK)
			{
				fmodEventProjectGetName(p, &name);
				if( !strcmpi(projectName, name) )
				{
					*project = p;
					result = 1;
					break;
				}
			}

			//fmodResult = p->getEvent(event_name, flags, &e);
		}
	}

	return result;
}

typedef void (*FmodTraverseGroupsFunc)(char *path, void *group, char *groupName, void *userData);

void fmodGetGroupNames(char *path, void *eventGroup, char *groupName, void *userData)
{
	cEArrayHandle *groups = (cEArrayHandle*)userData;
	char buffer[MAX_PATH];
	char *dupStr;

	strcpy(buffer, path);
	strcat(buffer, "/");
	strcat(buffer, groupName);		

	dupStr = strdup(buffer);
	eaPush(groups, dupStr);
}

void fmodGetGroupNamesStash(char *path, void *eventGroup, char *groupName, void *userData)
{
	StashTable stashTable = (StashTable)userData;
	char buffer[MAX_PATH];

	strcpy(buffer, path);
	strcat(buffer, "/");
	strcat(buffer, groupName);		

	stashAddPointer(stashTable, buffer, eventGroup, true);
}

U32 fmodGetLeafGroupNamesRecursive(void *eventGroup, char *path, FmodTraverseGroupsFunc func, void *userData)
								   //char ***groups)
{
	U32 result = 1;

	if(eventGroup)
	{
		char buffer[MAX_PATH];
		int group_count = 0;
		char *groupName;

		fmodEventGroupGetName(eventGroup, &groupName);

		FMOD::EventGroup *group = (FMOD::EventGroup*)eventGroup;

		group->getNumGroups(&group_count);
		if(group_count > 0)
		{
			int i;

			strcpy(buffer, path);
			strcat(buffer, "/");
			strcat(buffer, groupName);		

			for(i = 0; i < group_count; i++)
			{
				FMOD::EventGroup *childGroup = NULL;
				if( group->getGroupByIndex(i, 0, &childGroup) != FMOD_OK )
				{
					result = 0;
					break;
				}

				result = fmodGetLeafGroupNamesRecursive((void*)childGroup, buffer, func, userData);
			}
		}
		else
		{
			func(path, eventGroup, groupName, userData);

		}
	}
	else
	{
		result = 0;
	}

	return result;
}

U32 fmodProjectGetLeafGroupStash(void *project, StashTable stashTable)
{
	int i;
	int group_count = 0;
	FMOD::EventProject *p = (FMOD::EventProject*)project;

	if(!event_system)
	{
		return 0;
	}

	p->getNumGroups(&group_count);
	for(i=0; i<group_count; i++)
	{
		FMOD::EventGroup *group = NULL;
		p->getGroupByIndex(i, 0, &group);

		fmodGetLeafGroupNamesRecursive(group, "", fmodGetGroupNamesStash, stashTable);
	}

	return 1;
}

U32 fmodProjectGetLeafGroupNames(void *project, char ***groups)
{
	int i;
	int group_count = 0;
	FMOD::EventProject *p = (FMOD::EventProject*)project;

	if(!event_system)
	{
		return 0;
	}

	p->getNumGroups(&group_count);
	for(i=0; i<group_count; i++)
	{
		FMOD::EventGroup *group = NULL;
		p->getGroupByIndex(i, 0, &group);

		fmodGetLeafGroupNamesRecursive(group, "", fmodGetGroupNames, groups);
	}

	return 1;
}

U32 fmodGetGroupNamesFromProject(const char *projectName, char ***groupNames)
{
	U32 result = 0;
	void *fmodProject;
	if( fmodProjectByName(projectName, &fmodProject) ) 
	{
		result = fmodProjectGetLeafGroupNames(fmodProject, groupNames);
	}

	return result;
}



U32 fmodProjectGetGroups(void *project, void ***groups)
{
	int i;
	int group_count = 0;
	FMOD::EventProject *p = (FMOD::EventProject*)project;

	if(!event_system)
	{
		return 0;
	}

	p->getNumGroups(&group_count);
	for(i=0; i<group_count; i++)
	{
		FMOD::EventGroup *group = NULL;
		p->getGroupByIndex(i, 0, &group);

		if(group)
		{
			eaPush((cEArrayHandle*)groups, group);
		}
	}

	return 1;
}


U32 fmodGroupGetGroups(void *group_in, void ***groups)
{
	int i;
	int group_count = 0;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group_in;

	if(!event_system)
	{
		return 0;
	}

	g->getNumGroups(&group_count);
	for(i=0; i<group_count; i++)
	{
		FMOD::EventGroup *group = NULL;
		g->getGroupByIndex(i, 0, &group);

		if(group)
		{
			eaPush((cEArrayHandle*)groups, group);
		}
	}

	return 1;
}

U32 fmodGetGroupByProjectWithPath(void *project, void *startGroup, const char *path, void **eventGroup)
{
	U32 result = 0;
	if(!startGroup)
	{

	}
	

	return result;
}

U32 fmodGroupGetEventGroups(void *group, void ***eventGroups)
{
	int i;
	int numGroups = 0;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	if(!event_system)
	{
		return 0;
	}

	g->getNumGroups(&numGroups);
	for(i=0; i<numGroups; i++)
	{
		FMOD::EventGroup *eventGroup = NULL;
		g->getGroupByIndex(i, FMOD_EVENT_INFOONLY, &eventGroup);

		if(eventGroup)
		{
			eaPush((cEArrayHandle*)eventGroups, eventGroup);
		}
	}

	return 1;
}


U32 fmodGroupGetEventsWithPrefix(void *group, char *eventNamePrefix, void ***events)
{
	int i;
	int event_count = 0;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	if(!event_system)
	{
		return 0;
	}

	g->getNumEvents(&event_count);
	for(i=0; i<event_count; i++)
	{
		char *eventName = NULL;
		char *result;
		FMOD::Event *event = NULL;
		g->getEventByIndex(i, FMOD_EVENT_INFOONLY, &event);

		if(event)
		{
			fmodEventGetName(event, &eventName);
			if(eventName)
			{
				result = strstri(eventName, eventNamePrefix);
				if(result && result == eventName) // was it found, was it found at the beginning 
				{
					eaPush((cEArrayHandle*)events, event);
				}
			}
		}
	}

	return 1;

}

U32 fmodGroupGetEvents(void *group, void ***events)
{
	int i;
	int event_count = 0;
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	if(!event_system)
	{
		return 0;
	}

	g->getNumEvents(&event_count);
	for(i=0; i<event_count; i++)
	{
		FMOD::Event *event = NULL;
		g->getEventByIndex(i, FMOD_EVENT_INFOONLY, &event);

		if(event)
		{
			eaPush((cEArrayHandle*)events, event);
		}
	}

	return 1;
}

U32 fmodEventGetEvents(void *event, void ***events_out)
{
	int i;
	FMOD_EVENT_INFO info;// = {0};
	FMOD_EVENT *events[40] = {0};
	FMOD::Event *e = (FMOD::Event*)event;

	if(!event_system)
	{
		return 0;
	}

	info.numinstances = ARRAY_SIZE(events);
	info.instances = events;
	e->getInfo(NULL, NULL, &info);

	for(i=0; i<info.numinstances; i++)
	{
		if(events[i])
		{
			eaPush((cEArrayHandle*)events_out, events[i]);
		}
	}

	return 1;
}

void fmodEventProjectGetName(void *project, char **name)
{
	FMOD::EventProject *p = (FMOD::EventProject*)project;
	static FMOD_EVENT_PROJECTINFO projinfo;

	p->getInfo(&projinfo);
	*name = projinfo.name;
}

void fmodEventGroupGetName(void *group, char **name)
{
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	g->getInfo(NULL, name);
}

FMOD_RESULT fmodEventGetName(void *event, char **name)
{
	FMOD::Event *e = (FMOD::Event*)event;

	return e->getInfo(NULL, name, NULL);
}

U32 fmodEventGroupIsPlaying(void *group)
{
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;
	FMOD_EVENT_STATE state = 0;

	g->getState(&state);

	return !!(state & FMOD_EVENT_STATE_PLAYING);
}

void fmodEventProjectSetUserData(void *project, void *userdata)
{
	FMOD::EventProject *p = (FMOD::EventProject*)project;

	p->setUserData(userdata);
}

void fmodEventProjectGetUserData(void *project, void **userdata)
{
	FMOD::EventProject *p = (FMOD::EventProject*)project;

	p->getUserData(userdata);
}

void fmodEventGroupSetUserData(void *group, void *userdata)
{
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	g->setUserData(userdata);
}

void fmodEventGroupGetUserData(void *group, void **userdata)
{
	FMOD::EventGroup *g = (FMOD::EventGroup*)group;

	g->getUserData(userdata);
}

void fmodEventGetInfoEvent(void *event, void **event_info)
{
	FMOD::Event *e = (FMOD::Event*)event;
	FMOD::EventGroup *parent = NULL;

	if(e)
	{
		int index = 0;
		if(e->getInfo(&index, NULL, NULL)==FMOD_OK)
		{
			e->getParentGroup(&parent);
			parent->getEventByIndex(index, FMOD_EVENT_INFOONLY, (FMOD::Event**)event_info);
		}
	}
}

void fmodClearPerMapFlags(void)
{
	int i;

	for(i=0; i<eaSize((cEArrayHandle*)&oncePerMapList); i++)
	{
		void *e = oncePerMapList[i];

		fmodEventSetCanPlay(e, 1);
	}
}

//typedef void* (*sndTreeIterator)(char* name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata);
U32 perMapGather(char *name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **new_p_userdata)
{
	if(type!=STT_EVENT)
	{
		return 1;
	}

	if(fmodEventHasProperty(e_g_c, "OncePerMap"))
	{
		eaPush((cEArrayHandle*)&oncePerMapList, e_g_c);
	}

	return 1;
}

void fmodPreProcessEvents(void)
{
	if(oncePerMapList)
	{
		eaClear((cEArrayHandle*)&oncePerMapList);
	}
	FMOD_EventSystem_TreeTraverse(STT_PROJECT, perMapGather, NULL, NULL);
	fmodClearPerMapFlags();
}

U32 fmodEventIsInfoOnly(void *event)
{
	FMOD_EVENT_STATE state = 0;
	FMOD::Event *e = (FMOD::Event*)event;

	e->getState(&state);

	return !!(state & FMOD_EVENT_STATE_INFOONLY);
}

//void* fmodEventGetMemTracker(void *event)
//{
//	FMOD::Event *e = (FMOD::Event*)event;
//
//	static_tracker.clear();
//	e->getMemoryUsed(NULL);
//	e->getMemoryUsed(&static_tracker);
//	e->getMemoryUsed(NULL);
//
//	return (void*)&static_tracker;
//}


U32 FMOD_EventSystemCreateEventReverb(void **eventReverb)
{
	FMOD_RESULT result = FMOD_OK;

	if(!event_system)
	{
		return result;
	}

	FMOD::EventReverb *reverb;
	//FMOD_REVERB_PROPERTIES prop1 = FMOD_PRESET_CONCERTHALL;
	//FMOD_REVERB_PROPERTIES prop2 = FMOD_PRESET_CAVE;
	//FMOD_REVERB_PROPERTIES prop3 = FMOD_PRESET_SEWERPIPE;
	FMOD_REVERB_PROPERTIES prop4 = FMOD_PRESET_LIVINGROOM;
	FMOD_REVERB_PROPERTIES properties = prop4;
	//int i;
	//
	//i = randInt(4);
	//switch(i)
	//{
	//	case 0: properties = prop1; break;
	//	case 1: properties = prop2; break;
	//	case 2: properties = prop3; break;
	//	case 3: properties = prop4; break;
	//}

 
	result = event_system->createReverb(&reverb);
	reverb->setProperties(&properties);

	*eventReverb = reverb;

	return result;
}

U32 FMOD_EventSystemGetReverbProperties(void *properties)
{
	FMOD_RESULT result = FMOD_OK;

	if(!event_system)
	{
		return result;
	}
 
	FMOD_REVERB_PROPERTIES *props = (FMOD_REVERB_PROPERTIES*)properties;

	result = event_system->getReverbProperties(props);

	return result;
}


U32 FMOD_EventReverbSet3DAttributes(void *eventReverb, Vec3 pos, F32 minDistance, F32 maxDistance)
{
	FMOD_RESULT result = FMOD_OK;

	FMOD::EventReverb *reverb = (FMOD::EventReverb *)eventReverb;
	if(reverb)
	{
		result = reverb->set3DAttributes((FMOD_VECTOR*)pos, minDistance, maxDistance);
	}
	
	return result;
}

U32 FMOD_EventReverbGet3DAttributes(void *eventReverb, Vec3 *pos, F32 *minDistance, F32 *maxDistance)
{
	FMOD_RESULT result = FMOD_OK;

	FMOD::EventReverb *reverb = (FMOD::EventReverb *)eventReverb;
	if(reverb)
	{
		result = reverb->get3DAttributes((FMOD_VECTOR*)pos, minDistance, maxDistance);
	}

	return result;
}

U32 FMOD_EventReverbSetActive(void *eventReverb, bool active)
{
	FMOD_RESULT result = FMOD_OK;

	FMOD::EventReverb *reverb = (FMOD::EventReverb *)eventReverb;
	if(reverb)
	{
		result = reverb->setActive(active);
	}

	return result;
}

U32 FMOD_EventReverbGetActive(void *eventReverb, bool *active)
{
	FMOD_RESULT result = FMOD_OK;

	FMOD::EventReverb *reverb = (FMOD::EventReverb *)eventReverb;
	if(reverb)
	{
		result = reverb->getActive(active);
	}

	return result;
}


int fmodEventSetReverbDryLevel(void *event, F32 level)
{
    FMOD_RESULT result = FMOD_OK;
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value = level;

	result = e->setPropertyByIndex(FMOD_EVENTPROPERTY_REVERBDRYLEVEL, &value);

	return result == FMOD_OK;
}

F32 fmodEventGetReverbDryLevel(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_REVERBDRYLEVEL, &value);

	return value;
}

int fmodEventSetReverbWetLevel(void *event, F32 level)
{
    FMOD_RESULT result = FMOD_OK;
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value = level;

	result = e->setPropertyByIndex(FMOD_EVENTPROPERTY_REVERBWETLEVEL, &value);

	return result == FMOD_OK;
}

F32 fmodEventGetReverbWetLevel(void *event)
{
	FMOD::Event *e = (FMOD::Event*)event;
	F32 value;

	e->getPropertyByIndex(FMOD_EVENTPROPERTY_REVERBWETLEVEL, &value);

	return value;
}


U32 fmodDSPGetNumParameters(void *dsp, int *numParams)
{
	FMOD_RESULT result = FMOD_OK;
	
	if(dsp)
	{
		FMOD::DSP *fmodDSP = (FMOD::DSP *)dsp;
		result = fmodDSP->getNumParameters(numParams);
	}

	return result;
}

U32 fmodDSPGetParameter(void *dsp, int index, float *value, char *valueStr, int valueStrLen)
{
	FMOD_RESULT result = FMOD_OK;

	if(dsp)
	{
		FMOD::DSP *fmodDSP = (FMOD::DSP *)dsp;
		result = fmodDSP->getParameter(index, value, valueStr, valueStrLen);
	}

	return result;
}

U32 fmodDSPGetParameterInfo(void *dsp, int index, char *name, char *label, char *descrption, int descriptionLen, float *min, float *max)
{
	FMOD_RESULT result = FMOD_OK;

	if(dsp)
	{
		FMOD::DSP *fmodDSP = (FMOD::DSP *)dsp;
		result = fmodDSP->getParameterInfo(index, name, label, descrption, descriptionLen, min, max);
	}

	return result;
}

U32 fmodEventGetPropertyByName(void *event, const char *propertyName, void *value, bool thisInstanceOnly)
{
	FMOD_RESULT result = FMOD_OK;

	FMOD::Event *fmodEvent = (FMOD::Event *)event;
	if(fmodEvent)
	{
		result = fmodEvent->getProperty(propertyName, value, thisInstanceOnly);
	}

	return result;
}

FMOD_SYSTEM *fmodGetSystem(void)
{
	return (FMOD_SYSTEM*)fmod_system;
}

const char *fmodGetErrorText(FMOD_RESULT result)
{
	return FMOD_ErrorString(result);
}

typedef struct AmpAnalysisState
{
	float amp;
	void *userData;
} AmpAnalysisState;


FMOD_RESULT F_CALLBACK sndAmpAnalysisDSPCreate(FMOD_DSP_STATE *dsp)
{
	AmpAnalysisState *state;

	state = (AmpAnalysisState *)calloc(1, sizeof(AmpAnalysisState));
	if (!state)
	{
		return FMOD_ERR_MEMORY;
	}

	// grab the current DSP's userData and save that to our internal
	((FMOD::DSP*)dsp->instance)->getUserData(&state->userData);

	// keep track
	dsp->plugindata = state;

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK sndAmpAnalysisDSPRelease(FMOD_DSP_STATE *dsp)
{
	SAFE_FREE(dsp->plugindata);

	return FMOD_OK;
}

FMOD_RESULT F_CALLBACK sndAmpAnalysisDSPCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int outchannels) 
{ 
	int i;
	int maxIndex;
	FMOD_DSP *thisdsp = dsp_state->instance; 
	SoundSource *source;
	AmpAnalysisState *state = (AmpAnalysisState*)dsp_state->plugindata;
	F32 amp;
	
	assert(inchannels == outchannels);

	source = (SoundSource*)state->userData;
	
	state = (AmpAnalysisState*)dsp_state->plugindata;

	amp = state->amp;

	maxIndex = length * inchannels;

	// just use one channel to determine amp
	for(i = 0; i < maxIndex; i += inchannels)
	{
		amp = fabs(inbuffer[i]) * 0.01 + amp * 0.99;
	}

	state->amp = amp;

	// TMP! not thread safe
	if(source)
	{
		source->currentAmp = state->amp;
	}

	// copy interleaved data
	memcpy(outbuffer, inbuffer, sizeof(float) * length * inchannels);
	
	return FMOD_OK; 
} 

U32 sndAnalysisCreateDSP(void *userData, void **dspOut)
{
	FMOD_RESULT result;
	FMOD_DSP_DESCRIPTION dspdesc; 
	FMOD::DSP *ampAnalysisDSP;

	if(!fmod_system)
	{
		return FMOD_OK;
	}

	memset(&dspdesc, 0, sizeof(FMOD_DSP_DESCRIPTION)); 

	strcpy(dspdesc.name, "Amp Analysis"); 
	dspdesc.channels = 0;                   // 0 = whatever comes in, else specify. 
	dspdesc.read = sndAmpAnalysisDSPCallback; 
	dspdesc.create = sndAmpAnalysisDSPCreate;
	dspdesc.release = sndAmpAnalysisDSPRelease;
	dspdesc.userdata = userData;

	result = fmod_system->createDSP(&dspdesc, &ampAnalysisDSP);
	if(result != FMOD_OK)
	{
		Errorf("Error initializing analysis DSP (%d) %s\n", result, FMOD_ErrorString(result));
	}

	if(ampAnalysisDSP)
	{
		*dspOut = (void*)ampAnalysisDSP;
	}

	return result;
}

#endif