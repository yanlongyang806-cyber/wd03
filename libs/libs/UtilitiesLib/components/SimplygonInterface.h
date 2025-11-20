#ifndef _SIMPLYGONINTERFACE_H_
#define _SIMPLYGONINTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SIMPLYGON_STANDARD_SDK_LICENSE_DATA_FILE	"SimplygonLicense.dat"
#define SIMPLYGON_STANDARD_SDK_DLL_X64				"SimplygonSDKRuntimeReleasex64.dll"
#define SIMPLYGON_STANDARD_SDK_DLL_WIN32			"SimplygonSDKRuntimeReleaseWin32.dll"
#define SIMPLYGON_CRYPTICNW_SDK_DLL_X64				"SimplygonSDKCrypticNWReleasex64.dll"
#define SIMPLYGON_CRYPTICNW_SDK_DLL_X64_PDB			"SimplygonSDKCrypticNWReleasex64.pdb"
#define SIMPLYGON_CRYPTICNW_SDK_DLL_WIN32			"SimplygonSDKCrypticNWReleaseWin32.dll"
#define SIMPLYGON_CRYPTICNW_SDK_DLL_WIN32_PDB		"SimplygonSDKCrypticNWReleaseWin32.pdb"

// These must be consistent with SimplygonSDKCrypticNW.h
#define SIMPLYGON_MATERIAL_TEXTURE_DIFFUSE "Diffuse"
#define SIMPLYGON_MATERIAL_TEXTURE_NORMALS "Normals"
#define SIMPLYGON_MATERIAL_TEXTURE_SPECULAR "Specular"

typedef struct SimplygonMesh SimplygonMesh;
typedef struct SimplygonScene SimplygonScene;
typedef struct SimplygonNode SimplygonNode;
typedef struct SimplygonMaterial SimplygonMaterial;
typedef struct SimplygonMaterialTable SimplygonMaterialTable;

typedef enum RemeshSettings {
	REMESH_DISABLE_NORMALS = 1 << 0,
	REMESH_DISABLE_SPECULAR = 1 << 1,
} RemeshSettings;

typedef struct SimplygonRemeshStats
{
	S64 timeRemesh;
	S64 timeCastDiffuse;
	S64 timeCastNormal;
	S64 timeCastSpecular;
} SimplygonRemeshStats;

// ----------------------------------------------------------------------
// Simplygon meshes
// ----------------------------------------------------------------------

// Creation/destruction
SimplygonMesh *simplygon_createMesh(const char *name);
void simplygon_destroyMesh(SimplygonMesh *mesh);

// Vertex count
void simplygon_setMeshNumVertices(SimplygonMesh *mesh, unsigned int numVertices);
unsigned int simplygon_getMeshNumVertices(const SimplygonMesh *mesh);

// Triangle count
void simplygon_setMeshNumTriangles(SimplygonMesh *mesh, unsigned int numTriangles);
unsigned int simplygon_getMeshNumTriangles(const SimplygonMesh *mesh);

// Set/get triangle indexes. Number of indexes must be 3 * numTriangles.
void simplygon_setMeshTriangles(
	SimplygonMesh *mesh,
	unsigned int *triangleIndexes,
	unsigned int *triangleTexIds);
void simplygon_getMeshTriangles(
	const SimplygonMesh *mesh,
	unsigned int *triangleIndexes,
	unsigned int *triangleTexIds);

// Set/get vertex positions. Number of floats must be 3 * numVertices.
void simplygon_setMeshVertexPositions(
	SimplygonMesh *mesh,
	float *positions,
	unsigned int channel);
void simplygon_getMeshVertexPositions(
	const SimplygonMesh *mesh,
	float *positions,
	unsigned int channel);

// Set/get normals. Number of floats must be 3 * 3 * numTriangles. (Three normals (3 floats each) per triangle - one per
// triangle corner.)
void simplygon_setMeshVertexNormals(
	SimplygonMesh *mesh,
	float *normals,
	unsigned int channel);
void simplygon_getMeshVertexNormals(
	SimplygonMesh *mesh,
	float *normals,
	unsigned int channel);

void simplygon_setMeshVertexTangentSpace(
	SimplygonMesh *mesh,
	float *tangents,
	float *binormals);
void simplygon_getMeshVertexTangentSpace(
	SimplygonMesh *mesh,
	float *tangents,
	float *binormals);

// Set/get texture coordinates. Number of floats must be 2 * 3 * numTriangles. (Two floats per triangle corner.)
void simplygon_setMeshVertexTexCoords(
	SimplygonMesh *mesh,
	float *texcoords,
	int channel);
