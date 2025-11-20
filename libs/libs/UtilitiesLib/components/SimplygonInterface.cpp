#include <windows.h>

#include <iostream>
#include <fstream>
#include <cassert>
#include <sstream>
#include <ctime>
#include <vector>
#include <string>
#include <map>
using namespace std;

#include "SuperAssert.h"
#include "GenericMeshBits.h"
#include "file.h"

#include <SimplygonSDKCrypticNW.h>
#include <SimplygonSDKLoader.h>
using namespace SimplygonSDK;

#include "SimplygonInterface.h"

static SimplygonLoadPrintfCallbackFunc loadStartFunc = NULL;
static SimplygonLoadPrintfCallbackFunc loadEndFunc = NULL;
static SimplygonLoadPrintfCallbackFunc loadUpdateFunc = NULL;

// ----------------------------------------------------------------------
// Memory leak checking
// ----------------------------------------------------------------------

// We don't have memcheck included because of C++ compile issues, so this is here to keep track of stuff we've
// allocated.

static unsigned int numSimplygonObjects = 0;
class SimplygonMemUsageTracker {
public:
	SimplygonMemUsageTracker(void) {
		numSimplygonObjects++;
	}
	~SimplygonMemUsageTracker(void) {
		numSimplygonObjects--;
	}
};

void simplygon_assertFreedAll(void) {
	assert(numSimplygonObjects == 0);
}

// ----------------------------------------------------------------------
// Internal types
// ----------------------------------------------------------------------

// All of these should just be opaque pointers on the C side. All of them should have a SimplygonMemUsageTracker in
// them, too.

struct SimplygonMesh {
	int usageBits;
	spGeometryData geometryData;
	char *originalName;
	SimplygonMemUsageTracker memTracker;
};

struct SimplygonScene {
	spScene scene;
	SimplygonMemUsageTracker memTracker;
};

struct SimplygonNode {
	CountedPointer<ISceneNode> sceneNode;
	SimplygonMemUsageTracker memTracker;
};

struct SimplygonMaterial {
	CountedPointer<IMaterial> material;
	vector<char*> textureFilesToDelete; // Filenames of baked textures that we'll clean up when we're done remeshing.
	SimplygonMemUsageTracker memTracker;
};

struct SimplygonMaterialTable {
	CountedPointer<IMaterialTable> materialTable;
	map<std::string, unsigned int> namesToIds;
	vector<SimplygonMaterial*> ownedMaterials;
	SimplygonMemUsageTracker memTracker;
};

// ----------------------------------------------------------------------
// Internal logging stuff.
// ----------------------------------------------------------------------

class SimplygonLogger : public streambuf {
public:

	static const int logBufferLen = 4096;
	char logBuffer[logBufferLen];
	int charIndex;
	bool outputToCout;

	SimplygonLogger(void) {

		memset(logBuffer, 0, sizeof(logBuffer));
		charIndex = 0;

		outputToCout = false;

	}

	void setOutputToConsole(bool outputToConsole) {
		outputToCout = outputToConsole;
	}

	bool getOutputToConsole(void) {
		return outputToCout;
	}

	int overflow(int ch) {

		logBuffer[charIndex] = ch;
		charIndex++;
		charIndex %= (logBufferLen - 1);

		if(outputToCout) {
			cout << ((char)ch);
		}

		return ch;
	}
};

static SimplygonLogger *mainSimplygonLogger = NULL;
static ostream *mainSimplygonLoggerOut = NULL;

static ostream &getSimplygonLogger(void) {
	if(!mainSimplygonLogger) {
		mainSimplygonLogger = new SimplygonLogger();
	}
	if(!mainSimplygonLoggerOut) {
		mainSimplygonLoggerOut = new ostream(mainSimplygonLogger);
	}
	return *mainSimplygonLoggerOut;
}

// If you use this function, it is expected that you will NOT need to keep track of this
// information for an extended period of time.  If you do, copy the string provided to something
// more permanent.
char* simplygon_constructLogDump(void) {
	static char logBuffer[SimplygonLogger::logBufferLen + 1];
	int actualIndex = 0;

	if(mainSimplygonLogger) {
		int startIndex = mainSimplygonLogger->charIndex;
		int curIndex = startIndex;
		do {
			if(mainSimplygonLogger->logBuffer[curIndex]) {
				logBuffer[actualIndex] = mainSimplygonLogger->logBuffer[curIndex];
			}
			curIndex++;
			actualIndex++;
			curIndex %= SimplygonLogger::logBufferLen;
		} while(startIndex != curIndex);
	}
	logBuffer[actualIndex] = '\0';

	return logBuffer;
}

void simplygon_dumpRecentLogs(void) {
	cout << simplygon_constructLogDump() << endl;
}

void simplygon_setOutputToConsole(bool outputToConsole) {
	getSimplygonLogger();
	mainSimplygonLogger->setOutputToConsole(outputToConsole);
}

// Cheap and ugly way to redirect all our debugging output to a memory log.
#define sgOut (getSimplygonLogger())

// ----------------------------------------------------------------------
// Progress notification stuff
// ----------------------------------------------------------------------

