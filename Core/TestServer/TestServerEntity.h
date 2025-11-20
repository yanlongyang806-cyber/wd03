#pragma once

typedef U32 ContainerID;
typedef U32 GlobalType;
typedef enum MultiValType MultiValType;
typedef struct WTCmdPacket WTCmdPacket;
typedef bool (*PercedContainerCallback)(GlobalType eType, ContainerID id, void *userData);
typedef bool (*ContainerPercCallback)(GlobalType eType, int numContainers, int offset, void *userData);

typedef enum TestServerObjectRequest
{
	TSObj_XInfo,
	TSObj_XMembers,
	TSObj_XIndices,
} TestServerObjectRequest;

typedef enum TestServerXPathType
{
	TSXPT_NotFound,
	TSXPT_Invalid,
	TSXPT_Number,
	TSXPT_String,
	TSXPT_Array,
	TSXPT_Struct,
} TestServerXPathType;

void TestServer_InitEntity(void);
void TestServer_ContainerPerc_Generic(GlobalType eType, int batch, int offset, PercedContainerCallback pContainerCallback, void *pContainerUserData, ContainerPercCallback pCallback, void *pUserData);

void TestServer_XPathRequest(TestServerObjectRequest reqType, GlobalType eType, ContainerID id, const char *xPath);
void TestServer_XPathRequestHandle(void *user_data, void *data, WTCmdPacket *packet);
bool TestServer_XPathRequestReady(void);
MultiValType TestServer_XPathType(void);
const char *TestServer_XPathStringValue(void);
float TestServer_XPathNumberValue(void);
int TestServer_XPathNumKeys(void);
bool TestServer_XPathIndexed(void);
char *TestServer_XPathKey(int index);