#pragma once
GCC_SYSTEM

#ifndef BEACONFILE_H
#define BEACONFILE_H

#include "stdtypes.h"
#include "WorldLibEnums.h"

typedef struct WorldColl WorldColl;
typedef struct ZoneMapInfo ZoneMapInfo;

AUTO_ENUM;
typedef enum BeaconMapWarning {
	BCN_MAPWARN_NO_SPAWNS = BIT(0),
} BeaconMapWarning;

AUTO_ENUM;
typedef enum BeaconMapFailureReason {
	BCN_MAPFAIL_FILE_DNE = 1 << 0,
	BCN_MAPFAIL_CHECKOUT = 1 << 1,
	BCN_MAPFAIL_NO_BEACONS = 1 << 2,
} BeaconMapFailureReason;

AUTO_STRUCT;
typedef struct SimpleWaypoint {
	Vec3 pos;
} SimpleWaypoint;

AUTO_STRUCT;
typedef struct SimplePath {
	SimpleWaypoint **waypoints;
} SimplePath;

AUTO_ENUM;
typedef enum BeaconProcessPhase {
	BPP_RECV_LOAD_MAP,
	BPP_GENERATE,
	BPP_CONNECT,
	BPP_COUNT,
} BeaconProcessPhase;

AUTO_STRUCT;
typedef struct BeaconMapMetaData {
	BeaconMapWarning mapWarning;
	BeaconMapFailureReason failureReason;
	S32 metaDataVersion;
	S32 dataProcessVersion;
	S32 patchViewTime;
	U32 fullCRC;
	U32 geoCRC;
	U32 geoRoundCRC;
	U32 encCRC;
	U32 cfgCRC;
	Vec3 minXYZ;
	Vec3 maxXYZ;
	S32 beaconCount;
	S32 beaconRaisedCount;
	S32 beaconGroundCount;
	F32 beaconClientSeconds[BPP_COUNT];

	SimplePath **invalidPaths;

	U32 zippedSize;
	U32 unzippedSize;
} BeaconMapMetaData;

typedef void (*BeaconWriterCallback)(const void* data, U32 size);
typedef int (*BeaconReaderCallback)(void* data, U32 size);

S32		beaconGetReadFileVersion(void);
void	beaconWriteCompressedFile(char* fileName, void* data, U32 byteCount, int doCheckoutCheckin, const char* comment);
void	beaconPreProcessMap(void);
void	beaconReload(void);
void	beaconReadInvalidSpawnFile(void);
void	beaconProcessNPCBeacons(void);
void	beaconProcessTrafficBeacons(void);
void	beaconProcess(S32 noFileCheck, S32 removeOldFiles, S32 generateOnly);
S32		beaconDoesTheBeaconFileMatchTheMap(int iPartitionIdx, WorldColl *wc, S32 printFullInfo);
void	beaconReaderDisablePopupErrors(S32 set);
S32		beaconReaderArePopupErrorsDisabled(void);
S32		beaconFileReadFile(ZoneMapInfo *zmi, S32* versionOut);
int		readBeaconFileCallback(BeaconReaderCallback callback);
void	writeBeaconFileCallback(BeaconWriterCallback callback, S32 writeCheck);
U32		beaconFileGetFileCRC(const char* beaconDateFile, S32 getTime, S32 *procVersion);
U32		beaconFileGetMapGeoCRC(void);
U32		beaconFileGetCRC(S32 *procVersion);
S32		beaconFileIsUpToDate(S32 noFileCheck);
S32		beaconFileMetaDataMatch(BeaconMapMetaData *file, BeaconMapMetaData *map, int quiet);
void	beaconLogCRCInfo(WorldColl *wc);
const char* beaconFileGetLogFile(char* tag);
const char* beaconFileGetLogPath(const char* logfile);
S32		beaconFileGetCurVersion(void);
S32		beaconFileGetProcVersion(void);
void	beaconFileAddCRCPos(const Vec3 pos);
void	beaconClearCRCData(void);
U32		beaconCalculateGeoCRC(WorldColl *wc, S32 rounded);
void	beaconMeasureWorld(WorldColl *wc, S32 quiet);
void	beaconFileGatherMetaData(int iPartitionIdx, U32 quiet, U32 printFull);
void	beaconReadDateFile(const char *beaconDateFile);
void	beaconWriteDateFile(S32 doCheckoutCheckin);
void	beaconCheckinDateFile(void);
void	beaconFileMakeFilenames(ZoneMapInfo *zmi, int version);
S32		fileExistsInList(const char** mapList, const char* findName, S32* index);
S32		beaconHasSpaceRegion(ZoneMapInfo *zminfo);
S32		beaconHasRegionType(ZoneMapInfo *zminfo, WorldRegionType wrtIn);

#endif
