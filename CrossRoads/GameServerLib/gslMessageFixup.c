#include "errno.h"
#include "file.h"

#include "CostumeCommonLoad.h"
#include "Message.h"
#include "CostumeCommon.h"
#include "ResourceManager.h"
#include "SimpleParser.h"
#include "StringUtil.h"
#include "time.h"

#include "gimmeDLLWrapper.h"
#include "gslMessageFixup.h"

#include "AutoGen/gslMessageFixup_c_ast.h"
#include "AutoGen/gslMessageFixup_h_ast.h"

#include "AutoGen/CostumeCommon_h_ast.h"
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors);); // Used editors because none of the memory allocated here will be done so by normal players

//////////////////////////////////////////////////////////////////////////
// Types

AUTO_STRUCT;
typedef struct MsgKeyFixupList {
	MsgKeyFixup **eaMsgKeyFixups;
} MsgKeyFixupList;

typedef enum MsgKeyFixupOptions {
	MSGKEYFIXUP_WRITE_FILES		= 1 << 0,
	MSGKEYFIXUP_APPLY_DICT		= 1 << 1,
	MSGKEYFIXUP_CHECKOUT_FILES	= 1 << 2,
} MsgKeyFixupOptions;

#define MSG_KEY_FIXUP_FILE "c:\\msgkeyfixup.txt"

//////////////////////////////////////////////////////////////////////////
// Globals & static globals
extern DictionaryHandle *gTranslationDicts;

//////////////////////////////////////////////////////////////////////////
// Functions

static void gslMsgFixupCheckoutFiles(MsgKeyFixup **ppMsgKeyFixups)
{
	MsgKeyFixup **failures = NULL;
	eaStackCreate(&failures, eaSize(&ppMsgKeyFixups));

	EARRAY_FOREACH_BEGIN(ppMsgKeyFixups, i);
	{
		const char *owner = gimmeDLLQueryIsFileLocked(ppMsgKeyFixups[i]->pFilename);
		if (owner)
		{
			if (gimmeDLLQueryIsFileLockedByMeOrNew(ppMsgKeyFixups[i]->pFilename))
			{
				printf("Have checked out: %s\n", ppMsgKeyFixups[i]->pFilename);
			}
			else
			{
				MsgKeyFixup *failure = StructCreate(parse_MsgKeyFixup);
				estrPrintf(&failure->pFilename, "%s", ppMsgKeyFixups[i]->pFilename);
				StructFreeString(failure->pcNewDescripton);
				failure->pcNewDescripton = StructAllocString(owner);
				eaPush(&failures, failure);
			}
		}
		else
		{
			GimmeErrorValue err = gimmeDLLDoOperation(ppMsgKeyFixups[i]->pFilename, GIMME_CHECKOUT, 0);
			switch(err)
			{
			case GIMME_NO_ERROR:
				printf("Checked out file: %s\n", ppMsgKeyFixups[i]->pFilename);
				break;
			case GIMME_ERROR_NO_SC:
			case GIMME_ERROR_NO_DLL:
			case GIMME_ERROR_FILENOTFOUND:
			case GIMME_ERROR_NOT_IN_DB:
				break;
			case GIMME_ERROR_ALREADY_CHECKEDOUT:
				{
					MsgKeyFixup *failure = StructCreate(parse_MsgKeyFixup);
					owner = gimmeDLLQueryIsFileLocked(ppMsgKeyFixups[i]->pFilename);
					estrPrintf(&failure->pFilename, "%s", ppMsgKeyFixups[i]->pFilename);
					StructFreeString(failure->pcNewDescripton);
					failure->pcNewDescripton = StructAllocString(owner);
					eaPush(&failures, failure);
				} break;
			default:
				printf("\"%s\" could not be checked out. Code: %d", ppMsgKeyFixups[i]->pFilename, err);
				break;
			}
		}
	}
	EARRAY_FOREACH_END;

	eaStableSortUsingColumn(&failures, parse_MsgKeyFixup, PARSE_MSGKEYFIXUP_NEWDESCRIPTON_INDEX );

	if (eaSize(&failures))
	{
		printf("\n\n\nThe following files could not be checked out:\n");
		EARRAY_FOREACH_BEGIN(failures, i);
		{
			printf("%s\t\t%s\n", failures[i]->pcNewDescripton, failures[i]->pFilename);
		}
		EARRAY_FOREACH_END;
	}

	eaDestroyStruct(&failures,parse_MsgKeyFixup);
}

