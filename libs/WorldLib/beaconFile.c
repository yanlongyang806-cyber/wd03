#include <process.h>

#include "beaconFile.h"
#include "beaconClientServerPrivate.h"
#include "beaconConnection.h"

#include "cmdparse.h"
#include "gimmeDLLWrapper.h"
#include "fileutil2.h"
#include "wininclude.h"
#include "WorldGrid.h"
#include "zutils.h"
#include "logging.h"
#include "UGCProjectUtils.h"

#include "wlBeacon_h_ast.h"
#include "beaconClientServerPrivate_h_ast.h"
#include "beaconFile_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// ------------ Beacon file format history ------------------------------------------------------------------
// Version 0: [*** UNSUPPORTED ***]
//   - ???
// Version 1: [*** UNSUPPORTED ***]
//   - ???
// Version 2: [*** UNSUPPORTED ***]
//   - ???
// Version 3: [*** UNSUPPORTED ***]
//   - ???
// Version 4: [*** UNSUPPORTED ***]
//   - ???
// Version 5: [*** UNSUPPORTED ***]
//   - Added connections between basic (NPC) beacons.
//   - Pre-stored beacon indexes for quick lookup when writing the file.
// Version 6:
//   - The pre-stored indexes from v5 were botched for combat beacons.
//   - Bumped lowest-allowable-version to 6, all previous files are fucked.
// Version 7:
//   - Removed beacon radius for combat beacons.
//   - Added beacon ceiling distances (not floor distance though).
// Version 8:
//   - Added beacon floor distances (damn I'm an idiot, see version 7).
//   - Added beacon block galaxies - groups of blocks that can walk to each other.
//   - Removed "blockConn" check data.
// Version 9:
//	 - Changed beacon connection heights to S16s
// Version 10:
//   - Changed file extension to .zone
// Version 11:
//	 - Changed beacon connection heights to U16s
// ----------------------------------------------------------------------------------------------------------

// ------------ Beacon date file format history -------------------------------------------------------------
// Version 0:
//   - Only contains latest data date.
// Skipped 1-8 for some reason.
// Version 9:
//   - Contains CRC of stuff.
// ----------------------------------------------------------------------------------------------------------

#if !PLATFORM_CONSOLE

BeaconProcessState beacon_process;

static const S32 curBeaconFileVersion = 11;
static const S32 oldestAllowableBeaconFileVersion = 8;

static const S32 curBeaconProcVersion = 16;
static const S32 curBeaconDateFileVersion = 10;

static struct {
	FILE*					fileHandle;
	S32						useFileCheck;
	BeaconWriterCallback	callback;
} beaconWriter;

#define FWRITE(x) beaconWriter.callback(&x, sizeof(x))
#define FWRITE_U32(x) {fwriteCompressedU32(x);}
#define FWRITE_U8(x) {U8 i__ = (x);FWRITE(i__);}
#define FWRITE_CHECK(x) if(beaconWriter.useFileCheck) beaconWriter.callback(x,(U32)strlen(x))

static void fwriteCompressedU32(U32 i){
	//fwrite(&i, sizeof(i), 1, f);
	
	if(i < (1 << 6)){
		U8 value = 0x0 | (i << 2);
		FWRITE_U8(value);
	}
	else if(i < (1 << 14)){
		U16 value = 0x1 | (i << 2);
		FWRITE_U8(value & 0xff);
		FWRITE_U8((value >> 8) & 0xff);
	}
	else if(i < (1 << 22)){
		U32 value = 0x2 | (i << 2);
		FWRITE_U8(value & 0xff);
		FWRITE_U8((value >> 8) & 0xff);
		FWRITE_U8((value >> 16) & 0xff);
	}
	else{
		U32 value = 0x3 | (i << 2);
		FWRITE_U8(value & 0xff);
		FWRITE_U8((value >> 8) & 0xff);
		FWRITE_U8((value >> 16) & 0xff);
		FWRITE_U8((value >> 24) & 0xff);
		FWRITE_U8((i >> 30) & 0xff);
	}
}

//static S32 getBeaconIndex(Array* array, Beacon* beacon){
//	S32 i;
//	
//	for(i = 0; i < array->size; i++){
//		if(beacon == array->storage[i]){
//			return i;
//		}
//	}
//	
//	assert(0 && "Uh oh, some majorly messed up stuff happened finding a beacon index.  Please tell Martin.");
//	
//	return 0;
//}

static void writeBeacon(Beacon* b){
	S32 i;
	
	FWRITE(b->pos);
	
	FWRITE(b->ceilingDistance);
	FWRITE(b->floorDistance);
	
	// Write ground connections.
	
	FWRITE_U32(b->gbConns.size);
	
	for(i = 0; i < b->gbConns.size; i++){
		BeaconConnection* conn = b->gbConns.storage[i];
		
		// Write the beacon index.
		
		FWRITE_U32(conn->destBeacon->globalIndex);
	}

	// Write raised connections.
	
	FWRITE_U32(b->rbConns.size);
	
	for(i = 0; i < b->rbConns.size; i++){
		BeaconConnection* conn = b->rbConns.storage[i];
		
		// Write the beacon index.

		FWRITE_U32(conn->destBeacon->globalIndex);
		
		// Write the min and max height of the connection.
		
		FWRITE(conn->minHeight);
		FWRITE(conn->maxHeight);
	}
}

#if 0
static void writeBlockConnection(BeaconBlockConnection* conn, S32 raised){
	//FWRITE_CHECK("blockconn");

	FWRITE_U32(getGridBlockIndex(conn->destBlock->parentBlock));

	FWRITE_U32(getSubBlockIndex(conn->destBlock));

	if(raised){
		FWRITE(conn->minHeight);
		FWRITE(conn->maxHeight);
	}
}
#endif

static void writeSubBlockStep1(BeaconBlock* subBlock){
	Vec3 pos;
	// Write just the position and beacon count so that the beacon array can be created.
	
	//FWRITE_CHECK("subblock1");

	copyVec3(subBlock->pos, pos);
	FWRITE(pos);
	FWRITE_U32(subBlock->beaconArray.size);
}

static void writeSubBlockStep2(BeaconBlock* subBlock){
	S32 i;

	//FWRITE_CHECK("subblock2");
	
	// Write the connection information.
	
	if(1){
		FWRITE_U32(0);
		FWRITE_U32(0);
	}else{
		#if 0
		FWRITE_U32(subBlock->gbbConns.size);

		for(i = 0; i < subBlock->gbbConns.size; i++){
			writeBlockConnection(subBlock->gbbConns.storage[i], 0);
		}

		FWRITE_U32(subBlock->rbbConns.size);

		for(i = 0; i < subBlock->rbbConns.size; i++){
			writeBlockConnection(subBlock->rbbConns.storage[i], 1);
		}
		#endif
	}
		
	// Write the beacons.
	
	for(i = 0; i < subBlock->beaconArray.size; i++){
		writeBeacon(subBlock->beaconArray.storage[i]);
	}
}

static void writeGridBlockStep1(BeaconBlock* gridBlock){
	Vec3 pos;
	// Write just the position, beacon count, and sub-block count so the arrays can be created.
	//FWRITE_CHECK("gridblock1");
	
	copyVec3(gridBlock->pos, pos);
	FWRITE(pos);
	FWRITE_U32(gridBlock->beaconArray.size);
	FWRITE_U32(gridBlock->subBlockArray.size);
}

static void writeGridBlockStep2(BeaconBlock* gridBlock){
	S32 i;
	
	//FWRITE_CHECK("gridblock2");
	
	for(i = 0; i < gridBlock->subBlockArray.size; i++){
		BeaconBlock* subBlock = gridBlock->subBlockArray.storage[i];
		
		writeSubBlockStep1(subBlock);
	}

	for(i = 0; i < gridBlock->subBlockArray.size; i++){
		BeaconBlock* subBlock = gridBlock->subBlockArray.storage[i];
		
		writeSubBlockStep2(subBlock);
	}
}

static int beaconForcePut(const char *fileName)
{
	char cmdline[2048];
	char absoluteFile[MAX_PATH];

	fileLocateWrite(fileName, absoluteFile);
	sprintf(cmdline, "gimme -nocomments -forceput \"%s\"", absoluteFile);
	system(cmdline);

	return 1;
}

static void beaconWriteInvalidBeaconFile(S32 doCheckoutCheckin)
{
	int i;
	FILE* invalidFile;
	char fileName[MAX_PATH];

	assert(estrLength(&beacon_process.beaconInvalidFileName));
	
	if(!eaSize(&invalidEncounterArray))
		return;

	fileLocateWrite(beacon_process.beaconInvalidFileName, fileName);
	mkdirtree(fileName);
	invalidFile = fileOpen(beacon_process.beaconInvalidFileName, "wb");
	
	if(!invalidFile)
	{
		if(doCheckoutCheckin)
			return;

		chmod(fileName, _S_IREAD|_S_IWRITE);

		invalidFile = fileOpen(beacon_process.beaconInvalidFileName, "wb");
	}

	for(i=0; i<eaSize(&invalidEncounterArray); i++)
	{
		fprintf(invalidFile, "%s\n", invalidEncounterArray[i]);
	}

	fileClose(invalidFile);
}

void beaconWriteDateFile(S32 doCheckoutCheckin){
	char fileName[MAX_PATH];
	FILE* f;
	assert(estrLength(&beacon_process.beaconDateFileName));

	fileLocateWrite(beacon_process.beaconDateFileName, fileName);
	mkdirtree(fileName);
	f = fileOpen(fileName, "w");
	if(!f)
	{
		if(doCheckoutCheckin)
			return;

		chmod(fileName, _S_IREAD | _S_IWRITE);
	}
	fileClose(f);

	ParserWriteTextFile(beacon_process.beaconDateFileName, parse_BeaconMapMetaData, beacon_process.mapMetaData, 0, 0);
}

void beaconReadDateFile(const char *filename)
{
	StructAllocIfNull(parse_BeaconMapMetaData, beacon_process.fileMetaData);
	StructReset(parse_BeaconMapMetaData, beacon_process.fileMetaData);
	ANALYSIS_ASSUME(beacon_process.fileMetaData);

	if(!filename)
	{
		filename = beacon_process.beaconDateFileName;
	}

	if(!ParserReadTextFile(filename, parse_BeaconMapMetaData, beacon_process.fileMetaData, PARSER_NOERRORFSONPARSE))
	{
		FILE* f = fileOpen(filename, "rb");

		if(f)
		{
			S32 version = 0;
			fread(&beacon_process.fileMetaData->metaDataVersion, sizeof(beacon_process.fileMetaData->metaDataVersion), 1, f);

			// Read the file time.
			if(fread(&beacon_process.fileMetaData->patchViewTime, sizeof(beacon_process.fileMetaData->patchViewTime), 1, f)){
				if(beacon_process.fileMetaData->metaDataVersion >= 10)
				{
					fread(&beacon_process.fileMetaData->dataProcessVersion, sizeof(beacon_process.fileMetaData->dataProcessVersion), 1, f);
				}

				if(beacon_process.fileMetaData->metaDataVersion >= 9){
					// Read the CRC.
					fread(&beacon_process.fileMetaData->fullCRC, sizeof(beacon_process.fileMetaData->fullCRC), 1, f);
				}
			}

			fileClose(f);
		}	
	}
}

