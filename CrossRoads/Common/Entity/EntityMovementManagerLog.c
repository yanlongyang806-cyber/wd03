/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "EString.h"
#include "logging.h"
#include "AutoGen/EntityMovementManagerLog_c_ast.h"
#include "AutoGen/EntityMovementManager_h_ast.h"
#include "mutex.h"
#include "net.h"
#include "structNet.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_STRUCT;
typedef struct MovementLogsLocation {
	MovementLog**			logs;
	StashTable				stLogs;	NO_AST
} MovementLogsLocation;

typedef struct MovementLogs {
	MovementLogsLocation*	remote;
	MovementLogsLocation*	local;
} MovementLogs;

#if _PS3
	static __thread S32 mgFlagIsForegroundThread;
#else
	static S32 mgFlagIsForegroundThreadTlsIndex;
#endif

void mmSetIsForegroundThreadForLogging(void){
	#if _PS3
		mgFlagIsForegroundThread = 1;
	#else
		if(!mgFlagIsForegroundThreadTlsIndex){
			mgFlagIsForegroundThreadTlsIndex = TlsAlloc();
		}
		TlsSetValue(mgFlagIsForegroundThreadTlsIndex, (void*)(intptr_t)1);
	#endif
}

S32 mmIsForegroundThreadForLogging(void){
	if(mgState.fg.flags.notThreaded){
		return !mgState.bg.flags.threadIsBG;
	}

	if(GetCurrentThreadId() == mgState.fg.threadID){
		return 1;
	}
	#if _PS3
		return mgFlagIsForegroundThread;
	#else
		return	mgFlagIsForegroundThreadTlsIndex &&
				!!TlsGetValue(mgFlagIsForegroundThreadTlsIndex);
	#endif
}

static CrypticalSection csLogLock;

static void mmLogLockEnter(void){
	csEnter(&csLogLock);
}

static void mmLogLockLeave(void){
	csLeave(&csLogLock);
}

static MovementLog* mmLogGetByName(	MovementLog*** eaLogs,
									StashTable* stLogs,
									const char* moduleName)
{
	MovementLog* log;

	if(!*stLogs){
		*stLogs = stashTableCreateWithStringKeys(10, StashDefault);
	}

	if(!stashFindPointer(*stLogs, moduleName, &log)){
		log = StructAlloc(parse_MovementLog);

		eaPush(eaLogs, log);

		log->name = StructAllocString(moduleName);

		stashAddPointer(*stLogs, log->name, log, true);
	}

	return log;
}

static void mmLogsLocationDestroy(MovementLogsLocation** logsLocationInOut){
	MovementLogsLocation* logsLocation = SAFE_DEREF(logsLocationInOut);

	if(logsLocation){
		stashTableDestroy(logsLocation->stLogs);
		logsLocation->stLogs = NULL;

		StructDestroySafe(parse_MovementLogsLocation, logsLocationInOut);
	}
}

static void mmGetLogsLocation(	MovementLogsLocation** logsLocationOut,
								MovementLogs* logs,
								S32 local)
{
	*logsLocationOut = local ? logs->local : logs->remote;

	if(!*logsLocationOut){
		*logsLocationOut = StructAlloc(parse_MovementLogsLocation);

		if(local){
			logs->local = *logsLocationOut;
		}else{
			logs->remote = *logsLocationOut;
		}
	}
}

static S32 mmGetLogs(	MovementLogs** logsOut,
						EntityRef er,
						S32 create)
{
	if(	!logsOut ||
		!er)
	{
		return 0;
	}

	if(!mgState.debug.stLogs){
		mgState.debug.stLogs = stashTableCreateInt(10);
	}
	else if(stashIntFindPointer(mgState.debug.stLogs,
								er,
								logsOut))
	{
		if(!!*logsOut){
			return 1;
		}
	}

	if(!create){
		return 0;
	}

	*logsOut = callocStruct(MovementLogs);

	stashIntAddPointer(mgState.debug.stLogs, er, *logsOut, true);

	return 1;
}