static void gslMsgFixupListFiles(MsgKeyFixup **ppMsgKeyFixups)
{
	MsgKeyFixup **list = NULL;
	char * file = "";
	eaStackCreate(&list, eaSize(&ppMsgKeyFixups));
	eaCopyStructs(&ppMsgKeyFixups, &list, parse_MsgKeyFixup);

	eaStableSortUsingColumn(&list, parse_MsgKeyFixup, PARSE_MSGKEYFIXUP_FILENAME_INDEX);
	EARRAY_FOREACH_BEGIN(list, i);
	{
		forwardSlashes(list[i]->pFilename);
		if (stricmp(list[i]->pFilename, file))
		{
			file = list[i]->pFilename;
			printf("%s\n", file);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_FOREACH_REVERSE_BEGIN(list, i);
	{
		const char *owner = gimmeDLLQueryIsFileLocked(list[i]->pFilename);
		if (owner)
		{
			StructFreeString(list[i]->pcNewDescripton);
			list[i]->pcNewDescripton = StructAllocString(owner);
		}
		else
		{
			MsgKeyFixup *removed = eaRemove(&list, i);
			StructDestroy(parse_MsgKeyFixup, removed);
		}
	}
	EARRAY_FOREACH_END;

	eaStableSortUsingColumn(&list, parse_MsgKeyFixup, PARSE_MSGKEYFIXUP_NEWDESCRIPTON_INDEX );

	EARRAY_FOREACH_BEGIN(list, i);
	{
		printf("%s\t\t%s\n", list[i]->pcNewDescripton, list[i]->pFilename);
	}
	EARRAY_FOREACH_END;

	eaDestroyStruct(&list, parse_MsgKeyFixup);
}

static void gslMsgFixupSave(MsgKeyFixup ***peaMsgKeyFixups) {
	MsgKeyFixupList list = { 0 };
	StructInit(parse_MsgKeyFixupList, &list);

	eaCopyStructs(peaMsgKeyFixups, &(list.eaMsgKeyFixups), parse_MsgKeyFixup);
	ParserWriteTextFile(MSG_KEY_FIXUP_FILE, parse_MsgKeyFixupList, &list, 0, 0);

	printf("Wrote %d key fixups to %s\n", eaSize(peaMsgKeyFixups), MSG_KEY_FIXUP_FILE);
	StructDeInit(parse_MsgKeyFixupList, &list);
}

static void gslMsgFixupLoad(MsgKeyFixup ***peaMsgKeyFixups) {
	MsgKeyFixupList list = { 0 };
	StructInit(parse_MsgKeyFixupList, &list);

	ParserReadTextFile(MSG_KEY_FIXUP_FILE, parse_MsgKeyFixupList, &list, 0);
	eaCopyStructs(&(list.eaMsgKeyFixups), peaMsgKeyFixups, parse_MsgKeyFixup);

	printf("Read %d key fixups from %s\n", eaSize(peaMsgKeyFixups), MSG_KEY_FIXUP_FILE);
	StructDeInit(parse_MsgKeyFixupList, &list);
}

static void gslMsgFixupGetKey(char *pchLine, char **ppchKey) {
	const char *pchPrefix = "MessageKey ";
	char *pchSuffix = "\n";
	char *pchKeyStart = strstri(pchLine, pchPrefix);
	char *pchKeyEnd = strrstr(pchLine, pchSuffix);

	if (pchKeyStart) {
		pchKeyStart += strlen(pchPrefix);
	}

	estrClear(ppchKey);
	if (pchKeyStart && pchKeyEnd && pchKeyStart < pchKeyEnd) {
		estrConcat(ppchKey, pchKeyStart, pchKeyEnd-pchKeyStart);
	}
}

static void gslMsgFixupUpdateTranslationFile(const char *pchFilename, MsgKeyFixup ***peaMsgKeyFixups) {
	char backupFile[1024];
	char line[1024];
	char *pchExistingKey=NULL;
	FILE *fin;
	FILE *fout;
	StashTable stKeyFixups;
	S32 i=0;
	int errno;
	char pchRealFilename[1024];
	MsgKeyFixup *pFixup = NULL;

	if (!fileLocatePhysical(pchFilename, pchRealFilename)) {
		printf("File not found: %s\n", pchFilename);
		return;
	}
	backSlashes(pchRealFilename);

	if (!fileExists(pchRealFilename)) {
		return;
	}

	sprintf(backupFile, "%s.bak", pchRealFilename);
	if (fileExists(backupFile)) {
		fileForceRemove(backupFile);
	}
	
	if (errno=rename(pchRealFilename, backupFile)) {
		printf("Unable to rename '%s' to '%s'\n", pchRealFilename, backupFile);
		return;
	}

	printf("Fixing up %s\n", pchRealFilename);
	fin = fopen(backupFile, "r");
	if (!fin) {
		printf("Failed to open '%s' for reading.\n", backupFile);
		return;
	}
	fout = fopen(pchRealFilename, "w");
	if (!fin) {
		printf("Failed to open '%s' for writing.\n", pchRealFilename);
		return;
	}

	// Put key fixups in a stash for quick lookup
	stKeyFixups = stashTableCreateWithStringKeys(eaSize(peaMsgKeyFixups)*2+1, StashDefault);
	for (i=0; i < eaSize(peaMsgKeyFixups); i++) {
		MsgKeyFixup *pMsgKeyFixup = (*peaMsgKeyFixups)[i];
		stashAddPointer(stKeyFixups, pMsgKeyFixup->pcOldKey, pMsgKeyFixup, false);
	}
	
	while (fgets(line, 1024, fin)) {
		const char *pchStart = removeLeadingWhiteSpaces(line);
		if (strStartsWith(pchStart, "MessageKey ")) {
			const char *pchNewKey = NULL;

			gslMsgFixupGetKey(line, &pchExistingKey);
			if (!pchExistingKey || strlen(pchExistingKey) == 0) {
				printf("ERROR: Line has MessageKey, but isn't formatted precisely!  Line: '%s'\n", line);
				fprintf(fout, "%s", line); // Newline should already be included in 'line'
				continue;
			}

			if (stashFindPointer(stKeyFixups, pchExistingKey, &pFixup)) {
				pchNewKey = pFixup->pcNewKey;
			} else {
				// Key didn't exist, don't fix up.
				pchNewKey = pchExistingKey;
			}

			fwrite(line, 1, pchStart-line, fout); // Preserve indent
			fprintf(fout, "MessageKey %s\n", pchNewKey);
		} else if (pchExistingKey && *pchExistingKey && strStartsWith(pchStart, "Description ")) {
			if (pFixup && pFixup->pcNewDescripton && *pFixup->pcNewDescripton) {
				fwrite(line, 1, pchStart-line, fout); // Preserve indent
				fprintf(fout, "Description %s\n", pFixup->pcNewDescripton);
			} else {
				fprintf(fout, "%s", line); // Newline should already be included in 'line'
			}
		} else {
			fprintf(fout, "%s", line); // Newline should already be included in 'line'
		}
	}
	fclose(fin);
	fclose(fout);

	stashTableDestroy(stKeyFixups);
}

// Translation file creation is bad voodoo.
// We can't actually recreate one with the text parser
// libraries because the files contain fields that
// don't normally exist and much of the stuff in there
// has been hand coded.  So, what we do is look for
// message keys that happen to match the modified ones and
// do a simple text swap of those keys.  This has the advantage
// of creating the fewest diffs possible (due to message relocation
// and or field relocation) so it's easy to verify that only the
// translated keys have been updated.
void gslMsgFixupApplyTranslationFixups(MsgKeyFixup ***peaMsgKeyFixups, bool bDryRun) {
	const char *pchClientFilePrefix = "messages\\ClientMessages";
	const char *pchServerFilePrefix = "defs\\ServerMessages";
	char currentDir[1024];

	S32 iNumLocales = locGetMaxLocaleCount();
	S32 i;

	if (!getcwd(currentDir, 1024)) {
		printf("Unable to get current directory.\n");
		return;
	}

	printf("Current directory: %s\n", currentDir);

	for (i=0; i < iNumLocales; i++) {
		char pchFilename[1024];
		const char *pchLocale = locGetName(i);

		// Update translated client message keys
		sprintf(pchFilename, "%s.%s.translation", pchClientFilePrefix, pchLocale);
		gslMsgFixupUpdateTranslationFile(pchFilename, peaMsgKeyFixups);

		// Update translated client message keys
		sprintf(pchFilename, "%s.%s.translation", pchServerFilePrefix, pchLocale);
		gslMsgFixupUpdateTranslationFile(pchFilename, peaMsgKeyFixups);
	}
}


static bool gslMsgFixupIsTerseKey(const char *pchKey) {
	// A terse key is one or two letters followed by a period and then any number of other characters.
	const char *pcCur = pchKey;
	const char *dot;

	if (!pcCur || !*pcCur) {
		return false;
	}
	dot = strchr(pchKey, '.');

	if (!dot || dot > pchKey + 2 || dot == pcCur)
	{	//we accept 1 or 2 characters before the required dot. Dot can't be first.
		return false;
	}

	while (pcCur < dot)
	{	//increment past the prefix characters
		if (!isalpha(*pcCur))
			return false;
		pcCur++;
	}
	pcCur++;

	//we need at least one suffix character.
	if (!*pcCur)
		return false;

	while (*pcCur)
	{
		if (!*pcCur)
			break;
		if (!msgCharIsEncoded(*pcCur))
			return false;
		pcCur++;
	}

	return true;
}

static void gslMsgFixupCostumeMsgKeys(MsgKeyFixup ***peaMsgKeyFixups, 
									  DictionaryHandle hDict, ParseTable subCostumePti[], 
									  const char *pcKeyPrefix, const char *pcSubCostumePart, 
									  U32 options, U32 iMaxFixup,
									  Message *(*getDisplayMessage)(void*),
									  const char *(*getMessageKeySeed)(void*),
									  const char *(*getNameFunc)(void*), 
									  const char *(*getFilenameFunc)(void*),
									  Message *(*fixupKeyFunc)(void *, const char *)) {
	const char *pcNoSave = (MSGKEYFIXUP_WRITE_FILES & ~options) ? "(No Save) " : "";
	U32 iTotalCount = 0;
	U32 iConversionCount = 0;
	U32 iBadKeyCount = 0;
	U32 iTotalOldBytes = 0;
	U32 iTotalNewBytes = 0;
	U32 iSkipCount = 0;

	printf("Fixup %s\n", pcSubCostumePart);

	FOR_EACH_IN_REFDICT(hDict, void, pOrigDef)
	{
		Message *pOldMsg = getDisplayMessage(pOrigDef);
		const char *pcOldKey = pOldMsg ? pOldMsg->pcMessageKey : NULL;
		iTotalCount++;
		if (pcOldKey && !gslMsgFixupIsTerseKey(pcOldKey)) {
			const char *pcNewKeySeed = getMessageKeySeed(pOrigDef);
			const char *pcNewKey = msgCreateUniqueKey(pcKeyPrefix, pcNewKeySeed, NULL);

			if (!(pcNewKey && *pcNewKey))
			{
				const char *pcName = getNameFunc(pOrigDef);
				printf("WARNING: Bad message key for Costume %s: %s\n", pcSubCostumePart, pcName);
				iBadKeyCount++;
				continue;
			}
			else if (iMaxFixup > 0 && iConversionCount >= iMaxFixup)
			{
				iSkipCount++;
				continue;
			}
			else if (options & MSGKEYFIXUP_CHECKOUT_FILES)
			{
				const char *pcFilename = getFilenameFunc(pOrigDef);
				MsgKeyFixup *pMsgKeyFixup = StructAlloc(parse_MsgKeyFixup);

				estrPrintf(&pMsgKeyFixup->pFilename, "%s", pcFilename);
				eaPush(peaMsgKeyFixups, pMsgKeyFixup);
			}
			else if (pOrigDef && (options & MSGKEYFIXUP_APPLY_DICT))
			{
				void *pWorkingCopy = StructCloneVoid(subCostumePti, pOrigDef);
				MsgKeyFixup *pMsgKeyFixup = StructAlloc(parse_MsgKeyFixup);
				const char *pcName = getNameFunc(pOrigDef);
				const char *pcFilename = getFilenameFunc(pWorkingCopy);
				Message *pNewMsg;

				langMakeEditorCopy(subCostumePti, pWorkingCopy, false);
				pNewMsg = fixupKeyFunc(pWorkingCopy, pcNewKey);
				langApplyEditorCopy2(subCostumePti, pWorkingCopy, pOrigDef, false, (MSGKEYFIXUP_WRITE_FILES & ~options), false);
				if (MSGKEYFIXUP_WRITE_FILES & options) {
					ParserWriteTextFileFromSingleDictionaryStruct(pcFilename, hDict, pWorkingCopy, 0, 0);
				}
				StructDestroyVoid(subCostumePti, pWorkingCopy);

				pMsgKeyFixup->pcOldKey = pcOldKey;
				pMsgKeyFixup->pcNewKey = pcNewKey;
				pMsgKeyFixup->pcNewDescripton = StructAllocString(pNewMsg->pcDescription);
				estrPrintf(&pMsgKeyFixup->pFilename, "%s", pcFilename);
				eaPush(peaMsgKeyFixups, pMsgKeyFixup);

				iTotalNewBytes += (U32) strlen(pcNewKey) + 1;
				iTotalOldBytes += (U32) strlen(pcOldKey) + 1;
				//printf("%sConverted %s key %-14s from %s (%s)\n", pcNoSave, pcSubCostumePart, pcNewKey, pcOldKey, pcFilename);
				//printf("%d) %sConverted %s key %-10s from %s\n", iConversionCount, pcNoSave, pcSubCostumePart, pcNewKey, pcOldKey);
				printf("%d\r", iConversionCount);
				iConversionCount++;

			}
		}
	}
	FOR_EACH_END;

	if (options & MSGKEYFIXUP_APPLY_DICT)
	{
		printf("%sCostume %s key conversion complete.  Converted %u of %u keys (%u bad) [%u skipped].\n",
			pcNoSave, pcSubCostumePart, iConversionCount, iTotalCount, iBadKeyCount, iSkipCount);
		printf("Bytes - Total Old: %u  Total New: %u  Total Savings: %u\n", iTotalOldBytes, iTotalNewBytes, iTotalOldBytes-iTotalNewBytes);
	}
}

static Message *getMsgFixupCostumeGeometryDisplayMessage(PCGeometryDef *pDef) {
	return GET_REF(pDef->displayNameMsg.hMessage);
}

static const char *gslMsgFixupCostumeGeometryGetMessageKeySeed(PCGeometryDef *pDef) {
	static char *pchTmp = NULL;
	estrPrintf(&pchTmp, "Costume.Geometry.%s", pDef->pcName);
	return pchTmp;
}

static const char *gslMsgFixupCostumeGeometryGetName(PCGeometryDef *pDef) {
	return pDef->pcName;
}

static const char *gslMsgFixupCostumeGeometryGetFilename(PCGeometryDef *pDef) {
	return pDef->pcFileName;
}

static Message *gslMsgFixupCostumeGeometryFixupMsgKey(PCGeometryDef *pWorkingCopy, const char *pcNewKey) {
	char buf[1024];
	pWorkingCopy->displayNameMsg.pEditorCopy->pcMessageKey = pcNewKey;

	sprintf(buf, "Costume geometry name for %s", pWorkingCopy->pcName);
	StructFreeStringSafe(&pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription);
	pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);

	return pWorkingCopy->displayNameMsg.pEditorCopy;
}

static void gslMsgFixupCostumeGeometryMsgKeys(MsgKeyFixup ***peaMsgKeyFixups, U32 options, U32 iMaxFixup) {
	gslMsgFixupCostumeMsgKeys(peaMsgKeyFixups, 
		g_hCostumeGeometryDict, parse_PCGeometryDef, 
		MKP_COSTUME_GEO, "Geo", options, iMaxFixup,
		getMsgFixupCostumeGeometryDisplayMessage,
		gslMsgFixupCostumeGeometryGetMessageKeySeed,
		gslMsgFixupCostumeGeometryGetName,
		gslMsgFixupCostumeGeometryGetFilename,
		gslMsgFixupCostumeGeometryFixupMsgKey);
}

static Message *getMsgFixupCostumeMaterialDisplayMessage(PCMaterialDef *pDef) {
	return GET_REF(pDef->displayNameMsg.hMessage);
}

static const char *gslMsgFixupCostumeMaterialGetMessageKeySeed(PCGeometryDef *pDef) {
	static char *pchTmp = NULL;
	estrPrintf(&pchTmp, "Costume.Material.%s", pDef->pcName);
	return pchTmp;
}

static const char *gslMsgFixupCostumeMaterialGetName(PCMaterialDef *pDef) {
	return pDef->pcName;
}

static const char *gslMsgFixupCostumeMaterialGetFilename(PCMaterialDef *pDef) {
	return pDef->pcFileName;
}

static Message* gslMsgFixupCostumeMaterialFixupMsgKey(PCMaterialDef *pWorkingCopy, const char *pcNewKey) {
	char buf[1024];
	pWorkingCopy->displayNameMsg.pEditorCopy->pcMessageKey = pcNewKey;

	sprintf(buf, "Costume material name for %s", pWorkingCopy->pcName);
	StructFreeStringSafe(&pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription);
	pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);

	return pWorkingCopy->displayNameMsg.pEditorCopy;
}