void writeBeaconFileCallback(BeaconWriterCallback callback, S32 writeCheck){
	S32 curBeacon = 0;
	S32 i;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, false);
	
	beaconWriter.callback = callback;

	beaconWriter.useFileCheck = writeCheck;
	
	// Write a version so as not to be an idiot when the file format turns out to suck or something.
	
	FWRITE(curBeaconFileVersion);
	
	FWRITE(beaconWriter.useFileCheck);
	
	// Write the bad connections flag. (This is now removed, so it's always 0).
	
	FWRITE_U32(0);
	
	// Write the regular beacons.

	FWRITE_CHECK("regular");

	FWRITE_U32(0);
	
	// Write the beacon block size.
	
	FWRITE_CHECK("block size");

	FWRITE(combatBeaconGridBlockSize);
	
	// Write the combat beacons.
	
	FWRITE_U32(combatBeaconArray.size);
	
	// Should be the count of grid blocks.
	
	FWRITE_U32(partition->combatBeaconGridBlockArray.size);
	
	// Re-order the beacons in the combat beacon array.

	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		S32 j;
		
		for(j = 0; j < gridBlock->subBlockArray.size; j++){
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];
			S32 k;
			
			for(k = 0; k < subBlock->beaconArray.size; k++){
				combatBeaconArray.storage[curBeacon++] = subBlock->beaconArray.storage[k];
			}
		}
	}
	
	assert(curBeacon == combatBeaconArray.size);

	// Assign numbers to each beacon for easy reference.  Must be after re-ordering of beacon array.
	
	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* beacon = combatBeaconArray.storage[i];
		
		beacon->globalIndex = i;
	}		

	// Step 1 is to allow the blocks to create all the sub-blocks with no information inside.
	// Step 2 is to fill in the sub-blocks.
	// Step 1 is necessary so step 2 can refer to sub-blocks of other grid blocks which otherwise
	//   would not have been created yet.
	
	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		writeGridBlockStep1(partition->combatBeaconGridBlockArray.storage[i]);
	}
	
	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		writeGridBlockStep2(partition->combatBeaconGridBlockArray.storage[i]);
	}

	// Write the galaxy data.
	
	FWRITE_CHECK("galaxies");
	
	FWRITE_U32(0);
		
	FWRITE_CHECK("finished");
}

static void writeBeaconFileToFile(const void* data, U32 size){
	fwrite(data, size, 1, beaconWriter.fileHandle);
}

void beaconCheckinDateFile(void)
{
	GimmeErrorValue gev = gimmeDLLDoOperation(beacon_process.beaconDateFileName, GIMME_FORCECHECKIN, GIMME_QUIET);
	if (gev != GIMME_NO_ERROR)
	{
		printf("Error checking in .date file! %d\n", gev);
	}
}

static S32 beaconFileFinalize(S32 doCheckoutCheckin, const char* comment) 
{
	// Write the date file
	beaconWriteDateFile(doCheckoutCheckin);

	// Write invalid beacon file 
	beaconWriteInvalidBeaconFile(doCheckoutCheckin);

	if(doCheckoutCheckin)
	{
		char **files = NULL;
		eaPush(&files, beacon_process.beaconFileName);
		eaPush(&files, beacon_process.beaconDateFileName);
		eaPush(&files, beacon_process.beaconInvalidFileName);

		gimmeDLLSetDefaultCheckinComment(comment);
		gimmeDLLDoOperations(files, GIMME_CHECKIN, GIMME_QUIET);
		eaDestroy(&files);
	}

	return 1;
}

void beaconWriteCompressedFile(char* fileName, void* data, U32 byteCount, int doCheckoutCheckin, const char* comment)
{
	char filename[MAX_PATH];
	FILE* f;

	fileLocateWrite(fileName, filename);
	mkdirtree(filename);
	f = fileOpen(filename, "wb");

	if(!f)
	{
		if(doCheckoutCheckin)
			return;

		chmod(filename, _S_IREAD|_S_IWRITE);
		f = fileOpen(filename, "wb");
	}

	fwrite(data, 1, byteCount, f);

	fileClose(f);

	beaconFileFinalize(doCheckoutCheckin, comment);
}

#undef FWRITE
#undef FWRITE_U32
#undef FWRITE_CHECK

#define FREAD_FAIL(x)					{displayFailure(__FILE__, __LINE__, x);goto failed;}
#define CHECK_RESULT(x)					if(!(x))FREAD_FAIL(#x)
#define FREAD_ERROR(x,name,size)		if(!beaconReader.callback((char*)(x), size)){FREAD_FAIL(name);}
#define FREAD(x)						FREAD_ERROR(&x,#x,sizeof(x))
#define FREAD_U32(x)					{CHECK_RESULT(freadCompressedU32(&x));}
#define FREAD_U8(x)						{U8 temp__;FREAD_ERROR(&temp__,#x,sizeof(temp__));x = temp__;}
#define FREAD_CHECK(x)					if(beaconReader.useFileCheck){freadCheck(x,__FILE__,__LINE__);}
#define INIT_CHECK_ARRAY(array,size)	initArray((array),size);CHECK_RESULT((array)->maxSize >= size)

static struct {
	FILE*					fileHandle;
	BeaconReaderCallback	callback;

	S32						fileVersion;
	S32						useFileCheck;

	char*					fileBuffer;
	const char*				fileBufferPos;
	S32						fileBufferSize;
	S32						fileBufferValidRemaining;
	S32						fileCursor;
	
	S32						fileTotalSize;
	S32						fileTotalPos;
	
	S32						failed;
	S32						disablePopupErrors;
} beaconReader;

S32 freadBeaconFile(char* data, S32 size){
	S32 sizeRemaining = size;
	
	if(	!beaconReader.fileBuffer ||
		beaconReader.failed || 
		size > beaconReader.fileTotalSize - beaconReader.fileTotalPos)
	{
		beaconReader.failed = 1;
		return 0;
	}

	while(sizeRemaining > 0){
		S32 readSize;
		
		if(!beaconReader.fileBufferValidRemaining){
			S32	fileRemainingSize = beaconReader.fileTotalSize - beaconReader.fileTotalPos;
			
			readSize = min(beaconReader.fileBufferSize, fileRemainingSize);
			
			if(!fread(beaconReader.fileBuffer, readSize, 1, beaconReader.fileHandle)){
				beaconReader.failed = 1;
				return 0;
			}
			
			assert(beaconReader.fileBufferPos == beaconReader.fileBuffer);
			
			beaconReader.fileBufferValidRemaining = readSize;
		}
		
		readSize = min(sizeRemaining, beaconReader.fileBufferValidRemaining);
		
		memcpy(data, beaconReader.fileBufferPos, readSize);
		beaconReader.fileBufferPos += readSize;
		beaconReader.fileBufferValidRemaining -= readSize;
		beaconReader.fileTotalPos += readSize;
		
		assert(beaconReader.fileTotalPos <= beaconReader.fileTotalSize);
		
		sizeRemaining -= readSize;
		
		if(sizeRemaining > 0 || !beaconReader.fileBufferValidRemaining){
			data += readSize;
			
			beaconReader.fileBufferPos = beaconReader.fileBuffer;
			
			assert(!beaconReader.fileBufferValidRemaining);
		}
	}
	
	return 1;
}

S32 beaconFileGetCurVersion(void){
	return curBeaconFileVersion;
}

S32 beaconGetReadFileVersion(void){
	return beaconReader.fileVersion;
}

void beaconReaderDisablePopupErrors(S32 set){
	beaconReader.disablePopupErrors = set ? 1 : 0;
}

S32 beaconReaderArePopupErrorsDisabled(void){
	return beaconReader.disablePopupErrors;
}

static void displayFailure(const char* fileName, S32 fileLine, const char* placeName){
	char messageBuffer[10000];
	
	if(beaconReaderArePopupErrorsDisabled()){
		return;
	}
	
	sprintf(messageBuffer,
			"  %s[%d], byte 0x%x/0x%x: FAILED: \"%s\"\n",
			fileName,
			fileLine,
			beaconReader.fileTotalPos,
			beaconReader.fileTotalSize,
			placeName);
			
	printf("%s", messageBuffer);
			
	if(	fileIsUsingDevData() &&
		beaconReader.fileTotalSize)
	{
		Errorf(	"Hi!\n"
				"\n"
				"Please ask ADAM to debug this.\n");
	}
	
	filelog_printf("beacon_load_error.log", "%s,%s", "get this name from worldlib somehow", messageBuffer);
}

S32 freadCheck(const char* name, const char* fileName, S32 fileLine){
	char temp[1000];
	
	if(!beaconReader.callback(temp, (U32)strlen(name))){
		sprintf(temp, "CHECK: %s", name);
		FREAD_FAIL(temp);
	}

	CHECK_RESULT(!strncmp(name,temp,strlen(name)));

	#if 0
		printf("  %s[%d]: Check passed: %s\n", fileName, fileLine, name)
	#endif
		
	return 1;

	failed:
	return 0;
}

static S32 freadCompressedU32(U32 *i){
	U8 v0, v1, v2, v3, v4;
	S32 type;
	S32 value;

	if(beaconReader.fileVersion < 8){
		FREAD(*i);
		return 1;
	}

	FREAD_U8(v0);
	
	type = v0 & 3;
	
	value = v0 >> 2;
	
	if(type >= 1){
		FREAD_U8(v1);
		
		value |= v1 << 6;
	}
	
	if(type >= 2){
		FREAD_U8(v2);
	
		value |= v2 << 14;
	}
	
	if(type >= 3){
		FREAD_U8(v3);
		FREAD_U8(v4);
		
		value |= v3 << 22;
		value |= v4 << 30;
	}
	
	*i = value;
	
	return 1;	
	
	failed:
	return 0;
}

static S32 readBlockConnection(BeaconStatePartition *partition, BeaconBlock* subBlock, S32 raised)
{
	BeaconBlockConnection* conn = NULL;
	BeaconBlock *dstBlock = NULL;
	S32 gridBlockIndex;
	S32 subBlockIndex;
	BeaconBlock* gridBlock;
	Array* connArray = raised ? &subBlock->rbbConns : &subBlock->gbbConns;

	if(beaconReader.fileVersion <= 7){
		FREAD_CHECK("blockconn");
	}

	FREAD_U32(gridBlockIndex);
	FREAD_U32(subBlockIndex);
	
	CHECK_RESULT(gridBlockIndex >= 0 && gridBlockIndex < partition->combatBeaconGridBlockArray.size);

	gridBlock = partition->combatBeaconGridBlockArray.storage[gridBlockIndex];
	
	CHECK_RESULT(subBlockIndex >= 0 && subBlockIndex < gridBlock->subBlockArray.size);
	
	dstBlock = gridBlock->subBlockArray.storage[subBlockIndex];
	conn = beaconBlockConnectionCreate(subBlock, dstBlock);
	
	if(raised){
		if(beaconReader.fileVersion<=8)
		{
			F32 minH, maxH;
			FREAD(minH); FREAD(maxH);
			conn->minHeight = minH;
			conn->maxHeight = maxH;
		}
		else if(beaconReader.fileVersion<=10)
		{
			S16 minH;
			S16 maxH;
			FREAD(minH);
			FREAD(maxH);

			conn->minHeight = minH;
			conn->maxHeight = maxH;
		}
		else
		{
			FREAD(conn->minHeight);
			FREAD(conn->maxHeight);
		}
	}
	
	if(conn->destBlock == subBlock){
		// Remove links to myself.  What an idiot I am.
		
		beaconBlockConnectionDestroy(conn);
	}else{
		arrayPushBack(connArray, conn);
	}
	
	return 1;

	failed:
	return 0;
}