static S32 mmLogvInternal(	MovementManager* mm,
							const char* module,
							const MovementRequester* mr,
							const char* format,
							va_list va)
{
	const char*			forcedModule;
	char				logPrefix[100];
	char				logLine[5000];
	char				finalLine[5000];
	char*				logLineReal = logLine;
	char				logLineTag[200];
	char				moduleName[100];
	MovementLogLine*	line;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("compose line", 1);
	{
		logLine[0] = 0;
		logLineTag[0] = 0;
		finalLine[0] = 0;

		forcedModule = mmIsForegroundThreadForLogging() ? NULL : mgState.bg.log.forcedModule;

		vsprintf(	logLine,
					format,
					va);

		if(logLine[0] == '['){
			// The line has a tag in it, swap that to the front.
		
			char* tagEnd = strchr(logLine, ']');
		
			if(	tagEnd &&
				tagEnd != logLine + 1)
			{
				strncpy(logLineTag,
						logLine + 1, 
						tagEnd - logLine - 1);
					
				logLineReal = tagEnd + 1;
			
				while(*logLineReal == ' '){
					logLineReal++;
				}
			}
		}

		if(!module){
			module = "default";
		}

		sprintf(moduleName,
				"%s%s%s",
				module,
				forcedModule ? "_" : "",
				forcedModule ? forcedModule : "");

		sprintf(logPrefix,
				"%4d %s: ",
				mgState.frameCount % 10000,
				mmIsForegroundThreadForLogging() ? "fg" : "bg");

		PERFINFO_AUTO_START("StructAlloc", 1);
		line = StructAlloc(parse_MovementLogLine);
		PERFINFO_AUTO_STOP();

		if(	mr ||
			logLineTag[0])
		{
			char mrPrefix[200];

			mrPrefix[0] = 0;

			if(mr){
				if(mr->fg.netHandle){
					sprintf(mrPrefix,
							"%s[%u/%u]: ",
							mr->mrc->name,
							mr->handle,
							mr->fg.netHandle);
				}else{
					sprintf(mrPrefix,
							"%s[%u]: ",
							mr->mrc->name,
							mr->handle);
				}
			}

			sprintf(finalLine, 
					"%s[%s%s%s%s] %s%s",
					logPrefix,
					mr ? "mr." : "",
					mr ? mr->mrc->name : "",
					logLineTag[0] && mr ? "," : "",
					logLineTag,
					mrPrefix,
					logLineReal);
		}else{
			sprintf(finalLine,
					"%s%s",
					logPrefix,
					logLineReal);
		}

		PERFINFO_AUTO_START("estrCopy2", 1);
		estrCopy2(&line->text, finalLine);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
	
	PERFINFO_AUTO_START("push line", 1);
	{
		mmLogLockEnter();

		if(SAFE_MEMBER(mm, flags.debugging)){
			MovementLog*			log;
			MovementLogsLocation*	logsLocation;

			if(!mm->logs){
				mmGetLogs(	&mm->logs,
							mm->entityRef,
							1);
			}

			mmGetLogsLocation(	&logsLocation,
								mm->logs,
								1);

			log = mmLogGetByName(	&logsLocation->logs,
									&logsLocation->stLogs,
									moduleName);

			if(log->bytesTotal < 100 * SQR(1000)){
				eaPush(&log->lines, line);
			
				log->bytesTotal += estrGetCapacity(&line->text) + 10;

				// NULL line so it doesn't get destroyed.

				line = NULL;
			}
		}

		mmLogLockLeave();
	}
	PERFINFO_AUTO_STOP();

	if(line){
		PERFINFO_AUTO_START("destroy unused line", 1);
		StructDestroySafe(parse_MovementLogLine, &line);
		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 wrapped_mmLogv(	MovementManager* mm,
					const char* module,
					MovementRequester* mr,
					const char* format,
					va_list va)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();

	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogvInternal(mm, module, mr, format, va);
	
	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mmLog(	MovementManager* mm,
					const char* module,
					const char* format,
					...)
{
	S32 ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	VA_START(va, format);
		ret = mmLogvInternal(mm, module, NULL, format, va);
	VA_END();

	PERFINFO_AUTO_STOP();

	return ret;
}

static S32 mmLogPointListInternal(	MovementManager* mm,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3* points,
									S32 pointCount)
{
	char	buffer[5000];
	char*	pos = buffer;
	S32		i;

	buffer[0] = 0;

	pos += snprintf_s(	pos,
						STRBUF_REMAIN(buffer, pos),
						"[%s] PointList.#%2.2x%2.2x%2.2x%2.2x:",
						tags,
						argb >> 24,
						(argb >> 16) & 0xff,
						(argb >> 8) & 0xff,
						argb & 0xff);

	for(i = 0; i < pointCount; i++){
		pos += snprintf_s(	pos,
							STRBUF_REMAIN(buffer, pos),
							"(%1.3f,%1.3f,%1.3f)",
							vecParamsXYZ(points[i]));
	}

	return mmLog(mm, module, "%s", buffer);
}

S32 wrapped_mmLogPointList(	MovementManager* mm,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3* points,
							S32 pointCount)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(mm, module, tags, argb, points, pointCount);
	
	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mmLogPoint(	MovementManager* mm,
						const char* module,
						const char* tags,
						U32 argb,
						const Vec3 point)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(mm, module, tags, argb, (Vec3*)point, 1);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

static S32 mmLogSegmentListInternal(MovementManager* mm,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3* vecArray,
									S32 segmentCount)
{
	char	buffer[5000];
	char*	pos = buffer;
	S32		i;

	buffer[0] = 0;

	pos += snprintf_s(	pos,
						STRBUF_REMAIN(buffer, pos),
						"[%s] SegList.#%2.2x%2.2x%2.2x%2.2x:",
						tags,
						argb >> 24,
						(argb >> 16) & 0xff,
						(argb >> 8) & 0xff,
						argb & 0xff);

	for(i = 0; i < segmentCount; i++){
		pos += snprintf_s(	pos,
							STRBUF_REMAIN(buffer, pos),
							"(%1.3f,%1.3f,%1.3f)-(%1.3f,%1.3f,%1.3f)",
							vecParamsXYZ(vecArray[i * 2]),
							vecParamsXYZ(vecArray[i * 2 + 1]));
	}

	return mmLog(mm, module, "%s", buffer);
}

S32 wrapped_mmLogSegmentList(	MovementManager* mm,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* vecs,
								S32 segmentCount)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogSegmentListInternal(mm, module, tags, argb, vecs, segmentCount);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mmLogSegment(	MovementManager* mm,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 a,
							const Vec3 b)
{
	Vec3	vecs[2];
	S32		ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	copyVec3(a, vecs[0]);
	copyVec3(b, vecs[1]);

	ret = mmLogSegmentListInternal(mm, module, tags, argb, vecs, 1);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mmLogSegmentOffset(	MovementManager* mm,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 offset)
{
	Vec3	b;
	S32		ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	addVec3(a, offset, b);
	
	ret = wrapped_mmLogSegment(	mm,
								module,
								tags,
								argb,
								a,
								b);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mmLogSegmentOffset2(MovementManager* mm,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 aOffset,
								const Vec3 b,
								const Vec3 bOffset)
{
	Vec3	a2;
	Vec3	b2;
	S32		ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER(mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	addVec3(a, aOffset, a2);
	addVec3(b, bOffset, b2);
	
	ret = wrapped_mmLogSegment(	mm,
								module,
								tags,
								argb,
								a2,
								b2);

	PERFINFO_AUTO_STOP();

	return ret;
}
									
S32 wrapped_mrLog(	const MovementRequester* mr,
					const char* module,
					const char* format,
					...)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(mr, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	VA_START(va, format);
		ret = mmLogvInternal(mr->mm, module, mr, format, va);
	VA_END();

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mrLogSegmentList(	const MovementRequester* mr,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* vecs,
								S32 segmentCount)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(mr, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogSegmentListInternal(mr->mm, module, tags, argb, vecs, segmentCount);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mrLogSegment(	const MovementRequester* mr,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 a,
							const Vec3 b)
{
	Vec3	vecs[2];
	S32		ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(mr, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	copyVec3(a, vecs[0]);
	copyVec3(b, vecs[1]);

	ret = mmLogSegmentListInternal(mr->mm, module, tags, argb, vecs, 1);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mrLogPointList(	const MovementRequester* mr,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3* points,
							S32 pointCount)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(mr, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(mr->mm, module, tags, argb, points, pointCount);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mrLogPoint(	const MovementRequester* mr,
						const char* module,
						const char* tags,
						U32 argb,
						const Vec3 point)
{
	S32 ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(mr, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(mr->mm, module, tags, argb, (Vec3*)point, 1);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

S32 wrapped_mrmLog(	const MovementRequesterMsg* msg,
					const char* module,
					const char* format,
					...)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	VA_START(va, format);
		ret = wrapped_mmLogv(pd->mm, module, pd->mr, format, va);
	VA_END();

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mrmLogSegmentList(	const MovementRequesterMsg* msg,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* vecs,
								S32 segmentCount)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogSegmentListInternal(pd->mm, module, tags, argb, vecs, segmentCount);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mrmLogSegment(	const MovementRequesterMsg* msg,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 a,
							const Vec3 b)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	Vec3								vecs[2];
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	copyVec3(a, vecs[0]);
	copyVec3(b, vecs[1]);

	ret = mmLogSegmentListInternal(pd->mm, module, tags, argb, vecs, 1);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mrmLogSegmentOffset(const MovementRequesterMsg* msg,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 offset)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	Vec3								b;
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	addVec3(a, offset, b);
	
	ret = wrapped_mrmLogSegment(msg,
								module,
								tags,
								argb,
								a,
								b);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mrmLogPointList(const MovementRequesterMsg* msg,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3* points,
							S32 pointCount)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(pd->mm, module, tags, argb, points, pointCount);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mrmLogPoint(const MovementRequesterMsg* msg,
						const char* module,
						const char* tags,
						U32 argb,
						const Vec3 point)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	S32									ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	ret = mmLogPointListInternal(pd->mm, module, tags, argb, (Vec3*)point, 1);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mmrmLog(const MovementManagedResourceMsg* msg,
					const char* module,
					const char* format,
					...)
{
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	S32										ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	VA_START(va, format);
		ret = wrapped_mmLogv(pd->mm, module, NULL, format, va);
	VA_END();

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 wrapped_mmrmLogSegment(	const MovementManagedResourceMsg* msg,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 a,
							const Vec3 b)
{
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	Vec3									vecs[2];
	S32										ret;

	PERFINFO_AUTO_START_FUNC();
	
	if(!SAFE_MEMBER2(pd, mm, flags.debugging)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	copyVec3(a, vecs[0]);
	copyVec3(b, vecs[1]);

	ret = mmLogSegmentListInternal(pd->mm, module, tags, argb, vecs, 1);

	PERFINFO_AUTO_STOP();

	return ret;
}

S32 mmLogGetCopy(	EntityRef er,
					MovementLog** logOut,
					S32 useLocalLog,
					const char* moduleName)
{
	MovementLogs*	logs = NULL;
	S32				ret = 0;

	if(!logOut){
		return 0;
	}

	mmLogLockEnter();

	stashIntFindPointer(mgState.debug.stLogs, er, &logs);

	if(logs){
		MovementLogsLocation*	logsLocation = useLocalLog ? logs->local : logs->remote;
		MovementLog*			log;

		if(	logsLocation &&
			stashFindPointer(	logsLocation->stLogs,
								moduleName ? moduleName : "default",
								&log))
		{
			*logOut = StructAlloc(parse_MovementLog);
			StructCopyFields(parse_MovementLog, log, *logOut, 0, 0);
			ret = 1;
		}
	}

	mmLogLockLeave();

	return ret;
}

void mmLogsForEach(MovementLogsForEachCallback callback){
	StashTableIterator	it;
	StashElement		element;

	if(!callback){
		return;
	}

	mmLogLockEnter();

	stashGetIterator(mgState.debug.stLogs, &it);

	while(stashGetNextElement(&it, &element)){
		MovementLogs*	logs = stashElementGetPointer(element);
		S32				i;
		EntityRef		er = stashElementGetIntKey(element);

		for(i = 0; i < 2; i++){
			MovementLogsLocation* logsLocation = i ? logs->local : logs->remote;

			if(logsLocation){
				EARRAY_CONST_FOREACH_BEGIN(logsLocation->logs, j, size);
					MovementLog* log = logsLocation->logs[j];

					callback(er, log->name, i);
				ARRAY_FOREACH_END;
			}
		}
	}

	mmLogLockLeave();
}

S32 mmLogDestroy(MovementLog** logInOut){
	if(SAFE_DEREF(logInOut)){
		StructDestroySafe(parse_MovementLog, logInOut);

		return 1;
	}

	return 0;
}

void mmLogReceive(Packet* pak){
	EntityRef				erTarget;
	MovementLogs*			logs;
	MovementLogsLocation*	logsLocation;
	
	erTarget = pktGetU32(pak);

	if(mmGetLogs(	&logs,
					erTarget,
					1))
	{
		mmLogsLocationDestroy(SAFE_MEMBER_ADDR(logs, remote));
	}

	mmGetLogsLocation(	&logsLocation,
						logs,
						0);

	ParserRecv(	parse_MovementLogsLocation,
				pak,
				logsLocation,
				0);

	logsLocation->stLogs = stashTableCreateWithStringKeys(10, StashDefault);

	EARRAY_CONST_FOREACH_BEGIN(logsLocation->logs, i, size);
		stashAddPointer(logsLocation->stLogs,
						logsLocation->logs[i]->name,
						logsLocation->logs[i],
						true);
	EARRAY_FOREACH_END;
}

void mmLogSend(	MovementClient* mc,
				EntityRef erTarget)
{
	MovementLogs*	logs;
	Packet*			pak;

	if(	!mc ||
		!mmGetLogs(&logs, erTarget, 0))
	{
		return;
	}

	mmLogLockEnter();

	if(!logs->local){
		mmLogLockLeave();
		return;
	}

	{
		U32 lineCount = 0;
		U32 charCount = 0;

		EARRAY_CONST_FOREACH_BEGIN(logs->local->logs, i, isize);
			EARRAY_CONST_FOREACH_BEGIN(logs->local->logs[i]->lines, j, jsize);
				lineCount++;
				charCount += estrLength(&logs->local->logs[i]->lines[i]->text) + 1;
			EARRAY_FOREACH_END;
		EARRAY_FOREACH_END;

		printf(	"Sending mmLog, er 0x%8.8x, %u lines, %u characters: ",
				erTarget,
				lineCount,
				charCount);
	}
	
	if(mmClientPacketToClientCreate(mc, &pak, "Log")){
		pktSendU32(pak, erTarget);
		pktSendStruct(pak, logs->local, parse_MovementLogsLocation);
		mmClientPacketToClientSend(mc, &pak);
	}

	printf("done.\n");

	mmLogLockLeave();
}

AUTO_COMMAND ACMD_NAME(mmLogOnCreate);
void mmCmdLogOnCreate(S32 enabled){
	mgState.flagsMutable.logOnCreate = !!enabled;
	
	if(!enabled){
		mgState.debug.logOnCreate.radius = 0.f;
	}
}

AUTO_COMMAND ACMD_NAME(mmLogOnCreateAtPos);
void mmCmdLogOnCreateAtPos(	S32 enabled,
							const Vec3 pos,
							F32 radius)
{
	mgState.flagsMutable.logOnCreate = !!enabled;
	copyVec3(pos, mgState.debug.logOnCreate.pos);
	mgState.debug.logOnCreate.radius = radius;
}

AUTO_COMMAND ACMD_NAME(mmLogOnClientAttach);
void mmCmdLogOnClientAttach(S32 enabled){
	mgState.flagsMutable.logOnClientAttach = !!enabled;
}

static void mmGlobalLogv(	FORMAT_STR const char* format,
							va_list va)
{
	if(!mgState.debug.activeLogCount){
		return;
	}

	readLockU32(&mgState.debug.managersLock);
	{
		EARRAY_CONST_FOREACH_BEGIN(mgState.debug.mmsActive, i, isize);
		{
			MovementManager* mm = mgState.debug.mmsActive[i];
			
			wrapped_mmLogv(mm, NULL, NULL, format, va);
		}
		EARRAY_FOREACH_END;
	}
	readUnlockU32(&mgState.debug.managersLock);
}

S32 wrapped_mmGlobalLog(FORMAT_STR const char* format,
						...)
{
	if(!mgState.debug.activeLogCount){
		return 0;
	}

	VA_START(va, format);
	mmGlobalLogv(format, va);
	VA_END();

	return 1;
}

static void mmGlobalLogCamera(	const char* tags,
								const Mat4 mat)
{
	if(!mgState.debug.activeLogCount){
		return;
	}
	
	readLockU32(&mgState.debug.managersLock);
	{
		EARRAY_CONST_FOREACH_BEGIN(mgState.debug.mmsActive, i, isize);
			MovementManager* mm = mgState.debug.mmsActive[i];
			
			mmLogCameraMat(mm, mat, tags, tags);
		EARRAY_FOREACH_END;
	}
	readUnlockU32(&mgState.debug.managersLock);
}

static void mmGlobalLogSegment(	const char* tags,
								const Vec3 p0,
								const Vec3 p1,
								U32 argb)
{
	if(!mgState.debug.activeLogCount){
		return;
	}
	
	readLockU32(&mgState.debug.managersLock);
	{
		EARRAY_CONST_FOREACH_BEGIN(mgState.debug.mmsActive, i, isize);
			MovementManager* mm = mgState.debug.mmsActive[i];
			
			mmLogSegment(mm, NULL, tags, argb, p0, p1);
		EARRAY_FOREACH_END;
	}
	readUnlockU32(&mgState.debug.managersLock);
}

S32 mmSetDebugging(	MovementManager* mm,
					S32 enabled)
{
	if(	!mm
		||
		enabled &&
		mm->flags.destroying)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	enabled = !!enabled;

	if(mm->flags.debugging != (U32)enabled){
		StashTableIterator	it;
		StashElement		element;

		writeLockU32(&mgState.debug.managersLock, 0);
		mmLogLockEnter();
		{
			if(mm->flagsMutable.debugging != (U32)enabled){
				mgState.debug.changeCount++;

				if(enabled){
					if(eaFind(&mgState.debug.mmsActiveMutable, mm) >= 0){
						assert(0);
					}
					
					eaPush(&mgState.debug.mmsActiveMutable, mm);
				}
				else if(eaFindAndRemove(&mgState.debug.mmsActiveMutable, mm) < 0){
					assert(0);
				}
				
				mm->flagsMutable.debugging = enabled;
				
				if(enabled){
					if(!mgState.debug.activeLogCount++){
						globMovementLogIsEnabled = 1;
						globMovementLogSetFuncs(mmGlobalLogv,
												mmGlobalLogCamera,
												mmGlobalLogSegment);
					}
				}else{
					assert(mgState.debug.activeLogCount);
					if(!--mgState.debug.activeLogCount){
						globMovementLogIsEnabled = 0;
					}
				}
				
				mgPublic.activeLogCount = mgState.debug.activeLogCount;

				if(!mm->logs){
					mmGetLogs(	&mm->logs,
								mm->entityRef,
								0);
				}

				stashGetIterator(SAFE_MEMBER2(mm->logs, local, stLogs), &it);

				while(stashGetNextElement(&it, &element)){
					MovementLog* log = stashElementGetPointer(element);

					if(enabled){
						//printf(	"Freeing mm log 0x%p:0x%p:0x%p (%d lines).\n",
						//		mm,
						//		log,
						//		log->lines,
						//		eaSize(&log->lines));

						eaDestroyStruct(&log->lines, parse_MovementLogLine);
						
						log->bytesTotal = 0;
					}else{
						//printf(	"Disabling mm log 0x%p:0x%p:0x%p (%d lines).\n",
						//		mm,
						//		log,
						//		log->lines,
						//		eaSize(&log->lines));

						if(mm->flags.writeLogFiles){
							const char*		module = stashElementGetStringKey(element);
							char			logName[1000];
							char*			buffer = NULL;

							estrStackCreate(&buffer);

							if(!stricmp(module, "default")){
								module = NULL;
							}

							sprintf(logName,
									"movement_%s%s%d",
									module ? module : "",
									module ? "_" : "",
									mm->entityRef);

							EARRAY_CONST_FOREACH_BEGIN(log->lines, i, size);
								estrConcatf(&buffer, "%s\n", log->lines[i]->text);
							EARRAY_FOREACH_END;

							filelog_printf(logName, "%s", buffer);

							estrDestroy(&buffer);
						}
					}
				}
			}
		}
		mmLogLockLeave();
		writeUnlockU32(&mgState.debug.managersLock);
		
		if(enabled){
			mmRareLockEnter(mm);
			{
				mmLog(	mm,
						NULL,
						"[fg.pos] Last setpos: %s",
						FIRST_IF_SET(mm->lastSetPosInfoString, "not set"));
			}
			mmRareLockLeave(mm);
		}
	}
	
	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mmIsDebugging(const MovementManager* mm){
	return SAFE_MEMBER(mm, flags.debugging);
}

S32 mrIsDebugging(const MovementRequester* mr){
	return SAFE_MEMBER(mr, mm->flags.debugging);
}

S32 mmSetWriteLogFiles(	MovementManager* mm,
						S32 enabled)
{
	if(!mm){
		return 0;
	}

	mm->flagsMutable.writeLogFiles = !!enabled;

	return 1;
}

void mmLogCameraMat(MovementManager* mm,
					const Mat4 mat,
					const char* tag,
					const char* tagDraw)
{
	FOR_BEGIN(j, 3);
		mmLogSegmentOffset(	mm,
							NULL,
							tagDraw,
							0xff000000 | (0xff << ((2 - j) * 8)),
							mat[3],
							mat[j]);
	FOR_END;
	
	mmLog(	mm,
			NULL,
			"[%s] CameraMat (%f,%f,%f)(%f,%f,%f)(%f,%f,%f)(%f,%f,%f)",
			tag,
			vecParamsXYZ(mat[0]),
			vecParamsXYZ(mat[1]),
			vecParamsXYZ(mat[2]),
			vecParamsXYZ(mat[3]));
}

void mmCopyServerLogList(U32** logListOut){
	eaiCopy(logListOut, &mgState.debug.serverLogList);
}

void mmCopyLocalLogList(MovementManager*** logListOut){
	readLockU32(&mgState.debug.managersLock);
	{
		eaCopy(logListOut, &mgState.debug.mmsActive);
	}
	readUnlockU32(&mgState.debug.managersLock);
}

#include "AutoGen/EntityMovementManagerLog_c_ast.c"