void simplygon_getMeshVertexTexCoords(
	const SimplygonMesh *mesh,
	float *texcoords,
	int channel);

// Set/get vertex colors. Number of floats must be 4 * 3 * numTriangles. (Four color channels per triangle corner.)
void simplygon_setMeshVertexDiffuseColors(
	SimplygonMesh *mesh,
	float *colors);
void simplygon_getMeshVertexDiffuseColors(
	const SimplygonMesh *mesh,
	float *colors);

// Set/get mesh skinning information.
void simplygon_setMeshBoneData(
	SimplygonMesh *mesh,
	unsigned short *boneIds,
	float *boneWeights);
void simplygon_getMeshBoneData(
	const SimplygonMesh *mesh,
	unsigned short *boneIds,
	float *boneWeights);

// Get the GMesh usagebits. Usagebits are created based on which fields have been set so far.
int simplygon_getMeshUsageBits(const SimplygonMesh *mesh);

float simplygon_reduceMesh(
	SimplygonMesh *mesh,
	float maxError,
	float targetTriCount);

// ----------------------------------------------------------------------
// Simplygon scene graph
// ----------------------------------------------------------------------

SimplygonScene *simplygon_createScene(void);
void simplygon_destroyScene(SimplygonScene *scene);

// All these functions that create or return scene nodes need to be explicitly cleaned up when they're no longer
// used. They contain reference counted pointers.
SimplygonNode *simplygon_sceneGetRootNode(SimplygonScene *scene);
SimplygonNode *simplygon_createSceneNode(void);
SimplygonNode *simplygon_createSceneNodeFromMesh(SimplygonMesh *mesh);
void simplygon_destroySceneNode(SimplygonNode *node);

// Set a node's transformation with a 4x4 matrix.
void simplygon_nodeSetMatrix(SimplygonNode *node, float *mat);

void simplygon_nodeAddChild(SimplygonNode *parent, SimplygonNode *child);

SimplygonMesh *simplygon_doRemesh(
	SimplygonScene *scene,
	unsigned int onScreenSize,
	const unsigned int textureHeight,
	const unsigned int textureWidth,
	unsigned int maxPixelDeviation,
	SimplygonMaterialTable *materialTable,
	const char *diffusemapFilename,
	const char *normalmapFilename,
	const char *specularmapFilename, 
	SimplygonRemeshStats *remeshStats,
	const RemeshSettings *remeshSettings);

void simplygon_obj_export(SimplygonMesh *sMesh, const char* fileoutName);

// ----------------------------------------------------------------------
// Testing stuff
// ----------------------------------------------------------------------

void simplygon_assertFreedAll(void);
bool simplygon_interfaceTest(void);
char* simplygon_constructLogDump(void);
void simplygon_shutdown(void);
void simplygon_setSimplygonPath(const char *newPath);
typedef void (*SimplygonPathCallbackFunc)(void);
typedef void (*SimplygonLoadPrintfCallbackFunc)(const char* fmt, ...);
void simplygon_setSimplygonCallbacks(
	SimplygonPathCallbackFunc func,
	SimplygonLoadPrintfCallbackFunc loadStart,
	SimplygonLoadPrintfCallbackFunc loadEnd,
	SimplygonLoadPrintfCallbackFunc loadUpdate);
void simplygon_dumpRecentLogs(void);
void simplygon_setOutputToConsole(bool outputToConsole);
void simplygon_dumpSceneNodeHierarchy(SimplygonNode *node);
const char *simplygon_getMeshName(SimplygonMesh *mesh);

// ----------------------------------------------------------------------
// Materials
// ----------------------------------------------------------------------

SimplygonMaterial *simplygon_createMaterial(void);
void simplygon_destroyMaterial(SimplygonMaterial *mat);
void simplygon_setMaterialTexture(
	SimplygonMaterial *mat,
	const char *textureName,
	const char *fileName,
	bool deleteTextureFileWhenDestroyed);

SimplygonMaterialTable *simplygon_createMaterialTable(void);
void simplygon_destroyMaterialTable(SimplygonMaterialTable *table);

int simplygon_materialTableAddMaterial(
	SimplygonMaterialTable *table,
	SimplygonMaterial *material,
	const char *name,
	bool ownMaterial);

int simplygon_materialTableGetMaterialId(
	SimplygonMaterialTable *table,
	const char *name);

// ----------------------------------------------------------------------
// Utility Functions
// ----------------------------------------------------------------------

const char* simplygon_getVersion();

#ifdef __cplusplus
}
#endif

#endif // _SIMPLYGONINTERFACE_H_