static S32 readBeacon(BeaconBlock* subBlock, S32 index, Beacon* beacon){
	S32 i;
	S32 connCount;
	BeaconPartitionData *beaconPartition = beaconGetPartitionData(beacon, 0, true);
	
	beacon->madeGroundConnections = 1;
	beacon->madeRaisedConnections = 1;

	subBlock->beaconArray.storage[index] = beacon;
	
	arrayPushBack(&subBlock->parentBlock->beaconArray, beacon);
	
	beaconPartition->block = subBlock;

	FREAD(beacon->pos);
	
	if(beaconReader.fileVersion == 6)
	{
		FREAD(beacon->proximityRadius);
	}
	else
	{
		FREAD(beacon->ceilingDistance);
		
		if(beaconReader.fileVersion >= 8)
			FREAD(beacon->floorDistance);
	}
	
	// Read ground connections.
	
	FREAD_U32(connCount);
	
	beaconInitArray(&beacon->gbConns, connCount);
	
	for(i = 0; i < connCount; i++)
	{
		S32 dest;
		BeaconConnection* conn = createBeaconConnection();
		
		FREAD_U32(dest);
		
		CHECK_RESULT(dest >= 0 && dest < combatBeaconArray.size && dest != beacon->globalIndex);

		conn->destBeacon = combatBeaconArray.storage[dest];
		
		arrayPushBack(&beacon->gbConns, conn);
	}

	// Read raised connections.
	
	FREAD_U32(connCount);
	
	beaconInitArray(&beacon->rbConns, connCount);
	
	for(i = 0; i < connCount; i++)
	{
		S32 dest;
		BeaconConnection* conn = createBeaconConnection();
		
		FREAD_U32(dest);
		
		CHECK_RESULT(dest >= 0 && dest < combatBeaconArray.size);
		
		conn->destBeacon = combatBeaconArray.storage[dest];

		if(beaconReader.fileVersion<=8)
		{
			// 8 or less implies floats
			F32 minH, maxH;

			FREAD(minH);
			FREAD(maxH);
			conn->minHeight = minH;
			conn->maxHeight = maxH;
		}
		else
		{
			FREAD(conn->minHeight);
			FREAD(conn->maxHeight);
		}
		
		conn->minHeight = max(conn->minHeight, 1);
		conn->maxHeight = max(conn->maxHeight, conn->minHeight);
		
		if(beaconReader.fileVersion <= 8)
		{
			conn->maxHeight -= 5;
			
			if(conn->maxHeight < conn->minHeight)
				conn->maxHeight = conn->minHeight;
		}

		conn->raised = true;
		arrayPushBack(&beacon->rbConns, conn);
	}

	return 1;

	failed:
	return 0;
}

static void beaconReaderStop(void){
	SAFE_FREE(beaconReader.fileBuffer);
	fileClose(beaconReader.fileHandle);
	beaconReader.fileHandle = NULL;
}

S32 readBeaconFileCallback(BeaconReaderCallback callback){
	S32 blockCount;
	S32 beaconCount;
	S32 curBeacon = 0;
	S32 gridBlockBeaconCount;
	S32 subBlockBeaconCount;
	S32 i;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	
	beaconClearAllBlockData(partition);

	beaconClearBeaconData();
	
	beaconReader.callback = callback;

	// Read the file version.

	FREAD(beaconReader.fileVersion);
	
	FREAD(beaconReader.useFileCheck);
	
	// Check that the version is within the bounds of legal version numbers.

	CHECK_RESULT(	beaconReader.fileVersion >= oldestAllowableBeaconFileVersion &&
					beaconReader.fileVersion <= curBeaconFileVersion);
	
	// Read the bad connections flag. 

	FREAD_U32(i);
	
	// Read the regular beacons.
	
	FREAD_CHECK("regular");

	FREAD_U32(beaconCount);
	
	verbose_printf("  Regular beacon count: %d\n", beaconCount);

	// Get the block size, beacon count and set the combat beacon array.

	FREAD_CHECK("block size");

	FREAD(combatBeaconGridBlockSize);
	
	FREAD_U32(beaconCount);

	verbose_printf("  Combat beacon count: %d\n", beaconCount);
	verbose_printf("  Block size: %1.f\n", combatBeaconGridBlockSize);
	
	assert(!combatBeaconArray.size && !combatBeaconArray.storage);

	INIT_CHECK_ARRAY(&combatBeaconArray, beaconCount);

	for(i = 0; i < beaconCount; i++){
		Beacon* beacon = createBeacon();
		
		beacon->globalIndex = i;
		
		arrayPushBack(&combatBeaconArray, beacon);
	}	
	
	// Get the grid block count and set the sizes of the hash table and array.
	
	FREAD_U32(blockCount);
	
	verbose_printf("  Grid block count: %d\n", blockCount);
	
	if(partition->combatBeaconGridBlockTable)
		stashTableDestroy(partition->combatBeaconGridBlockTable);
	
	partition->combatBeaconGridBlockTable = stashTableCreateInt((blockCount * 4) / 3);
	
	destroyArrayPartialEx(&partition->combatBeaconGridBlockArray, beaconGridBlockDestroy);
	INIT_CHECK_ARRAY(&partition->combatBeaconGridBlockArray, blockCount);
	
	// Read the grid blocks, step 1.
	gridBlockBeaconCount = 0;
	
	for(i = 0; i < blockCount; i++ )
	{
		BeaconBlock* gridBlock = beaconGridBlockCreate(0);
		S32 subBlockCount;
		S32 j;
		Vec3 blockPos;
		
		gridBlock->isGridBlock = 1;
		gridBlock->madeConnections = 1;
		
		if(beaconReader.fileVersion < 8)
			FREAD_CHECK("gridblock1");
			
		FREAD(blockPos);
		copyVec3(blockPos, gridBlock->pos);
		FREAD_U32(beaconCount);
		FREAD_U32(subBlockCount);
		
		gridBlockBeaconCount += beaconCount;
		
		CHECK_RESULT(gridBlockBeaconCount <= combatBeaconArray.size);

		INIT_CHECK_ARRAY(&gridBlock->beaconArray, beaconCount);
		
		beaconBlockInitArray(&gridBlock->subBlockArray, subBlockCount);
		
		CHECK_RESULT(stashIntAddPointer(partition->combatBeaconGridBlockTable,
										beaconMakeGridBlockHashValue(vecParamsXYZ(gridBlock->pos)),
										gridBlock, false));

		arrayPushBack(&partition->combatBeaconGridBlockArray, gridBlock);
		
		for(j = 0; j < subBlockCount; j++)
		{
			BeaconBlock* subBlock = beaconSubBlockCreate(0);

			subBlock->madeConnections = 1;
			subBlock->parentBlock = gridBlock;
			
			// Put the sub-block into the parent block array.
			
			gridBlock->subBlockArray.storage[gridBlock->subBlockArray.size++] = subBlock;
		}
	}
	
	// Read the grid blocks, step 2.
	subBlockBeaconCount = 0;

	for(i = 0; i < blockCount; i++)
	{
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		S32 j;
		
		if(((i+1)*100) / blockCount != (i*100) / blockCount)
			verbose_printf("  Loading blocks: %d%%\r", ((i+1)*100) / blockCount);

		if(beaconReader.fileVersion < 8)
			FREAD_CHECK("gridblock2");
			
		// Read sub-blocks, step 1.
		for(j = 0; j < gridBlock->subBlockArray.size; j++)
		{
			Vec3 subBlockPos;
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];
			
			if(beaconReader.fileVersion < 8)
				FREAD_CHECK("subblock1");
			
			FREAD(subBlockPos);
			copyVec3(subBlockPos, subBlock->pos);
			FREAD_U32(beaconCount);
			
			subBlockBeaconCount += beaconCount;
			
			beaconBlockInitArray(&subBlock->beaconArray, beaconCount);
			
			subBlock->beaconArray.size = beaconCount;
		}

		// Read sub-blocks, step 2.
		
		for(j = 0; j < gridBlock->subBlockArray.size; j++)
		{
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];
			S32 k;
			S32 connCount;
			
			if(beaconReader.fileVersion < 8)
				FREAD_CHECK("subblock2");
			
			// Read the ground connection information.
			
			FREAD_U32(connCount);
			
			beaconBlockInitArray(&subBlock->gbbConns, connCount);

			for(k = 0; k < connCount; k++)
				CHECK_RESULT(readBlockConnection(partition, subBlock, 0));

			// Read the raised connection information.
			
			FREAD_U32(connCount);
			
			beaconBlockInitArray(&subBlock->rbbConns, connCount);

			for(k = 0; k < connCount; k++)
				CHECK_RESULT(readBlockConnection(partition, subBlock, 1));
			
			// Read the beacons.
			
			for(k = 0; k < subBlock->beaconArray.size; k++)
			{
				Beacon *bcn;
				CHECK_RESULT(readBeacon(subBlock, k, combatBeaconArray.storage[curBeacon++]));
				bcn = combatBeaconArray.storage[curBeacon-1];
				//printf("Beacon %d: %f %f %f\n", k, vecParamsXYZ(bcn->pos));
			}
		}
	}

	if(beaconReader.fileVersion >= 8)
	{
		S32 galaxyCount;
		S32 j;
		
		FREAD_CHECK("galaxies");
		
		FREAD_U32(galaxyCount);
		
		clearArrayEx(&partition->combatBeaconGalaxyArray[0], beaconGalaxyDestroy);
		beaconBlockInitArray(&partition->combatBeaconGalaxyArray[0], galaxyCount);
		
		for(i = 0; i < galaxyCount; i++)
		{
			BeaconBlock* galaxy = beaconGalaxyCreate(0, 0);
			galaxy->isGalaxy = 1;
			partition->combatBeaconGalaxyArray[0].storage[partition->combatBeaconGalaxyArray[0].size++] = galaxy;
		}
		
		assert(partition->combatBeaconGalaxyArray[0].size == galaxyCount);
		
		for(i = 0; i < galaxyCount; i++)
		{
			BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];
			S32 count;
			
			// Read ground connections.
			
			FREAD_U32(count);
			
			beaconBlockInitArray(&galaxy->gbbConns, count);
			
			for(j = 0; j < count; j++)
			{
				BeaconBlockConnection* conn = NULL;
				BeaconBlock *destBlock = NULL;
				S32 index;
				
				FREAD_U32(index);
				
				CHECK_RESULT(index >= 0 && index < partition->combatBeaconGalaxyArray[0].size && index != i);
				
				destBlock = partition->combatBeaconGalaxyArray[0].storage[index];
				
				assert(destBlock != galaxy);
				conn = beaconBlockConnectionCreate(galaxy, destBlock);
				
				arrayPushBack(&galaxy->gbbConns, conn);
			}

			// Read ground connections.
			
			FREAD_U32(count);
			
			beaconBlockInitArray(&galaxy->rbbConns, count);
			
			for(j = 0; j < count; j++)
			{
				BeaconBlockConnection* conn = NULL;
				BeaconBlock *destBlock = NULL;
				S32 index;
				
				FREAD_U32(index);
				FREAD(conn->minHeight);
				FREAD(conn->maxHeight);
				
				CHECK_RESULT(index >= 0 && index < partition->combatBeaconGalaxyArray[0].size && index != i);
				
				destBlock = partition->combatBeaconGalaxyArray[0].storage[index];

				assert(destBlock != galaxy);
				conn = beaconBlockConnectionCreate(galaxy, destBlock);
				conn->raised = true;

				arrayPushBack(&galaxy->rbbConns, conn);
			}
			
			// Read the sub-block IDs.
			
			FREAD_U32(count);
			
			beaconBlockInitArray(&galaxy->subBlockArray, count);
			
			CHECK_RESULT(galaxy->subBlockArray.maxSize >= count);
			
			for(j = 0; j < count; j++)
			{
				BeaconBlock* block;
				S32 id;
				
				FREAD_U32(id);
				
				CHECK_RESULT(id >= 0 && id < partition->combatBeaconGridBlockArray.size);
				
				block = partition->combatBeaconGridBlockArray.storage[id];
				
				FREAD_U32(id);
				
				CHECK_RESULT(id >= 0 && id < block->subBlockArray.size);
				
				block = block->subBlockArray.storage[id];
				
				block->galaxy = galaxy;
				
				arrayPushBack(&galaxy->subBlockArray, block);
			}
		}
	}
	else
	{
		destroyArrayPartialEx(&partition->combatBeaconGalaxyArray[0], beaconGalaxyDestroy);
	}
	
	FREAD_CHECK("finished");
	
	return 1;
	
	failed:
	
	return 0;
}