static void gslMsgFixupCostumeMaterialMsgKeys(MsgKeyFixup ***peaMsgKeyFixups, U32 options, U32 iMaxFixup) {
	gslMsgFixupCostumeMsgKeys(peaMsgKeyFixups, 
		g_hCostumeMaterialDict, parse_PCMaterialDef, 
		MKP_COSTUME_MAT, "Mat", options, iMaxFixup,
		getMsgFixupCostumeMaterialDisplayMessage,
		gslMsgFixupCostumeMaterialGetMessageKeySeed,
		gslMsgFixupCostumeMaterialGetName,
		gslMsgFixupCostumeMaterialGetFilename,
		gslMsgFixupCostumeMaterialFixupMsgKey);
}

static Message *getMsgFixupCostumeTextureDisplayMessage(PCTextureDef *pDef) {
	return GET_REF(pDef->displayNameMsg.hMessage);
}

static const char *gslMsgFixupCostumeTextureGetMessageKeySeed(PCGeometryDef *pDef) {
	static char *pchTmp = NULL;
	estrPrintf(&pchTmp, "Costume.Texture.%s", pDef->pcName);
	return pchTmp;
}

static const char *gslMsgFixupCostumeTextureGetName(PCTextureDef *pDef) {
	return pDef->pcName;
}