class SimplygonProgressIndicator : public SimplygonSDK::robserver {
private:
	int last_notified_percent;

public:
	SimplygonProgressIndicator() : last_notified_percent( 100 )
	{
		// NOTE: No heap allocations can happen here. An instance of this class is statically allocated and makes some
		// of our internal memory tracking stuff very confused.
	}

private:
	virtual void Execute(
		SimplygonSDK::IObject *subject,
		SimplygonSDK::rid eventId,
		void *eventParamBlock,
		unsigned int eventParamBlockSize) {

		if(eventId == SG_EVENT_PROGRESS) {
			int percent = *((int*)eventParamBlock);
			*((int*)eventParamBlock) = 1;

			if (last_notified_percent != percent)
			{
				last_notified_percent = percent;
				sgOut << "Simplygon progress: " << percent << "%" << endl;
				loadUpdateFunc(" %5d%%...", percent);
			}
		}
		else
		if(eventId == SG_EVENT_PROCESS_STARTED) {
			loadUpdateFunc(" %5d%%...", 0);
			sgOut << "Simplygon procress started" << endl;
		}
	}
};

static SimplygonProgressIndicator progressIndicator;

// ----------------------------------------------------------------------
// Simplygon error handling
// ----------------------------------------------------------------------

class SimplygonErrorHandler : public SimplygonSDK::rerrorhandler {
private:
public:

	SimplygonErrorHandler(void) {
		// NOTE: No heap allocations can happen here. An instance of this class is statically allocated and makes some
		// of our internal memory tracking stuff very confused.
	}

	virtual void HandleError(
		IObject *object,
		const char *interfaceName,
		const char *methodName,
		rid errorType,
		const char *errorText) {

		sgOut << "Simplygon error in " <<
			interfaceName << "::" << methodName << " : " <<
			errorText << endl;
	}
};

static SimplygonErrorHandler errorHandler;

// ----------------------------------------------------------------------
// Simplygon SDK object
// ----------------------------------------------------------------------

static ISimplygonSDK *simplygonSDK = NULL;
static bool failedLoad = false;
static bool isLoaded = false;
static const char *simplygonPath = NULL;
static SimplygonPathCallbackFunc simplygonPathCallback = NULL;

static bool checkSimplygon(void) {

	if(simplygonPathCallback && !simplygonPath) {
		sgOut << "Running path callback." << endl;
		simplygonPathCallback();
		sgOut << "Done running path callback." << endl;
	}

	if(!simplygonPath) {
		sgOut << "Couldn't find Simplygon DLL!" << endl;
		return false;
	}

	if(!isLoaded) {

		sgOut << "Not loaded." << endl;

		if(failedLoad) {
			// Don't keep trying to load Simplygon all the time. The init function is slow.
			return false;
		}

		sgOut << "Attempting load." << endl;

		// Attempt to load license data.
		char *licenseData = NULL;
		if(simplygonPath && strlen(simplygonPath)) {

			// License is expected to be in the same directory as the DLL, so start from there and just replace the file
			// name at the end.
			char licensePathStart[MAX_PATH] = {0};
			char licensePath[MAX_PATH] = {0};

			strcpy(licensePathStart, simplygonPath);

			const char *licensePathEnd = "SimplygonLicense.dat";
			int i;
			for(i = (int)strlen(licensePathStart) - 1; i >= 0; i--) {
				if(licensePathStart[i] == '/' || licensePathStart[i] == '\\') {
					licensePathStart[i+1] = 0;
					break;
				}
			}

			if(licensePathStart[0]) {
				char nodelockLicensePath[MAX_PATH] = "C:\\ProgramData\\DonyaLabs\\SimplygonSDK\\License.dat";

				if (fileExists(nodelockLicensePath)) {
					strcpy(licensePath,nodelockLicensePath);
				} else {
					if (i >= 0)
						strcpy(licensePath, licensePathStart);
					strcat(licensePath, licensePathEnd);
				}
				
				ifstream inFile(licensePath);

				int retryOpenLicenseCount = 3;
				
#if 0
				// I wrote this to code to test for failures to access the license file, while diagnosing
				// Simplygon startup failures in concurrent processes. The code first asserts then
				// automatically retries. The assert is to allow trapping with a debugger.
				assert(inFile.is_open());
#ifdef open
	#undef open
#endif
				while (!inFile.is_open() && retryOpenLicenseCount)
				{
					sgOut << "Opening license failed. Retrying." << endl;
					Sleep( 30 );
					--retryOpenLicenseCount;
					inFile.open(licensePath);
				}
#endif

				if(inFile.is_open() && inFile.good()) {

					inFile.seekg(0, ios::end);
					int fileLen = inFile.tellg();
					inFile.seekg(0, ios::beg);

					licenseData = new char[fileLen+1];
					inFile.read(licenseData, fileLen);
					licenseData[fileLen] = 0;
				}
				else
					sgOut << "Opening license failed. Retrying." << endl;
			}

		}

		if(licenseData) {

			sgOut << "Got license data." << endl;

			loadStartFunc("Initializing Simplygon SDK...");

			int err = SimplygonSDK::Initialize(&simplygonSDK, simplygonPath, licenseData);
			if(err != SimplygonSDK::SG_ERROR_NOERROR) {

				failedLoad = true;
				simplygonSDK = NULL;

				sgOut << "Simplygon failed to load: " << SimplygonSDK::GetError(err) << endl;

			} else {

				sgOut << "Simplygon is now loaded." << endl;
				sgOut << "Simplygon version: " << simplygonSDK->GetVersion() << endl;
				isLoaded = true;

				sgOut << "Setting error handler." << endl;
				simplygonSDK->SetErrorHandler(&errorHandler);
				simplygonSDK->SetGlobalSetting( "OverrideDefaultParameterizer", 2 );
			}

			delete[] licenseData;

			loadEndFunc("done");

		} else {

			sgOut << "Failed to load license data." << endl;
		}
	}

	return !!simplygonSDK;
}