S32 readBeaconFileFromFile(void* data, U32 size){
	return freadBeaconFile(data, size);
}

static S32 beaconReaderCallbackReadMemoryFile(void* data, U32 size){
	if((S32)size > beaconReader.fileBufferSize - (beaconReader.fileBufferPos-beaconReader.fileBuffer)){
		return 0;
	}

	devassert(!beaconReader.fileHandle);
	memcpy(	data,
			beaconReader.fileBufferPos,
			size);

	beaconReader.fileBufferPos += size;

	return 1;
}

void beaconGetBeaconFileName(ZoneMapInfo *zmi, char* outName, S32 outName_size, S32 version){
	char mapName[MAX_PATH];
	char preMaps[MAX_PATH];
	char* maps;
	char old_char;

	strcpy(mapName, zmapInfoGetFilename(zmi));
	if(version<10)
	{
		char fileName[MAX_PATH];
		strstriReplace(mapName, ".zone", ".worldgrid");
		forwardSlashes(mapName);
		strcpy(fileName, mapName);
		maps = getFileName(fileName);
		getDirectoryName(mapName);
		getDirectoryName(mapName);
		strcatf(mapName, "/%s", maps);
	}

	maps = strstri(mapName, "maps");

	if(!maps){
		maps = mapName;
	}

	old_char = *maps;
	*maps = 0;

	sprintf(preMaps, "%s", mapName);

	*maps = old_char;

	sprintf_s(SAFESTR2(outName), "%sserver/%s.v%d.bcn", preMaps, maps, version);
}

void beaconFileMakeFilename(ZoneMapInfo* zmi, char **estr, const char *suffix, int version)
{
	char tempFileName[1000];
	beaconGetBeaconFileName(zmi, SAFESTR(tempFileName), version);

	estrCopy2(estr, tempFileName);
	if(suffix)
		estrAppend2(estr, suffix);
}

void beaconFileMakeFilenames(ZoneMapInfo *zmi, int version)
{
	if(!zmi && !worldGetActiveMap())
		return;

	beaconFileMakeFilename(zmi, &beacon_process.beaconFileName, "", version);
	beaconFileMakeFilename(zmi, &beacon_process.beaconDateFileName, ".date", version);
	beaconFileMakeFilename(zmi, &beacon_process.beaconInvalidFileName, ".invalid", version);

	// HACK: UGC publish process changed to put files in data/maps/mission/blah instead of data/maps/blah
	// So we'll check to see if the file exists in the pre-moved location
	if(!fileExists(beacon_process.beaconFileName))
	{
		if(strStartsWith(beacon_process.beaconFileName, "ns/"))
		{
			static char* estr = NULL;
			estrCopy(&estr, &beacon_process.beaconFileName);
			if(!estrReplaceOccurrences_CaseInsensitive(&estr, "/Mission", ""))
				return;

			if(fileExists(estr))
			{
				estrReplaceOccurrences_CaseInsensitive(&beacon_process.beaconFileName, "/Mission", "");
				estrReplaceOccurrences_CaseInsensitive(&beacon_process.beaconDateFileName, "/Mission", "");
				estrReplaceOccurrences_CaseInsensitive(&beacon_process.beaconInvalidFileName, "/Mission", "");
			}
		}
	}
}

S32 readBeaconFile(ZoneMapInfo *zmi, int version){
	if(!zmi && !worldGetActiveMap())
	{
		printf("  Beacon file can't load: no map\n");
		return 0;
	}

	beaconFileMakeFilenames(zmi, version);

	if(!fileExists(beacon_process.beaconFileName)){
		printf("  Beacon file doesn't exist: %s\n", beacon_process.beaconFileName);
		return 0;
	}
	
	verbose_printf("  Reading beacon file: %s\n", beacon_process.beaconFileName);

	beaconReadDateFile(NULL);
	
	verbose_printf("  File size: %d bytes.\n", beaconReader.fileTotalSize);

	ZeroStruct(&beaconReader);
	beaconReader.failed						= 0;
	beaconReader.fileTotalPos				= 0;

	if(version<=10)
	{
		beaconReader.fileHandle					= fileOpen(beacon_process.beaconFileName, "rb");

		beaconReader.fileTotalSize				= fileSize(beacon_process.beaconFileName);
		beaconReader.fileBufferSize 			= 1024 * 1024;
		beaconReader.fileBuffer					= malloc(beaconReader.fileBufferSize);
		beaconReader.fileBufferPos				= beaconReader.fileBuffer;
		beaconReader.fileBufferValidRemaining	= 0;
		beaconReader.fileVersion				= version;

		CHECK_RESULT(readBeaconFileCallback(readBeaconFileFromFile));
	}
	else
	{
		S32 ziplength = 0, unziplength = 0, ret;
		char *zipbuf = fileAlloc(beacon_process.beaconFileName, &ziplength);
		char *unzipbuf = malloc(beacon_process.fileMetaData->unzippedSize);

		unziplength = beacon_process.fileMetaData->unzippedSize;
		ret = unzipDataEx(unzipbuf, &unziplength, zipbuf, ziplength, 1);
		beacon_process.fileMetaData->zippedSize = ziplength;
		beacon_process.fileMetaData->unzippedSize = unziplength;

		free(zipbuf);

		CHECK_RESULT(!ret);

		beaconReader.fileBufferSize				= beacon_process.fileMetaData->unzippedSize;
		beaconReader.fileBuffer					= unzipbuf;
		beaconReader.fileBufferPos				= beaconReader.fileBuffer;
		beaconReader.fileVersion				= version;

		CHECK_RESULT(readBeaconFileCallback(beaconReaderCallbackReadMemoryFile));
	}

	beaconReaderStop();
	
	verbose_printf("  Successfully read beacon file.\n");

	return 1;

	failed:
	
	beaconReaderStop();

	printf("  ERROR: Failed to read beacon file: %s\n\n\n", beacon_process.beaconFileName);

	return 0;	
}

#undef CHECK_RESULT
#undef FREAD
#undef FREAD_CHECK

static S32 loadOnlyTrafficBeacons = 0;

F32 beaconSnapPosToGround(int iPartitionIdx, Vec3 posInOut){
	F32 floorDistance = beaconGetJitteredPointFloorDistance(iPartitionIdx, posInOut);
	
	vecY(posInOut) += 2.0f - floorDistance;
	
	return floorDistance;
}

Beacon* addCombatBeacon(const Vec3 beaconPos,
						S32 silent,
						S32 snapToGround,
						S32 destroyIfTooHigh,
						U32 isSpecial)
{
	BeaconBlock *gridBlock;
	Beacon *beacon;
	Vec3 pos;
	S32 i;
	S32 failure;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	
	beacon = createBeacon();
	
	//beacon->type = BEACONTYPE_COMBAT;
	
	copyVec3(beaconPos, beacon->pos);

	if(snapToGround){
		F32 floorDistance = beaconSnapPosToGround(partition->id, beacon->pos);

		if(floorDistance > 4000.0f){
			if(destroyIfTooHigh){
				destroyCombatBeacon(beacon);
				beacon = NULL;
				failure = 0;
			}
		}
	}

	if(beacon)
	{
		beacon->proximityRadius = 0;
		beacon->floorDistance = 0;
		beacon->ceilingDistance = 0;

		copyVec3(beacon->pos, pos);

		beaconMakeGridBlockCoords(pos);

		gridBlock = beaconGetGridBlockByCoords(partition, vecParamsXYZ(pos), 1);

		for(i = 0; i < gridBlock->beaconArray.size; i++){
			Beacon* otherBeacon = gridBlock->beaconArray.storage[i];

			if(	distance3SquaredXZ(otherBeacon->pos, beacon->pos) < SQR(1.0f) &&
				distanceY(otherBeacon->pos, beacon->pos) < 5.0f)
			{
				destroyCombatBeacon(beacon);
				beacon = NULL;
				failure = 1;
				break;
			}
		}

		if(beacon)
		{
			BeaconPartitionData *beaconPartition = beaconGetPartitionData(beacon, 0, true);
			arrayPushBack(&gridBlock->beaconArray, beacon);

			beaconPartition->block = gridBlock;

			arrayPushBack(&combatBeaconArray, beacon);
		}
	}

	if(!beacon)
	{
		if(beaconServerDebugCombatPlace())
		{
			beaconServerSendDebugPoint(BDO_COMBAT, beaconPos, failure ? 0xFFFFFF00 : 0xFF00FFFF);
		}
	}

	if (beacon)
	{
		beacon->isSpecial = isSpecial;
	}

	return beacon;
}

void beaconRemoveOldFiles(void){
	S32 i;
	
	printf("\n\n*** Removing Old Beacon Files ***\n");
	
	for(i = 0; i < curBeaconFileVersion; i++){
		char fileName[1000];
		
		beaconGetBeaconFileName(NULL, SAFESTR(fileName), i);
		
		if(fileExists(fileName)){
			printf("   REMOVING: %s\n", fileName);
			
			gimmeDLLDoOperation(fileName, GIMME_DELETE, 1);
		}
		
		strcat(fileName, ".date");
		
		if(fileExists(fileName)){
			printf("   REMOVING: %s\n", fileName);
			
			gimmeDLLDoOperation(fileName, GIMME_DELETE, 1);
		}
	}
	printf("*** Done Removing Old Beacon Files ***\n\n");
}

void beaconReadInvalidSpawnFile(void)
{
	char beaconFileName[MAX_PATH];
	int filelen;
	char *filebuffer;
	char *line, *newline;
	FILE* invalidfile;

	beaconGetBeaconFileName(NULL, SAFESTR(beaconFileName), curBeaconFileVersion);
	strcat(beaconFileName, ".invalid");

	invalidfile = fileOpen(beaconFileName, "~r");

	if(!invalidfile)
	{
		return;
	}

	filelen = fileGetSize(invalidfile);
	filebuffer = malloc((filelen+1)*sizeof(char));

	filelen = (int)fread(filebuffer, sizeof(char), filelen, invalidfile);
	if(!filelen)
	{
		free( filebuffer );
		return;
	}

	filebuffer[filelen] = '\0';

	fileClose(invalidfile);

	line = filebuffer;
	newline = strchr(line, '\n');
	
	eaClearEx(&invalidEncounterArray, NULL);
	while(newline)
	{
		newline[0] = '\0';
		eaPush(&invalidEncounterArray, strdup(line));

		line = newline+1;

		newline = strchr(line, '\n');
	}

	free(filebuffer);
}