static const char *gslMsgFixupCostumeTextureGetFilename(PCTextureDef *pDef) {
	return pDef->pcFileName;
}

static Message* gslMsgFixupCostumeTextureFixupMsgKey(PCTextureDef *pWorkingCopy, const char *pcNewKey) {
	char buf[1024];
	pWorkingCopy->displayNameMsg.pEditorCopy->pcMessageKey = pcNewKey;

	sprintf(buf, "Costume texture name for %s", pWorkingCopy->pcName);
	StructFreeStringSafe(&pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription);
	pWorkingCopy->displayNameMsg.pEditorCopy->pcDescription = StructAllocString(buf);

	return pWorkingCopy->displayNameMsg.pEditorCopy;
}

static void gslMsgFixupCostumeTextureMsgKeys(MsgKeyFixup ***peaMsgKeyFixups, U32 options, U32 iMaxFixup) {
	gslMsgFixupCostumeMsgKeys(peaMsgKeyFixups, 
		g_hCostumeTextureDict, parse_PCTextureDef, 
		MKP_COSTUME_TEX, "Tex", options, iMaxFixup,
		getMsgFixupCostumeTextureDisplayMessage,
		gslMsgFixupCostumeTextureGetMessageKeySeed,
		gslMsgFixupCostumeTextureGetName,
		gslMsgFixupCostumeTextureGetFilename,
		gslMsgFixupCostumeTextureFixupMsgKey);
}

