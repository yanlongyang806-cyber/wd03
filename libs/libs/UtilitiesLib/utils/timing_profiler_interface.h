#pragma once
GCC_SYSTEM


#if _PS3
	#define autoTimerInit()
	#define autoTimerThreadFrameBegin(x)
	#define autoTimerThreadFrameEnd()

	#define timerTickBegin()
	#define timerTickEnd()

	#define autoTimerDisableRecursion(x)
	#define autoTimerEnableRecursion(x)
#else

#include "timing_profiler.h"

typedef struct AutoTimerReader			AutoTimerReader;
typedef struct AutoTimerData			AutoTimerData;
typedef struct AutoTimerDecoder			AutoTimerDecoder;
typedef struct AutoTimerReaderStream	AutoTimerReaderStream;
typedef struct PerformanceInfo			PerformanceInfo;
typedef struct FragmentedBuffer			FragmentedBuffer;
typedef struct ProfileFileWriter		ProfileFileWriter;
typedef struct ProfileFileReader		ProfileFileReader;

typedef enum PerformanceInfoType {
	PERFINFO_TYPE_CPU	= 0,
	PERFINFO_TYPE_BITS	= 1,
	PERFINFO_TYPE_MISC	= 2,
} PerformanceInfoType;

// Initialization.

void 	autoTimerInit(void);

const AutoTimerData *autoTimerGet(void);
void 	autoTimerSet(const AutoTimerData *data);
bool autoTimerDisableConnectAtStartup(bool disabled);

// Thread wrappers.

void	autoTimerThreadFrameBegin(const char* threadName);
void	autoTimerThreadFrameEnd(void);

#define timerTickBegin()	autoTimerThreadFrameBegin(__FUNCTION__)
#define timerTickEnd()		autoTimerThreadFrameEnd()

void	autoTimerDisableRecursion(S32* didDisableOut);
void	autoTimerEnableRecursion(S32 didDisable);

// Reader interface.

typedef enum AutoTimerReaderMsgType {
	ATR_MSG_NEW_BUFFER,
	ATR_MSG_WAIT_FOR_BUFFER_AVAILABLE,
	ATR_MSG_ANY_THREAD_NEW_BUFFER_AVAILABLE,
} AutoTimerReaderMsgType;

typedef struct AutoTimerReaderMsgOut {
	union {
		struct {
			struct {
				U32					stopSendingBuffers : 1;
			} flags;
		} newBuffer;
	};
} AutoTimerReaderMsgOut;

typedef struct AutoTimerReaderMsg {
	AutoTimerReaderMsgType			msgType;
	void*							userPointer;
	
	union {
		struct {
			FragmentedBuffer*		fb;
			U32						updatesRemaining;
			AutoTimerReaderStream** streams;
			void**					streamUserPointers;
			U32						streamCount;
		} newBuffer;
	};
	
	AutoTimerReaderMsgOut*			out;
} AutoTimerReaderMsg;

typedef void (*AutoTimerReaderMsgHandler)(const AutoTimerReaderMsg* msg);

void	autoTimerReaderCreate(	AutoTimerReader** rOut,
								AutoTimerReaderMsgHandler msgHandler,
								void* userPointer);

void	autoTimerReaderDestroy(AutoTimerReader** rInOut);

void	autoTimerReaderStreamCreate(AutoTimerReader* r,
									AutoTimerReaderStream** streamOut,
									void* userPointer);

void	autoTimerReaderStreamDestroy(AutoTimerReaderStream** streamInOut);

void	autoTimerReaderStreamGetUserPointer(AutoTimerReaderStream* stream,
											void** userPointerOut);

void	autoTimerReaderRead(AutoTimerReader* r);

void	autoTimerReaderGetBytesRemaining(	AutoTimerReader* r,
											U32* bytesOut);

void	autoTimerSetTimerBreakPoint(U32 threadID,
									U32 startInstanceID,
									U32 timerInstanceID,
									S32 enabled);

void	autoTimerSetTimerForcedOpen(U32 threadID,
									U32 startInstanceID,
									U32 timerInstanceID,
									S32 enabled);

// Decoder interface.

typedef enum AutoTimerDecoderMsgType {
	AT_DECODER_MSG_SYSTEM_INFO,
	
	AT_DECODER_MSG_DT_CREATED,
	AT_DECODER_MSG_DT_DESTROYED,
	
	AT_DECODER_MSG_DT_NAMED,
	AT_DECODER_MSG_DT_FRAME_UPDATE,
	AT_DECODER_MSG_DT_MAX_DEPTH_UPDATE,
	AT_DECODER_MSG_DT_SCAN_FRAME,
	AT_DECODER_MSG_DT_ERROR,

	AT_DECODER_MSG_DTI_CREATED,
	AT_DECODER_MSG_DTI_DESTROYED,

	AT_DECODER_MSG_DTI_FRAME_UPDATE,
	AT_DECODER_MSG_DTI_FLAGS_UPDATE,
} AutoTimerDecoderMsgType;

typedef struct AutoTimerDecodedThread			AutoTimerDecodedThread;
typedef struct AutoTimerDecodedTimerInstance	AutoTimerDecodedTimerInstance;