static S32 isFileTimeNewer(time_t older, const char* checkFileName){
	time_t newer = fileLastChanged(checkFileName);
	time_t t;
	
	if (older <= 0 || newer <= 0)
		return 1;

   	t = newer - older;
   	
   	if(!beacon_process.latestDataFileName || !beacon_process.latestDataFileName[0] || newer - beacon_process.latestDataFileTime > 0){
   		beacon_process.latestDataFileTime = newer;
  		estrCopy2(&beacon_process.latestDataFileName, checkFileName);
   	}
   	
	return t > 0;
}

static S32 printedFirstLine;

static void printFirstLine(void){
	if(!printedFirstLine){
		printedFirstLine = 1;
		consoleSetColor(COLOR_GREEN, 0);
		printf("  +---------------------------------------------------------------------------\n");
	}
}

static const char* getDateStringBase2000(time_t t){
	static char** tempBuffers;
	static S32 curBufferIndex;

	struct tm theTime;
	char timeString[100];
	char** curBuffer;

	if(!tempBuffers){
		tempBuffers = callocStructs(char*, 10);
	}

	curBufferIndex = (curBufferIndex + 1) % 10;

	curBuffer = tempBuffers + curBufferIndex;

	localtime_s(&theTime, &t);
	theTime.tm_year += 30; // Time is seconds since 2000, not 1970, so need 30 years
	asctime_s(SAFESTR(timeString), &theTime);

	estrCopy2(curBuffer, timeString);

	(*curBuffer)[strlen(*curBuffer) - 1] = '\0';

	return *curBuffer;
}

static const char* getDateString(time_t t){
	static char** tempBuffers;
	static S32 curBufferIndex;
	
	struct tm theTime;
	char timeString[100];
	char** curBuffer;

	if(!tempBuffers){
		tempBuffers = callocStructs(char*, 10);
	}
	
	curBufferIndex = (curBufferIndex + 1) % 10;
	
	curBuffer = tempBuffers + curBufferIndex;

	localtime_s(&theTime, &t);
	asctime_s(SAFESTR(timeString), &theTime);
	
	estrCopy2(curBuffer, timeString);
	
	(*curBuffer)[strlen(*curBuffer) - 1] = '\0';
	
	return *curBuffer;
}

static void undoCheckout(char* fileName){
	if(!fileName || !fileName[0]){
		return;
	}
		
	printf("Undoing checkout: %s...", fileName);

	gimmeDLLDoOperation(fileName, GIMME_UNDO_CHECKOUT, 1);
	
	printf(" DONE!\n");
}

void beaconProcessUndoCheckouts(void){
	if(beacon_process.bcn_checkedout)
	{
		undoCheckout(beacon_process.beaconFileName);
		estrDestroy(&beacon_process.beaconFileName);

		beacon_process.bcn_checkedout = 0;
	}

	if(beacon_process.bcn_date_checkedout)
	{
		undoCheckout(beacon_process.beaconDateFileName);
		estrDestroy(&beacon_process.beaconDateFileName);

		beacon_process.bcn_date_checkedout = 0;
	}

	if(beacon_process.bcn_invalid_checkedout)
	{
		undoCheckout(beacon_process.beaconInvalidFileName);
		estrDestroy(&beacon_process.beaconInvalidFileName);

		beacon_process.bcn_invalid_checkedout = 0;
	}
}

static BOOL consoleCtrlHandler(DWORD fdwCtrlType){ 
#if !PLATFORM_CONSOLE
	switch (fdwCtrlType){ 
		case CTRL_CLOSE_EVENT: 
		case CTRL_LOGOFF_EVENT: 
		case CTRL_SHUTDOWN_EVENT: 
		case CTRL_BREAK_EVENT: 
		case CTRL_C_EVENT: 
			consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
			printf("\n\n\nBEACON PROCESSING CANCELED!!!\n");
			consoleSetDefaultColor();

			beaconProcessUndoCheckouts();
			
			return FALSE; 

		// Pass other signals to the next handler.

		default: 
			return FALSE; 
	} 
#else 
	return FALSE;
#endif
} 

void beaconProcessSetConsoleCtrlHandler(S32 on){
#if !PLATFORM_CONSOLE
	static S32 curState = 0;
	
	on = on ? 1 : 0;

	if(curState != on){
		SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleCtrlHandler, on);

		curState = on;
	}
#endif
}

static S32 beaconEnsureFileExists(char* fileName, S32* isNew, char **errOut){
	S32 gimme_ret;

	forwardSlashes(fileName);

	printf("Determining file exists: %s\n", fileName);
		
	gimme_ret = gimmeDLLDoOperation(fileName, GIMME_GLV, 1);
	
	if(	gimme_ret == GIMME_ERROR_NOT_IN_DB ||
		gimme_ret == GIMME_ERROR_ALREADY_DELETED || 
		!fileExists(fileName))
	{
		FILE* f;
		
		makeDirectoriesForFile(fileName);

		printf("\n  WARNING: File not found (%s) in gimme database, trying to create it...\n", fileName);

		if(!fileExists(fileName))
		{
			f = fileOpen(fileName, "wb");

			if(!f){
				estrPrintf(errOut, "CANCELED: Can't create temp file: %s\n", fileName);
				return 0;
			}
			
			fileClose(f);
		}

		{
			//char cmdline[2048];
			//gimme_no_comments = 1; // Do not ask for checkin comments, do not pop up link break warnings
			//sprintf(cmdline, "-nocomments -checkin \"%s\"", fullPath);
			gimmeDLLDoCommand("-nocomments");
			if(gimmeDLLDoOperation(fileName, GIMME_CHECKIN, GIMME_QUIET)){
				estrPrintf(errOut, "CANCELED: Can't checkin temp file: %s\n", fileName);
				return 0;
			}
		}
		
		if(isNew){
			*isNew = 1;
		}
	}
	
	if(gimme_ret){
		estrPrintf(errOut, "\nCANCELED: Can't get latest(%d): %s\n", gimme_ret, fileName);
		return 0;
	}
	
	printf("    SUCCESS!\n");
	
	return 1;
}

void beaconProcessSetTitle(F32 progress, const char* sectionName){
#if !PLATFORM_CONSOLE
	static char last_section[100] = "None";
	
	char buffer[2000];
	
	if(!beacon_process.titleMapName){
		return;
	}
	
	if(sectionName){
		STR_COMBINE_BEGIN(last_section);
		STR_COMBINE_CAT(sectionName);
		STR_COMBINE_CAT(" ");
		STR_COMBINE_END(last_section);
	}
	
	STR_COMBINE_BEGIN(buffer);
	STR_COMBINE_CAT(last_section);
	STR_COMBINE_CAT_D((S32)progress);
	STR_COMBINE_CAT(".");
	STR_COMBINE_CAT_D(((S32)(progress * 10) % 10));
	STR_COMBINE_CAT("% - ");
	STR_COMBINE_CAT_D(_getpid());
	STR_COMBINE_CAT(" : ");
	STR_COMBINE_CAT(beacon_process.titleMapName);
	STR_COMBINE_END(buffer);
	
	setConsoleTitle(buffer);
#endif
}

typedef struct CRCTriangle {
	Vec3			normal;
	Vec3			vert[3];
	S32				noGroundConnections;
} CRCTriangle;

typedef struct CRCBasicBeacon {
	Vec3			pos;
	S32				validBeaconLevel;
} CRCBasicBeacon;

typedef struct CRCTrafficBeacon {
	Mat4			mat;
	S32				killer;
	S32				group;
	S32				typeNumber;
} CRCTrafficBeacon;

typedef struct CRCModelInfo {
	Model*			model;
	S32				count;
} CRCModelInfo;

static struct {
	StashTable				htModels;
	struct {
		CRCModelInfo*		models;
		S32					count;
		S32					maxCount;
	} models;
	
	U32						totalCRC;
	U8*						buffer;
	U32						cur;
	U32						size;
	U32*					tri_count;
	
	struct {
		CRCTriangle*		buffer;
		U32					count;
		U32					maxCount;
	} tris;
	
	struct {
		CRCBasicBeacon*		buffer;
		U32					count;
		U32					maxCount;
	} basicBeacons;
	
	struct {
		CRCBasicBeacon*		buffer;
		U32					count;
		U32					maxCount;
		S32					maxValidLevel;
	} legalBeacons;

	struct {
		CRCTrafficBeacon*	buffer;
		U32					count;
		U32					maxCount;
	} trafficBeacons;
	
	S32						no_coll_count;
} crc_info;

#define GET_STRUCTS(name, structCount)							\
	dynArrayAddStructs(	crc_info.name.buffer,					\
						crc_info.name.count,					\
						crc_info.name.maxCount,				\
						structCount),							\
	ZeroStruct(crc_info.name.buffer + crc_info.name.count)

//static CRCTriangle* getTriMemory(U32 count){
//	return GET_STRUCTS(tris, count);
//}

static CRCBasicBeacon* getBasicBeacon(void){
	return GET_STRUCTS(basicBeacons, 1);
}

static CRCBasicBeacon* getLegalBeacon(void){
	return GET_STRUCTS(legalBeacons, 1);
}

static CRCTrafficBeacon* getTrafficBeacon(void){
	return GET_STRUCTS(trafficBeacons, 1);
}

static F32 approxF32(F32 value){
	return value - fmod(value, 1.0 / 1024);
}

#undef GET_STRUCTS

static U32 freshCRC(void* dataParam, U32 length){
	cryptAdler32Init();
	
	return cryptAdler32((U8*)dataParam, length);
}

static void flushCRC(void){
	U32 crc = freshCRC(crc_info.buffer, crc_info.cur);
	
	crc_info.totalCRC += crc;
	
	//printf("\nadding %5d bytes: 0x%8.8x ==> 0x%8.8x\n", crc_info.cur, crc, crc_info.totalCRC);

	crc_info.cur = 0;
}

static void crcData(void* dataParam, U32 length){
	U8* data = dataParam;
	
	while(length){
		U32 addLen = min(length, crc_info.size - crc_info.cur);
		
		memcpy(crc_info.buffer + crc_info.cur, data, addLen);
		
		length -= addLen;
		crc_info.cur += addLen;
		
		data += addLen;
		
		if(crc_info.cur == crc_info.size){
			flushCRC();
		}
	}
}

static void crcS32(S32 value){
	crcData((U8*)&value, sizeof(value));
}

static void crcApproxF32(F32 value){
	value = approxF32(value);
	
	crcData((U8*)&value, sizeof(value));
}

static void crcVec3(const Vec3 vec){
	crcApproxF32(vec[0]);
	crcApproxF32(vec[1]);
	crcApproxF32(vec[2]);
}

static copyApproxVec3(const Vec3 in, Vec3 out){
	out[0] = approxF32(in[0]);
	out[1] = approxF32(in[1]);
	out[2] = approxF32(in[2]);
}

static copyApproxMat4(const Mat4 in, Mat4 out){
	copyApproxVec3(in[0], out[0]);
	copyApproxVec3(in[1], out[1]);
	copyApproxVec3(in[2], out[2]);
	copyApproxVec3(in[3], out[3]);
}

//static S32 __cdecl compareTriangles(const CRCTriangle* t1, const CRCTriangle* t2){
//	S32 i;
//	
//	for(i = 0; i < 3; i++){
//		S32 compare = compareVec3(t1->vert[i], t2->vert[i]);
//		
//		if(compare){
//			return compare;
//		}
//	}
//	
//	return compareVec3(t1->normal, t2->normal);
//}