static void gslMsgFixupMsgKeys(U32 options, U32 iMaxFixup) {
	MsgKeyFixup **eaMsgKeyFixups = NULL;

	resSetDictionaryEditModeServer(gMessageDict, true);
	resSetDictionaryEditModeServer(g_hCostumeGeometryDict, true);

	gslMsgFixupCostumeGeometryMsgKeys(&eaMsgKeyFixups, options, iMaxFixup);
	gslMsgFixupCostumeMaterialMsgKeys(&eaMsgKeyFixups, options, iMaxFixup);
	gslMsgFixupCostumeTextureMsgKeys(&eaMsgKeyFixups, options, iMaxFixup);

	if (options & MSGKEYFIXUP_CHECKOUT_FILES)
	{
		gslMsgFixupCheckoutFiles(eaMsgKeyFixups);
	}
	else
	{
		gslMsgFixupListFiles(eaMsgKeyFixups);
		gslMsgFixupSave(&eaMsgKeyFixups);
		gslMsgFixupApplyTranslationFixups(&eaMsgKeyFixups, (MSGKEYFIXUP_WRITE_FILES & ~options));
	}


	eaClearStruct(&eaMsgKeyFixups, parse_MsgKeyFixup);
}

AUTO_COMMAND;
void FixupMsgKeys(bool bDoSave, U32 iMaxFixup) {
	time_t start;
	time_t finish;
	U32 iDuration;
	U32 iHour;
	U32 iMin;
	U32 iSec;
	U32 options = MSGKEYFIXUP_APPLY_DICT;

	if (bDoSave)
		options |= MSGKEYFIXUP_WRITE_FILES;

	time(&start);
	gslMsgFixupMsgKeys(options, iMaxFixup);
	time(&finish);
	iDuration = (U32) difftime(finish, start);
	iSec = iDuration % 60;
	iMin = (iDuration/60) % 60;
	iHour = iDuration/3600;

	printf("Duration: %d:%02d:%02d\n", iHour, iMin, iSec);
}

