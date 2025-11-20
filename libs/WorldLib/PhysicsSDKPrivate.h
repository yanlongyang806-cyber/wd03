
#pragma once
GCC_SYSTEM

//#define PSDK_STREAM_DEBUG

#include "pub/PhysicsSDK.h"

#if !PSDK_DISABLED

#define PSDK_KEEP_ACTOR_LIST 0

#ifdef __cplusplus
#include "Nxf.h"
#include "NxStream.h"
#include "NxMaterial.h"

class NxScene;
class NxActor;
class NxFluid;
class NxPhysicsSDK;
class NxTriangleMesh;
class NxConvexMesh;
class NxHeightField;
class PSDKAllocedStream;

extern NxPhysicsSDK* nxSDK;
#endif

#include "timing.h"

C_DECLARATIONS_BEGIN

typedef struct AssociationList		AssociationList;
typedef struct PSDKQueuedRelease	PSDKQueuedRelease;
typedef struct FileWrapper			FileWrapper;

//----------------------------------------------------------------------
// PhysicsSDK.cpp
//----------------------------------------------------------------------

enum PSDKCookedMeshType {
	PSDK_CMT_TRIANGLE,
	PSDK_CMT_CONVEX,
	PSDK_CMT_HEIGHTFIELD,
	PSDK_CMT_SPHERE,
	PSDK_CMT_CAPSULE,
	PSDK_CMT_BOX,
	PSDK_CMT_COUNT,
};
C_DECLARATIONS_END

#ifdef __cplusplus
typedef struct PSDKScene {
	U32							instanceID;

	void*						userPointer;
	NxScene*					nxScene;
	
	PSDKGeoInvalidatedFunc		geoInvalidatedFunc;
	
	struct {
		U32						startedThreadID;
	} simulate;
	
	struct {
		U32						count;
		U32						createCount;
		U32						createFailedCount;
		U32						destroyCount;
	} actor;

	struct {
		PSDKScenePruningType	staticPruningType;
		PSDKScenePruningType	dynamicPruningType;
	} config;

	struct {
		U32						simulating : 1;
	} flags;
} PSDKScene;

typedef struct PSDKActor {
	void*						userPointer;
	NxActor*					nxActor;
	AssociationList*			alMeshes;
	
	struct {
		U32						destroyed		: 1;
		U32						geoInvalidated	: 1;
		U32						isDynamic		: 1;
		U32						isKinematic		: 1;
		U32						hasContactEvent	: 1;
		U32						ignoreForChar	: 1;
		U32						hasOneWayCollision : 1;
	} flags;
} PSDKActor;

typedef struct PSDKCookedMesh {
	U32							byteSize;
	
	union {
		struct {
			NxTriangleMesh*		nxTriangleMesh; // Can be NULL if being cooked in the background, see flags
			PSDKAllocedStream*	cookedStream; // Returned from background thread
		} triangleMesh;

		NxConvexMesh*			nxConvexMesh;

		struct{
			NxHeightField*		nxHeightField;
			S32					gridSize[2];
			F32					worldSize[2];
			F32					minHeight;
			F32					maxHeight;
			S32					maxIntHeight;
		} heightField;

		struct{
			F32					radius;
		} sphere;

		struct{
			Vec3				size;
		} box;

		struct{
			F32					radius;
			F32					height;
		} capsule;
	};

	AssociationList*			alRefs;
	
	void*						collisionData;

	struct {
		PSDKCookedMeshType		meshType					: 10;
		U32						destroyed					: 1;
		U32						queuedForRelease			: 1;
		U32						createTriangleMeshFailed	: 1;
		U32						hadRandomFailure			: 1;
	} flags;
	
	struct {
		volatile U32			triangleMeshIsCooking		: 1; // Sent to the background thread
		volatile U32			triangleMeshIsCooked		: 1; // Background thread is done with it
		volatile U32			cookingFailed				: 1;
	} threadFlags;
} PSDKCookedMesh;

typedef struct PSDKFluid {
	NxFluid *nxFluid;
} PSDKFluid;

#endif

C_DECLARATIONS_BEGIN
#define NOMINMAX
#include "wininclude.h"

typedef struct PSDKSimulationOwnership {
	U32 unused;
} PSDKSimulationOwnership;