static S32 getIntProperty(GroupDef* def, const char* propName){
	const char* propValue = groupDefFindProperty(def, propName);
	
	return propValue ? atoi(propValue) : 0;
}

static S32 __cdecl compareBasicBeacons(const CRCBasicBeacon* b1, const CRCBasicBeacon* b2){
	return cmpVec3XYZ(b1->pos, b2->pos);
}

static S32 compareInt(S32 i1, S32 i2){
	if(i1 < i2)
		return -1;
	else if(i1 > i2)
		return 1;
	else
		return 0;
}

static S32 __cdecl compareTrafficBeacons(const CRCTrafficBeacon* b1, const CRCTrafficBeacon* b2){
	S32 i;
	S32 compare;
	
	#define COMPARE(x){		\
		compare = x;		\
		if(compare){		\
			return compare;	\
		}					\
	}
	
	for(i = 0; i < 4; i++){
		COMPARE(cmpVec3XYZ(b1->mat[i], b2->mat[i]));
	}
	
	COMPARE(compareInt(b1->group, b2->group));
	
	COMPARE(compareInt(b1->killer, b2->killer));
	
	COMPARE(compareInt(b1->group, b2->group));
	
	return 0;
}

//static S32 __cdecl compareCRCModelInfo(const CRCModelInfo* i1, const CRCModelInfo* i2){
//	return stricmp(i1->model->name, i2->model->name);
//}

typedef struct BeaconGetMapCRCInfo
{
	S32 collisionOnly;
	S32 quiet;
	S32 printFullInfo;
} BeaconGetMapCRCInfo;

/*
static int compareBeaconInfo(const WorldCollObjectInfo **info1, const WorldCollObjectInfo **info2)
{
	int result = 0;
	int vertex = 0;

	while(result==0)
	{
		Vec3 p1, p2;

		if(vertex >= (*info1)->smd->vert_count)	// If it's greater than both, they must be identical->order doesn't matter
		{
			result = -1;
			break;
		}
		if(vertex >= (*info2)->smd->vert_count)
		{
			result = 1;
			break;
		}

		mulVecMat4((*info1)->smd->verts[vertex], (*info1)->mat, p1);
		mulVecMat4((*info2)->smd->verts[vertex], (*info2)->mat, p2);

		result = cmpVec3XZY(p1, p2);
		vertex++;
	}

	return result;
}
*/

static int compareBeaconInfo2(const WorldCollObjectInfo **info1, const WorldCollObjectInfo **info2)
{
	return beaconU32Cmp((*info1)->crc, (*info2)->crc);
}

typedef struct BeaconVec3 {
	Vec3 pos;
} BeaconVec3;

BeaconVec3 **spawnPositions;

void beaconFileAddCRCPos(const Vec3 pos)
{
	BeaconVec3 *spawn = NULL;

	spawn = callocStruct(BeaconVec3);
	copyVec3(pos, spawn->pos);

	eaPush(&spawnPositions, spawn);
}

S32 beaconSortSpawn(const BeaconVec3 **p1, const BeaconVec3 **p2)
{
	return cmpVec3XYZ((*p1)->pos, (*p2)->pos);
}

void beaconClearCRCData(void)
{
	eaClearEx(&spawnPositions, NULL);
	beaconDestroyObjects();
}

const char* beaconFileGetLogPath(const char* logfile)
{
	static char logpath[MAX_PATH];

	strcpy(logpath, logGetDir());
	//strcat(logpath, "/");
	strcat(logpath, logfile);

	return logpath;
}

const char* beaconFileGetLogFile(char* tag)
{
	static char logfile[MAX_PATH];
	char logname[MAX_PATH];

	sprintf(logname, "%s_%s_%s_%d", 
				tag,
				beaconIsClient()?"client":"server", 
				beaconIsClient()?beaconClientGetMapname() : beaconServerGetMapName(),
				beaconIsClient()?_getpid():0);

	strcpy(logfile, logname);
	strcat(logfile, ".log");

	return logfile;
}

U32 beaconCalculateGeoCRC(WorldColl *wc, S32 rounded)
{
	int i;
	WorldCollObjectInfo **ea = NULL;

	ea = *beaconGatherObjects(wc);

	if(eaSize(&ea)==0)
	{
		return 0;  // No objects in the map?!
	}

	for(i=0; i<eaSize(&ea); i++)
	{
		WorldCollObjectInfo *info = ea[i];

		beaconObjectPrep(info);
		info->crc = beaconCRCObject(info, rounded);
	}

	eaQSort(ea, compareBeaconInfo2);

	cryptAdler32Init();
	for(i=0; i<eaSize(&ea); i++)
	{
		WorldCollObjectInfo *info = ea[i];

		cryptAdler32Update((U8*)&info->crc, sizeof(U32));
	}
	return cryptAdler32Final();
}

static U32 beaconCalculateMapCRC(WorldColl *wc, S32 quiet, S32 printFullInfo)
{
	U32 geoCRC, cfgCRC, encCRC;
	int i;
	BeaconProcessConfig *cfg = NULL;
	StructAllocIfNull(parse_BeaconMapMetaData, beacon_process.mapMetaData);
	StructReset(parse_BeaconMapMetaData, beacon_process.mapMetaData);
	
	geoCRC = cfgCRC = encCRC = 0;

	geoCRC = beaconCalculateGeoCRC(wc, true);

	cfg = beaconServerGetProcessConfig();
	if(cfg)
		cfgCRC = StructCRC(parse_BeaconProcessConfig, beaconServerGetProcessConfig());

	if(eaSize(&spawnPositions))
	{
		eaQSort(spawnPositions, beaconSortSpawn);

		for(i=0; i<eaSize(&spawnPositions); i++)
		{
			int j;
			
			for(j=0; j<3; j++)
				spawnPositions[i]->pos[j] = roundFloatWithPrecision(spawnPositions[i]->pos[j], 0.01);
		}

		cryptAdler32Init();
		for(i=0; i<eaSize(&spawnPositions); i++)
		{
			cryptAdler32Update((U8*)spawnPositions[i], sizeof(BeaconVec3));
		}
		encCRC = cryptAdler32Final();
	}

	if(printFullInfo)
	{
		const char *logfile = beaconFileGetLogFile("mapcrc");
		const char *logpath = beaconFileGetLogPath(logfile);
		WorldCollObjectInfo **ea = NULL;

		if(beaconIsBeaconizer())
		{
			fileForceRemove(logpath);
		}

		ea = *beaconGatherObjects(wc);
		for(i=0; i<eaSize(&ea); i++)
		{
			WorldCollObjectInfo *info = ea[i];

			beaconPrintObject(info, logfile, true);
		}

		if(eaSize(&spawnPositions))
		{
			for(i=0; i<eaSize(&spawnPositions); i++)
			{
				filelog_printf(logfile, "Spawn Pos: %.2f %.2f %.2f %x %x %x\n", vecParamsXYZ(spawnPositions[i]->pos), vecParamsXYZHex(spawnPositions[i]->pos));
			}
		}

		filelog_printf(logfile,"Done geo:%x enc:%x cfg:%x!", geoCRC, encCRC, cfgCRC);
	}

	cryptAdler32Init();
	cryptAdler32Update((U8*)&cfgCRC, sizeof(U32));
	if(encCRC)
		cryptAdler32Update((U8*)&encCRC, sizeof(U32));
	cryptAdler32Update((U8*)&geoCRC, sizeof(U32));
	beacon_process.mapMetaData->fullCRC = cryptAdler32Final();

	beacon_process.mapMetaData->cfgCRC = cfgCRC;
	beacon_process.mapMetaData->encCRC = encCRC;
	beacon_process.mapMetaData->geoCRC = geoCRC;

	return beacon_process.mapMetaData->fullCRC;
	return 0;
}

void beaconLogCRCInfo(WorldColl *wc)
{
	beaconCalculateMapCRC(wc, 1, 1);
}

U32 beaconFileGetCRC(S32 *procV)
{
	char filename[1000];
	beaconGetBeaconFileName(NULL, SAFESTR(filename), curBeaconFileVersion);

	strcat(filename, ".date");

	return beaconFileGetFileCRC(filename, 0, procV);
}

S32	beaconFileGetProcVersion(void)
{
	return curBeaconProcVersion;
}

U32 beaconFileGetMapGeoCRC(void)
{
	if(!beacon_process.mapMetaData)
		return -1;

	return beacon_process.mapMetaData->geoCRC;
}

U32 beaconFileGetFileCRC(const char* beaconDateFile, S32 getTime, S32 *procV)
{
	if(!beacon_process.mapMetaData)
	{
		if(procV)
			*procV = 0;

		return 0;
	}

	// Get the date from the date file first, and failing that the beacon file.
	if(procV)
	{
		*procV = 0;
	}

	beaconReadDateFile(beaconDateFile);

	if(procV)
		*procV = beacon_process.mapMetaData->dataProcessVersion;

	if(beaconIsBeaconizer())
		printf("Previous Newest Data Time: %s\n", getDateStringBase2000(beacon_process.mapMetaData->patchViewTime));

	return beacon_process.mapMetaData->fullCRC;
}

static void beaconCollTraverseCallback(	void* unused,
									   const WorldCollObjectTraverseParams* params)
{
	// Create the blocks overlapped by this wco.
	WorldCollStoredModelData*	smd = NULL;
	WorldCollModelInstanceData* inst = NULL;

	if(wcoGetStoredModelData(	&smd,
								&inst,
								params->wco,
								WC_FILTER_BIT_MOVEMENT))
	{
		int i;
		Vec3 min, max;

		assert(inst);
		beaconCalcSMDMatMinMaxSlow(smd, inst->world_mat, min, max);

		for(i = 0; i < 3; i++)
		{
			if(min[i] < beacon_process.world_min_xyz[i])
				beacon_process.world_min_xyz[i] = min[i];
			if(max[i] > beacon_process.world_max_xyz[i])
				beacon_process.world_max_xyz[i] = max[i];
		}
	}

	SAFE_FREE(inst);
}

void beaconMeasureWorld(WorldColl *wc, S32 quiet)
{
	setVec3same(beacon_process.world_min_xyz, FLT_MAX);
	setVec3same(beacon_process.world_max_xyz, -FLT_MAX);

	if(!quiet){
		printf("Measuring world: ");
	}

	beaconProcessSetTitle(0, "Measure");

	beacon_process.validStartingPointMaxLevel = 0;

	wcTraverseObjects(	wc,
						beaconCollTraverseCallback,
						NULL,
						NULL,
						NULL,
						/*unique=*/1,
						WCO_TRAVERSE_STATIC);
}

void beaconFileGatherMetaData(int iPartitionIdx, U32 quiet, U32 logFullInfo)
{
	BeaconMapMetaData *mmd;
	StructAllocIfNull(parse_BeaconMapMetaData, beacon_process.mapMetaData);
	StructReset(parse_BeaconMapMetaData, beacon_process.mapMetaData);
	ANALYSIS_ASSUME(beacon_process.mapMetaData);

	mmd = beacon_process.mapMetaData;

	beaconCalculateMapCRC(worldGetActiveColl(iPartitionIdx), quiet, logFullInfo);

	mmd->metaDataVersion = curBeaconDateFileVersion;
	mmd->dataProcessVersion = curBeaconProcVersion;
	beaconMeasureWorld(worldGetActiveColl(iPartitionIdx), 1);
	copyVec3(beacon_process.world_min_xyz, mmd->minXYZ);
	copyVec3(beacon_process.world_max_xyz, mmd->maxXYZ);
	mmd->patchViewTime = beacon_process.latestDataFileTime;
}