AUTO_COMMAND;
void CheckoutFilesForMsgKeyFixup(U32 iMaxFixup) {
	gslMsgFixupMsgKeys(MSGKEYFIXUP_CHECKOUT_FILES, iMaxFixup);
}

static void gslMsgFixupTranslatedMsgKeys(bool bDryRun) {
	MsgKeyFixup **eaMsgKeyFixups = NULL;

	gslMsgFixupLoad(&eaMsgKeyFixups);
	gslMsgFixupApplyTranslationFixups(&eaMsgKeyFixups, bDryRun);

	eaClearStruct(&eaMsgKeyFixups, parse_MsgKeyFixup);
}


AUTO_COMMAND;
void FixupTranslatedMsgKeys(bool bDoSave) {
	time_t start;
	time_t finish;
	U32 iDuration;
	U32 iHour;
	U32 iMin;
	U32 iSec;

	time(&start);
	gslMsgFixupTranslatedMsgKeys(!bDoSave);
	time(&finish);
	iDuration = difftime(finish, start);
	iSec = iDuration % 60;
	iMin = (iDuration/60) % 60;
	iHour = iDuration/3600;

	printf("Duration: %d:%02d:%02d\n", iHour, iMin, iSec);
}

#include "AutoGen/gslMessageFixup_c_ast.c"
#include "AutoGen/gslMessageFixup_h_ast.c"