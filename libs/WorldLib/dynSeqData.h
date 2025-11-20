#pragma once
GCC_SYSTEM


#include "dynBitField.h"
#include "referencesystem.h"

typedef struct DynAction DynAction;
typedef struct DynAnimFXMessage DynAnimFXMessage;

extern const char* pcHipsName;
extern const char *pcRootUpperbodyName;
extern const char* pcWaistName;
extern const char* pcHandL;
extern const char* pcHandR;

#define AnimFileError(a, format, ...) ErrorFilenameGroupf(a, "Animation", 3, format, __VA_ARGS__)
#define CharacterFileError(a, format, ...) ErrorFilenameGroupf(a, "Character Animation", 3, format, __VA_ARGS__)
#define CharacterFileErrorAndInvalidate(a, format, ...) ErrorFilenameGroupInvalidDataf(a, "Character Animation", 3, format, __VA_ARGS__)

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynSeqData
{
	const char*		pcName;					AST(KEY POOL_STRING STRUCTPARAM)
	DynBitFieldStatic		requiresBits;
	DynBitFieldStatic		optionalBits;
	DynAction**		eaActions;				AST(NAME("DynAction"))
	const char*		pcFileName;				AST(CURRENTFILE)
	const char**	eaMember;				AST(POOL_STRING)
	U32				uiNumCriticalBits;		NO_AST
	F32				fPriority;
	DynAction*		pDefaultFirstAction;	NO_AST
	DynAnimFXMessage**	eaOnExitFXMessages;	AST(NAME(OnExitFXMessage))
	bool			bVerified;				NO_AST
	bool			bOverRidden;			NO_AST
	bool			bCore;
	bool			bDefaultSequence;		AST(BOOLFLAG)
	bool			bInterruptEverything;	AST(BOOLFLAG)
	bool			bDisableTorsoPointing;	AST(BOOLFLAG)
	U32				uiLastUsedFrameStamp;	NO_AST
} DynSeqData;

typedef struct DynSeqDataReference
{
	REF_TO(DynSeqData) hSeqData;
} DynSeqDataReference;

typedef struct DynSeqDataCollection
{
	const char* pcSequencerName;
	DynSeqDataReference **eaDynSeqDatas;
	REF_TO(DynSeqData)	hDefaultSequence;
	bool		bDefault;
} DynSeqDataCollection;



DynSeqDataCollection* dynSeqDataCollectionFromName(const char* pcSequencer);

void dynSeqDataLoadAll(void);
void dynSeqDataReloadAll(void);

void dynSeqDataCacheUpdate(void);
const DynSeqData* dynSeqDataFromBits(SA_PARAM_NN_STR const char* pcSequencer, SA_PARAM_NN_VALID const DynBitField* pBF, bool *pIsDefault);
const DynSeqData* dynSeqDataFromName(const char* pcSeqName);
void dynSeqDataCheckReloads(void);
bool debugDynSeqDataFromBits(const char* pcSequencerName, const DynBitField* pBF, char** pcDebugLog);