S32 beaconFileMetaDataMatch(BeaconMapMetaData *file, BeaconMapMetaData *map, int quiet)
{
	if(!file || !map)
	{
		if(!quiet)
			printfColor(COLOR_BRIGHT|COLOR_RED, "Failed to find cur or file meta data (cur: %p   file: %p", map, file);
		return 0;
	}

	if(file->dataProcessVersion!=map->dataProcessVersion){
		if(!quiet)
			printfColor(COLOR_BRIGHT|COLOR_GREEN, "Processing version doesn't match!  (cur: 0x%8.8x   file: 0x%8.8x)\n", 
			map->dataProcessVersion, file->dataProcessVersion);
		return 0;
	}

	if(file->encCRC!=map->encCRC){
		if(!quiet)
			printfColor(COLOR_BRIGHT|COLOR_GREEN, "Encounter CRC doesn't match!  (cur: 0x%8.8x   file: 0x%8.8x)\n", 
			map->encCRC, file->encCRC);
		return 0;
	}

	if(file->geoCRC!=map->geoCRC){
		if(!quiet)
			printfColor(COLOR_BRIGHT|COLOR_GREEN, "Collision CRC doesn't match!  (cur: 0x%8.8x   file: 0x%8.8x)\n", 
			map->geoCRC, file->geoCRC);
		return 0;
	}

	if(file->cfgCRC!=map->cfgCRC){
		if(!quiet)
			printfColor(COLOR_BRIGHT|COLOR_GREEN, "Config CRC doesn't match!  (cur: 0x%8.8x   file: 0x%8.8x)\n", 
			map->cfgCRC, file->cfgCRC);
		return 0;
	}

	// Would be bad if they were the same up there but different here.
	devassert(file->fullCRC==map->fullCRC);

	return 1;
}

S32 beaconFileMatchesMapCRC(const char* beaconDateFile, S32 getTime, int quiet){
	BeaconMapMetaData *cur, *file;
	beaconReadDateFile(beaconDateFile);

	cur = beacon_process.mapMetaData;
	file = beacon_process.fileMetaData;

	return beaconFileMetaDataMatch(file, cur, quiet);
}

S32 beaconFileIsUpToDate(S32 noFileCheck){
	if(!noFileCheck){
		S32 crcMatches = beaconFileMatchesMapCRC(beacon_process.beaconDateFileName, 1, 0);
		S32 readFile;

		PERFINFO_AUTO_START("readBeaconFile", 1);
			readFile = readBeaconFile(NULL, curBeaconFileVersion);
		PERFINFO_AUTO_STOP();
		
		if(!readFile){
			printf("WARNING: Current beacon file won't load.  Forcing beaconizing.\n");
		}
		else if(beaconIsBeaconizer() && combatBeaconArray.size==0 && !strstri(zmapInfoGetFilename(NULL), "TestMaps")) {
			printf("WARNING: Current beacon file has no beacons.  Forcing beaconizing.\n");
		}
		else if(crcMatches){
			consoleSetColor(COLOR_BRIGHT|COLOR_RED, 0);
			printf("CANCELED: CRC matches.\n");
			consoleSetDefaultColor();
			return 1;
		}
	}else{
		printf("WARNING: Skipping file-date check.\n");
	}
	
	return 0;
}	

S32 beaconDoesTheBeaconFileMatchTheMap(int iPartitionIdx, WorldColl *wc, S32 printFullInfo){
	// Thie is an function to call from an external source to check if the beacons are up to date.

	char beaconDateFile[1000];
	
	beaconGetBeaconFileName(NULL, SAFESTR(beaconDateFile), curBeaconFileVersion);
	strcat(beaconDateFile, ".date");

	beaconFileGatherMetaData(iPartitionIdx, 1, 0);
	
	return beaconFileMatchesMapCRC(beaconDateFile, 0, 1);
}

S32 beaconEnsureFilesExist(S32 noFileCheck)
{
	char *errStr = NULL;
	int is_new_tmp = 0;
	int is_new = 0;

	// Create a temporary lock file if the beacon file doesn't currently exist.
	if(!beaconEnsureFileExists(beacon_process.beaconFileName, &is_new_tmp, &errStr)){
		beaconServerEmailMapFailure(BCN_MAPFAIL_FILE_DNE, errStr);
		estrDestroy(&errStr);
		return 0;
	}
	is_new = is_new || is_new_tmp;

	estrClear(&errStr);
	if(!beaconEnsureFileExists(beacon_process.beaconDateFileName, &is_new_tmp, &errStr)){
		beaconServerEmailMapFailure(BCN_MAPFAIL_FILE_DNE, errStr);
		estrDestroy(&errStr);
		return 0;
	}
	is_new = is_new || is_new_tmp;

	estrClear(&errStr);
	if(!beaconEnsureFileExists(beacon_process.beaconInvalidFileName, &is_new_tmp, &errStr)){
		beaconServerEmailMapFailure(BCN_MAPFAIL_FILE_DNE, errStr);
		estrDestroy(&errStr);
		return 0;
	}
	is_new = is_new || is_new_tmp;

	if(!is_new && beaconFileIsUpToDate(noFileCheck))
	{
		return 0;
	}

	beacon_process.is_new_file = is_new;

	return 1;
}

S32 beaconCheckoutBeaconFiles(S32 noFileCheck, S32 removeOldFiles){
	char *errStr = NULL;
	char **fileStrs = NULL;
	int ret;

	// All three files exist - check them out together
	eaPush(&fileStrs, beacon_process.beaconFileName);
	eaPush(&fileStrs, beacon_process.beaconDateFileName);
	eaPush(&fileStrs, beacon_process.beaconInvalidFileName);
	ret = gimmeDLLDoOperations(fileStrs, GIMME_CHECKOUT, GIMME_QUIET);

	if(ret)
	{
		estrPrintf(&errStr, "Gimme checkout failed (%d) - %s", ret, gimmeDLLGetErrorString(ret));
		beaconServerEmailMapFailure(BCN_MAPFAIL_CHECKOUT, errStr);
		gimmeDLLDoOperations(fileStrs, GIMME_UNDO_CHECKOUT, GIMME_QUIET);
		eaDestroy(&fileStrs);
		estrDestroy(&errStr);
		return 0;
	}
	beacon_process.bcn_checkedout = 1;
	beacon_process.bcn_date_checkedout = 1;
	beacon_process.bcn_invalid_checkedout = 1;

	eaDestroy(&fileStrs);
	estrDestroy(&errStr);

	// Force a run through the def list to get the latest data file change.

	beacon_process.latestDataFileTime = 0;
	estrClear(&beacon_process.latestDataFileName);
	
	printf("Current Newest Data Time: %s\n", getDateString(beacon_process.latestDataFileTime));

	if(beacon_process.latestDataFileName){
		printf("Current Newest Data File: %s\n", beacon_process.latestDataFileName);
	}
	
	return 1;
}

S32 beaconFileReadFile(ZoneMapInfo *zmi, S32 *versionOut)
{
	S32 v;
	S32 readFile;

	for(v = curBeaconFileVersion; v >= oldestAllowableBeaconFileVersion; v--){
		beaconFileMakeFilenames(NULL, v);
		if(fileExists(beacon_process.beaconFileName)){
			loadstart_printf("Reading beacon file...");
			PERFINFO_AUTO_START("readBeaconFile", 1);
			readFile = readBeaconFile(zmi, v);
			PERFINFO_AUTO_STOP();
			loadend_printf(" done.");

			if(!readFile){
				// Re-initialize so there isn't partial garbage in the beacon variables.

				beaconClearBeaconData();
			}else{
				if(versionOut)
					*versionOut = v;
				return 1;
			}
		}
	}

	return 0;
}

#endif

S32 beaconHasSpaceRegion(ZoneMapInfo *zminfo)
{
	WorldRegion **regions = zmapInfoGetAllWorldRegions(zminfo);

	FOR_EACH_IN_EARRAY(regions, WorldRegion, region)
	{
		WorldRegionType wrt = worldRegionGetType(region);
		if(wrt == WRT_Space || wrt == WRT_SectorSpace)
			return true;
	}
	FOR_EACH_END;

	return false;
}

S32 beaconHasRegionType(ZoneMapInfo *zminfo, WorldRegionType wrtIn)
{
	WorldRegion **regions = zmapInfoGetAllWorldRegions(zminfo);

	FOR_EACH_IN_EARRAY(regions, WorldRegion, region)
	{
		WorldRegionType wrt = worldRegionGetType(region);
		if(wrt == wrtIn)
			return true;
	}
	FOR_EACH_END;

	return false;
}

void beaconPreProcessMap(void)
{
#if !PLATFORM_CONSOLE
	int i;
	WorldRegion **regions = NULL;

	beacon_process.isSpaceMap = false;
	regions = worldGetAllWorldRegions();
	for(i=0; i<eaSize(&regions); i++)
	{
		WorldRegionType rType = worldRegionGetType(regions[i]);
		if(rType==WRT_Space || rType==WRT_SectorSpace)
			beacon_process.isSpaceMap = true;
	}

	beaconGalaxyGroupJumpIncrement = 5;
	if(beacon_process.isSpaceMap)
		beaconGalaxyGroupJumpIncrement = 10000;
#endif
}

void beaconReload(void)
{
#if !PLATFORM_CONSOLE
	S32 readFile = 0;
	S32 v;
	int i;
	F32 maxFloor = 0;

	if(beaconIsClient())
	{
		return;
	}
	if(isProductionEditMode() && resNamespaceIsUGC(zmapInfoGetPublicName(NULL))) {
		eaDestroyEx(&beacon_state.partitions, beaconPartitionDestroy);
		beaconClearBeaconData();
		return;
	}

	eaDestroyEx(&beacon_state.partitions, beaconPartitionDestroy);

	loadstart_printf("Loading beacons...");

	// Read the stored beacon file if it exists.

	readFile = beaconFileReadFile(NULL, &v);

	// Initialize the beacon variables.

	if(!readFile)
	{
		const char* map_name = zmapInfoGetPublicName(NULL);
		if(isProductionMode() && map_name!=NULL && stricmp(map_name, "emptymap") != 0)
		{
			Errorf("Beacon file not loaded for map: %s", map_name);

			filelog_printf("beacon.log", "Can't load beacons for: %s", map_name);
		}

		beaconReader.fileVersion = 0;
	}

	beaconPreProcessMap();

	// Need version 8 to be able to clusterize.
	if(readFile && v >= 8){
		beaconRebuildBlocks(0, 1, 0);
	}

	beaconPathInit(1);

	beaconReadInvalidSpawnFile();

	for(i=0; i<combatBeaconArray.size; i++)
	{
		Beacon *b = combatBeaconArray.storage[i];
		int j;

		b->globalIndex = i;

		b->floorDistance = beaconGetJitteredPointFloorDistance(worldGetAnyCollPartitionIdx(), b->pos);
		if(b->floorDistance > maxFloor)
			maxFloor = b->floorDistance;

		//assert(b->floorDistance < BEACON_CONN_FLOOR_DIST);

		for(j=0; j<b->gbConns.size; j++)
		{
			BeaconConnection *conn = b->gbConns.storage[j];

			conn->distance = (F16)distance3(b->pos, conn->destBeacon->pos);
			if(b->floorDistance > BEACON_CONN_FLOOR_DIST)
				conn->groundDist = BEACON_CONN_FLOOR_DIST;
			else
				conn->groundDist = b->floorDistance;
		}

		for(j=0; j<b->rbConns.size; j++)
		{
			BeaconConnection *conn = b->rbConns.storage[j];

			conn->distance = (F16)distance3XZ(b->pos, conn->destBeacon->pos);
			if(b->floorDistance > BEACON_CONN_FLOOR_DIST)
				conn->groundDist = BEACON_CONN_FLOOR_DIST;
			else
				conn->groundDist = b->floorDistance;
		}
	}

	beaconConnReInit();

	loadend_printf(" done.");

	printf("Max floor dist: %f\n", maxFloor);
#else 
#endif
}