typedef struct PSDKState {
	U32									threadID;
	
	CRITICAL_SECTION					csQueuedReleases;
	CRITICAL_SECTION					csMainPSDK;
	CRITICAL_SECTION					csCooking;

	struct {
		U32								count;
		U32								lastInstanceID;
	} scene;
	
	U32									totalMeshCount;
	
	U32									meshTypeCount[PSDK_CMT_COUNT];
	
	PSDKSimulationOwnership*			so;
	U32									soThreadID;

	U32									simulationLockCountFG;
	U32									simulationLockCountBG;
	U32									simulatingScenesCountBG;
	
	PSDKQueuedRelease**					queuedReleases;
	
	U32									totalCookedBytes;
	
	PSDKDestroyCollisionDataCB			cbDestroyCollisionData;
	PSDKContactCB						cbContact;
	
	struct {
		U32								maxActors;
		U32								maxBodies;
		U32								maxStaticShapes;
		U32								maxDynamicShapes;
		U32								maxJoints;
	} limits;
	
	struct {
		Vec3							findHeightFieldPoint;
		F32								findHeightFieldPointRadius;
		
		struct {
			U32							findHeightFieldPointEnabled : 1;
		} flags;
	} debug;

	struct {
		U32								sceneRequestFailed	: 1;
		U32								noHardwareSupport	: 1;
		U32								printCooking		: 1;
		U32								printTestThing		: 1;
		U32								printNewActorBounds	: 1;
		U32								assertOnFailure		: 1;
		U32								disableLimits		: 1;
		U32								disableErrors		: 1;
	} flags;
} PSDKState;

extern PSDKState	psdkState;
C_DECLARATIONS_END

//----------------------------------------------------------------------
// PhysicsSDKHelpers.cpp
//----------------------------------------------------------------------

#ifdef __cplusplus
class NxUserOutputStream;
class NxUserAllocator;

NxUserOutputStream*		psdkGetOutputStream(void);
NxUserAllocator*		psdkGetAllocator(void);

class PSDKSharedStream : public NxStream
{
	private:
		#ifdef PSDK_STREAM_DEBUG
			FileWrapper *sdFile;
			char *sdContents;
			const char *sdContentsCursor;
			int sdContentsLen;
		#endif
	public:
		PSDKSharedStream();
		virtual ~PSDKSharedStream();

		virtual bool isValid() const;

		void resetByteCounter();
		void checkTempBufferSize( NxU32 size);
		static void freeTempBuffer();

		NxU8* getBuffer();
		int	getBufferSize();

		virtual NxU8		readByte() const;
		virtual NxU16		readWord() const;
		virtual NxU32		readDword() const;
		virtual NxF32		readFloat() const;
		virtual NxF64		readDouble() const;
		virtual void		readBuffer(void* buffer, NxU32 size) const;

		virtual NxStream& 	storeByte(NxU8 a);
		virtual NxStream& 	storeWord(NxU16 a);
		virtual NxStream& 	storeDword(NxU32 a);
		virtual NxStream& 	storeFloat(NxReal a);
		virtual NxStream& 	storeDouble(NxF64 a);
		virtual NxStream& 	storeBuffer(const void* buffer, NxU32 size);


		static NxU8* pTempBuffer;
		static NxU32 uiBufferSize;
		static NxU8* pCursor;
};

class PSDKAllocedStream : public NxStream
{
	private:
		#ifdef PSDK_STREAM_DEBUG
			FileWrapper *sdFile;
			int byteCount;
			char *sdContents;
			const char *sdContentsCursor;
			int sdContentsLen;
		#endif

	public:
		PSDKAllocedStream();
		PSDKAllocedStream(void* buffer, NxU32 size);
		virtual ~PSDKAllocedStream();

		virtual bool isValid() const;

		void resetByteCounter();
		void checkTempBufferSize( NxU32 size);

		NxU8* getBuffer();
		int	getBufferSize();
		void* stealBuffer();
        static void freeBuffer(void *data);

		virtual NxU8		readByte() const;
		virtual NxU16		readWord() const;
		virtual NxU32		readDword() const;
		virtual NxF32		readFloat() const;
		virtual NxF64		readDouble() const;
		virtual void		readBuffer(void* buffer, NxU32 size) const;

		virtual NxStream& 	storeByte(NxU8 a);
		virtual NxStream& 	storeWord(NxU16 a);
		virtual NxStream& 	storeDword(NxU32 a);
		virtual NxStream& 	storeFloat(NxReal a);
		virtual NxStream& 	storeDouble(NxF64 a);
		virtual NxStream& 	storeBuffer(const void* buffer, NxU32 size);


		NxU8* pTempBuffer;
		NxU32 uiBufferSize;
		NxU8* pCursor;
};

#undef FILE

S32 psdkCanModify(PSDKSimulationOwnership* so);

__forceinline static void setNxMat34FromMat4(	NxMat34& nxMat,
												const Mat4 mat)
{
	nxMat.t = mat[3];
	nxMat.M.setColumnMajor(mat[0]);
}

__forceinline static void setNxVec3FromVec3(NxVec3& nxVec,
											const Vec3 vec)
{
	nxVec.x = vec[0]; nxVec.y = vec[1]; nxVec.z = vec[2];
}


#endif

#endif