typedef struct AutoTimerDecoderMsg {
	AutoTimerDecoderMsgType				msgType;
	
	struct {
		AutoTimerDecoder*				d;
		void*							userPointer;
	} d;
	
	struct {
		AutoTimerDecodedThread*			dt;
		void*							userPointer;
	} dt;
	
	struct {
		AutoTimerDecodedTimerInstance*	dti;
		void*							userPointer;
	} dti;
	
	union {
		struct {
			struct {
				U64						cyclesPerSecond;
				U32						countReal;
				U32						countVirtual;
			} cpu;
		} systemInfo;

		struct {
			U32							id;
		} dtCreated;
		
		struct {
			const char*					name;
		} dtNamed;

		struct {
			U32							frame;
			
			struct {
				U64						begin;
				U64						active;
				U64						blocking;
			} cycles;
			
			struct {
				U64						cycles;
				
				struct {
					U64					user;
					U64					kernel;
				} ticks;
			} os;
			
			U32							bytesReceived;
		} dtFrameUpdate;
		
		struct {
			U32							maxDepth;
		} dtMaxDepthUpdate;
		
		struct {
			struct {
				U64						begin;
				U64						total;
			} cycles;

			struct {
				U64						cycles;
				
				struct {
					U64					user;
					U64					kernel;
				} ticks;
			} os;
		} dtScanFrame;
		
		struct {
			const char*					errorText;
		} dtError;

		struct {
			const char*					name;
			U32							id;
			U32							instanceID;
			U64							timerID;
			U32							infoType;
			
			struct {
				U32						isBlocking : 1;
			} flags;
		} dtiCreated;
		
		struct {
			U64							cyclesActive;
			U64							cyclesBlocking;
			U64							cyclesActiveChildren;
			U32							count;
		} dtiFrameUpdate;
		
		struct {
			U32							instanceID;
			
			struct {
				U32						isBreakpoint	: 1;
				U32						forcedOpen		: 1;
				U32						forcedClosed	: 1;
			} flags;
		} dtiFlagsUpdate;
	};
} AutoTimerDecoderMsg;

typedef void (*AutoTimerDecoderMsgHandler)(const AutoTimerDecoderMsg* msg);

void	autoTimerDecoderCreate(	AutoTimerDecoder** dOut,
								AutoTimerDecoderMsgHandler msgHandler,
								void* userPointer);

void	autoTimerDecoderDestroy(AutoTimerDecoder** dInOut);

S32		autoTimerDecoderDecode(	AutoTimerDecoder* d,
								FragmentedBuffer* fb);

S32		autoTimerDecodedThreadSetUserPointer(	AutoTimerDecodedThread* dt,
												void* userPointer);
												
S32		autoTimerDecodedThreadGetUserPointer(	AutoTimerDecodedThread* dt,
												void** userPointerOut);

S32		autoTimerDecodedThreadGetID(AutoTimerDecodedThread* dt,
									U32* idOut);

S32		autoTimerDecodedTimerInstanceSetUserPointer(AutoTimerDecodedTimerInstance* dti,
													void* userPointer);

S32		autoTimerDecodedTimerInstanceGetUserPointer(AutoTimerDecodedTimerInstance* dti,
													void** userPointerOut);

S32		autoTimerDecodedTimerInstanceGetParent(	AutoTimerDecodedTimerInstance* dti,
												AutoTimerDecodedTimerInstance** dtiParentOut);

S32		autoTimerDecodedTimerInstanceGetInstanceID(	AutoTimerDecodedTimerInstance* dti,
													U32* instanceIDOut);

// ProfileFileWriter interface.

typedef enum ProfileFileWriterMsgType {
	PFW_MSG_NEW_BUFFER_AVAILABLE,
	PFW_MSG_WAIT_FOR_NEW_BUFFER,
} ProfileFileWriterMsgType;

typedef void (*ProfileFileWriterMsgHandler)(ProfileFileWriter* pfw,
											ProfileFileWriterMsgType msgType,
											void* userPointer);

S32		pfwCreate(	ProfileFileWriter** pfwOut,
					ProfileFileWriterMsgHandler msgHandler,
					void* userPointer);

S32		pfwDestroy(ProfileFileWriter** pfwInOut);

S32		pfwStart(	ProfileFileWriter* pfw,
					const char* fileName,
					S32 createLocalReader,
					AutoTimerDecoder* d);

void	pfwStop(ProfileFileWriter* pfw);

S32		pfwWriteFragmentedBuffer(	ProfileFileWriter* pfw,
									FragmentedBuffer* fb);

S32		pfwReadReader(ProfileFileWriter* pfw);

// ProfileFileReader interface.

S32		pfrCreate(ProfileFileReader** pfrOut);

S32		pfrDestroy(ProfileFileReader** prfInOut);

S32		pfrStart(	ProfileFileReader* pfr,
					const char* fileName,
					AutoTimerDecoder* d);

S32		pfrStop(ProfileFileReader* pfr);

S32		pfrReadFragmentedBuffer(ProfileFileReader* pfr,
								FragmentedBuffer** fbOut);
						
// timerRecordThread interface.

void	timerRecordThreadStop(void);
void	timerRecordThreadStart(const char* fileName);

// ProfilerConnect.

void	profilerConnectPort(const char* hostName,
							U32 hostPort);
							
void	profilerConnect(const char* hostName);

// ThreadSampler.

#if !PLATFORM_CONSOLE
typedef struct ThreadSampler ThreadSampler;

void	threadSamplerCreate(	ThreadSampler** tsOut,
								U32 maxSamples);

void	threadSamplerDestroy(ThreadSampler** tsInOut);

void	threadSamplerSetThreadID(	ThreadSampler* ts,
									U32 tid);

void	threadSamplerStart(ThreadSampler* ts);

void	threadSamplerStop(ThreadSampler* ts);

void	threadSamplerReport(ThreadSampler* ts);
#endif

#endif