#if !PLATFORM_CONSOLE

int cmpZMInfo(const ZoneMapInfo** lhs, const ZoneMapInfo** rhs)
{
	const ZoneMapInfo *l = *lhs;
	const ZoneMapInfo *r = *rhs;
	return stricmp(zmapInfoGetPublicName(l), zmapInfoGetPublicName(r));
}

typedef struct BeaconMapReport {
	ZoneMapInfo *zminfo;

	int beacons;
	int beaconGConns;
	int beaconRConns;
	int blocks;
	int blockConns;
	int galaxies;
	int galaxyConns;
	int clusters;
	int clusterConns;

	int memory;
} BeaconMapReport;

struct {
	ZoneMapType zmType;
	WorldRegionType wrType;

	int totalMaps;
	BeaconMapReport **mapreports;
	const ZoneMapInfo **noBeaconList;
} bcnReport;

typedef F32 (*accessor)(void* data, void* user_data);
F32 eaSum(void **ea, accessor func, void* user_data)
{
	F32 sum = 0;
	FOR_EACH_IN_EARRAY(ea, void*, data)
		sum += func(data, user_data);
	FOR_EACH_END;

	return sum;
}

F32 eaMean(void **ea, accessor func, void* userdata)
{
	if(eaSize(&ea)==0)
		return 0;

	return eaSum(ea, func, userdata)/eaSize(&ea);
}

F32 bmrGetData(BeaconMapReport *report, void* offsetptr) { return *((U32*)(((U8*)report)+(intptr_t)offsetptr));}

S32 beaconReportMapMatchesFilter(ZoneMapInfo *zminfo, ZoneMapType zmType, WorldRegionType wrType)
{

	if(zmapInfoGetNoBeacons(zminfo))
		return false;

	if(zmType!=-1 && zmType!=zmapInfoGetMapType(zminfo))
	{
		return false;
	}

	if(wrType!=-1)
	{
		int i;
		int goodRegion = 0;
		WorldRegion **regions = zmapInfoGetWorldRegions(zminfo);
		for(i=0; i<eaSize(&regions); i++)
		{
			if(worldRegionGetType(regions[i])==wrType)
			{
				goodRegion = true;
				break;
			}
		}

		if(!goodRegion)
			return false;
	}

	return true;
}

AUTO_COMMAND;
void beaconFileReport(CmdContext *context, ACMD_NAMELIST(ZoneMapTypeEnum, STATICDEFINE) const char* zmTypeStr, ACMD_NAMELIST(WorldRegionTypeEnum, STATICDEFINE) const char* wrTypeStr)
{
	ZoneMapType zmType = StaticDefineIntGetInt(ZoneMapTypeEnum, zmTypeStr);
	WorldRegionType wrType = StaticDefineIntGetInt(WorldRegionTypeEnum, wrTypeStr);
	RefDictIterator iter;
	ZoneMapInfo *zminfo;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);

	if((zmType==bcnReport.zmType || bcnReport.zmType==-1) && 
	   (wrType==bcnReport.wrType || bcnReport.wrType==-1))
		return;

	bcnReport.zmType = zmType;
	bcnReport.wrType = wrType;

	eaClearEx(&bcnReport.mapreports, NULL);
	eaClear(&bcnReport.noBeaconList);
	bcnReport.totalMaps = 0;

	// This can take a really long time
	worldGetZoneMapIterator(&iter);
	while (zminfo = worldGetNextZoneMap(&iter))
	{
		int i;
		BeaconMapReport *report;

		if(zmapInfoGetPrivacy(zminfo))
			continue;

		bcnReport.totalMaps++;

		if(!beaconReportMapMatchesFilter(zminfo, bcnReport.zmType, bcnReport.wrType))
			continue;

		printf("\rProcessing %s...", zmapInfoGetPublicName(zminfo));

		if(!beaconFileReadFile(zminfo, NULL))
		{
			eaPush(&bcnReport.noBeaconList, zminfo);
			continue;
		}

		if(!combatBeaconArray.size)
		{
			eaPush(&bcnReport.noBeaconList, zminfo);
			continue;
		}

		beaconRebuildBlocks(0, 1, 0);

		report = callocStruct(BeaconMapReport);
		report->zminfo = zminfo;

		report->beacons += combatBeaconArray.size;
		for(i=0; i<combatBeaconArray.size; i++)
		{
			Beacon *b = combatBeaconArray.storage[i];

			report->beaconGConns += b->gbConns.size;
			report->beaconRConns += b->rbConns.size;
		}

		report->blocks += partition->combatBeaconGridBlockArray.size;
		for(i=0; i<partition->combatBeaconGridBlockArray.size; i++)
		{
			BeaconBlock *block = partition->combatBeaconGridBlockArray.storage[i];
			int j;

			report->blockConns += block->rbbConns.size;
			report->blockConns += block->gbbConns.size;

			report->blocks += block->subBlockArray.size;
			for(j=0; j<block->subBlockArray.size; j++)
			{
				BeaconBlock *subBlock = block->subBlockArray.storage[j];

				report->blockConns += subBlock->rbbConns.size;
				report->blockConns += subBlock->gbbConns.size;					
			}
		}

		for(i = 0; i < beacon_galaxy_group_count; i++)
		{
			int j;

			report->galaxies += partition->combatBeaconGalaxyArray[i].size;

			for(j=0; j<partition->combatBeaconGalaxyArray[i].size; j++)
			{
				BeaconBlock *galaxy = partition->combatBeaconGalaxyArray[i].storage[j];

				report->galaxyConns += galaxy->rbbConns.size;
				report->galaxyConns += galaxy->gbbConns.size;
			}
		}

		report->clusters += partition->combatBeaconClusterArray.size;
		for(i=0; i<partition->combatBeaconClusterArray.size; i++)
		{
			BeaconBlock *cluster = partition->combatBeaconClusterArray.storage[i];

			report->clusterConns += cluster->gbbConns.size;
			report->clusterConns += cluster->rbbConns.size;
		}

		report->memory =	report->beacons * sizeof(Beacon) + 
							report->beaconGConns * sizeof(BeaconConnection) +
							report->beaconRConns * sizeof(BeaconConnection) +
							(report->blocks + report->galaxies + report->clusters) * sizeof(BeaconBlock) + 
							(report->blockConns + report->galaxyConns) * sizeof(BeaconBlockConnection) +
							report->clusterConns * sizeof(BeaconClusterConnection);

		eaPush(&bcnReport.mapreports, report);
	}

	if(eaSize(&bcnReport.mapreports)==0)
	{
		printf("No maps found\n");
		return;
	}

	printf("Processed %d maps, analyzed %d\n", bcnReport.totalMaps, eaSize(&bcnReport.mapreports));

	// Reset beacons to what they were
	beaconFileReadFile(zminfo, NULL);
}

AUTO_COMMAND;
void beaconReportStats(void)
{
	int count, totalMaps;
	F32 avgBcns, avgBcnGConns, avgBcnRConns, avgBlocks, avgBlockConns, avgGalaxies, 
		avgGalaxyConns, avgClusters, avgClusterConns, avgMemory;

	if(eaSize(&bcnReport.mapreports)==0)
	{
		printf("No maps found\n");
		return;
	}

	count = eaSize(&bcnReport.mapreports);
	totalMaps = bcnReport.totalMaps;
	avgBcns			= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, beacons));
	avgBcnGConns	= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, beaconGConns));
	avgBcnRConns	= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, beaconRConns));
	avgBlocks		= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, blocks));
	avgBlockConns	= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, blockConns));
	avgGalaxies		= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, galaxies));
	avgGalaxyConns	= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, galaxyConns));
	avgClusters		= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, clusters));
	avgClusterConns = eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, clusterConns));
	avgMemory		= eaMean(bcnReport.mapreports, bmrGetData, (intptr_t*)OFFSETOF(BeaconMapReport, memory));

	printf( "\r\nReport: Maps checked: %d of %d\n"
			"\tBeacons:  %4.2f - GConns:  %4.2f - RConns: %4.2f\n"
			"\tBlocks:	 %4.2f - BlockConns:   %4.2f\n"
			"\tGalaxies: %4.2f - GalaxyConns:  %4.2f\n"
			"\tClusters: %4.2f - ClusterConns: %4.2f\n"
			"\tEstimated Memory Usage Total : %d\n",
			count, totalMaps,
			avgBcns, avgBcnGConns, avgBcnRConns,
			avgBlocks, avgBlockConns,
			avgGalaxies, avgGalaxyConns,
			avgClusters, avgClusterConns,
			(int)avgMemory);

	if(eaSize(&bcnReport.noBeaconList))
	{
		int i;
		eaQSort(bcnReport.noBeaconList, cmpZMInfo);
		printf("Maps without beacons: %d\n", eaSize(&bcnReport.noBeaconList));
		for(i=0; i<eaSize(&bcnReport.noBeaconList); i++)
		{
			printf("\t%s\n", zmapInfoGetPublicName(bcnReport.noBeaconList[i]));
		}
	}
}

static int bcnReportSortGalaxies(const BeaconMapReport **lhs, const BeaconMapReport **rhs)
{
	const BeaconMapReport *l = *lhs, *r = *rhs;

	if(l->galaxies==r->galaxies)
		return (intptr_t)l - (intptr_t)r;

	return r->galaxies - l->galaxies;
}

AUTO_COMMAND;
void beaconReportLargestGalaxies(void)
{
	int i;
	if(eaSize(&bcnReport.mapreports)==0)
	{
		printf("No maps found\n");
		return;
	}

	eaQSort(bcnReport.mapreports, bcnReportSortGalaxies);

	for(i=0; i<20 && i<eaSize(&bcnReport.mapreports); i++)
	{
		BeaconMapReport *report = bcnReport.mapreports[i];
		printf("%d\t\t%s\n", report->galaxies, zmapInfoGetPublicName(report->zminfo));
	}
}

static int bcnReportSortMemory(const BeaconMapReport **lhs, const BeaconMapReport **rhs)
{
	const BeaconMapReport *l = *lhs, *r = *rhs;

	if(l->galaxies==r->galaxies)
		return (intptr_t)l - (intptr_t)r;

	return r->memory - l->memory;
}

AUTO_COMMAND;
void beaconReportMostMemory(void)
{
	int i;
	if(eaSize(&bcnReport.mapreports)==0)
	{
		printf("No maps found\n");
		return;
	}

	eaQSort(bcnReport.mapreports, bcnReportSortMemory);

	for(i=0; i<20 && i<eaSize(&bcnReport.mapreports); i++)
	{
		BeaconMapReport *report = bcnReport.mapreports[i];
		printf("%d\t\t%s\n", report->memory, zmapInfoGetPublicName(report->zminfo));
	}
}

#endif

#include "AutoGen/WorldLib_autogen_ClientCmdWrappers.c"

#include "beaconFile_h_ast.c"