extern "C" {

	void simplygon_setSimplygonPath(const char *newPath) {
		simplygonPath = newPath;
	}

	void simplygon_setSimplygonCallbacks(
		SimplygonPathCallbackFunc func,
		SimplygonLoadPrintfCallbackFunc loadStart,
		SimplygonLoadPrintfCallbackFunc loadEnd,
		SimplygonLoadPrintfCallbackFunc loadUpdate) {

		simplygonPathCallback = func;
		loadStartFunc = loadStart;
		loadEndFunc = loadEnd;
		loadUpdateFunc = loadUpdate;
	}

	bool simplygon_initialize(void) {
		return checkSimplygon();
	}

	void simplygon_shutdown(void) {
		if(simplygonSDK && isLoaded) {
			SimplygonSDK::Deinitialize();
			simplygonSDK = NULL;
			isLoaded = false;
		}
	}

	SimplygonMesh *simplygon_createMesh(const char *name) {

		assert(checkSimplygon());

		size_t nameLen = strlen(name) + 1;
		char *origName = new char[nameLen];
		strcpy_s(origName, nameLen, name);

		SimplygonMesh *mesh = new SimplygonMesh();
		mesh->geometryData = simplygonSDK->CreateGeometryData();
		mesh->usageBits = 0;
		mesh->originalName = origName;

		return mesh;

	}

	const char *simplygon_getMeshName(SimplygonMesh *mesh) {
		return mesh->originalName;
	}

	void simplygon_destroyMesh(SimplygonMesh *mesh) {

		assert(checkSimplygon());

		delete[] mesh->originalName;

		delete mesh;
	}

	void simplygon_setMeshVertexPositions(
		SimplygonMesh *mesh,
		float *positions,
		unsigned int channel) {

		assert(checkSimplygon());

		unsigned int vertexCount = mesh->geometryData->GetVertexCount();
		spRealArray coords;

		if(!channel) {
			coords = mesh->geometryData->GetCoords();
			mesh->usageBits |= USE_POSITIONS;
		} else {
			ostringstream nameStr;
			nameStr << "positions" << channel << endl;
			mesh->geometryData->AddBaseTypeUserVertexField(TYPES_ID_REAL, nameStr.str().c_str(), 3);
			spValueArray coords_values = mesh->geometryData->GetUserVertexField(nameStr.str().c_str());
			coords = IRealArray::SafeCast(coords_values.GetPointer());
			mesh->usageBits |= USE_POSITIONS2;
		}

		for(unsigned int i = 0; i < vertexCount; i++) {
			coords->SetItem(i*3,   positions[i*3]);
			coords->SetItem(i*3+1, positions[i*3+1]);
			coords->SetItem(i*3+2, positions[i*3+2]);
		}
	}

	void simplygon_getMeshVertexPositions(
		const SimplygonMesh *mesh,
		float *positions,
		unsigned int channel) {

		assert(checkSimplygon());

		unsigned int vertexCount = mesh->geometryData->GetVertexCount();
		spRealArray coords;

		if(!channel) {
			assert(mesh->usageBits & USE_POSITIONS);
			coords = mesh->geometryData->GetCoords();
		} else {
			assert(mesh->usageBits & USE_POSITIONS2);
			ostringstream nameStr;
			nameStr << "positions" << channel << endl;
			const spValueArray coords_values = mesh->geometryData->GetUserVertexField(nameStr.str().c_str());
			coords = IRealArray::SafeCast(coords_values.GetPointer());
		}

		for(unsigned int i = 0; i < vertexCount; i++) {
			positions[i*3] = coords->GetItem(i*3);
			positions[i*3+1] = coords->GetItem(i*3+1);
			positions[i*3+2] = coords->GetItem(i*3+2);
		}
	}

	const char* simplygon_getVersion()
	{
		assert(checkSimplygon());
		return simplygonSDK->GetVersion();
	}

	void simplygon_setMeshNumVertices(
		SimplygonMesh *mesh,
		unsigned int numVertices) {

		assert(checkSimplygon());

		mesh->geometryData->SetVertexCount(numVertices);
	}

	unsigned int simplygon_getMeshNumVertices(
		const SimplygonMesh *mesh) {

		assert(checkSimplygon());

		return mesh->geometryData->GetVertexCount();
	}

	void simplygon_setMeshNumTriangles(
		SimplygonMesh *mesh,
		unsigned int numTriangles) {

		assert(checkSimplygon());

		mesh->geometryData->SetTriangleCount(numTriangles);
	}

	unsigned int simplygon_getMeshNumTriangles(
		const SimplygonMesh *mesh) {

		assert(checkSimplygon());

		return mesh->geometryData->GetTriangleCount();
	}

	// Normals are stored per triangle corner!
	void simplygon_setMeshVertexNormals(
		SimplygonMesh *mesh,
		float *normals,
		unsigned int channel) {

		assert(checkSimplygon());

		spRealArray meshNormals;
		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		if(!channel) {
			mesh->geometryData->AddNormals();
			meshNormals = mesh->geometryData->GetNormals();

			mesh->usageBits |= USE_NORMALS;

		} else {
			ostringstream nameStr;
			nameStr << "normals" << channel << endl;
			mesh->geometryData->AddBaseTypeUserTriangleVertexField(TYPES_ID_REAL, nameStr.str().c_str(), 3);
			spValueArray meshNormals_values = mesh->geometryData->GetUserTriangleField(nameStr.str().c_str());
			meshNormals = IRealArray::SafeCast(meshNormals_values.GetPointer());

			mesh->usageBits |= USE_NORMALS2;
		}

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			meshNormals->SetItem(i*3,   normals[i*3]);
			meshNormals->SetItem(i*3+1, normals[i*3+1]);
			meshNormals->SetItem(i*3+2, normals[i*3+2]);
		}
	}

	void simplygon_getMeshVertexNormals(
		SimplygonMesh *mesh,
		float *normals,
		unsigned int channel) {

		assert(checkSimplygon());

		spRealArray meshNormals;
		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		if(!channel) {
			assert(mesh->usageBits & USE_NORMALS);
			meshNormals = mesh->geometryData->GetNormals();
		} else {
			assert(mesh->usageBits & USE_NORMALS2);
			ostringstream nameStr;
			nameStr << "normals" << channel << endl;
			spValueArray meshNormals_values = mesh->geometryData->GetUserTriangleField(nameStr.str().c_str());
			meshNormals = IRealArray::SafeCast(meshNormals_values.GetPointer());
		}

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			normals[i*3] = meshNormals->GetItem(i*3);
			normals[i*3+1] = meshNormals->GetItem(i*3+1);
			normals[i*3+2] = meshNormals->GetItem(i*3+2);
		}
	}

	void simplygon_setMeshVertexTangentSpace(
		SimplygonMesh *mesh,
		float *tangents,
		float *binormals) {

		assert(checkSimplygon());

		mesh->geometryData->AddBitangents(0);
		mesh->geometryData->AddTangents(0);

		spRealArray meshBitangents = mesh->geometryData->GetBitangents(0);
		spRealArray meshTangents   = mesh->geometryData->GetTangents(0);
		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		mesh->usageBits |= USE_BINORMALS | USE_TANGENTS;

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			meshBitangents->SetItem(i*3,   binormals[i*3]   * -1.0);
			meshBitangents->SetItem(i*3+1, binormals[i*3+1] * -1.0);
			meshBitangents->SetItem(i*3+2, binormals[i*3+2] * -1.0);
			meshTangents->SetItem(i*3,   tangents[i*3]);
			meshTangents->SetItem(i*3+1, tangents[i*3+1]);
			meshTangents->SetItem(i*3+2, tangents[i*3+2]);
		}
	}

	void simplygon_getMeshVertexTangentSpace(
		SimplygonMesh *mesh,
		float *tangents,
		float *binormals) {

		assert(checkSimplygon());

		spRealArray meshBitangents = mesh->geometryData->GetBitangents(0);
		spRealArray meshTangents   = mesh->geometryData->GetTangents(0);
		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		assert((mesh->usageBits & (USE_BINORMALS | USE_TANGENTS)) == (USE_BINORMALS | USE_TANGENTS));

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			binormals[i*3]   = meshBitangents->GetItem(i*3)   * -1.0;
			binormals[i*3+1] = meshBitangents->GetItem(i*3+1) * -1.0;
			binormals[i*3+2] = meshBitangents->GetItem(i*3+2) * -1.0;
			tangents[i*3]    = meshTangents->GetItem(i*3);
			tangents[i*3+1]  = meshTangents->GetItem(i*3+1);
			tangents[i*3+2]  = meshTangents->GetItem(i*3+2);
		}
	}

	void simplygon_setMeshVertexTexCoords(
		SimplygonMesh *mesh,
		float *texcoords,
		int channel) {

		assert(checkSimplygon());

		if(channel == 0) {
			mesh->usageBits |= USE_TEX1S;
		} else if(channel == 1) {
			mesh->usageBits |= USE_TEX2S;
		}

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		mesh->geometryData->AddTexCoords(channel);
		spRealArray meshTexcoords = mesh->geometryData->GetTexCoords(channel);

		int itemCount = meshTexcoords->GetItemCount();

		assert((unsigned int)itemCount == triangleCount * 3 * 2);

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			meshTexcoords->SetItem(i*2,   texcoords[i*2]);
			meshTexcoords->SetItem(i*2+1, texcoords[i*2+1]);
		}
	}

	void simplygon_getMeshVertexTexCoords(
		const SimplygonMesh *mesh,
		float *texcoords,
		int channel) {

		assert(checkSimplygon());

		if(channel == 0) {
			assert(mesh->usageBits & USE_TEX1S);
		} else if(channel == 1) {
			assert(mesh->usageBits & USE_TEX2S);
		}

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();
		const spRealArray meshTexcoords = mesh->geometryData->GetTexCoords(channel);

		int itemCount = meshTexcoords->GetItemCount();

		assert((unsigned int)itemCount == triangleCount * 3 * 2);

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			texcoords[i*2] = meshTexcoords->GetItem(i*2);
			texcoords[i*2+1] = meshTexcoords->GetItem(i*2+1);
		}
	}

	void simplygon_setMeshVertexDiffuseColors(
		SimplygonMesh *mesh,
		float *colors) {

		assert(checkSimplygon());

		mesh->usageBits |= USE_COLORS;

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		mesh->geometryData->AddDiffuseColors();
		spRealArray diffuseColors = mesh->geometryData->GetDiffuseColors();

		int itemCount = diffuseColors->GetItemCount();

		assert((unsigned int)itemCount == triangleCount * 3 * 4);

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			diffuseColors->SetItem(i*4,   colors[i*4]);
			diffuseColors->SetItem(i*4+1, colors[i*4+1]);
			diffuseColors->SetItem(i*4+2, colors[i*4+2]);
			diffuseColors->SetItem(i*4+3, colors[i*4+3]);
		}
	}

	void simplygon_getMeshVertexDiffuseColors(
		const SimplygonMesh *mesh,
		float *colors) {

		assert(checkSimplygon());

		assert(mesh->usageBits & USE_COLORS);

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();
		spRealArray diffuseColors = mesh->geometryData->GetDiffuseColors();
		int itemCount = diffuseColors->GetItemCount();

		assert((unsigned int)itemCount == triangleCount * 3 * 4);

		for(unsigned int i = 0; i < triangleCount * 3; i++) {
			colors[i*4]   = diffuseColors->GetItem(i*4);
			colors[i*4+1] = diffuseColors->GetItem(i*4+1);
			colors[i*4+2] = diffuseColors->GetItem(i*4+2);
			colors[i*4+3] = diffuseColors->GetItem(i*4+3);
		}
	}

	void simplygon_setMeshTriangles(
		SimplygonMesh *mesh,
		unsigned int *triangleIndexes,
		unsigned int *triangleTexIds) {

		assert(checkSimplygon());

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		mesh->geometryData->AddMaterialIds();
		spRidArray texIds = mesh->geometryData->GetMaterialIds();
		spRidArray triIds = mesh->geometryData->GetVertexIds();

		for(unsigned int i = 0; i < triangleCount; i++) {
			triIds->SetItem(i*3,   triangleIndexes[i*3]);
			triIds->SetItem(i*3+1, triangleIndexes[i*3+1]);
			triIds->SetItem(i*3+2, triangleIndexes[i*3+2]);
			texIds->SetItem(i, triangleTexIds[i]);
		}
	}

	void simplygon_getMeshTriangles(
		const SimplygonMesh *mesh,
		unsigned int *triangleIndexes,
		unsigned int *triangleTexIds) {

		assert(checkSimplygon());

		unsigned int triangleCount = mesh->geometryData->GetTriangleCount();

		const spRidArray texIds = mesh->geometryData->GetMaterialIds();
		const spRidArray triIds = mesh->geometryData->GetVertexIds();

		for(unsigned int i = 0; i < triangleCount; i++) {
			triangleIndexes[i*3]   = triIds->GetItem(i*3);
			triangleIndexes[i*3+1] = triIds->GetItem(i*3+1);
			triangleIndexes[i*3+2] = triIds->GetItem(i*3+2);
			if(texIds.GetPointer()) {
				triangleTexIds[i] = texIds->GetItem(i);
			} else {
				triangleTexIds[i] = 0;
			}
		}
	}

	void simplygon_setMeshBoneData(
		SimplygonMesh *mesh,
		unsigned short *boneIds,
		float *boneWeights) {

		assert(checkSimplygon());

		mesh->usageBits |= USE_BONEWEIGHTS;

		mesh->geometryData->AddBoneWeights(4);
		mesh->geometryData->AddBoneIds(4);

		spRealArray meshBoneWeights = mesh->geometryData->GetBoneWeights();
		spRidArray meshBoneIds = mesh->geometryData->GetBoneIds();

		int itemCount = meshBoneWeights->GetItemCount();
		int numVerts = mesh->geometryData->GetVertexCount();

		assert(numVerts * 4 == itemCount);

		for(int i = 0; i < numVerts; i++) {
			for(int j = 0; j < 4; j++) {
				meshBoneWeights->SetItem(i * 4 + j, boneWeights[i * 4 + j]);
				meshBoneIds->SetItem(i * 4 + j, boneIds[i * 4 + j]);
			}
		}
	}

	void simplygon_getMeshBoneData(
		const SimplygonMesh *mesh,
		unsigned short *boneIds,
		float *boneWeights) {

		assert(checkSimplygon());

		assert(mesh->usageBits & USE_BONEWEIGHTS);

		spRealArray meshBoneWeights = mesh->geometryData->GetBoneWeights();
		spRidArray meshBoneIds = mesh->geometryData->GetBoneIds();

		int itemCount = meshBoneWeights->GetItemCount();
		int numVerts = mesh->geometryData->GetVertexCount();
		assert(numVerts * 4 == itemCount);

		for(int i = 0; i < numVerts; i++) {

			float *boneWeightsForVert = &(boneWeights[i * 4]);
			unsigned short *boneIdsForVert = &(boneIds[i * 4]);

			for(int j = 0; j < 4; j++) {
				boneWeightsForVert[j] = meshBoneWeights->GetItem(i * 4 + j);
				boneIdsForVert[j] = (unsigned short)(meshBoneIds->GetItem(i * 4 + j));
			}
		}
	}

	int simplygon_getMeshUsageBits(const SimplygonMesh *mesh) {
		return mesh->usageBits;
	}

	float simplygon_reduceMesh(
		SimplygonMesh *mesh,
		float maxError,
		float targetTriCount) {

		assert(checkSimplygon());

		assert(mesh->geometryData->GetTriangleCount());

		spReductionProcessor rd = simplygonSDK->CreateReductionProcessor();
		rd->SetGeometry(mesh->geometryData);

		spReductionSettings reduction_settings = rd->GetReductionSettings();
		reduction_settings->SetMaxDeviation( SimplygonSDK::real(maxError) );

		if(targetTriCount && mesh->geometryData->GetTriangleCount()) {
			targetTriCount /= float(mesh->geometryData->GetTriangleCount());
			reduction_settings->SetReductionRatio( SimplygonSDK::real(targetTriCount) );
		}

		spNormalCalculationSettings normalCalculationSettings = rd->GetNormalCalculationSettings();
		normalCalculationSettings->SetHardEdgeAngle(45.0f);
		normalCalculationSettings->SetReplaceNormals(true);

		rd->RunProcessing();

		return rd->GetMaxDeviation();
	}


	SimplygonScene *simplygon_createScene(void) {

		assert(checkSimplygon());

		SimplygonScene *scene = new SimplygonScene();
		scene->scene = simplygonSDK->CreateScene();
		return scene;
	}

	void simplygon_destroyScene(SimplygonScene *scene) {

		assert(checkSimplygon());

		scene->scene->Clear();
		delete scene;
	}

	SimplygonNode *simplygon_sceneGetRootNode(SimplygonScene *scene) {

		assert(checkSimplygon());

		SimplygonNode *newNode = new SimplygonNode();
		newNode->sceneNode = scene->scene->GetRootNode();
		return newNode;
	}

	SimplygonNode *simplygon_createSceneNode(void) {

		assert(checkSimplygon());

		SimplygonNode *newNode = new SimplygonNode();
		newNode->sceneNode = simplygonSDK->CreateSceneNode();
		return newNode;
	}

	void simplygon_destroySceneNode(SimplygonNode *node) {
		delete node;
	}

	void simplygon_sceneNodeSetName(SimplygonNode *node, const char *name) {
		assert(checkSimplygon());

		node->sceneNode->SetOriginalName(name);
	}

	const char *simplygon_sceneNodeGetName(SimplygonNode *node) {
		assert(checkSimplygon());

		// FIXME: There may be some data lifetime issues with this. I don't know when the data pointed to by the return
		// value will go away. rstring is a counted pointer, so as long as the scene node is around, it should be
		// valid. This is only for debugging anyway.
		rstring name = node->sceneNode->GetOriginalName();
		return name.GetText();
	}

	static void simplygon_dumpSceneNodeHierarchyInternal(CountedPointer<ISceneNode> node, int recursionLevel) {

		for(int i = 0; i < recursionLevel; i++) {
			cout << "  ";
		}

		cout << node->GetClass() << ":";
		if(node->GetOriginalName().GetText()) {
			cout << " " << node->GetOriginalName().GetText();
		}
		cout << " (" << node->GetChildCount() << ")";
		cout << endl;

		for(unsigned int i = 0; i < node->GetChildCount(); i++) {
			simplygon_dumpSceneNodeHierarchyInternal(node->GetChild(i), recursionLevel + 1);
		}
	}

	void simplygon_dumpSceneNodeHierarchy(SimplygonNode *node) {
		simplygon_dumpSceneNodeHierarchyInternal(node->sceneNode, 0);
	}

	void simplygon_nodeSetMatrix(SimplygonNode *node, float *mat) {

		assert(checkSimplygon());

		CountedPointer<IMatrix4x4> nodeMat = node->sceneNode->GetRelativeTransform();
		for(int row = 0; row < 4; row++) {
			for(int col = 0; col < 4; col++) {
				nodeMat->SetElement(row, col, mat[row * 4 + col]);
			}
		}
	}

	void simplygon_printMeshUsageBits(int usageBits) {
	  #define PRINTBIT(x) { if(usageBits & x) printf("%s\n", #x); }
		PRINTBIT(USE_POSITIONS);
		PRINTBIT(USE_POSITIONS2);
		PRINTBIT(USE_NORMALS);
		PRINTBIT(USE_NORMALS2);
		PRINTBIT(USE_BINORMALS);
		PRINTBIT(USE_TANGENTS);
		PRINTBIT(USE_TEX1S);
		PRINTBIT(USE_TEX2S);
		PRINTBIT(USE_BONEWEIGHTS);
		PRINTBIT(USE_COLORS);
		PRINTBIT(USE_VARCOLORS);
	}

	static int usageBitsFromGeometryData(spGeometryData geoData) {

		int usageBits = USE_POSITIONS;

		if(geoData->GetUserVertexField("positions2")) usageBits |= USE_POSITIONS2;
		if(geoData->GetNormals()) usageBits |= USE_NORMALS;
		if(geoData->GetUserTriangleField("normals2")) usageBits |= USE_NORMALS2;
		if(geoData->GetTexCoords(0)) usageBits |= USE_TEX1S;
		if(geoData->GetTexCoords(1)) usageBits |= USE_TEX2S;
		if(geoData->GetDiffuseColors()) usageBits |= USE_COLORS;
		if(geoData->GetBoneWeights()) usageBits |= USE_BONEWEIGHTS;
		if(geoData->GetBitangents(0)) usageBits |= USE_BINORMALS;
		if(geoData->GetTangents(0)) usageBits |= USE_TANGENTS;

		return usageBits;
	}

	SimplygonNode *simplygon_createSceneNodeFromMesh(SimplygonMesh *mesh) {

		assert(checkSimplygon());

		assert(usageBitsFromGeometryData(mesh->geometryData) == mesh->usageBits);

		CountedPointer<ISceneMesh> sceneMesh = simplygonSDK->CreateSceneMesh();

		sceneMesh->SetGeometry(mesh->geometryData->NewCopy(true));
		sceneMesh->SetOriginalName(mesh->originalName);

		SimplygonNode *newNode = new SimplygonNode();
		newNode->sceneNode = Cast<ISceneNode>(sceneMesh);
		return newNode;
	}

	void simplygon_nodeAddChild(SimplygonNode *parent, SimplygonNode *child) {

		assert(checkSimplygon());

		parent->sceneNode->AddChild(child->sceneNode);
	}

	void simplygon_obj_export(SimplygonMesh *sMesh, const char* fileoutName) {
		spWavefrontExporter exporter = simplygonSDK->CreateWavefrontExporter();
		string outputName;

		assert(checkSimplygon());

		outputName = "c:\\testModels\\";
		outputName += fileoutName;
		outputName += ".obj";
		exporter->SetSingleGeometry(sMesh->geometryData);
		exporter->SetExportFilePath(outputName.c_str());
		exporter->RunExport();
	}

	static void loadUpdateMemStatus()
	{
		MEMORYSTATUS memoryStatus;
		ZeroStruct(&memoryStatus);
		memoryStatus.dwLength = sizeof(memoryStatus);

		GlobalMemoryStatus(&memoryStatus);

		loadUpdateFunc("%u MB used", (memoryStatus.dwTotalVirtual - memoryStatus.dwAvailVirtual)/ (1024 * 1024));
	}

	typedef __int64 S64;

	// This must match the TaskProfile helper functions, which cannot be accessed here due to 
	// header file conflicts.
	__forceinline static S64 GetCPUTicks64()
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return (S64)li.QuadPart;
	}

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
		const RemeshSettings *remeshSettings) {

		spSceneNode rootSceneNode;
		spSceneNode sceneChildMeshNode;
		spSceneMesh sceneMesh;
		spGeometryData geo;
		SimplygonMesh *newMesh = NULL;
		S64 cpuTicksStart;

		loadStartFunc("Doing Simplygon remeshing...");

		assert(checkSimplygon());

		spRemeshingProcessor rm = simplygonSDK->CreateRemeshingProcessor();
		rm->SetSceneRoot(scene->scene->GetRootNode());

		spRemeshingSettings rmsettings = rm->GetRemeshingSettings();
		rmsettings->SetOnScreenSize(onScreenSize);
		rmsettings->SetUseGroundPlane(false);

		// TODO: Fill this out with settings for material stuff.
		spMappingImageSettings misettings = rm->GetMappingImageSettings();
		misettings->SetGenerateMappingImage(true);
		misettings->SetWidth(textureWidth);
		misettings->SetHeight(textureHeight);

		loadStartFunc("Geometry remesh...");

		sgOut << "Beginning remeshing operation." << endl;
		rm->AddObserver(&progressIndicator, SG_EVENT_PROGRESS);
		cpuTicksStart = GetCPUTicks64();
		rm->RemeshGeometry();
		if (remeshStats)
			remeshStats->timeRemesh = GetCPUTicks64() - cpuTicksStart;
		sgOut << "Done remeshing." << endl;

		loadUpdateMemStatus();
		loadEndFunc("done");

		// TODO: More error checking here so we don't jump headfirst into some missing scene node.
		rootSceneNode = scene->scene->GetRootNode();
		if (rootSceneNode && rootSceneNode->GetChildCount())
		{
			sceneChildMeshNode = rootSceneNode->GetChild(0);
			sceneMesh = Cast<ISceneMesh>(sceneChildMeshNode);
			if (sceneMesh)
				geo = sceneMesh->GetGeometry();
			else
				sgOut << "Scene root child zero is not a scene mesh." << endl;
		}
		else
			sgOut << "Scene root is NULL or has no children." << endl;

		if(geo.IsNull()) {

			// Some error happened and we can't continue.
			sgOut << "Remeshing failed." << endl;

			// TODO: Report the error message.

		} else {

			loadStartFunc("Diffuse color casting...");

			int newMeshUsageBits = usageBitsFromGeometryData(geo);

			newMesh = new SimplygonMesh();
			newMesh->geometryData = geo;
			newMesh->usageBits = newMeshUsageBits;
			newMesh->originalName = NULL;

			// Now do material casting.
			spMaterialTable outMaterialTable = simplygonSDK->CreateMaterialTable();
			spMaterial outMaterial = simplygonSDK->CreateMaterial();
			outMaterialTable->AddMaterial(outMaterial);

			spMappingImage mappingImage = rm->GetMappingImage();

			// TODO: The path for the output needs to be decided so we can properly process it as a texture.
			spColorCaster caster = simplygonSDK->CreateColorCaster();

			caster->SetColorType(SG_MATERIAL_CHANNEL_DIFFUSE);
			caster->SetSourceMaterials(materialTable->materialTable);
			caster->SetMappingImage(mappingImage);
			caster->SetOutputChannels(4);
			caster->SetOutputChannelBitDepth(8);
			caster->SetDilation(10);
			caster->SetOutputFilePath(diffusemapFilename ? diffusemapFilename : "c:\\testModels\\out_remeshed.png");

			sgOut << "Beginning material casting operation." << endl;
			caster->AddObserver(&progressIndicator, SG_EVENT_PROGRESS);
			cpuTicksStart = GetCPUTicks64();
			caster->CastMaterials();
			if (remeshStats)
				remeshStats->timeCastDiffuse = GetCPUTicks64() - cpuTicksStart;
			sgOut << "Done material casting." << endl;

			loadUpdateMemStatus();
			loadEndFunc("done");

			if (!(remeshSettings && (*remeshSettings & REMESH_DISABLE_SPECULAR)))
			{
				loadStartFunc("Specular map casting...");

				caster = simplygonSDK->CreateColorCaster();
				caster->SetColorType(SG_MATERIAL_CHANNEL_SPECULAR);
				caster->SetSourceMaterials(materialTable->materialTable);
				caster->SetMappingImage(mappingImage);
				caster->SetOutputChannels(3);
				caster->SetOutputChannelBitDepth(8);
				caster->SetDilation(10);
				caster->SetOutputFilePath(specularmapFilename ? specularmapFilename : "c:\\testModels\\out_remeshed_specular.png");

				sgOut << "Beginning specular casting operation." << endl;
				caster->AddObserver(&progressIndicator, SG_EVENT_PROGRESS);
				cpuTicksStart = GetCPUTicks64();
				caster->CastMaterials();
				if (remeshStats)
					remeshStats->timeCastSpecular = GetCPUTicks64() - cpuTicksStart;
				sgOut << "Done specular casting." << endl;

				loadUpdateMemStatus();
				loadEndFunc("done");
			}

			if (!(remeshSettings && (*remeshSettings & REMESH_DISABLE_NORMALS)))
			{
				loadStartFunc("Normal casting...");

				spNormalCaster normalCaster = simplygonSDK->CreateNormalCaster();
				normalCaster->SetMappingImage(mappingImage);
				normalCaster->SetSourceMaterials(materialTable->materialTable);
				normalCaster->SetOutputChannels(3);
				normalCaster->SetOutputChannelBitDepth(8);
				normalCaster->SetDilation(10);
				normalCaster->SetOutputFilePath(normalmapFilename ? normalmapFilename : "c:\\testModels\\out_remeshed_normals.png");
				normalCaster->SetFlipBackfacingNormals(false);
				normalCaster->SetGenerateTangentSpaceNormals(true);

				sgOut << "Beginning normal casting operation." << endl;
				caster->AddObserver(&progressIndicator, SG_EVENT_PROGRESS);
				cpuTicksStart = GetCPUTicks64();
				normalCaster->CastMaterials();
				if (remeshStats)
					remeshStats->timeCastNormal = GetCPUTicks64() - cpuTicksStart;
				sgOut << "Done with normals casting." << endl;
			}

			// TODO: Remove this. It's just for testing.
			spWavefrontExporter exporter = simplygonSDK->CreateWavefrontExporter();
			exporter->SetSingleGeometry(geo);
			exporter->SetExportFilePath("c:\\testModels\\out_remeshed.obj");
			exporter->RunExport();

			loadUpdateMemStatus();
			loadEndFunc("done");

		}

		loadEndFunc("done");

		return newMesh;
	}

	bool simplygon_interfaceTest(void) {
		return checkSimplygon();
	}

	SimplygonMaterial *simplygon_createMaterial(void) {

		assert(checkSimplygon());

		SimplygonMaterial *mat = new SimplygonMaterial;
		mat->material = simplygonSDK->CreateMaterial();
		return mat;
	}

	void simplygon_destroyMaterial(SimplygonMaterial *mat) {

		assert(checkSimplygon());

		for(unsigned int i = 0; i < mat->textureFilesToDelete.size(); i++) {
			// FIXME: Textures might be used on multiple materials now. Probably not an issue as long as all these
			// Simplygon materials are created and destroyed at the same time. But something to count the number of
			// materials referring to a specific texture might be nice.

			delete[] mat->textureFilesToDelete[i];
		}

		delete mat;
	}

	void simplygon_setMaterialTexture(
		SimplygonMaterial *mat,
		const char *textureName,
		const char *fileName,
		bool deleteTextureFileWhenDestroyed) {

		assert(checkSimplygon());

		sgOut << "Setting " << fileName << " for " << textureName << endl;

		mat->material->SetTexture(textureName, fileName);

		if(!strcmp(textureName, SG_MATERIAL_TEXTURE_NORMALS)) {
			mat->material->SetTangentSpaceNormals(true);
		}

		if(!strcmp(textureName, SG_MATERIAL_TEXTURE_SPECULAR)) {
			mat->material->SetSpecularColor(1.0, 1.0, 1.0);
			mat->material->SetShininess(128.0);
		}

		if(!strcmp(textureName, SG_MATERIAL_TEXTURE_DIFFUSE)) {
			mat->material->SetDiffuseColor(1.0, 1.0, 1.0);
		}

		assert(strlen(fileName));

		if(deleteTextureFileWhenDestroyed) {
			size_t len = strlen(fileName) + 1;
			char *name = new char[len];
			strcpy_s(name, len, fileName);
			mat->textureFilesToDelete.push_back(name);
		}
	}

	SimplygonMaterialTable *simplygon_createMaterialTable(void) {

		assert(checkSimplygon());

		SimplygonMaterialTable *table = new SimplygonMaterialTable();
		table->materialTable = simplygonSDK->CreateMaterialTable();
		return table;
	}

	void simplygon_destroyMaterialTable(SimplygonMaterialTable *table) {

		assert(checkSimplygon());

		for(unsigned int i = 0; i < table->ownedMaterials.size(); i++) {
			simplygon_destroyMaterial(table->ownedMaterials[i]);
		}

		delete table;
	}

	int simplygon_materialTableAddMaterial(
		SimplygonMaterialTable *table,
		SimplygonMaterial *material,
		const char *name,
		bool ownMaterial) {

		assert(checkSimplygon());

		assert(!table->namesToIds.count(name));

		int id = table->materialTable->AddMaterial(material->material);
		table->namesToIds[name] = id;

		if(ownMaterial) {
			table->ownedMaterials.push_back(material);
		}

		return id;
	}

	int simplygon_materialTableGetMaterialId(
		SimplygonMaterialTable *table,
		const char *name) {

		assert(checkSimplygon());

		if(!table->namesToIds.count(name)) {
			return -1;
		}

		return table->namesToIds[name];

	}


}
