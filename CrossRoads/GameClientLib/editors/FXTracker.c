#include "mathutil.h"
#include "Prefs.h"
#include "cmdparse.h"
#include "utilitiesLib.h"
#include "sysutil.h"
#include "crypt.h"

#include "UIModalDialog.h"
#include "UIInternal.h"
#include "UITextEntry.h"
#include "UIExpander.h"
#include "UICheckButton.h"
#include "UIComboBox.h"
#include "UIList.h"
#include "UISMFView.h"
#include "UILabel.h"
#include "gfxspritetext.h"
#include "GraphicsLib.h"
#include "UIColorButton.h"
#include "UIAutoWidget.h"
#include "UIColorSlider.h"
#include "UISkin.h"
#include "UISlider.h"
#include "UITabs.h"
#include "UIPane.h"
#include "UITree.h"
#include "UIMenu.h"
#include "UITextArea.h"
#include "UIProgressBar.h"

#include "GraphicsLib.h"
#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "CBox.h"
#include "GfxPrimitive.h"
#include "GfxFont.h"
#include "StringUtil.h"

#include "dynSeqData.h"
#include "dynSequencer.h"
#include "dynSkeleton.h"
#include "StringCache.h"
#include "dynFxDebug.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "file.h"

#include "ResourceInfo.h"

#include "gimmeDLLWrapper.h"

#include "PowerAnimFX.h"

#include "UIScrollbar.h"

#include "ThreadManager.h"

#if !PLATFORM_CONSOLE
#include <Windows.h>
#include <ShellAPI.h>
#endif

#include "FxTracker_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:fxTrackerLoadFilesThread", BUDGET_Editors););

static void dynDrawDebugTick(F32 fDeltaTime);
static void dfxDisplayRecording(void);

char ***dfxAssetChoices();
char ***dfxPriorityChoices();

typedef bool (*DfxSearchFunc)(const char *srcStr, const char *searchStr);
static bool dfxSearchStringContains(const char *srcStr, const char *searchStr );
static bool dfxSearchStringWildcard(const char *srcStr, const char *searchStr );


AUTO_RUN;
void FXTrackerSetCallbacks(void)
{
	dynFxDebugSetCallbacks(dynDrawDebugTick, dfxDisplayRecording);
}

typedef struct DfxFileBuffer 
{
	char *pchBuffer;
	int iMaxSize;
	int iSize;
	U8 bRead : 1;
} DfxFileBuffer;

static DfxFileBuffer s_dfxFileBuffer = { NULL, 0, 0 };


AUTO_STRUCT;
typedef struct FXTrackerSettings
{
	F32 fOpacity;
} FXTrackerSettings;

typedef struct DfxTreeNodeData 
{
	char *pchName;
	
	REF_TO(DynFxInfo) hInfo;

	DfxFileBuffer dfxFileBuffer;

	struct DfxTreeNodeData **ppChildren;
	struct DfxTreeNodeData *pParentNode;

	U8 bIsVisible : 1; // determines whether the node will be displayed in the tree
} DfxTreeNodeData;

typedef enum eAssetType
{
	eAssetType_Audio,
	eAssetType_Geometry,
	eAssetType_Material,
	eAssetType_ParticleEmitter,
	eAssetType_Texture,
	eAssetType_EndOfList
} eAssetType;

#define MaxFilterStringLength (127)

typedef struct DfxTreeFilter
{
	char pchName[MaxFilterStringLength+1];
	char pchAssetName[MaxFilterStringLength+1];
	char pchFileContents[MaxFilterStringLength+1];
	eDynPriority priority;
	eAssetType assetType;
	U8 bRequiresAssetType : 1;
	U8 bAssetNameFilter : 1;
	U8 bRequiresPriority : 1;
	U8 bOnlyLeafNodes : 1;
	U8 bSearchFileContents : 1;

	// search functions
	DfxSearchFunc nameSearchFunc;
	DfxSearchFunc assetSearchFunc;
	DfxSearchFunc fileContentsFunc;

} DfxTreeFilter;

typedef struct DfxPowerArtData
{
	char *pchName;
	REF_TO(PowerAnimFX) hPowerAnimFx;

} DfxPowerArtData;

typedef enum eDfxPowerArtFxType
{
	ePowerArtFx_StanceStickyFX,
	ePowerArtFx_StanceFlashFX,
	ePowerArtFx_ChargeStickyFX,
	ePowerArtFx_ChargeFlashFX,
	ePowerArtFx_LungeStickyFX,
	ePowerArtFx_LungeFlashFX,
	ePowerArtFx_ActivateStickyFX,
	ePowerArtFx_ActivateFX,
	ePowerArtFx_ActivateMissFX,
	ePowerArtFx_PeriodicActivateFX,
	ePowerArtFx_DeactivateFX,
	ePowerArtFx_TargetedFX,
	ePowerArtFx_HitFX,
	ePowerArtFx_HitStickyFX,
	ePowerArtFx_BlockFX,
	ePowerArtFx_DeathFX,
	ePowerArtFx_EndOfList
} eDfxPowerArtFxType;

typedef struct DfxPowerArtFilter
{
	char pchName[MaxFilterStringLength+1];
	char pchFxName[MaxFilterStringLength+1];
	eDfxPowerArtFxType iType;
	U8 bRequiresFx : 1;
} DfxPowerArtFilter;

typedef struct DfxPowerArtFx
{
	char *pchName;
	char *pchFxName;
	eDfxPowerArtFxType iType;
} DfxPowerArtFx;

static struct
{
	UIWindow* pWindow;
	UISkin* pSkin;
	UIRebuildableTree* pTree;
	RdrDevice* pFxDebugDevice;
	UIList* pDebugUpdateList;
	UIList* pDebugDrawList;
	bool bPopOut;

	UITab *pInstancesTab;
	UITab *pHierarchyTab;
	UITab *pPowerArtTab;
	UITabGroup *pTabGroup;

	UIPane *pInstancesPane;
	UIPane *pHierarchyPane;
	UIPane *pPowerArtPane;

	//
	// Fx Hierarchy
	//
	UITree *pFxTree;
	DfxTreeNodeData *pTreeRoot;


	UITextEntry *pFilterTextEntry;
	UICheckButton *pExpandAllButton;
	UIList *pFxInfoPropertyList;
	UILabel *pFxNameLabel;

	UITreeNode *pSelectedTreeNode;
	UITabGroup *pAssetTabGroup;

	UICheckButton *pFileContentsToggle;
	UITextEntry *pFileContentsEntry;

	UITree *pAssetTree;

	UICheckButton *pReferencesAssetToggle;
	UIComboBox *pSelectedAsset;
	//UICheckButton *pAssetNameFilterToggle;
	UITextEntry *pAssetFilterEntry;

	UICheckButton *pPriorityToggle;
	UIComboBox *pSelectedPriority;

	UICheckButton *pOnlyLeafNodesToggle;

	UITabGroup *pPropertyTabGroup;
	UITab *pPropertiesTab;
	UITab *pFxSourceTab;
	UITab *pAssetsTab;

	DfxTreeFilter dfxTreeFilter;
	UIMenu *pRightClickMenu;

	UILabel *pNumSelectedLabel;

	UITextArea *pFxSourceTextArea;

	UIButton *pPlayEffectButton;

	//
	// PowerArt
	//
	UILabel *pNumPowerArtFilteredLabel;

	UIList *pPowerArtList;
	UITextEntry *pPowerArtFilterTextEntry;
	UIList *pPowerArtFxList;
	UIComboBox *pPowerArtFxFilterType;
	UITextEntry *pPowerArtFxFilterNameEntry;
	UICheckButton *pPowerArtFilterTypeToggle;

	DfxPowerArtData **ppFilteredPowerArtList; // filtered results
	DfxPowerArtData **ppPowerArtList; // holds all
	DfxPowerArtFx **ppPowerArtFxList;

	UILabel *pSelectedPowerArtName;
	DfxPowerArtFilter dfxPowerArtFilter;


	// thread for reading files
	ManagedThread *pManagedFileLoadingThread;
	U32 iLoadingProgress;
	U32 iTotalNumFx;
	UIProgressBar *pLoadingProgressBar;
	UIButton *pCancelLoadButton;
	UILabel *pLoadingLabel;

	U32 bKillLoadingThread : 1;
	U32 bDoneLoadingThread : 1;
	U32 bWindowActive : 1;
} fxTracker;


typedef void (*FieldDisplayFunc)(void *data, char **estrOutput);

void dfxFieldPriorityFormat(void *data, char **estrOutput)
{
	eDynPriority priority = *((eDynPriority*)data);
	switch(priority)
	{
	case edpOverride: estrPrintf(estrOutput, "Override"); break;
	case edpCritical: estrPrintf(estrOutput, "Critical"); break;
	case edpDefault: estrPrintf(estrOutput, "Default"); break;
	case edpDetail: estrPrintf(estrOutput, "Detail"); break;
	default:
	case edpNotSet: estrPrintf(estrOutput, "Not Set"); break;
	}
}

void dfxFieldBoolFormat(void *data, char **estrOutput)
{
	bool val = *((bool*)data);
	if(val)
	{
		estrPrintf(estrOutput, "true");
	}
	else
	{
		estrPrintf(estrOutput, "false");
	}
}

void dfxFieldStringFormat(void *data, char **estrOutput)
{
	char* str = *((char**)data);
	if(str)
	{
		estrPrintf(estrOutput, "%s", str);
	}
}


typedef struct 
{
	char pchName[64];
	size_t offset;
	char pchFormat[16];
	FieldDisplayFunc fpFunc;
} FieldProperty;

FieldProperty dynFxInfoFieldProperties[] = {
	{"Priority Level", OFFSETOF(DynFxInfo, iPriorityLevel), "", dfxFieldPriorityFormat},
	{"Filename", OFFSETOF(DynFxInfo, pcFileName), "", dfxFieldStringFormat},

	{"Draw Distance", OFFSETOF(DynFxInfo, fDrawDistance), "%.3f", NULL},
	{"Fade Distance", OFFSETOF(DynFxInfo, fFadeDistance), "%.3f", NULL},
	{"Min Fade Distance", OFFSETOF(DynFxInfo, fMinFadeDistance), "%.3f", NULL},
	{"Min Draw Distance", OFFSETOF(DynFxInfo, fMinDrawDistance), "%.3f", NULL},
	{"Radius", OFFSETOF(DynFxInfo, fRadius), "%.3f", NULL},
	{"Default Hue", OFFSETOF(DynFxInfo, fDefaultHue), "%.3f", NULL},
	{"Playback Jitter", OFFSETOF(DynFxInfo, fPlaybackJitter), "%.3f", NULL},

	{"Suppression Tag", OFFSETOF(DynFxInfo, pcSuppressionTag), "%s", NULL},
	{"IK Target Tag", OFFSETOF(DynFxInfo, pcIKTargetTag), "%s", NULL},
	{"IK Target Bone", OFFSETOF(DynFxInfo, eaIKTargetBone), "%s", NULL},

	{"Don't Draw", OFFSETOF(DynFxInfo, bDontDraw), "", dfxFieldBoolFormat},
	{"Force Don't Draw", OFFSETOF(DynFxInfo, bForceDontDraw), "", dfxFieldBoolFormat},

	{"VerifyFailed", OFFSETOF(DynFxInfo, bVerifyFailed), "", dfxFieldBoolFormat},
	{"Self Terminates", OFFSETOF(DynFxInfo, bSelfTerminates), "", dfxFieldBoolFormat},
	{"Kill if Orphaned", OFFSETOF(DynFxInfo, bKillIfOrphaned), "", dfxFieldBoolFormat},
	{"Don't Hue Shfit", OFFSETOF(DynFxInfo, bDontHueShift), "", dfxFieldBoolFormat},
	{"Don't Hue Shift Children", OFFSETOF(DynFxInfo, bDontHueShiftChildren), "", dfxFieldBoolFormat},
	{"Has Auto Events", OFFSETOF(DynFxInfo, bHasAutoEvents), "", dfxFieldBoolFormat},
	{"Debug Fx", OFFSETOF(DynFxInfo, bDebugFx), "", dfxFieldBoolFormat},
	{"Forward Messages", OFFSETOF(DynFxInfo, bForwardMessages), "", dfxFieldBoolFormat},
	{"No Alpha Inherit", OFFSETOF(DynFxInfo, bNoAlphaInherit), "", dfxFieldBoolFormat},
	{"Unique", OFFSETOF(DynFxInfo, bUnique), "", dfxFieldBoolFormat},
	{"Force 2d", OFFSETOF(DynFxInfo, bForce2D), "", dfxFieldBoolFormat},
	{"Local Player Only", OFFSETOF(DynFxInfo, bLocalPlayerOnly), "", dfxFieldBoolFormat},
	{"Low Res", OFFSETOF(DynFxInfo, bLowRes), "", dfxFieldBoolFormat},
	{"Ent Needs Aux Pass", OFFSETOF(DynFxInfo, bEntNeedsAuxPass), "", dfxFieldBoolFormat},
	{"Don't Leak Test", OFFSETOF(DynFxInfo, bDontLeakTest), "", dfxFieldBoolFormat},
	{"Inherit Playback Speed", OFFSETOF(DynFxInfo, bInheritPlaybackSpeed), "", dfxFieldBoolFormat},
	{"Hibernate", OFFSETOF(DynFxInfo, bHibernate), "", dfxFieldBoolFormat},

	{"", (size_t)0, "", NULL} // terminate
};

static void dfxTreeDisplayText(UITreeNode *node, const char *text, const char *highlightText, U32 uiColor, Color highlightColor, UI_MY_ARGS, F32 z);
static void dfxTreeExpandAllNodes(UITreeNode *pTreeNode);
static void dfxTreeUpdateFilter(bool bForceUpdate);

static void dfxPowerArtUpdateFilter();
static const char *dfxPowerArtFxNameByType(eDfxPowerArtFxType iType);
static void dfxPowerArtPowerAnimFXByType(PowerAnimFX *pPowerAnimFX, eDfxPowerArtFxType iType, const char ***pppcNames);

static void dfxEditFile(const char *pcFileName);
static void fxTrackerUpdateLoadingStatus();



static DWORD WINAPI fxTrackerLoadFilesThread(LPVOID lpParam);

void dfxTreePlayEffectCB(UIAnyWidget *pWidget, UserData userData);

////////////////////////////////////////////////////////////////////////////
//  
//   Update Debug Section
//
///////////////////////////////////////////////////////////////////////////

static void sortByNameReverse(UIListColumn* column, void* userData);
static void sortByName(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_Name);
	ui_ListColumnSetClickedCallback(column, sortByNameReverse, NULL);
}

static void sortByNameReverse(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_NameReverse);
	ui_ListColumnSetClickedCallback(column, sortByName, NULL);
}

static void sortByNumReverse(UIListColumn* column, void* userData);
static void sortByNum(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_Num);
	ui_ListColumnSetClickedCallback(column, sortByNumReverse, NULL);
}

static void sortByNumReverse(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_NumReverse);
	ui_ListColumnSetClickedCallback(column, sortByNum, NULL);
}

static void sortByPeakReverse(UIListColumn* column, void* userData);
static void sortByPeak(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_Peak);
	ui_ListColumnSetClickedCallback(column, sortByPeakReverse, NULL);
}

static void sortByPeakReverse(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_PeakReverse);
	ui_ListColumnSetClickedCallback(column, sortByPeak, NULL);
}

static void sortByMemReverse(UIListColumn* column, void* userData);
static void sortByMem(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_Mem);
	ui_ListColumnSetClickedCallback(column, sortByMemReverse, NULL);
}

static void sortByMemReverse(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_MemReverse);
	ui_ListColumnSetClickedCallback(column, sortByMem, NULL);
}

static void sortByLevelReverse(UIListColumn* column, void* userData);
static void sortByLevel(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_Level);
	ui_ListColumnSetClickedCallback(column, sortByLevelReverse, NULL);
}

static void sortByLevelReverse(UIListColumn* column, void* userData)
{
	dynFxDebugSort(eDebugSortMode_LevelReverse);
	ui_ListColumnSetClickedCallback(column, sortByLevel, NULL);
}

static void killAllFx(UIButton* button, void* userData)
{
	dfxKillAll();
}

static void autoClear(UICheckButton* button, void* userData)
{
	dynDebugState.bAutoClearTrackers = button->state;
}

static void noCostumeFX(UICheckButton* button, void* pSource)
{
	dfxNoCostumeFX(!button->state);
}

static void noUIFX(UICheckButton* button, void* pSource)
{
	dfxNoUIFX(!button->state);
}

static void noEnvironmentFX(UICheckButton* button, void* pSource)
{
	dfxNoEnvironmentFX(!button->state);
}

static void fxQualityCBSelected(UIComboBox* pComboBox, void* pUnused)
{
	dynDebugState.iFxQuality = CLAMP(pComboBox->iSelected, 0, 2);
}

static void recordFunc(UIComboBox* pComboBox, void* pUnused)
{
	dfxRecord();

}

static void hueOverride(UICheckButton* button, void* userData)
{
	dynDebugState.bGlobalHueOverride = button->state;
}

static void killSelected(UIButton* button, void* userData)
{
	UIList* pList = (UIList*)userData;

	const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(pList);
	if (peaiMultiSelected)
	{
		const U32 uiNum = eaiSize(peaiMultiSelected);
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiNum; ++uiIndex)
		{
			const S32 iListIndex = (*peaiMultiSelected)[uiIndex];
			DynFxTracker* pFxTracker;
			if (iListIndex < 0)
				continue;
			pFxTracker = (DynFxTracker*)((*pList->peaModel)[iListIndex]);
			dtFxManStopUsingFxInfo(0, pFxTracker->pcFxName, false);
		}
	}
}

static void debugSelected(UIButton* button, void* userData)
{
	UIList* pList = (UIList*)userData;

	const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(pList);
	if (peaiMultiSelected)
	{
		const U32 uiNum = eaiSize(peaiMultiSelected);
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiNum; ++uiIndex)
		{
			const S32 iListIndex = (*peaiMultiSelected)[uiIndex];
			DynFxTracker* pFxTracker;
			if (iListIndex < 0)
				continue;
			pFxTracker = (DynFxTracker*)((*pList->peaModel)[iListIndex]);
			dynFxInfoToggleDebug(pFxTracker->pcFxName);
		}
	}
	globCmdParse("dfxLogger 1");
}

static void editSelected(UIButton* button, void* userData)
{
	UIList* pList = (UIList*)userData;

	const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(pList);
	if (peaiMultiSelected)
	{
		const U32 uiNum = eaiSize(peaiMultiSelected);
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiNum; ++uiIndex)
		{
			const S32 iListIndex = (*peaiMultiSelected)[uiIndex];
			DynFxTracker* pFxTracker;
			const char* pcFileName;
			if (iListIndex < 0)
				continue;
			pFxTracker = (DynFxTracker*)((*pList->peaModel)[iListIndex]);
			pcFileName = dynFxInfoGetFileName(pFxTracker->pcFxName);
			if (pcFileName)
			{
				char cFileNameBuf[512];
				if (fileLocateWrite(pcFileName, cFileNameBuf))
				{
					fileOpenWithEditor(cFileNameBuf);
				}
				else
				{
					Errorf("Could not find file %s for editing", pcFileName);
				}
			}
			else
				Errorf("Could not find DynFxInfo for FX %s", pFxTracker->pcFxName);
		}
	}
}

static void openDirOfSelected(UIButton* button, void* userData)
{
#if !PLATFORM_CONSOLE

	UIList* pList = (UIList*)userData;

	const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(pList);
	if (peaiMultiSelected)
	{
		const U32 uiNum = eaiSize(peaiMultiSelected);
		U32 uiIndex;
		for (uiIndex=0; uiIndex<uiNum; ++uiIndex)
		{
			const S32 iListIndex = (*peaiMultiSelected)[uiIndex];
			DynFxTracker* pFxTracker;
			const char* pcFileName;
			if (iListIndex < 0)
				continue;
			pFxTracker = (DynFxTracker*)((*pList->peaModel)[iListIndex]);
			pcFileName = dynFxInfoGetFileName(pFxTracker->pcFxName);
			if (pcFileName)
			{
				char cFileNameBuf[512];
				if (fileLocateWrite(pcFileName, cFileNameBuf))
				{
					char* directory;
					forwardSlashes(cFileNameBuf);

					directory = strrchr(cFileNameBuf, '/');
					if (!directory)
						return;
					*directory = '\0';

					ulShellExecute(NULL, "open", cFileNameBuf, NULL, NULL, SW_SHOWNORMAL);
				}
				else
				{
					Errorf("Could not find file %s for directory opening", pcFileName);
				}
			}
			else
				Errorf("Could not find DynFxInfo for FX %s", pFxTracker->pcFxName);
		}
	}
#endif
}

static void clearEmptyTrackers(UIButton* button, void* userData)
{
	dynFxDebugClearEmpty();
}

static void pauseFX(UIButton* button, void* userData)
{
	dfxPauseAll(1);
}

static void dynFxActivatedCB(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	DynFxTracker* pFxTracker = (DynFxTracker*)((*pList->peaModel)[iRow]);
	if(pFxTracker)
	{
		ui_TextEntrySetText(fxTracker.pFilterTextEntry, pFxTracker->pcFxName);
		strncpy(fxTracker.dfxTreeFilter.pchName, pFxTracker->pcFxName, MaxFilterStringLength);

		// turn these off
		fxTracker.dfxTreeFilter.bRequiresAssetType = 0; 
		ui_CheckButtonSetState(fxTracker.pReferencesAssetToggle, 0);

		fxTracker.dfxTreeFilter.bRequiresPriority = 0;
		ui_CheckButtonSetState(fxTracker.pPriorityToggle, 0);

		// switch to the tab before updating the filter (as the tab may not have been loaded)
		ui_TabGroupSetActiveIndex(fxTracker.pTabGroup, 1);

		dfxTreeUpdateFilter(true);
	}
}

static void displayDynFxName(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	DynFxTracker* pFxTracker = (DynFxTracker*)((*pList->peaModel)[index]);
	U32 uiRed = CLAMP( (U32)(255.0f * (pFxTracker->fTimeSinceZero / 5.0f)),
		0,
		255);
	U32 uiColor = 0x000000FF | (uiRed << 24);
	gfxfont_SetColorRGBA(uiColor, uiColor);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pFxTracker->pcFxName);
}

static void displayDynFxCurrent(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%d", ((DynFxTracker*)(*pList->peaModel)[index])->uiNumFx);
}

static void displayDynFxPeak(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%d", ((DynFxTracker*)(*pList->peaModel)[index])->uiNumFxPeak);
}

static void displayDynFxMem(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%dK", (((DynFxTracker*)(*pList->peaModel)[index])->iMemUsage) >> 10);
}

static void displayDynFxLevel(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", dynPriorityLookup((((DynFxTracker*)(*pList->peaModel)[index])->iPriorityLevel)));
}




static void shutdownWindow(void);
static bool dynFxDebugWindowClose(UIWindow* pCloseWindow, void* userData)
{
	assert( pCloseWindow == fxTracker.pWindow );
	shutdownWindow();
	return true;
}

static void popOutFrameCallback(RdrDevice *rdr_device, void *userData)
{
	gfxAuxDeviceDefaultTop(rdr_device, 0, ui_OncePerFramePerDevice);
	gfxAuxDeviceDefaultBottom(rdr_device, 0);
}

static bool popOutCloseCallback(RdrDevice *rdr_device, void *userData)
{
	ui_WidgetQueueFree((UIWidget*)fxTracker.pWindow);
	fxTracker.pWindow = NULL;
	ui_StateFreeForDevice(fxTracker.pFxDebugDevice);
	gfxAuxDeviceRemove(fxTracker.pFxDebugDevice);
	fxTracker.pFxDebugDevice = NULL;
	return true;
}

void dfxDebug(void);

static void shutdownWindow(void)
{
	fxTracker.bKillLoadingThread = 1;
	fxTracker.bWindowActive = false;

	dynDebugState.bFxDebugOn = false;
	dynDebugState.bFxDrawOnlySelected = false;
	if (fxTracker.pFxDebugDevice)
	{
		popOutCloseCallback(fxTracker.pFxDebugDevice, NULL);
	}
	else if (fxTracker.pWindow)
	{
		ui_WidgetQueueFree((UIWidget *) fxTracker.pWindow);
		fxTracker.pWindow = NULL;
	}
}

static void resetWindow()
{
	shutdownWindow();
	dfxDebug();
}

static void popOut(UIButton* button, void* userData)
{
	/*
	Errorf("PopOut disabled until stablity/framerate issues resolved");
	return;
	*/
	fxTracker.bPopOut = true;
	resetWindow();
}

static void popIn(UIButton* button, void* userData)
{
	fxTracker.bPopOut = false;
	resetWindow();
}

static void changeDebugHue(UIColorSlider* pSlider, UITextEntry* pText)
{
	char cHue[16];
	dynDebugState.fTestHue = pSlider->current[0];
	sprintf(cHue, "%.1f", pSlider->current[0]);
	ui_TextEntrySetText(pText, cHue);
}

static void changeDebugSaturation(UISlider* pSlider, bool finished, UITextEntry* pText)
{
	char cSat[16];
	dynDebugState.fTestSaturation = pSlider->currentVals[0];
	sprintf(cSat, "%.1f", pSlider->currentVals[0]);
	ui_TextEntrySetText(pText, cSat);
}

static void changeDebugValue(UISlider* pSlider, bool finished, UITextEntry* pText)
{
	char cVal[16];
	dynDebugState.fTestValue = pSlider->currentVals[0];
	sprintf(cVal, "%.1f", pSlider->currentVals[0]);
	ui_TextEntrySetText(pText, cVal);
}

static void setHueFromEntry(UITextEntry *entry, UIColorSlider *slider)
{
	F32 value;
	UIColorWindow *window = slider->changedData;
	Vec3 newColor;
	copyVec3(slider->current, newColor);
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1)
	{
		newColor[0] = value;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_ColorSliderSetValueAndCallback(slider, newColor);
}

static void setSaturationFromEntry(UITextEntry *entry, UISlider *slider)
{
	F32 value = 0;
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1) {
		dynDebugState.fTestSaturation = value;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_SliderSetValueAndCallback(slider, value);
}

static void setValueFromEntry(UITextEntry *entry, UISlider *slider)
{
	F32 value = 0;
	if (sscanf(ui_TextEntryGetText(entry), "%f", &value) == 1) {
		dynDebugState.fTestValue = value;
	}
	ui_TextEntrySetCursorPosition(entry, 0);
	ui_SliderSetValueAndCallback(slider, value);
}

static void setOpacity(F32 fOpacity)
{
	ui_SkinSetBackground(fxTracker.pSkin,
		CreateColor(148, 148, 148, fOpacity));
	ui_SkinSetEntry(fxTracker.pSkin,
		CreateColor(178, 178, 178, fOpacity));
}

static void fxTrackerSettingsChanged(UIRTNode* pDummy, FXTrackerSettings* pSettings)
{
	UISlider* pSlider = (UISlider*)pDummy->widget1;
	{
		GamePrefStoreFloat("FXTracker\\Opacity",  pSettings->fOpacity);
		setOpacity(pSettings->fOpacity);
	}
}

static UISkin *fxTrackerGetSkin(FXTrackerSettings* pSettings, UISkin* pBase)
{
	if (!fxTracker.pSkin)
		fxTracker.pSkin = ui_SkinCreate(pBase);

	setOpacity(pSettings->fOpacity);

	return fxTracker.pSkin;
}


////////////////////////////////////////////////////////////////////////////
//  
//   Draw Debug Section
//
///////////////////////////////////////////////////////////////////////////

static const char* colWidthPrefName(int iColIdx, const char* pcListName)
{
	static char cBuf[64];
	sprintf(cBuf, "FX%sDebugColW%d", pcListName, iColIdx);
	return cBuf;
}

static const char* groupStatusPrefName(int iGroupIdx)
{
	static char cBuf[64];
	sprintf(cBuf, "FXDebugGroupStatus%d", iGroupIdx);
	return cBuf;
}


static void dynDrawDebugTick(F32 fDeltaTime)
{
	if (dynDebugState.bFxDebugOn)
	{
		if (fxTracker.pWindow)
		{
			GamePrefStoreInt("FXDebugX", UI_WIDGET(fxTracker.pWindow)->x);
			GamePrefStoreInt("FXDebugY", UI_WIDGET(fxTracker.pWindow)->y);
			GamePrefStoreInt("FXDebugW", UI_WIDGET(fxTracker.pWindow)->width);
			GamePrefStoreInt("FXDebugH", UI_WIDGET(fxTracker.pWindow)->height);

			GamePrefStoreInt("FXDebug.tab", ui_TabGroupGetActiveIndex(fxTracker.pTabGroup));
		}
		if (fxTracker.pTree)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(fxTracker.pTree->root->children, UIRTNode, pNode)
			{
				GamePrefStoreInt(groupStatusPrefName(ipNodeIndex), ui_ExpanderIsOpened(pNode->expander));
			}
			FOR_EACH_END;
		}
		if (fxTracker.pDebugDrawList)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(fxTracker.pDebugUpdateList->eaColumns, UIListColumn, pCol)
			{
				GamePrefStoreFloat(colWidthPrefName(ipColIndex, "Update"), pCol->fWidth);
			}
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY_FORWARDS(fxTracker.pDebugDrawList->eaColumns, UIListColumn, pCol)
			{
				GamePrefStoreFloat(colWidthPrefName(ipColIndex, "Draw"), pCol->fWidth);
			}
			FOR_EACH_END;


			eaClear(&eaDynDrawOnly);
			if (dynDebugState.bFxDrawOnlySelected)
			{
				const S32* const* peaiMultiSelected = ui_ListGetSelectedRows(fxTracker.pDebugDrawList);
				if (peaiMultiSelected)
				{
					const U32 uiNum = eaiSize(peaiMultiSelected);
					U32 uiIndex;
					for (uiIndex=0; uiIndex<uiNum; ++uiIndex)
					{
						const S32 iListIndex = (*peaiMultiSelected)[uiIndex];
						DynDrawTracker* pDrawTracker;
						if (iListIndex < 0)
							continue;
						pDrawTracker = (DynDrawTracker*)((*fxTracker.pDebugDrawList->peaModel)[iListIndex]);
						if (pDrawTracker)
							eaPush(&eaDynDrawOnly, pDrawTracker->pcName);
					}
				}
			}
		}
	}
}


static void drawDebugDisplayName(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	DynDrawTracker* pTracker = (DynDrawTracker*)((*pList->peaModel)[index]);
	/*
	U32 uiRed = CLAMP( (U32)(255.0f * (pFxTracker->fTimeSinceZero / 5.0f)),
	0,
	255);
	*/
	U32 uiColor = 0x000000FF;// | (uiRed << 24);
	gfxfont_SetColorRGBA(uiColor, uiColor);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pTracker->pcName);
}

static void drawDebugDisplayNum(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%d", ((DynDrawTracker*)(*pList->peaModel)[index])->uiNum);
}

static void drawDebugDisplaySubObjects(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%d", ((DynDrawTracker*)(*pList->peaModel)[index])->uiSubObjects);
}

static void drawDebugDisplayType(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", dynFxTypeLookup(((DynDrawTracker*)(*pList->peaModel)[index])->eType));
}

static void drawDebugDisplayPriority(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", dynPriorityLookup((((DynDrawTracker*)(*pList->peaModel)[index])->ePriority)));
}

static void drawOnlySelected(UICheckButton* button, void* userData)
{
	dynDebugState.bFxDrawOnlySelected = button->state;
}

//
// Asset Tree Functions
//

void dfxAssetTreeGetAsset(DynFxInfo *pDynFxInfo, eAssetType assetType, const char ***pppchStrOut)
{
	if(!pDynFxInfo) return;

	switch(assetType)
	{
	case eAssetType_Audio:
		{
			FOR_EACH_IN_EARRAY(pDynFxInfo->eaRaycasts, DynRaycast, pRaycast)
				FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
				FOR_EACH_IN_EARRAY(pEvent->eaSoundStart, char, pchSoundStart)
				eaPush(pppchStrOut, pchSoundStart);
			FOR_EACH_END;
			FOR_EACH_END;
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDynFxInfo->eaContactEvents, DynContactEvent, pContactEvent)
				FOR_EACH_IN_EARRAY(pContactEvent->eaSoundStart, char, pchSoundStart)
				eaPush(pppchStrOut, pchSoundStart);
			FOR_EACH_END;
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pDynEvent)
				FOR_EACH_IN_EARRAY(pDynEvent->keyFrames, DynKeyFrame, pDynKeyFrame)
				FOR_EACH_IN_EARRAY(pDynKeyFrame->ppcSoundStarts, char, pchSoundStart)
				eaPush(pppchStrOut, pchSoundStart);
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDynKeyFrame->ppcSoundEnds, char, pchSoundEnd)
				eaPush(pppchStrOut, pchSoundEnd);
			FOR_EACH_END;
			FOR_EACH_END;
			FOR_EACH_END;
			break;
		}
	case eAssetType_Geometry:
		{
			FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pDynEvent)
				FOR_EACH_IN_EARRAY(pDynEvent->keyFrames, DynKeyFrame, pDynKeyFrame)
				if(pDynKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName)
				{
					eaPush(pppchStrOut, pDynKeyFrame->objInfo[edoValue].obj.draw.pcModelName);
				}
				FOR_EACH_END;
				FOR_EACH_END;
				break;
		}
	case eAssetType_Material:
		{
			FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pDynEvent)
				FOR_EACH_IN_EARRAY(pDynEvent->keyFrames, DynKeyFrame, pDynKeyFrame)
				if(pDynKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName)
				{
					eaPush(pppchStrOut, pDynKeyFrame->objInfo[edoValue].obj.draw.pcMaterialName);
				}
				FOR_EACH_END;
				FOR_EACH_END;
				break;
		}
	case eAssetType_ParticleEmitter:
		{
			FOR_EACH_IN_EARRAY(pDynFxInfo->eaContactEvents, DynContactEvent, pContactEvent)
				FOR_EACH_IN_EARRAY(pContactEvent->eaEmitterStart, DynParticleEmitterRef, pParticleEmitterRef)
				eaPush(pppchStrOut, pParticleEmitterRef->pcTag);
			FOR_EACH_END;
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDynFxInfo->eaRaycasts, DynRaycast, pRaycast)
				FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
				FOR_EACH_IN_EARRAY(pEvent->eaEmitterStart, DynParticleEmitterRef, pParticleEmitterRef)
				eaPush(pppchStrOut, pParticleEmitterRef->pcTag);
			FOR_EACH_END
				FOR_EACH_END;
			FOR_EACH_END;

			FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pDynEvent)
				FOR_EACH_IN_EARRAY(pDynEvent->keyFrames, DynKeyFrame, pDynKeyFrame)
				FOR_EACH_IN_EARRAY(pDynKeyFrame->eaEmitterStart, DynParticleEmitterRef, pParticleEmitterRef)
				eaPush(pppchStrOut, pParticleEmitterRef->pcTag);
			FOR_EACH_END
				FOR_EACH_IN_EARRAY(pDynKeyFrame->eaEmitterStop, DynParticleEmitterRef, pParticleEmitterRef)
				eaPush(pppchStrOut, pParticleEmitterRef->pcTag);
			FOR_EACH_END
				FOR_EACH_END;
			FOR_EACH_END;
			break;
		}
	case eAssetType_Texture:
		{
			FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pDynEvent)
				FOR_EACH_IN_EARRAY(pDynEvent->keyFrames, DynKeyFrame, pDynKeyFrame)
				if(pDynKeyFrame->objInfo[edoValue].obj.draw.pcTextureName)
				{
					eaPush(pppchStrOut, pDynKeyFrame->objInfo[edoValue].obj.draw.pcTextureName);
				}
				if(pDynKeyFrame->objInfo[edoValue].obj.draw.pcTextureName2)
				{
					eaPush(pppchStrOut, pDynKeyFrame->objInfo[edoValue].obj.draw.pcTextureName2);
				}
				FOR_EACH_END;
				FOR_EACH_END;
				break;
		}

	}
}

#define MaxStringLength (1024)

static void dfxAssetTreeDrawNode(UITreeNode *pNode, const char *pchField, UI_MY_ARGS, F32 z)
{
	DfxTreeNodeData *pData = (DfxTreeNodeData*)pNode->contents;
	int iNumChildren = eaSize(&pData->ppChildren);
	int iVisibleChildrenCount = 0;
	static char pchStr[MaxStringLength];
	U32 rgbaColor = 0x000000CC;
	//	const char *pcFilterEntryText;
	static char **ppNames = NULL;
	int iNumAssets;
	eAssetType selectedAssetType;
	char *highlightText = NULL;
	Color highlightColor = { 0xff, 0xff, 0xff, 0x33 };

	eaClear(&ppNames);

	selectedAssetType = (eAssetType)ui_TabGroupGetActiveIndex(fxTracker.pAssetTabGroup);


	dfxAssetTreeGetAsset(GET_REF(pData->hInfo), selectedAssetType, &ppNames);
	iNumAssets = eaSize(&ppNames);

	snprintf_s(pchStr, 128, "%s", pData->pchName);

	if(iNumAssets > 0)
	{
		strcat(pchStr, " : ");
		highlightText = pchStr + strlen(pchStr);

		FOR_EACH_IN_EARRAY(ppNames, char, pAssetName)
			if(pAssetName)
			{
				if(strlen(pAssetName) + strlen(pchStr) + 1 < MaxStringLength)
				{
					strcat(pchStr, pAssetName);
					strcat(pchStr, " ");
				}
			}
			FOR_EACH_END;
	}

	//pcFilterEntryText = ui_TextEntryGetText(fxTracker.pFilterTextEntry);

	//ui_TreeDisplayText(pNode, pchStr, UI_MY_VALUES, z);
	dfxTreeDisplayText(pNode, pchStr, highlightText, rgbaColor, highlightColor, UI_MY_VALUES, z);
}

static void dfxAssetTreeFillChildren(UITreeNode *pNode, UserData pUserData)
{
	DfxTreeNodeData *pParentNode = (DfxTreeNodeData*)pUserData;
	int i;

	for ( i = 0; i < eaSize(&pParentNode->ppChildren); ++i )
	{
		DfxTreeNodeData *pChildNodeData = pParentNode->ppChildren[i];
		if(pChildNodeData->bIsVisible)
		{
			UITreeNode *pNewNode = ui_TreeNodeCreate(
				pNode->tree, cryptAdler32String(pChildNodeData->pchName), NULL, pChildNodeData,
				eaSize(&pChildNodeData->ppChildren) ? dfxAssetTreeFillChildren : NULL, pChildNodeData,
				dfxAssetTreeDrawNode, NULL, 20);
			ui_TreeNodeAddChild(pNode, pNewNode);
		}
	}
}

static void dfxAssetTreeFill(UITreeNode *pNode, UserData pUserData)
{
	DfxTreeNodeData *pChildNodeData = (DfxTreeNodeData*)pUserData;
	if(pChildNodeData)
	{
		if(pChildNodeData->bIsVisible)
		{
			UITreeNode *pNewNode = ui_TreeNodeCreate(
				pNode->tree, cryptAdler32String(pChildNodeData->pchName), NULL, pUserData,
				dfxAssetTreeFillChildren, pUserData, 
				dfxAssetTreeDrawNode, NULL, 
				20);
			ui_TreeNodeAddChild(pNode, pNewNode);
		}
	}
}

//
// Tree Functions
//

static void dfxTreeCopyTreeToClipboardCB(UIAnyWidget *widget, UserData userData) 
{
	char *estr = NULL;

	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(fxTracker.pFxTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;

		estrConcatf(&estr, "DynFx Selected Results : %d\n", numSelected);
		if(fxTracker.dfxTreeFilter.pchName && strlen(fxTracker.dfxTreeFilter.pchName))
		{
			estrConcatf(&estr, " filter by name : %s\n", fxTracker.dfxTreeFilter.pchName);	
		}

		if(fxTracker.dfxTreeFilter.bRequiresAssetType)
		{
			char ***pppchAssetChoices = dfxAssetChoices();
			if(pppchAssetChoices && fxTracker.dfxTreeFilter.assetType < eaSize(pppchAssetChoices))
			{
				char *pchAssetType = (*pppchAssetChoices)[fxTracker.dfxTreeFilter.assetType];

				estrConcatf(&estr, " with asset : %s", pchAssetType);	

				if(fxTracker.dfxTreeFilter.pchAssetName && strlen(fxTracker.dfxTreeFilter.pchAssetName))
				{
					estrConcatf(&estr, " containing string : %s", fxTracker.dfxTreeFilter.pchAssetName);	
				}

				estrConcatf(&estr, "\n");
			}
		}	
		
		if(fxTracker.dfxTreeFilter.bRequiresPriority)
		{
			char ***pppchPriorityChoices = dfxPriorityChoices();
			if(pppchPriorityChoices && fxTracker.dfxTreeFilter.priority < eaSize(pppchPriorityChoices))
			{
				char *pchPriorityType = (*pppchPriorityChoices)[fxTracker.dfxTreeFilter.priority];
				estrConcatf(&estr, " with priority : %s\n", pchPriorityType);	
			}
		}	

		if(fxTracker.dfxTreeFilter.bSearchFileContents)
		{
			estrConcatf(&estr, " with file contents containing string : %s\n", fxTracker.dfxTreeFilter.pchFileContents);	
		}	

		estrConcatf(&estr, "\n========================\n\n");
		

		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
				if(pTreeNodeData)
				{
					DfxTreeNodeData *pParentNode = pTreeNodeData->pParentNode;
					while(pParentNode)
					{
						estrConcatf(&estr, "\t");
						pParentNode = pParentNode->pParentNode;
					}
					
					estrConcatf(&estr, "%s\n", pTreeNodeData->pchName);
				}
			}
		}
		winCopyToClipboard(estr);

		estrDestroy(&estr);
	}
}

static void dfxTreeRightClickEditFileCB(UIAnyWidget *tree, UserData pUserData)
{
	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(fxTracker.pFxTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;
		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
				if(pTreeNodeData)
				{
					DynFxInfo *pDynFxInfo = GET_REF(pTreeNodeData->hInfo);
					if(pDynFxInfo)
					{
						dfxEditFile(pDynFxInfo->pcFileName);
					}
				}
			}
		}
	}
}

static void dfxTreeRightClickSearchCB(UIAnyWidget *tree, UserData pUserData)
{
	UITreeNode* pTreeNode = fxTracker.pFxTree->selected;
	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
		if(pTreeNodeData)
		{
			ui_TextEntrySetText(fxTracker.pFilterTextEntry, pTreeNodeData->pchName);

			// change filter settings
			strncpy(fxTracker.dfxTreeFilter.pchName, pTreeNodeData->pchName, MaxFilterStringLength);

			// apply it
			dfxTreeUpdateFilter(true);		
		}
	}
}

static void dfxTreeRightClickSearchPowerArtCB(UIAnyWidget *tree, UserData pUserData)
{
	UITreeNode* pTreeNode = fxTracker.pFxTree->selected;
	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
		if(pTreeNodeData)
		{
			ui_TextEntrySetText(fxTracker.pPowerArtFxFilterNameEntry, pTreeNodeData->pchName);

			// change filter settings
			strncpy(fxTracker.dfxPowerArtFilter.pchFxName, pTreeNodeData->pchName, MaxFilterStringLength);
			fxTracker.dfxPowerArtFilter.bRequiresFx = 1;
			fxTracker.dfxPowerArtFilter.iType = ePowerArtFx_EndOfList;
			ui_ComboBoxSetSelected(fxTracker.pPowerArtFxFilterType, ePowerArtFx_EndOfList);
			ui_CheckButtonSetState(fxTracker.pPowerArtFilterTypeToggle, 1);

			// switch to power art tab
			ui_TabGroupSetActiveIndex(fxTracker.pTabGroup, 2); 

			// apply it
			dfxPowerArtUpdateFilter();		
		}
	}
}

static void dfxTreeShowAllChildren(DfxTreeNodeData *pTreeNodeData)
{
	if(pTreeNodeData)
	{
		pTreeNodeData->bIsVisible = 1;

		FOR_EACH_IN_EARRAY(pTreeNodeData->ppChildren, DfxTreeNodeData, pChildNode)
			dfxTreeShowAllChildren(pChildNode);
		FOR_EACH_END;
	}
}

bool dfxTreeSelectNodeCondition(UITreeNode *pTreeNode, void *pUserData)
{
	bool result = false;
	DfxTreeNodeData **ppPrevSelected = (DfxTreeNodeData**)pUserData;
	FOR_EACH_IN_EARRAY(ppPrevSelected, DfxTreeNodeData, pDfxTreeNodeData)
	{
		if(pTreeNode->contents == pDfxTreeNodeData)
		{
			result = true;
			break;
		}
	}
	FOR_EACH_END;
	return result;
}

static void dfxTreeShowAllChildrenCB(UIAnyWidget *tree, UserData pUserData)
{
	DfxTreeNodeData **ppPrevSelected = NULL;

	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(fxTracker.pFxTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;
		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
				dfxTreeShowAllChildren(pTreeNodeData);
				dfxTreeExpandAllNodes((UITreeNode*)pTreeNode);
				eaPush(&ppPrevSelected, pTreeNodeData);
			}
		}
	}

	ui_TreeRefresh(fxTracker.pFxTree);
	ui_TreeRefresh(fxTracker.pAssetTree);

	ui_TreeSelectFromBranchWithCondition(fxTracker.pFxTree, &fxTracker.pFxTree->root, dfxTreeSelectNodeCondition, ppPrevSelected);

	eaDestroy(&ppPrevSelected);
}

static int dfxCheckoutFile(const char *pchFilename, bool bShowAlert)
{
	GimmeErrorValue ret;

	// If already writable, then no problem
	if (!fileIsReadOnly(pchFilename)) 
	{
		return 0;
	}

	if (!gimmeDLLQueryIsFileLatest(pchFilename)) {
		// The user doesn't have the latest version of the file, do not let them edit it!
		// If we were to check out the file here, the file would be changed on disk, but not reloaded,
		//   and that would be bad!  Someone else's changes would most likely be lost.
		if(bShowAlert)
		{
			Alertf("Error: file (%s) unable to be checked out, someone else has changed it since you last got latest.  Exit, get latest and reload the file.", pchFilename);
		}
		return -1;
	}

	ret = gimmeDLLDoOperation(pchFilename, GIMME_CHECKOUT, GIMME_QUIET);

	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB) {
		if (bShowAlert) {
			const char *pcLockee;
			if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT && (pcLockee = gimmeDLLQueryIsFileLocked(pchFilename))) {
				Alertf("File \"%s\" unable to be checked out, currently checked out by %s", pchFilename, pcLockee);
			} else {
				Alertf("File \"%s\" unable to be checked out (%s)", pchFilename, gimmeDLLGetErrorString(ret));
			}
		}
		return -1;
	}

	gfxStatusPrintf( "You have checked out: %s", pchFilename );

	return 0; // ok
}

static void dfxTreeRightClickCheckoutFileCB(UIAnyWidget *tree, UserData pUserData)
{
	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(fxTracker.pFxTree);
	if(selectedNodes)
	{
		bool bShowAlert = true;
		int result;
		int numSelected = eaSize(selectedNodes);
		int i;
		int failCount = 0;
		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
				if(pTreeNodeData)
				{
					DynFxInfo *pFxInfo = GET_REF(pTreeNodeData->hInfo);
					if(pFxInfo)
					{
						if(pFxInfo->pcFileName && strstr(pFxInfo->pcFileName, "/") != NULL)
						{
							result = dfxCheckoutFile(pFxInfo->pcFileName, bShowAlert);
							if(result != 0)
							{
								printf("Checkout failed: %s\n", pFxInfo->pcFileName);
								failCount++;
								if(failCount > 3)
								{
									Alertf("More than 3 checkout errors. Ignoring the rest. Check console log for complete list.");
									bShowAlert = false;
								}
							}
						}
					}
				}
			}
		}
	}
}

static void dfxTreeSelectAllCB(UIAnyWidget *tree, UserData pUserData)
{
	ui_TreeSelectAll(fxTracker.pFxTree);
}

static void dfxTreeRightClickContextCB(UIAnyWidget *tree, UserData pUserData)
{
	if (!fxTracker.pRightClickMenu)
		fxTracker.pRightClickMenu = ui_MenuCreate("");

	eaDestroyEx(&fxTracker.pRightClickMenu->items, ui_MenuItemFree);

	ui_MenuAppendItems(fxTracker.pRightClickMenu, NULL);

	ui_MenuAppendItems(fxTracker.pRightClickMenu,
		ui_MenuItemCreate("Play Effect (Enter)", UIMenuCallback, dfxTreePlayEffectCB, NULL, NULL),
		ui_MenuItemCreate("Edit File", UIMenuCallback, dfxTreeRightClickEditFileCB, NULL, NULL),
		ui_MenuItemCreate("Checkout", UIMenuCallback, dfxTreeRightClickCheckoutFileCB, NULL, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Search In Fx", UIMenuCallback, dfxTreeRightClickSearchCB, NULL, NULL),
		ui_MenuItemCreate("Search In PowerArt", UIMenuCallback, dfxTreeRightClickSearchPowerArtCB, NULL, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Select All (Ctrl+A)", UIMenuCallback, dfxTreeSelectAllCB, NULL, NULL),
		ui_MenuItemCreate("Copy Selection To Clipboard", UIMenuCallback, dfxTreeCopyTreeToClipboardCB, NULL, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Show All Children", UIMenuCallback, dfxTreeShowAllChildrenCB, NULL, NULL),
		NULL);

	ui_MenuPopupAtCursor(fxTracker.pRightClickMenu);
}

static void dfxEditFile(const char *pcFileName)
{	
	if(pcFileName && strstr(pcFileName, "/") != NULL)
	{
		char pcFileNameBuf[512];
		if (fileLocateWrite(pcFileName, pcFileNameBuf))
		{
			fileOpenWithEditor(pcFileNameBuf);
		}
		else
		{
			Errorf("Could not find file %s for editing", pcFileName);
		}
	}
}

static void dfxTreeActivateEffectCB(UIAnyWidget *pWidget, UserData pUserData)
{
	UITree* pTree = (UITree*)pWidget;
	UITreeNode* pTreeNode = pTree->selected;
	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
		if(pTreeNodeData)
		{
			if(GET_REF(pTreeNodeData->hInfo))
			{
				dfxEditFile(GET_REF(pTreeNodeData->hInfo)->pcFileName);
			}
		}
	}
}

static void dfxTreeUpdateSelectionCount()
{
	char txt[32];
	const UITreeNode * const * const *nodes = ui_TreeGetSelectedNodes(fxTracker.pFxTree);
	if(nodes)
	{
		int numSelected = eaSize(nodes);
		sprintf(txt, "%d selected", numSelected);
		ui_LabelSetText(fxTracker.pNumSelectedLabel, txt);
	}
}

static bool dfxReadFxFile(DynFxInfo *pFxInfo, DfxFileBuffer *pFileBuffer)
{
	bool bResult = false;

	char cFileNameBuf[512];
	if(fileLocateWrite(pFxInfo->pcFileName, cFileNameBuf))
	{
		S64 iFileSize;

		FILE *fp = fopen(cFileNameBuf, "r");
		if(fp)
		{
			fseek(fp, 0L, SEEK_END);
			iFileSize = ftell(fp);

			fseek(fp, 0L, SEEK_SET);
			if(iFileSize > 0)
			{
				if(iFileSize > pFileBuffer->iMaxSize)
				{
					pFileBuffer->pchBuffer = realloc(pFileBuffer->pchBuffer, iFileSize+1);
					pFileBuffer->iMaxSize = iFileSize + 1;
				}

				pFileBuffer->iSize = (int)fread(pFileBuffer->pchBuffer, sizeof(char), iFileSize, fp);
				pFileBuffer->pchBuffer[pFileBuffer->iSize] = '\0'; // terminate
				pFileBuffer->bRead = 1;
				bResult = true;
			}

			fclose(fp);
		}
	}
	return bResult;
}

static void dfxTreeDisplaySourceFile(DfxTreeNodeData *pTreeNodeData)
{
	if(pTreeNodeData)
	{
		if(!pTreeNodeData->dfxFileBuffer.bRead)
		{
			DynFxInfo *pDynFxInfo = GET_REF(pTreeNodeData->hInfo);
			if(pDynFxInfo)
			{
				dfxReadFxFile(pDynFxInfo, &pTreeNodeData->dfxFileBuffer);
			}
		}

		if(pTreeNodeData->dfxFileBuffer.pchBuffer && pTreeNodeData->dfxFileBuffer.pchBuffer[0])
		{
			ui_TextAreaSetText(fxTracker.pFxSourceTextArea, pTreeNodeData->dfxFileBuffer.pchBuffer);
			ui_TextAreaSetCursorPosition(fxTracker.pFxSourceTextArea, 0);
		}
		else
		{
			ui_TextAreaSetText(fxTracker.pFxSourceTextArea, "");
		}
	}
}

static void dfxTreeNodeSelectedCB(UIAnyWidget *pWidget, UserData userData) 
{
	UITree* pTree = (UITree*)pWidget;
	UITreeNode* pTreeNode = pTree->selected;
	DfxTreeNodeData *pTreeNodeData2 = NULL;

	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = pTreeNodeData2 = (DfxTreeNodeData*)pTreeNode->contents;
		DynFxInfo *pFxInfo = GET_REF(pTreeNodeData->hInfo);
		if(pFxInfo)
		{
			const char *pchName = pFxInfo->pcDynName;
			ui_LabelSetText(fxTracker.pFxNameLabel, pchName);
		}

		dfxTreeDisplaySourceFile(pTreeNodeData);
	}
	fxTracker.pSelectedTreeNode = pTreeNode;

	dfxTreeUpdateSelectionCount();

	ui_TreeNodeSetFillCallback(&fxTracker.pAssetTree->root, dfxAssetTreeFill, pTreeNodeData2);
	ui_TreeRefresh(fxTracker.pAssetTree);

	dfxTreeExpandAllNodes(&fxTracker.pAssetTree->root);
}

static void dfxTreeDisplayText(UITreeNode *node, const char *text, const char *highlightText, U32 uiColor, Color highlightColor, UI_MY_ARGS, F32 z)
{
	UITree* tree = node->tree;
	CBox box = {x, y, x + w, y + h};
	UIStyleFont *font = GET_REF(UI_GET_SKIN(tree)->hNormal);
	bool bSelected = node == tree->selected || (tree->multiselect && eaFind(&tree->multiselected, node) >= 0);

	//if(font) font->bBold = 0;
	ui_StyleFontUse(font, bSelected, UI_WIDGET(tree)->state);
	clipperPushRestrict(&box);
	if(!bSelected)
	{
		if(highlightText && highlightText[0])
		{
			Vec2 offsetSize;
			Vec2 highlightSize;
			char *begin;

			begin = strstri(text, highlightText);
			if(begin != NULL)
			{
				int offsetX = 0;

				if(begin != text)
				{
					gfxFontMeasureStringEx(g_font_Active, text, begin, offsetSize);
					offsetX = offsetSize[0];
				}

				gfxFontMeasureStringEx(g_font_Active, highlightText, highlightText + UTF8GetLength(highlightText), highlightSize);

				gfxDrawQuad(x+offsetX, y, x+highlightSize[0]+offsetX, y+h, z, highlightColor);
			}
		}

		gfxfont_SetColorRGBA(uiColor, uiColor);
	}
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", text);
	clipperPop();
}



static void dfxTreeDrawNode(UITreeNode *pNode, const char *pchField, UI_MY_ARGS, F32 z)
{
	DfxTreeNodeData *pData = (DfxTreeNodeData*)pNode->contents;
	int iNumChildren = eaSize(&pData->ppChildren);
	int iVisibleChildrenCount = 0;
	static char pchStr[128];
	U32 rgbaColor = 0x000000CC;
	const char *pcFilterEntryText;
	Color highlightColor = { 0x99, 0x99, 0x00, 0x33 };
	DynFxInfo *pFxInfo;

	if(iNumChildren > 0)
	{
		int i;
		// how many of the children are visible
		for(i = 0; i < iNumChildren; i++)
		{
			if(pData->ppChildren[i]->bIsVisible) iVisibleChildrenCount++;
		}
	} 
	else 
	{

	}

	if(pFxInfo = GET_REF(pData->hInfo))
	{

		switch(pFxInfo->iPriorityLevel)
		{
		case edpOverride:
		case edpCritical:
			rgbaColor = 0x660000FF;
			break;
		case edpDefault:
			rgbaColor = 0x000000FF;
			break;
		case edpDetail:
		case edpNotSet:			
			rgbaColor = 0x006600FF;
			break;
		}
	}

	if(iVisibleChildrenCount > 0)
	{
		sprintf(pchStr, "%s (%d)", pData->pchName, iVisibleChildrenCount);
	}
	else
	{
		sprintf(pchStr, "%s", pData->pchName);
	}

	pcFilterEntryText = ui_TextEntryGetText(fxTracker.pFilterTextEntry);


	//ui_TreeDisplayText(pNode, pchStr, UI_MY_VALUES, z);
	dfxTreeDisplayText(pNode, pchStr, pcFilterEntryText, rgbaColor, highlightColor, UI_MY_VALUES, z);
}

static void dfxTreeFillChildren(UITreeNode *pNode, UserData pUserData)
{
	DfxTreeNodeData *pParentNode = (DfxTreeNodeData*)pUserData;
	int i;

	for ( i = 0; i < eaSize(&pParentNode->ppChildren); ++i )
	{
		DfxTreeNodeData *pChildNodeData = pParentNode->ppChildren[i];
		if(pChildNodeData->bIsVisible)
		{
			UITreeNode *pNewNode = ui_TreeNodeCreate(
				pNode->tree, cryptAdler32String(pChildNodeData->pchName), NULL, pChildNodeData,
				eaSize(&pChildNodeData->ppChildren) ? dfxTreeFillChildren : NULL, pChildNodeData,
				dfxTreeDrawNode, NULL, 20);
			ui_TreeNodeAddChild(pNode, pNewNode);
		}
	}
}

static void dfxTreeFill(UITreeNode *pNode, UserData pUserData)
{
	DfxTreeNodeData *pChildNodeData = (DfxTreeNodeData*)pUserData;
	if(pChildNodeData)
	{
		if(pChildNodeData->bIsVisible)
		{
			UITreeNode *pNewNode = ui_TreeNodeCreate(
				pNode->tree, cryptAdler32String(pChildNodeData->pchName), NULL, pUserData,
				dfxTreeFillChildren, pUserData, 
				dfxTreeDrawNode, NULL, 
				20);
			ui_TreeNodeAddChild(pNode, pNewNode);
		}
	}
}

static void dfxTreeAddNode(DynFxInfo *pDynFxInfo, DfxTreeNodeData *pParentNode);

static void dfxTreeAddChildren(DynChildCallCollection* pCollection, DfxTreeNodeData *pParentNode)
{
	FOR_EACH_IN_EARRAY(pCollection->eaChildCall, DynChildCall, pChildCall)
		DynFxInfo *dynFxInfo = GET_REF(pChildCall->hChildFx);
	dfxTreeAddNode(dynFxInfo, pParentNode);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pCollection->eaChildCallList, DynChildCallList, pChildCallList)
		FOR_EACH_IN_EARRAY(pChildCallList->eaChildCall, DynChildCall, pChildCall)
		DynFxInfo *dynFxInfo = GET_REF(pChildCall->hChildFx);
	dfxTreeAddNode(dynFxInfo, pParentNode);
	FOR_EACH_END;
	FOR_EACH_END;
}

static void dfxTreeAddNode(DynFxInfo *pDynFxInfo, DfxTreeNodeData *pParentNode)
{
	DfxTreeNodeData *pNode;

	if(!pDynFxInfo) return;

	pNode = calloc(1, sizeof(DfxTreeNodeData));
	pNode->pchName = strdup(pDynFxInfo->pcDynName);
	//pNode->data = (void*)pDynFxInfo;
	SET_HANDLE_FROM_STRING(hDynFxInfoDict, pDynFxInfo->pcDynName, pNode->hInfo); 
	pNode->bIsVisible = 1;
	pNode->pParentNode = pParentNode;

	// add it to the lookup table
	//if(pDynFxInfo)
	//{
	//	stashAddPointer(fxTracker.pTreeStash, pDynFxInfo, pNode, 1);
	//}

	if( pParentNode != NULL )
	{
		// add as a child
		eaPush(&pParentNode->ppChildren, pNode);
	}

	
	
	

	FOR_EACH_IN_EARRAY(pDynFxInfo->eaLoops, DynLoop, pLoop)
		dfxTreeAddChildren(&pLoop->childCallCollection, pNode);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pDynFxInfo->eaRaycasts, DynRaycast, pRaycast)
		FOR_EACH_IN_EARRAY(pRaycast->eaHitEvent, DynRaycastHitEvent, pEvent)
		dfxTreeAddChildren(&pEvent->childCallCollection, pNode);
	FOR_EACH_END;
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pDynFxInfo->eaContactEvents, DynContactEvent, pContactEvent)
		dfxTreeAddChildren(&pContactEvent->childCallCollection, pNode);
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pDynFxInfo->events, DynEvent, pEvent)
		FOR_EACH_IN_EARRAY(pEvent->keyFrames, DynKeyFrame, pKeyFrame)
		dfxTreeAddChildren(&pKeyFrame->childCallCollection, pNode);
	if (pKeyFrame->pParentBhvr)
	{
		FOR_EACH_IN_EARRAY(pKeyFrame->pParentBhvr->eaNearEvents, DynParentNearEvent, pNearEvent)
			dfxTreeAddChildren(&pNearEvent->childCallCollection, pNode);
		FOR_EACH_END;
	}
	FOR_EACH_END;
	FOR_EACH_END;

}

int dfxSortNodeByName(const DfxTreeNodeData** a, const DfxTreeNodeData** b)
{
	return strcmp((*a)->pchName, (*b)->pchName);
}

static void dfxTreeSortByName(DfxTreeNodeData *pTreeNodeData)
{
	// sort the children
	eaQSort(pTreeNodeData->ppChildren, dfxSortNodeByName);

	// sort the childrens' children
	FOR_EACH_IN_EARRAY(pTreeNodeData->ppChildren, DfxTreeNodeData, pChildTreeNodeData)
		dfxTreeSortByName(pChildTreeNodeData);
	FOR_EACH_END;
}

int dfxPowerArtSortByName(const DfxPowerArtData** a, const DfxPowerArtData** b)
{
	return strcmp((*b)->pchName, (*a)->pchName);
}

static void dfxPowerArtInitData()
{
	RefDictIterator iter;
	const char* pcRef = 0;
	PowerAnimFX *pPowerAnimFX;
	int i = 0;
	int count = 0;

	// only do this once
	gfxStatusPrintf("Loading Power Art Data.  Please be patient.");

	if(isDevelopmentMode())
	{
		{
			int flags = PARSER_OPTIONALFLAG;
			if (IsServer())
			{
				flags |= RESOURCELOAD_SHAREDMEMORY;
			}
			resLoadResourcesFromDisk(g_hPowerAnimFXDict,"powerart",".powerart", "PowerAnimFX.bin", flags);
		}

		resRequestAllResourcesInDictionary(g_hPowerAnimFXDict);
	}

	eaClear(&fxTracker.ppPowerArtList);
	RefSystem_InitRefDictIterator(g_hPowerAnimFXDict, &iter);
	while((pPowerAnimFX = RefSystem_GetNextReferentFromIterator(&iter)))
	{
		DfxPowerArtData *pPowerArtData = calloc(1, sizeof(DfxPowerArtData));
		pPowerArtData->pchName = strdup(pPowerAnimFX->cpchName);

		SET_HANDLE_FROM_STRING(g_hPowerAnimFXDict, pPowerAnimFX->cpchName, pPowerArtData->hPowerAnimFx);

		eaPush(&fxTracker.ppPowerArtList, pPowerArtData);
		i++;
	}
	count = i;

	eaQSort(fxTracker.ppPowerArtList, dfxPowerArtSortByName);

	dfxPowerArtUpdateFilter();
}


static void fxTrackerUpdateLoadingStatus()
{
	static char pchLabel[64];

	if(!fxTracker.bWindowActive)
	{
		return;
	}

	if(fxTracker.pLoadingProgressBar)
	{
		if(fxTracker.iTotalNumFx != 0)
		{
			F32 fProgress = (F32)fxTracker.iLoadingProgress / (F32)fxTracker.iTotalNumFx;
			ui_ProgressBarSet(fxTracker.pLoadingProgressBar, fProgress);
		}
	}

	if(fxTracker.pLoadingLabel)
	{
		if(fxTracker.bKillLoadingThread)
		{
			sprintf(pchLabel, "Loading Cancelled");
		}
		else
		{
			//sprintf_s(pchLabel, 63, "Loading %d of %d Fx", fxTracker.iLoadingProgress, fxTracker.iTotalNumFx);
			sprintf(pchLabel, "Loading...");
		}
		
		ui_LabelSetText(fxTracker.pLoadingLabel, pchLabel);
	}

	if( fxTracker.bDoneLoadingThread )
	{
		if(fxTracker.pLoadingLabel)
		{
			ui_LabelSetText(fxTracker.pLoadingLabel, "Finished Loading Fx");
		}

		if(fxTracker.pFileContentsToggle)
		{
			ui_SetActive(UI_WIDGET(fxTracker.pFileContentsToggle), true);
		}

		if(fxTracker.pFileContentsEntry)
		{
			ui_SetActive(UI_WIDGET(fxTracker.pFileContentsEntry), true);
		}
	}
}


static void fxTrackerLoadFiles(DfxTreeNodeData *pTreeNodeData)
{
	if(pTreeNodeData)
	{
		FOR_EACH_IN_EARRAY(pTreeNodeData->ppChildren, DfxTreeNodeData, pChildTreeNodeData)
			//fxTrackerLoadFiles(pChildTreeNodeData);

			if(fxTracker.bKillLoadingThread) break;

			if(!pChildTreeNodeData->dfxFileBuffer.bRead)
			{
				DynFxInfo *pDynFxInfo = GET_REF(pChildTreeNodeData->hInfo);
				if(pDynFxInfo)
				{
					dfxReadFxFile(pDynFxInfo, &pChildTreeNodeData->dfxFileBuffer);
					

					SleepEx(10, TRUE); 
				}
			}
			fxTracker.iLoadingProgress++;

			if(fxTracker.bWindowActive && fxTracker.pLoadingProgressBar)
			{
				if(fxTracker.iTotalNumFx != 0)
				{
					F32 fProgress = (F32)fxTracker.iLoadingProgress / (F32)fxTracker.iTotalNumFx;
					ui_ProgressBarSet(fxTracker.pLoadingProgressBar, fProgress);
				}
			}


		FOR_EACH_END;
	}
}

static DWORD WINAPI fxTrackerLoadFilesThread(LPVOID lpParam)
{
	DfxTreeNodeData *pTreeNodeData = fxTracker.pTreeRoot;
	fxTracker.iLoadingProgress = 0;

	fxTrackerUpdateLoadingStatus();

	fxTrackerLoadFiles(pTreeNodeData);

	if(fxTracker.iLoadingProgress >= fxTracker.iTotalNumFx)
	{
		fxTracker.bDoneLoadingThread = 1;
	}		
	fxTrackerUpdateLoadingStatus();

	return 0;
}

static void dfxTreeInitData()
{
	RefDictIterator iter;
	const char* pcRef = 0;
	DynFxInfo *pDynFxInfo;
	int i = 0;
	int count = 0;

	// only do this once

	// init the tree data
	fxTracker.pTreeRoot = calloc(1, sizeof(DfxTreeNodeData));
	fxTracker.pTreeRoot->pchName = strdup("Root");
	fxTracker.pTreeRoot->bIsVisible = 1;

	//if(!fxTracker.pTreeStash)
	//	fxTracker.pTreeStash = stashTableCreateAddress(1000);
	//else
	//	stashTableClear(fxTracker.pTreeStash);

	gfxStatusPrintf("Loading Fx Data.  Please be patient.");


	if(isDevelopmentMode())
	{
		resRequestAllResourcesInDictionary(hDynFxInfoDict);
	}

	RefSystem_InitRefDictIterator(hDynFxInfoDict, &iter);
	while((pDynFxInfo = RefSystem_GetNextReferentFromIterator(&iter)))
	{
		dynFxInfoExists(pDynFxInfo->pcDynName);

		dfxTreeAddNode(pDynFxInfo, fxTracker.pTreeRoot);

		i++;
	}
	fxTracker.iTotalNumFx = i;

	dfxTreeSortByName(fxTracker.pTreeRoot);


	ui_TreeNodeSetFillCallback(&fxTracker.pFxTree->root, dfxTreeFill, fxTracker.pTreeRoot);
	ui_TreeRefresh(fxTracker.pFxTree);


				
	fxTracker.pManagedFileLoadingThread = tmCreateThread(fxTrackerLoadFilesThread, NULL);

	//{
	//	ResourceDictionaryInfo *resDictInfo = resDictGetInfo(hDynFxInfoDict);
	//	int numEntries = eaSize(&resDictInfo->ppInfos);

	//	for(i = 0; i < numEntries; i++)
	//	{
	//		ResourceInfo *resInfo = resDictInfo->ppInfos[i];
	//		if(resInfo)
	//		{
	//			const char *name = resInfo->resourceName;
	//			dynFxInfoExists(name);
	//		}
	//	}
	//}

}


static void dfxTreeApplyFilter(DfxTreeNodeData *pTreeNodeData, DfxTreeFilter *pFilter)
{
	bool bVisible = true;
	DynFxInfo *pDynFxInfo;
	if(!pTreeNodeData) return;

	// filter by name
	if(pFilter->pchName && pFilter->pchName[0])
	{
		if( pFilter->nameSearchFunc(pTreeNodeData->pchName, pFilter->pchName) ) 
		{
			bVisible = true;
		} 
		else
		{
			bVisible = false;
		}
	}

	pDynFxInfo = GET_REF(pTreeNodeData->hInfo);
	// check if asset type required
	if(pFilter->bRequiresAssetType && pDynFxInfo && bVisible)
	{
		// check if has at least one of the required asset type
		static char **ppchAssetList = NULL;
		eaClear(&ppchAssetList);
		dfxAssetTreeGetAsset(pDynFxInfo, pFilter->assetType, &ppchAssetList);
		if(eaSize(&ppchAssetList) <= 0)
		{
			bVisible = false;
		}
		else
		{
			if(pFilter->pchAssetName && pFilter->pchAssetName[0])
			{
				bVisible = false;
				FOR_EACH_IN_EARRAY(ppchAssetList, char, pAssetName)
					if( pAssetName && pFilter->assetSearchFunc(pAssetName, pFilter->pchAssetName) ) 
					{
						bVisible = true;
						break;
					} 
					FOR_EACH_END;
			}
		}
	}

	// File Content Search
	if(pFilter->bSearchFileContents && bVisible)
	{
		bVisible = false;
		if(pTreeNodeData->dfxFileBuffer.bRead && pFilter->pchFileContents && pFilter->pchFileContents[0])
		{
			if( pFilter->fileContentsFunc(pTreeNodeData->dfxFileBuffer.pchBuffer, pFilter->pchFileContents) )
			{
				bVisible = true;
			}
		}
	}

	if(pFilter->bRequiresPriority && bVisible)
	{
		//DynFxInfo *pDynFxInfo = GET_REF(pTreeNodeData->hInfo);//(DynFxInfo*)pTreeNodeData->data;
		if(pDynFxInfo && pDynFxInfo->iPriorityLevel != pFilter->priority)
		{
			bVisible = false;
		}
	}

	if(pFilter->bOnlyLeafNodes)
	{
		if(pTreeNodeData->pParentNode && pTreeNodeData->pParentNode != fxTracker.pTreeRoot)
		{
			bVisible = false;
		}
	}

	pTreeNodeData->bIsVisible = bVisible;

	// make sure parents are visible
	if(pTreeNodeData->bIsVisible)
	{
		DfxTreeNodeData *pParentNodeData = pTreeNodeData->pParentNode;
		while(pParentNodeData)
		{
			pParentNodeData->bIsVisible = 1;
			pParentNodeData = pParentNodeData->pParentNode;
		}
	}

	// filter the children
	FOR_EACH_IN_EARRAY(pTreeNodeData->ppChildren, DfxTreeNodeData, pChildTreeNodeData)
		dfxTreeApplyFilter(pChildTreeNodeData, pFilter);
	FOR_EACH_END;
}

static void dfxTreeExpandAllNodes(UITreeNode *pTreeNode)
{
	ui_TreeNodeExpandAndCallback(pTreeNode);

	FOR_EACH_IN_EARRAY(pTreeNode->children, UITreeNode, pChildTreeNode)
		dfxTreeExpandAllNodes(pChildTreeNode);
	FOR_EACH_END;
}

static void dfxTreeUpdateExpandAll()
{
	U32 state;

	state = ui_CheckButtonGetState(fxTracker.pExpandAllButton);
	if(state)
	{
		UITreeNode *rootTreeNode = &fxTracker.pFxTree->root;
		dfxTreeExpandAllNodes(rootTreeNode);
	}
}

static void dfxTreeUpdateFilter(bool bForceUpdate)
{
	static DfxTreeFilter lastTreeFilter;
	bool bChanged = bForceUpdate;
	
	// check for changes
	if(strcmp(lastTreeFilter.pchName, fxTracker.dfxTreeFilter.pchName) != 0) bChanged = true;
	if(!bChanged && fxTracker.dfxTreeFilter.bRequiresAssetType && strcmp(lastTreeFilter.pchAssetName, fxTracker.dfxTreeFilter.pchAssetName) != 0) bChanged = true;
	if(!bChanged && fxTracker.dfxTreeFilter.bSearchFileContents && strcmp(lastTreeFilter.pchFileContents, fxTracker.dfxTreeFilter.pchFileContents) != 0) bChanged = true;

	if(!bChanged && lastTreeFilter.priority != fxTracker.dfxTreeFilter.priority) bChanged = true;
	if(!bChanged && lastTreeFilter.assetType != fxTracker.dfxTreeFilter.assetType) bChanged = true;
	if(!bChanged && lastTreeFilter.bRequiresAssetType != fxTracker.dfxTreeFilter.bRequiresAssetType) bChanged = true;
	if(!bChanged && lastTreeFilter.bAssetNameFilter != fxTracker.dfxTreeFilter.bAssetNameFilter) bChanged = true;
	if(!bChanged && lastTreeFilter.bRequiresPriority != fxTracker.dfxTreeFilter.bRequiresPriority) bChanged = true;
	if(!bChanged && lastTreeFilter.bOnlyLeafNodes != fxTracker.dfxTreeFilter.bOnlyLeafNodes) bChanged = true;
	if(!bChanged && lastTreeFilter.bSearchFileContents != fxTracker.dfxTreeFilter.bSearchFileContents) bChanged = true;

	if(bChanged)
	{


		// search functions
		fxTracker.dfxTreeFilter.nameSearchFunc = dfxSearchStringContains;
		fxTracker.dfxTreeFilter.assetSearchFunc = dfxSearchStringContains;
		fxTracker.dfxTreeFilter.fileContentsFunc = dfxSearchStringContains;

		// determine which search functions to use depending on search string
		if(strstr(fxTracker.dfxTreeFilter.pchName, "*") || strstr(fxTracker.dfxTreeFilter.pchName, "?"))
		{
			fxTracker.dfxTreeFilter.nameSearchFunc = dfxSearchStringWildcard;
		}

		if(strstr(fxTracker.dfxTreeFilter.pchAssetName, "*") || strstr(fxTracker.dfxTreeFilter.pchAssetName, "?"))
		{
			fxTracker.dfxTreeFilter.assetSearchFunc = dfxSearchStringWildcard;
		}

		if(strstr(fxTracker.dfxTreeFilter.pchFileContents, "*") || strstr(fxTracker.dfxTreeFilter.pchFileContents, "?"))
		{
			fxTracker.dfxTreeFilter.fileContentsFunc = dfxSearchStringWildcard;
		}

		dfxTreeApplyFilter(fxTracker.pTreeRoot, &fxTracker.dfxTreeFilter);

		ui_TreeNodeSetFillCallback(&fxTracker.pFxTree->root, dfxTreeFill, fxTracker.pTreeRoot);
		ui_TreeRefresh(fxTracker.pFxTree);

		ui_TreeRefresh(fxTracker.pAssetTree);
		dfxTreeExpandAllNodes(&fxTracker.pAssetTree->root);

		dfxTreeUpdateExpandAll();
	}
	
	lastTreeFilter = fxTracker.dfxTreeFilter;
}

static void dfxTreeClearFilterCB(UIAnyWidget *pWidget, UserData pUserData)
{
	ui_TextEntrySetText(fxTracker.pFilterTextEntry, "");

	// change filter settings
	strcpy(fxTracker.dfxTreeFilter.pchName, "");

	// apply it
	dfxTreeUpdateFilter(false);
}

static void dfxTreeFilterCB(UIAnyWidget *pWidget, UserData pUserData)
{
	UITextEntry *pTextEntry = (UITextEntry*)pWidget;

	ui_CheckButtonSetState(fxTracker.pExpandAllButton, true);
	dfxTreeUpdateExpandAll();

	strncpy(fxTracker.dfxTreeFilter.pchName, ui_TextEntryGetText(pTextEntry), MaxFilterStringLength);

	dfxTreeUpdateFilter(false);
}

void dfxAssetTabChangedCB(UIWidget *pWidget, UserData pUserData)
{
	if(fxTracker.pAssetTree)
	{
		ui_TreeRefresh(fxTracker.pAssetTree);
	}
}

void dfxTreeExpandAllCB(UIWidget *pWidget, UserData pUserData)
{
	dfxTreeUpdateExpandAll();
}

void dfxInfoPropertyName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	FieldProperty *fieldProperties = (FieldProperty*)pDrawData;

	estrPrintf(estrOutput, "%s", fieldProperties[iRow].pchName);
}

void dfxInfoPropertyValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	UITreeNode* pTreeNode = fxTracker.pFxTree->selected;
	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)fxTracker.pSelectedTreeNode->contents;
		DynFxInfo *pFxInfo = GET_REF(pTreeNodeData->hInfo);//(DynFxInfo*)pTreeNodeData->data;
		if(pFxInfo)
		{
			FieldProperty *fieldProperties = (FieldProperty*)pDrawData;
			if(fieldProperties[iRow].fpFunc)
			{
				fieldProperties[iRow].fpFunc((char*)pFxInfo + fieldProperties[iRow].offset, estrOutput);
			}
			else
			{
				estrPrintf(estrOutput, FORMAT_OK(fieldProperties[iRow].pchFormat), (char*)pFxInfo + fieldProperties[iRow].offset);
			}
		}
	}
}

char ***dfxPriorityChoices()
{
	static char **ppPriorityChoices = NULL;
	if(!ppPriorityChoices)
	{
		eaPush(&ppPriorityChoices, "Override");
		eaPush(&ppPriorityChoices, "Critical");
		eaPush(&ppPriorityChoices, "Default");
		eaPush(&ppPriorityChoices, "Detail");
		eaPush(&ppPriorityChoices, "Not Set");
	}
	return &ppPriorityChoices;
}

char ***dfxAssetChoices()
{
	static char **ppAssetChoices = NULL;
	if(!ppAssetChoices)
	{
		eaPush(&ppAssetChoices, "Audio");
		eaPush(&ppAssetChoices, "Geometry");
		eaPush(&ppAssetChoices, "Material");
		eaPush(&ppAssetChoices, "Particle Emitter");
		eaPush(&ppAssetChoices, "Texture");
	}
	return &ppAssetChoices;
}

void dfxTreeAssetNameFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	strncpy(fxTracker.dfxTreeFilter.pchAssetName, ui_TextEntryGetText((UITextEntry*)pWidget), MaxFilterStringLength);

	dfxTreeUpdateFilter(false);
}

void dfxTreeFileContentsStringCB(UIAnyWidget *pWidget, UserData userData)
{
	strncpy(fxTracker.dfxTreeFilter.pchFileContents, ui_TextEntryGetText((UITextEntry*)pWidget), MaxFilterStringLength);

	dfxTreeUpdateFilter(false);
}

void dfxTreeFileContentsToggleCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.bSearchFileContents = ui_CheckButtonGetState((UICheckButton*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreePriorityFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.priority = ui_ComboBoxGetSelected((UIComboBox*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreeLeafNodesToggleCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.bOnlyLeafNodes = ui_CheckButtonGetState((UICheckButton*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreeAssetNameFilterToggleCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.bAssetNameFilter = ui_CheckButtonGetState((UICheckButton*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreePriorityToggleCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.bRequiresPriority = ui_CheckButtonGetState((UICheckButton*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreePlayEffectCB(UIAnyWidget *pWidget, UserData userData)
{
	UITreeNode* pTreeNode = fxTracker.pFxTree->selected;
	if(pTreeNode)
	{
		DfxTreeNodeData *pTreeNodeData = (DfxTreeNodeData*)pTreeNode->contents;
		DynFxInfo *pFxInfo = GET_REF(pTreeNodeData->hInfo);
		if(pFxInfo)
		{
			const char *pchName = pFxInfo->pcDynName;
			dtTestFx(pchName);
		}
	}
}

void dfxTreeReferencesAssetToggleCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.bRequiresAssetType = ui_CheckButtonGetState((UICheckButton*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreeAssetFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxTreeFilter.assetType = ui_ComboBoxGetSelected((UIComboBox*)pWidget);
	dfxTreeUpdateFilter(false);
}

void dfxTreeCancelLoadingCB(UIAnyWidget *pWidget, UserData userData);

void dfxTreeBeginLoadingCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.bKillLoadingThread = 0;

	ui_ButtonSetText(fxTracker.pCancelLoadButton, "Cancel");
	ui_ButtonSetCallback(fxTracker.pCancelLoadButton, dfxTreeCancelLoadingCB, NULL);

	fxTracker.pManagedFileLoadingThread = tmCreateThread(fxTrackerLoadFilesThread, NULL);

	fxTrackerUpdateLoadingStatus();
}

void dfxTreeCancelLoadingCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.bKillLoadingThread = 1;

	ui_ButtonSetText(fxTracker.pCancelLoadButton, "Load");
	ui_ButtonSetCallback(fxTracker.pCancelLoadButton, dfxTreeBeginLoadingCB, NULL);

	fxTrackerUpdateLoadingStatus();
}

static void dfxSetupHierarchyTab(UITab *pTab)
{
	F32 x, y;
	UILabel *pLabel;
	UIListColumn *pCol;
	static void **fakeVarEArray = NULL;

	fxTracker.pHierarchyPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	fxTracker.pHierarchyPane->widget.pOverrideSkin = fxTracker.pSkin;

	x = 5;
	y = 5;

	pLabel = ui_LabelCreate("Filter", x, y);
	ui_PaneAddChild(fxTracker.pHierarchyPane, pLabel);

	// Filter Entry
	fxTracker.pFilterTextEntry = ui_TextEntryCreate("", x + 60, y);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pFilterTextEntry), 0.4, UIUnitPercentage);
	ui_TextEntrySetChangedCallback(fxTracker.pFilterTextEntry, dfxTreeFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pFilterTextEntry);

	ui_PaneAddChild(fxTracker.pHierarchyPane, ui_ButtonCreate("x", 40, y, dfxTreeClearFilterCB, NULL));


	// Setup the tree
	fxTracker.pFxTree = ui_TreeCreate(x, y + 20, 1.0, 1.0);
	ui_TreeSetMultiselect(fxTracker.pFxTree, true);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pFxTree), 0, 0, 120, 55);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pFxTree), 0.4, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TreeSetSelectedCallback(fxTracker.pFxTree, dfxTreeNodeSelectedCB, NULL);
	//ui_TreeSetActivatedCallback(fxTracker.pFxTree, dfxTreeActivateEffectCB, NULL);
	ui_TreeSetActivatedCallback(fxTracker.pFxTree, dfxTreePlayEffectCB, NULL);
	
	ui_TreeSetContextCallback(fxTracker.pFxTree, dfxTreeRightClickContextCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pFxTree);
	//ui_ListSetCellActivatedCallback(aDebugUI.fmodEvents.eventInstancesList, aDebugActivateEventInstance, NULL);

	// Expand All
	fxTracker.pExpandAllButton = ui_CheckButtonCreate(0, 0, "Expand All", false);
	ui_CheckButtonSetToggledCallback(fxTracker.pExpandAllButton, dfxTreeExpandAllCB, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pExpandAllButton), 0, 30, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pExpandAllButton);

	// Num Selected
	fxTracker.pNumSelectedLabel = ui_LabelCreate("", 150, 0);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pNumSelectedLabel), 0, 0, 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pNumSelectedLabel), 150, 30, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pNumSelectedLabel);

	// Loading Progress Bar
	fxTracker.pLoadingProgressBar = ui_ProgressBarCreate(0, 0, 100);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pLoadingProgressBar), 0, 7, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pLoadingProgressBar);

	// Cancel Loading
	fxTracker.pCancelLoadButton = ui_ButtonCreate("Cancel", 0, 0, dfxTreeCancelLoadingCB, NULL);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pCancelLoadButton), 5, 5, 5, 0);
	if(fxTracker.bKillLoadingThread && !fxTracker.bDoneLoadingThread)
	{
		ui_ButtonSetText(fxTracker.pCancelLoadButton, "Load");
		ui_ButtonSetCallback(fxTracker.pCancelLoadButton, dfxTreeBeginLoadingCB, NULL);
	}
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pCancelLoadButton), 60, 25, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pCancelLoadButton), 105, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pCancelLoadButton);

	// Loading Progress Bar
	fxTracker.pLoadingLabel = ui_LabelCreate("", 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pLoadingLabel), 170, 3, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pLoadingLabel);




	// Play Effect Button
	fxTracker.pPlayEffectButton = ui_ButtonCreate("Play", x, y, dfxTreePlayEffectCB, NULL);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pPlayEffectButton), 5, 5, 5, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pPlayEffectButton), 50, 25, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pPlayEffectButton), 0, 0, 0, 0, UITopRight);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pPlayEffectButton);

	// Selected Name
	fxTracker.pFxNameLabel = ui_LabelCreate("", x, y);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pFxNameLabel), 5, 5, 5, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pFxNameLabel), 0.59, 25, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pFxNameLabel), 0, 0, 0, 0, UITopRight);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pFxNameLabel);

	


	// Must reference asset
	fxTracker.pReferencesAssetToggle = ui_CheckButtonCreate(5, y + 30, "References", false);
	ui_CheckButtonSetToggledCallback(fxTracker.pReferencesAssetToggle, dfxTreeReferencesAssetToggleCB, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pReferencesAssetToggle), 0, 0, 0, 0, UITopLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pReferencesAssetToggle);

	// Asset Choice
	fxTracker.pSelectedAsset = (UIComboBox*)ui_ComboBoxCreate(110, y + 30, 100, NULL, dfxAssetChoices(), NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pSelectedAsset), 120, UIUnitFixed); // appears to require width setting here.
	ui_ComboBoxSetSelectedCallback(fxTracker.pSelectedAsset, dfxTreeAssetFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pSelectedAsset);
	ui_ComboBoxSetSelected(fxTracker.pSelectedAsset, 0);
	// Toggle Asset Name Filter 
	//fxTracker.pAssetNameFilterToggle = ui_CheckButtonCreate(25, y + 60, "contains name", false);
	//ui_CheckButtonSetToggledCallback(fxTracker.pAssetNameFilterToggle, dfxTreeAssetNameFilterToggleCB, NULL);
	////ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pAssetNameFilterToggle), 0, 0, 0, 0, UITopLeft);
	//ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pAssetNameFilterToggle);

	// Filter Entry
	fxTracker.pAssetFilterEntry = ui_TextEntryCreate("", x + 230, y+30);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pAssetFilterEntry), 0.4, UIUnitPercentage);
	ui_TextEntrySetChangedCallback(fxTracker.pAssetFilterEntry, dfxTreeAssetNameFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pAssetFilterEntry);

	// Use Priority
	fxTracker.pPriorityToggle = ui_CheckButtonCreate(5, y + 60, "Priority", false);
	ui_CheckButtonSetToggledCallback(fxTracker.pPriorityToggle, dfxTreePriorityToggleCB, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pReferencesAssetToggle), 0, 0, 0, 0, UITopLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pPriorityToggle);

	// Selected Priority
	fxTracker.pSelectedPriority = (UIComboBox*)ui_ComboBoxCreate(110, y + 60, 100, NULL, dfxPriorityChoices(), NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pSelectedPriority), 120, UIUnitFixed); // appears to require width setting here.
	ui_ComboBoxSetSelectedCallback(fxTracker.pSelectedPriority, dfxTreePriorityFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pSelectedPriority);
	ui_ComboBoxSetSelected(fxTracker.pSelectedPriority, 0);



	// Search File Contents
	fxTracker.pFileContentsToggle = ui_CheckButtonCreate(5, y + 90, "File Contents", false);
	ui_SetActive(UI_WIDGET(fxTracker.pFileContentsToggle), false);
	ui_CheckButtonSetToggledCallback(fxTracker.pFileContentsToggle, dfxTreeFileContentsToggleCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pFileContentsToggle);

	// File Contents Search String
	fxTracker.pFileContentsEntry = ui_TextEntryCreate("", 110, y+90);
	ui_SetActive(UI_WIDGET(fxTracker.pFileContentsEntry), false);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pFileContentsEntry), 0.4, UIUnitPercentage);
	ui_TextEntrySetChangedCallback(fxTracker.pFileContentsEntry, dfxTreeFileContentsStringCB, NULL);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pFileContentsEntry);


	// Only Leaf Nodes
	fxTracker.pOnlyLeafNodesToggle = ui_CheckButtonCreate(5, y + 120, "Only Leaf Nodes", false);
	ui_CheckButtonSetToggledCallback(fxTracker.pOnlyLeafNodesToggle, dfxTreeLeafNodesToggleCB, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pReferencesAssetToggle), 0, 0, 0, 0, UITopLeft);
	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pOnlyLeafNodesToggle);







	// Properties Tab | Source View
	fxTracker.pPropertyTabGroup = ui_TabGroupCreate(0, 0, 300, 30);

	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pPropertyTabGroup), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pPropertyTabGroup), 5, 5, 30, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pPropertyTabGroup), 0.59, 1, UIUnitPercentage, UIUnitPercentage);

	ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pPropertyTabGroup);


	fxTracker.pAssetsTab = ui_TabCreate("Assets");
	ui_TabGroupAddTab(fxTracker.pPropertyTabGroup, fxTracker.pAssetsTab);

	fxTracker.pPropertiesTab = ui_TabCreate("Properties");
	ui_TabGroupAddTab(fxTracker.pPropertyTabGroup, fxTracker.pPropertiesTab);

	// Property List
	if(!fakeVarEArray) 
	{
		int i;
		int iNumElements = (sizeof(dynFxInfoFieldProperties)/sizeof(FieldProperty))-1;

		eaSetSize(&fakeVarEArray, iNumElements);
		for(i=0; eaSize(&fakeVarEArray) < iNumElements; i++)
		{
			eaPush(&fakeVarEArray, NULL);
		}
	}

	fxTracker.pFxInfoPropertyList = ui_ListCreate(NULL, &fakeVarEArray, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(fxTracker.pFxInfoPropertyList), 1);

	pCol = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)dfxInfoPropertyName, dynFxInfoFieldProperties);
	ui_ListAppendColumn(fxTracker.pFxInfoPropertyList, pCol);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)dfxInfoPropertyValue, dynFxInfoFieldProperties);
	ui_ListAppendColumn(fxTracker.pFxInfoPropertyList, pCol);

	//ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pFxInfoPropertyList), 5, 5, 30, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pFxInfoPropertyList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	//ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pFxInfoPropertyList), 0.59, 200, UIUnitPercentage, UIUnitFixed);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pFxInfoPropertyList), 0, 0, 0, 0, UITopRight);

	//ui_PaneAddChild(fxTracker.pHierarchyPane, UI_WIDGET(fxTracker.pFxInfoPropertyList));

	ui_TabAddChild(fxTracker.pPropertiesTab, UI_WIDGET(fxTracker.pFxInfoPropertyList));


	fxTracker.pFxSourceTab = ui_TabCreate("Source");
	ui_TabGroupAddTab(fxTracker.pPropertyTabGroup, fxTracker.pFxSourceTab);


	// Text Area for Source Viewing
	fxTracker.pFxSourceTextArea = ui_TextAreaCreate("");
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pFxSourceTextArea), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pFxSourceTextArea), 0, 0, 0, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pFxSourceTextArea), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(fxTracker.pFxSourceTab, fxTracker.pFxSourceTextArea);



	// Asset View
	fxTracker.pAssetTabGroup = ui_TabGroupCreate(0, 0, 300, 30);
	ui_TabGroupSetChangedCallback(fxTracker.pAssetTabGroup, dfxAssetTabChangedCB, NULL);
	ui_TabGroupAddTab(fxTracker.pAssetTabGroup, ui_TabCreate("Audio"));
	ui_TabGroupAddTab(fxTracker.pAssetTabGroup, ui_TabCreate("Geometry"));
	ui_TabGroupAddTab(fxTracker.pAssetTabGroup, ui_TabCreate("Material"));
	ui_TabGroupAddTab(fxTracker.pAssetTabGroup, ui_TabCreate("Particle Emitter"));
	ui_TabGroupAddTab(fxTracker.pAssetTabGroup, ui_TabCreate("Texture"));

	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pAssetTabGroup), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pAssetTabGroup), 1, 35.0, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pAssetTabGroup), 0, 0, 10, 0);

	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pAssetTabGroup), 0, 0, 0, 0, UITopRight);
	//ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pAssetTabGroup), 5, 5, 400+30, 0);
	//ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pAssetTabGroup), 0.59, 300, UIUnitPercentage, UIUnitFixed);


	//ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pAssetTabGroup);
	ui_TabAddChild(fxTracker.pAssetsTab, fxTracker.pAssetTabGroup);

	// Asset Tree View
	fxTracker.pAssetTree = ui_TreeCreate(x, 30, 1.0, 1.0);
	fxTracker.pAssetTree->widget.sb->scrollX = true; // horiz scroll
	//ui_TreeSetSelectedCallback(fxTracker.pAssetTree, dfxTreeNodeSelectedCB, NULL);
	//ui_TreeSetActivatedCallback(fxTracker.pAssetTree, dfxTreeActivateEffectCB, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pAssetTree), 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pAssetTree), 5, 5, 38, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pAssetTree), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	//ui_PaneAddChild(fxTracker.pHierarchyPane, fxTracker.pAssetTree);
	ui_TabAddChild(fxTracker.pAssetsTab, fxTracker.pAssetTree);


	// make sure the tree is setup correctly
	memset(&fxTracker.dfxTreeFilter, 0, sizeof(fxTracker.dfxTreeFilter));
	dfxTreeUpdateFilter(true);
	ui_TreeNodeExpandAndCallback(&fxTracker.pFxTree->root);


	fxTrackerUpdateLoadingStatus();

	// wait until after all is setup to select tab
	ui_TabGroupSetActiveIndex(fxTracker.pAssetTabGroup, 0); // select first tab

	// wait until after all is setup to select tab
	ui_TabGroupSetActiveIndex(fxTracker.pPropertyTabGroup, 0); // select first tab


	ui_TabAddChild(pTab, fxTracker.pHierarchyPane);
}

//
// Power Art
//

char ***dfxPowerArtFxChoices()
{
	static char **ppPowerArtFxChoices = NULL;
	if(!ppPowerArtFxChoices)
	{
		eaPush(&ppPowerArtFxChoices, "StanceStickyFX");
		eaPush(&ppPowerArtFxChoices, "StanceFlashFX");
		eaPush(&ppPowerArtFxChoices, "ChargeStickyFX");
		eaPush(&ppPowerArtFxChoices, "ChargeFlashFX");
		eaPush(&ppPowerArtFxChoices, "LungeStickyFX");
		eaPush(&ppPowerArtFxChoices, "LungeFlashFX");
		eaPush(&ppPowerArtFxChoices, "ActivateStickyFX");
		eaPush(&ppPowerArtFxChoices, "ActivateFX");
		eaPush(&ppPowerArtFxChoices, "PeriodicActivateFX");
		eaPush(&ppPowerArtFxChoices, "DeactivateFX");
		eaPush(&ppPowerArtFxChoices, "TargetedFX");
		eaPush(&ppPowerArtFxChoices, "HitFX");
		eaPush(&ppPowerArtFxChoices, "HitStickyFX");
		eaPush(&ppPowerArtFxChoices, "BlockFX");
		eaPush(&ppPowerArtFxChoices, "DeathFX");
		eaPush(&ppPowerArtFxChoices, "*Any FX*");
	}
	return &ppPowerArtFxChoices;
}


// Does a string compare to see if srcStr matches searchStr.  NOT case sensitive
// searchStr can contain DOS style wildcard characters such as '*' and '?'
// returns TRUE if they match, FALSE otherwise
static bool dfxSearchStringContains(const char *srcStr, const char *searchStr )
{
	return strstri(srcStr, searchStr) != NULL;
}

static bool dfxSearchStringWildcard(const char *srcStr, const char *searchStr )
{
	bool bMatchFail;
	const char *srcStrEnd = srcStr + strlen(srcStr);
	const char *searchStrEnd = searchStr + strlen(searchStr);
	const char *lastWildcard = NULL;
	const char *nextRetry = NULL;

	while (srcStr < srcStrEnd && searchStr < searchStrEnd)
	{
		switch(*searchStr)
		{
		case '*':
			searchStr++;
			// skip consecutive '*' 
			while (searchStr < searchStrEnd && *searchStr == '*') 
				searchStr++;   

			// end of searchStr is all '*', so match
			if (searchStr == searchStrEnd) 
				return TRUE; 

			lastWildcard = searchStr;

			// skip to the next match after the wildcard(s) 
			while (srcStr < srcStrEnd && (toupper(*srcStr) != toupper(*searchStr))) 
				srcStr++;

			nextRetry = srcStr;
			break;

		case '?':
			searchStr++;
			srcStr++;
			break;

		default:
			bMatchFail = (toupper(*searchStr) != toupper(*srcStr));

			if (!bMatchFail)
			{
				searchStr++;
				srcStr++;
				if (searchStr == searchStrEnd)
				{
					if (srcStr == srcStrEnd) 
						return TRUE;
					if (lastWildcard) 
						searchStr = lastWildcard;
				}
			}
			else
			{
				// check if we had an '*', so we can try unlimitedly
				if (lastWildcard) 
				{
					searchStr = lastWildcard;

					// scan sequence was a bMatchFail, so restart 1 char after the first char we checked last time 
					nextRetry++;
					srcStr = nextRetry;
				}
				else 
					return FALSE; 
			}
			break;
		}
	}

	// Ignore trailing '.' or '*' in searchStr
	while ( searchStr < searchStrEnd && ((*searchStr == '.') || (*searchStr == '*')) )
		searchStr++;  
	return (srcStr == srcStrEnd && searchStr == searchStrEnd);
}

static void dfxPowerArtUpdateFilter()
{
	DfxPowerArtFilter *pPowerArtFilter = &fxTracker.dfxPowerArtFilter;

	eDfxPowerArtFxType iType = pPowerArtFilter->iType;

	// default search functions
	DfxSearchFunc filterSearchFunc = dfxSearchStringContains;
	DfxSearchFunc filterFxSearchFunc = dfxSearchStringContains;

	// determine which search functions to use depending on search string
	if(strstr(pPowerArtFilter->pchName, "*") || strstr(pPowerArtFilter->pchName, "?"))
	{
		filterSearchFunc = dfxSearchStringWildcard;
	}
	if(strstr(pPowerArtFilter->pchFxName, "*") || strstr(pPowerArtFilter->pchFxName, "?"))
	{
		filterFxSearchFunc = dfxSearchStringWildcard;
	}

	eaClear(&fxTracker.ppFilteredPowerArtList);
	FOR_EACH_IN_EARRAY(fxTracker.ppPowerArtList, DfxPowerArtData, pPowerArtData)

		bool bVisible = true;

		// filter by name
		if(pPowerArtFilter->pchName && pPowerArtFilter->pchName[0])
		{
			if( filterSearchFunc(pPowerArtData->pchName, pPowerArtFilter->pchName) ) 
			{
				bVisible = true;
			} 
			else
			{
				bVisible = false;
			}
		}

		if(bVisible && pPowerArtFilter->bRequiresFx)
		{
			static const char **ppcFxNames = NULL;
			PowerAnimFX *pPowerAnimFx = GET_REF(pPowerArtData->hPowerAnimFx);

			bVisible = false;

			if(pPowerAnimFx)
			{
				eaClear(&ppcFxNames);

				if(iType < ePowerArtFx_EndOfList)
				{
					dfxPowerArtPowerAnimFXByType(pPowerAnimFx, iType, &ppcFxNames);
				}
				else if(iType == ePowerArtFx_EndOfList) // scan all
				{
					eDfxPowerArtFxType iTypeIter;
					for(iTypeIter = ePowerArtFx_StanceStickyFX; iTypeIter < ePowerArtFx_EndOfList; iTypeIter++)
					{
						dfxPowerArtPowerAnimFXByType(pPowerAnimFx, iTypeIter, &ppcFxNames);
					}
				}

				// do we have a string to test with?
				if(pPowerArtFilter->pchFxName && pPowerArtFilter->pchFxName[0])
				{
					bool bMatch = false;
					FOR_EACH_IN_EARRAY(ppcFxNames, const char, pcName)
						if( filterFxSearchFunc(pcName, pPowerArtFilter->pchFxName) ) 
						{
							bMatch = true;
							break;
						} 
						FOR_EACH_END;

						bVisible = bMatch;
				}
				else if(eaSize(&ppcFxNames) > 0) // only check whether there are fx of the selected type
				{
					bVisible = true;
				}
			}
		}

		if(bVisible)
		{
			eaPush(&fxTracker.ppFilteredPowerArtList, pPowerArtData);
		}

	FOR_EACH_END;

	{
		char txt[64];
		sprintf(txt, "Results %d", eaSize(&fxTracker.ppFilteredPowerArtList));
		ui_LabelSetText(fxTracker.pNumPowerArtFilteredLabel, txt);
	}
}

static void dfxPowerArtFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	const unsigned char *pcFilterText = ui_TextEntryGetText((UITextEntry*)pWidget);
	strncpy(fxTracker.dfxPowerArtFilter.pchName, pcFilterText, MaxFilterStringLength);
	dfxPowerArtUpdateFilter();
}

static void dfxPowerArtClearFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	ui_TextEntrySetText(fxTracker.pPowerArtFilterTextEntry, "");
	strcpy(fxTracker.dfxPowerArtFilter.pchName, "");
	dfxPowerArtUpdateFilter();
}

static void dfxPowerArtFxNameFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	const unsigned char *pcFilterText = ui_TextEntryGetText((UITextEntry*)pWidget);
	strncpy(fxTracker.dfxPowerArtFilter.pchFxName, pcFilterText, MaxFilterStringLength);
	dfxPowerArtUpdateFilter();
}

static void dfxPowerArtFxTypeFilterCB(UIAnyWidget *pWidget, UserData userData)
{
	fxTracker.dfxPowerArtFilter.bRequiresFx = ui_CheckButtonGetState(fxTracker.pPowerArtFilterTypeToggle);
	fxTracker.dfxPowerArtFilter.iType = ui_ComboBoxGetSelected(fxTracker.pPowerArtFxFilterType);

	dfxPowerArtUpdateFilter();
}

static const char *dfxPowerArtFxNameByType(eDfxPowerArtFxType iType)
{
	const char *pcResult = "Unknown";
	char ***pppPowerArtNames = dfxPowerArtFxChoices();
	if(pppPowerArtNames && *pppPowerArtNames)
	{
		if(iType < eaSize(pppPowerArtNames))
		{
			pcResult = (const char*)(*pppPowerArtNames)[iType];
		}
	}
	return pcResult;
}

static void dfxPowerArtAppendFx(PowerAnimFX *pPowerAnimFX, eDfxPowerArtFxType iType, DfxPowerArtFx ***pppPowerArtFx)
{
	const char **ppchNames = NULL;
	dfxPowerArtPowerAnimFXByType(pPowerAnimFX, iType, &ppchNames);

	FOR_EACH_IN_EARRAY(ppchNames, const char, pcName)
		DfxPowerArtFx *pPowerArtFx = (DfxPowerArtFx*)calloc(1, sizeof(DfxPowerArtFx));
	pPowerArtFx->pchName = strdup(pcName);
	pPowerArtFx->pchFxName = strdup(dfxPowerArtFxNameByType(iType));
	pPowerArtFx->iType = iType;
	eaPush(pppPowerArtFx, pPowerArtFx);
	FOR_EACH_END;
}

static void dfxPowerReleaseFx(DfxPowerArtFx ***pppPowerArtFx)
{
	if(pppPowerArtFx)
	{
		FOR_EACH_IN_EARRAY(*pppPowerArtFx, DfxPowerArtFx, pPowerArtFx)
			SAFE_FREE(pPowerArtFx->pchFxName);
		SAFE_FREE(pPowerArtFx->pchName);
		free(pPowerArtFx);
		FOR_EACH_END;
		eaClear(pppPowerArtFx);
	}
}

static void dfxPowerArtPowerAnimFXByType(PowerAnimFX *pPowerAnimFX, eDfxPowerArtFxType iType, const char ***pppcNames)
{
	if(pPowerAnimFX)
	{
		switch(iType)
		{
		case ePowerArtFx_StanceStickyFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchStanceStickyFX);
			break;
		case ePowerArtFx_StanceFlashFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchStanceFlashFX);
			break;
		case ePowerArtFx_ChargeStickyFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchChargeStickyFX);
			break;
		case ePowerArtFx_ChargeFlashFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchChargeFlashFX);
			break;
		case ePowerArtFx_LungeStickyFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchLungeStickyFX);
			break;
		case ePowerArtFx_LungeFlashFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchLungeFlashFX);
			break;
		case ePowerArtFx_ActivateStickyFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchActivateStickyFX);
			break;
		case ePowerArtFx_ActivateFX:
			FOR_EACH_IN_EARRAY(pPowerAnimFX->ppActivateFX, PowerActivateFX, pPowerActivateFX)
				eaPush(pppcNames, pPowerActivateFX->pchActivateFX);
			FOR_EACH_END;
			break;
		case ePowerArtFx_ActivateMissFX:
			FOR_EACH_IN_EARRAY(pPowerAnimFX->ppActivateMissFX, PowerActivateFX, pPowerActivateFX)
				eaPush(pppcNames, pPowerActivateFX->pchActivateFX);
			FOR_EACH_END;
			break;
		case ePowerArtFx_PeriodicActivateFX:
			FOR_EACH_IN_EARRAY(pPowerAnimFX->ppPeriodicActivateFX, PowerActivateFX, pPowerActivateFX)
				eaPush(pppcNames, pPowerActivateFX->pchActivateFX);
			FOR_EACH_END;
			break;
		case ePowerArtFx_DeactivateFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchDeactivateFX);
			break;
		case ePowerArtFx_TargetedFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchTargetedFX);
			break;
		case ePowerArtFx_HitFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchHitFX);
			break;
		case ePowerArtFx_HitStickyFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchHitStickyFX);
			break;
		case ePowerArtFx_BlockFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchBlockFX);
			break;
		case ePowerArtFx_DeathFX:
			eaPushEArray(pppcNames, &pPowerAnimFX->ppchDeathFX);
			break;
		}
	}
}


static void dfxPowerArtSelectedCB(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	// extract relevant data
	DfxPowerArtData **ppPowerArtData = (DfxPowerArtData**)(*pList->peaModel);

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	if(ppPowerArtData && *ppPowerArtData && iRow < eaSize(&ppPowerArtData))
	{
		DfxPowerArtData *pPowerArtData = ppPowerArtData[iRow];
		if(pPowerArtData)
		{
			PowerAnimFX *pPowerAnimFX = GET_REF(pPowerArtData->hPowerAnimFx);
			if(pPowerAnimFX)
			{
				eDfxPowerArtFxType iType;
				// update the selected data
				dfxPowerReleaseFx(&fxTracker.ppPowerArtFxList);

				// add them all
				for(iType = ePowerArtFx_StanceStickyFX; iType < ePowerArtFx_EndOfList; iType++)
				{
					dfxPowerArtAppendFx(pPowerAnimFX, iType, &fxTracker.ppPowerArtFxList);
				}
			}
			ui_LabelSetText(fxTracker.pSelectedPowerArtName, pPowerArtData->pchName);
		}
		else
		{
			ui_LabelSetText(fxTracker.pSelectedPowerArtName, "");
		}
	}
	else
	{
		ui_LabelSetText(fxTracker.pSelectedPowerArtName, "");
	}
}

static void dfxPowerArtActivatedCB(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	// extract relevant data
	DfxPowerArtData **ppPowerArtData = (DfxPowerArtData**)(*pList->peaModel);

	if(ppPowerArtData && *ppPowerArtData && iRow < eaSize(&ppPowerArtData))
	{
		DfxPowerArtData *pPowerArtData = ppPowerArtData[iRow];
		if(pPowerArtData)
		{
			PowerAnimFX *pPowerAnimFX = GET_REF(pPowerArtData->hPowerAnimFx);
			if(pPowerAnimFX)
			{
				dfxEditFile(pPowerAnimFX->cpchFile);
			}
		}
	}
}

static void dfxPowerArtFxActivatedCB(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	DfxPowerArtFx **ppPowerArtFx = (DfxPowerArtFx**)(*pList->peaModel);
	if(ppPowerArtFx)
	{
		DfxPowerArtFx *pPowerArtFx = ppPowerArtFx[iRow];

		strncpy(fxTracker.dfxTreeFilter.pchName, pPowerArtFx->pchName, MaxFilterStringLength);
		ui_TextEntrySetText(fxTracker.pFilterTextEntry, pPowerArtFx->pchName);

		// turn these off
		fxTracker.dfxTreeFilter.bRequiresAssetType = 0; 
		fxTracker.dfxTreeFilter.bRequiresPriority = 0;



		// switch to the tab before updating the filter (as the tab may not have been loaded)
		ui_TabGroupSetActiveIndex(fxTracker.pTabGroup, 1);

		dfxTreeUpdateFilter(true);
	}
}

// main power art list
static void dfxPowerArtNameCB(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	DfxPowerArtData **ppPowerArtData = (DfxPowerArtData**)(*pList->peaModel);
	DfxPowerArtData *pPowerArtData = ppPowerArtData[iRow];

	estrPrintf(estrOutput, "%s", pPowerArtData->pchName);
}

static void dfxPowerArtFxTypeCB(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	DfxPowerArtFx **ppPowerArtFx = (DfxPowerArtFx**)(*pList->peaModel);
	if(ppPowerArtFx)
	{
		DfxPowerArtFx *pPowerArtFx = ppPowerArtFx[iRow];
		estrPrintf(estrOutput, "%s", pPowerArtFx->pchFxName);
	}
}

static void dfxPowerArtFxNameCB(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	DfxPowerArtFx **ppPowerArtFx = (DfxPowerArtFx**)(*pList->peaModel);
	if(ppPowerArtFx)
	{
		DfxPowerArtFx *pPowerArtFx = ppPowerArtFx[iRow];
		estrPrintf(estrOutput, "%s", pPowerArtFx->pchName);
	}
}

static void dfxSetupPowerArtTab(UITab *pTab)
{
	F32 x, y;
	UILabel *pLabel;
	UIListColumn *pCol;
	static void **fakeVarEArray = NULL;
	F32 rowHeight = gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP;

	fxTracker.pPowerArtPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	fxTracker.pPowerArtPane->widget.pOverrideSkin = fxTracker.pSkin;

	x = 5;
	y = 5;


	// Selected Name
	//fxTracker.pSelectedPowerArtName = ui_LabelCreate("", x, y);
	//ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 5, 5, 5, 0);
	//ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 0.59, 25, UIUnitPercentage, UIUnitFixed);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 0, 0, 0, 0, UITopRight);
	//ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pSelectedPowerArtName);


	pLabel = ui_LabelCreate("Filter", x, y);
	ui_PaneAddChild(fxTracker.pPowerArtPane, pLabel);

	// Filter Entry
	fxTracker.pPowerArtFilterTextEntry = ui_TextEntryCreate("", x + 60, y);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pPowerArtFilterTextEntry), 0.4, UIUnitPercentage);
	ui_TextEntrySetChangedCallback(fxTracker.pPowerArtFilterTextEntry, dfxPowerArtFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtFilterTextEntry);

	ui_PaneAddChild(fxTracker.pPowerArtPane, ui_ButtonCreate("x", 40, y, dfxPowerArtClearFilterCB, NULL));

	// Must reference asset
	fxTracker.pPowerArtFilterTypeToggle = ui_CheckButtonCreate(5, y + 30, "References", false);
	ui_CheckButtonSetToggledCallback(fxTracker.pPowerArtFilterTypeToggle, dfxPowerArtFxTypeFilterCB, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pReferencesAssetToggle), 0, 0, 0, 0, UITopLeft);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtFilterTypeToggle);

	// Power Art Fx Type Filter
	fxTracker.pPowerArtFxFilterType = (UIComboBox*)ui_ComboBoxCreate(100, y + 30, 100, NULL, dfxPowerArtFxChoices(), NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pPowerArtFxFilterType), 130, UIUnitFixed); // appears to require width setting here.
	ui_ComboBoxSetSelectedCallback(fxTracker.pPowerArtFxFilterType, dfxPowerArtFxTypeFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtFxFilterType);
	ui_ComboBoxSetSelected(fxTracker.pPowerArtFxFilterType, 0);

	// Power Art Fx Filter Entry
	fxTracker.pPowerArtFxFilterNameEntry = ui_TextEntryCreate("", x + 230, y+30);
	ui_WidgetSetWidthEx(UI_WIDGET(fxTracker.pPowerArtFxFilterNameEntry), 0.4, UIUnitPercentage);
	ui_TextEntrySetChangedCallback(fxTracker.pPowerArtFxFilterNameEntry, dfxPowerArtFxNameFilterCB, NULL);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtFxFilterNameEntry);

	// Setup the list
	fxTracker.pPowerArtList = ui_ListCreate(NULL, &fxTracker.ppFilteredPowerArtList, rowHeight);
	ui_ListSetMultiselect(fxTracker.pPowerArtList, true);
	ui_WidgetSetClickThrough(UI_WIDGET(fxTracker.pPowerArtList), 1);
	ui_ListSetCellClickedCallback(fxTracker.pPowerArtList, dfxPowerArtSelectedCB, NULL);
	ui_ListSetCellActivatedCallback(fxTracker.pPowerArtList, dfxPowerArtActivatedCB, NULL);

	pCol = ui_ListColumnCreate(UIListTextCallback, "Power Art", (intptr_t)dfxPowerArtNameCB, NULL);
	ui_ListAppendColumn(fxTracker.pPowerArtList, pCol);

	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pPowerArtList), 0.4, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pPowerArtList), 0, 0, 70, 30);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtList);



	// Num Selected
	fxTracker.pNumPowerArtFilteredLabel = ui_LabelCreate("", 5, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pNumPowerArtFilteredLabel), 5, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pNumPowerArtFilteredLabel);

	// Selected Name
	fxTracker.pSelectedPowerArtName = ui_LabelCreate("", x, y);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 5, 5, 5, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 0.59, 25, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pSelectedPowerArtName), 0, 0, 0, 0, UITopRight);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pSelectedPowerArtName);





	// Setup the selected power art's fx list
	fxTracker.pPowerArtFxList = ui_ListCreate(NULL, &fxTracker.ppPowerArtFxList, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(fxTracker.pPowerArtFxList), 1);
	//ui_ListSetCellClickedCallback(fxTracker.pPowerArtFxList, dfxPowerArtSelectedCB, NULL);
	ui_ListSetCellActivatedCallback(fxTracker.pPowerArtFxList, dfxPowerArtFxActivatedCB, NULL);

	pCol = ui_ListColumnCreate(UIListTextCallback, "Fx Type", (intptr_t)dfxPowerArtFxTypeCB, NULL);
	ui_ListAppendColumn(fxTracker.pPowerArtFxList, pCol);
	pCol = ui_ListColumnCreate(UIListTextCallback, "Fx Name", (intptr_t)dfxPowerArtFxNameCB, NULL);
	ui_ListAppendColumn(fxTracker.pPowerArtFxList, pCol);

	ui_WidgetSetPositionEx(UI_WIDGET(fxTracker.pPowerArtFxList), 0, 0, 0, 0, UITopRight);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pPowerArtFxList), 0.59, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(fxTracker.pPowerArtFxList), 5, 5, 30, 0);
	ui_PaneAddChild(fxTracker.pPowerArtPane, fxTracker.pPowerArtFxList);


	// make sure the filter is init'd correctly
	memset(&fxTracker.dfxPowerArtFilter, 0, sizeof(fxTracker.dfxPowerArtFilter));
	dfxPowerArtUpdateFilter();
	

	ui_TabAddChild(pTab, fxTracker.pPowerArtPane);
}

static void dfxSetupInstancesTab(UITab *pTab, FXTrackerSettings *pTrackerSettings, int iGroupIndex)
{
	fxTracker.pInstancesPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	fxTracker.pInstancesPane->widget.pOverrideSkin = fxTracker.pSkin;

	ui_TabAddChild(pTab, fxTracker.pInstancesPane);


	//ui_RebuildableTreeInit(fxTracker.pTree, &fxTracker.pInstancesTab->eaChildren, 0, 0, UIRTOptions_YScroll);
	ui_RebuildableTreeInit(fxTracker.pTree, &UI_WIDGET(fxTracker.pInstancesPane)->children, 0, 0, UIRTOptions_YScroll);

	fxTracker.pDebugUpdateList = ui_ListCreate(NULL, &eaDynFxTrackers, 14);


	// add sort menu
	/*
	{
	UIMenu* pSortMenu;
	UIMenuItem* pSortMenuItem;
	pSortMenu = ui_MenuCreate("Sort");
	pSortMenuItem = ui_MenuItemCreate("Name", UIMenuCallback, sortByName, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	pSortMenuItem = ui_MenuItemCreate("Name Reverse", UIMenuCallback, sortByNameReverse, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	pSortMenuItem = ui_MenuItemCreate("Num", UIMenuCallback, sortByNum, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	pSortMenuItem = ui_MenuItemCreate("Num Reverse", UIMenuCallback, sortByNumReverse, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	pSortMenuItem = ui_MenuItemCreate("Peak", UIMenuCallback, sortByPeak, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	pSortMenuItem = ui_MenuItemCreate("Peak Reverse", UIMenuCallback, sortByPeakReverse, NULL, NULL);
	ui_MenuAppendItem(pSortMenu, pSortMenuItem);
	ui_WindowAddChild(fxTracker.pWindow, pSortMenu);
	}
	*/



	// Global Group
	{
		UIRTNode* pGroup = ui_RebuildableTreeAddGroup(fxTracker.pTree->root, "Global", "GlobalGroup", GamePrefGetInt(groupStatusPrefName(iGroupIndex++), 1), "Global Settings");
		UIColorSlider* pHue;
		UISlider *pSaturation;
		UISlider *pValue;
		UICheckButton* pCheckButton;
		UITextEntry* pHueText;
		UITextEntry* pSaturationText;
		UITextEntry* pValueText;
		
		UILabel *pHueLabel;
		UILabel *pSatLabel;
		UILabel *pValLabel;

		UIAutoWidgetParams params = {0};
		Vec3 vMin = {0.0f, 1.0f, 1.0f}, vMax = {360.0f, 1.0f, 1.0f}, vStart = {0.0f, 1.0f, 1.0f};
		F32 fHeight = 38;

		// Row 1 (Actually multiple rows for HSV sliders.)

		// Hue

		pHueLabel = ui_LabelCreate("H", 0, 0);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pHueLabel), NULL, false, "HuePickerLabel", &params);

		pHue = ui_ColorSliderCreate(0, 0, 180, vMin, vMax, true);

		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pHue), NULL, false, "HuePicker", &params);
		pHueText = ui_TextEntryCreate("", 0, 0);
		ui_WidgetSetDimensions(UI_WIDGET(pHueText), 50.0f, 22.0f);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pHueText), NULL, false, "HueText", &params);

		ui_ColorSliderSetChangedCallback(pHue, changeDebugHue, pHueText);
		ui_TextEntrySetFinishedCallback(pHueText, setHueFromEntry, pHue);

		vStart[0] = dynDebugState.fTestHue;
		ui_ColorSliderSetValueAndCallback(pHue, vStart);

		// Saturation

		pSatLabel = ui_LabelCreate("S", 0, 0);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pSatLabel), NULL, true, "SatPickerLabel", &params);

		pSaturation = ui_SliderCreate(0, 0, 180, -1, 1, dynDebugState.fTestSaturation);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pSaturation), NULL, false, "SaturationPicker", &params);

		pSaturationText = ui_TextEntryCreate("", 0, 0);
		ui_SliderSetChangedCallback(pSaturation, changeDebugSaturation, pSaturationText);
		ui_TextEntrySetFinishedCallback(pSaturationText, setSaturationFromEntry, pSaturation);

		ui_WidgetSetDimensions(UI_WIDGET(pSaturationText), 50.0f, 22.0f);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pSaturationText), NULL, false, "SaturationText", &params);

		ui_SliderSetValueAndCallback(pSaturation, dynDebugState.fTestSaturation);

		// Value

		pValLabel = ui_LabelCreate("V", 0, 0);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pValLabel), NULL, true, "ValPickerLabel", &params);

		pValue = ui_SliderCreate(0, 0, 180, -1, 1, dynDebugState.fTestValue);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pValue), NULL, false, "ValuePicker", &params);

		pValueText = ui_TextEntryCreate("", 0, 0);
		ui_SliderSetChangedCallback(pValue, changeDebugValue, pValueText);
		ui_TextEntrySetFinishedCallback(pValueText, setValueFromEntry, pValue);

		ui_WidgetSetDimensions(UI_WIDGET(pValueText), 50.0f, 22.0f);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pValueText), NULL, false, "ValueText", &params);

		ui_SliderSetValueAndCallback(pValue, dynDebugState.fTestValue);

		{
			pCheckButton = ui_CheckButtonCreate(0,0,"Global",dynDebugState.bGlobalHueOverride);
			ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, 70, fHeight, UIUnitFixed, UIUnitFixed);
			ui_CheckButtonSetToggledCallback(pCheckButton, hueOverride, NULL);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "HueOverride", &params);
		}

		params.min[0] = 1.0f;
		params.max[0] = 255.0f;
		params.step[0] = 1.0f;
		params.type = AWT_Slider;
		ui_AutoWidgetAdd(pGroup, parse_FXTrackerSettings, "Opacity", pTrackerSettings, false, fxTrackerSettingsChanged, pTrackerSettings, &params, "Window Opacity");

		// Row 2
		{
			UILabel* pLabel = ui_LabelCreate("FX Types", 0, 0);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pLabel), NULL, true, "FX Types", &params);
		}
		pCheckButton = ui_CheckButtonCreate(0,0,"Costume",!dynDebugState.bNoCostumeFX);
		ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
		ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, 80, fHeight, UIUnitFixed, UIUnitFixed);
		ui_CheckButtonSetToggledCallback(pCheckButton, noCostumeFX, NULL);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "Allow Costume FX", &params);

		pCheckButton = ui_CheckButtonCreate(0,0,"Environment",!dynDebugState.bNoEnvironmentFX);
		ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
		ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, 100, fHeight, UIUnitFixed, UIUnitFixed);
		ui_CheckButtonSetToggledCallback(pCheckButton, noEnvironmentFX, NULL);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "Allow Environment FX", &params);

		pCheckButton = ui_CheckButtonCreate(0,0,"UI",!dynDebugState.bNoUIFX);
		ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
		ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, 40, fHeight, UIUnitFixed, UIUnitFixed);
		ui_CheckButtonSetToggledCallback(pCheckButton, noUIFX, NULL);
		ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "Allow UI FX", &params);


		{
			UILabel* pLabel = ui_LabelCreate("FX Quality", 0, 0);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pLabel), NULL, false, "FX Quality", &params);
		}
		{
			static const char** eapcOptions = NULL;
			UIComboBox* pComboBox;
			if (!eapcOptions)
			{
				eaPush(&eapcOptions, "Low");
				eaPush(&eapcOptions, "Medium");
				eaPush(&eapcOptions, "High");
			}
			pComboBox = ui_ComboBoxCreate(0, 0, 60, NULL, &eapcOptions, "FX Quality");
			ui_ComboBoxSetSelectedCallback(pComboBox, fxQualityCBSelected, NULL);
			ui_ComboBoxSetSelected(pComboBox, dynDebugState.iFxQuality);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pComboBox), NULL, false, "FX Quality", &params);
		}

		{
			UIButton* pButton = ui_ButtonCreate("Record", 0, 0, recordFunc, NULL);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pButton), NULL, false, "Record", &params);
		}

	}

	// Update Group
	{
		UIRTNode* pGroup = ui_RebuildableTreeAddGroup(fxTracker.pTree->root, "Update", "Update", GamePrefGetInt(groupStatusPrefName(iGroupIndex++), 1), NULL);

		// Buttons
		{
			UIButton* pButton;
			UICheckButton* pCheckButton;
			F32 fWidth = 100;
			F32 fHeight = 38;
			UIAutoWidgetParams params = {0};


			// Row 1
			pButton = ui_AutoWidgetAddButton(pGroup, "KillAll", killAllFx, NULL, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			pButton = ui_AutoWidgetAddButton(pGroup, "PauseAll", pauseFX, NULL, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			pButton = ui_AutoWidgetAddButton(pGroup, "Kill", killSelected, fxTracker.pDebugUpdateList, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			pButton = ui_AutoWidgetAddButton(pGroup, "Debug", debugSelected, fxTracker.pDebugUpdateList, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			// Row 2
			pButton = ui_AutoWidgetAddButton(pGroup, "Edit", editSelected, fxTracker.pDebugUpdateList, true, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			pButton = ui_AutoWidgetAddButton(pGroup, "OpenDir", openDirOfSelected, fxTracker.pDebugUpdateList, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);


			pButton = ui_AutoWidgetAddButton(pGroup, "Clear", clearEmptyTrackers, NULL, false, NULL, &params);
			ui_WidgetSetPaddingEx((UIWidget*) pButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);

			pCheckButton = ui_CheckButtonCreate(0,0,"AutoClear",dynDebugState.bAutoClearTrackers);
			ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);
			ui_CheckButtonSetToggledCallback(pCheckButton, autoClear, NULL);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "AutoClear", &params);


		}

		// List
		{
			UIListColumn* pCol;
			UIAutoWidgetParams params = {0};
			ui_WidgetSetDimensionsEx((UIWidget*) fxTracker.pDebugUpdateList, 1, 600, UIUnitPercentage, UIUnitFixed);
			//ui_WidgetSetPositionEx((UIWidget*) fxTracker.pDebugUpdateList, 0, 0, 0, 0, UITopLeft);
			//ui_WidgetSetPaddingEx((UIWidget*) fxTracker.pDebugUpdateList, 0, 0, 80, 0);


			// add columns
			pCol = ui_ListColumnCreateCallback("Fx Name", displayDynFxName, NULL);
			pCol->fWidth = 250;
			ui_ListColumnSetClickedCallback(pCol, sortByName, NULL);
			ui_ListAppendColumn(fxTracker.pDebugUpdateList, pCol);

			pCol = ui_ListColumnCreateCallback("Num", displayDynFxCurrent, NULL);
			pCol->fWidth = 50;
			ui_ListColumnSetClickedCallback(pCol, sortByNum, NULL);
			ui_ListAppendColumn(fxTracker.pDebugUpdateList, pCol);

			pCol = ui_ListColumnCreateCallback("Peak", displayDynFxPeak, NULL);
			pCol->fWidth = 50;
			ui_ListColumnSetClickedCallback(pCol, sortByPeak, NULL);
			ui_ListAppendColumn(fxTracker.pDebugUpdateList, pCol);

			pCol = ui_ListColumnCreateCallback("Mem", displayDynFxMem, NULL);
			pCol->fWidth = 50;
			ui_ListColumnSetClickedCallback(pCol, sortByMem, NULL);
			ui_ListAppendColumn(fxTracker.pDebugUpdateList, pCol);

			pCol = ui_ListColumnCreateCallback("Lvl", displayDynFxLevel, NULL);
			pCol->fWidth = 50;
			ui_ListColumnSetClickedCallback(pCol, sortByLevel, NULL);
			ui_ListAppendColumn(fxTracker.pDebugUpdateList, pCol);


			ui_ListSetCellActivatedCallback(fxTracker.pDebugUpdateList, dynFxActivatedCB, NULL);

			FOR_EACH_IN_EARRAY_FORWARDS(fxTracker.pDebugUpdateList->eaColumns, UIListColumn, pMyCol)
			{
				pMyCol->fWidth = GamePrefGetFloat(colWidthPrefName(ipMyColIndex, "Update"), pMyCol->fWidth);
			}
			FOR_EACH_END;

			ui_ListSetMultiselect(fxTracker.pDebugUpdateList, true);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(fxTracker.pDebugUpdateList), NULL, true, "UpdateList", &params);
		}
	}


	// DrawGroup
	{
		UIRTNode* pGroup = ui_RebuildableTreeAddGroup(fxTracker.pTree->root, "Draw", "Draw", GamePrefGetInt(groupStatusPrefName(iGroupIndex++), 1), NULL);

		// Buttons
		{
			UICheckButton* pCheckButton;
			UIAutoWidgetParams params = {0};
			F32 fWidth = 100;
			F32 fHeight = 38;

			// Row 1
			pCheckButton = ui_CheckButtonCreate(0,0,"DrawOnly",dynDebugState.bFxDrawOnlySelected);
			ui_WidgetSetPaddingEx((UIWidget*) pCheckButton, 5, 5, 5, 5);
			ui_WidgetSetDimensionsEx((UIWidget*)pCheckButton, fWidth, fHeight, UIUnitFixed, UIUnitFixed);
			ui_CheckButtonSetToggledCallback(pCheckButton, drawOnlySelected, NULL);

			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(pCheckButton), NULL, false, "DrawOnly", &params);
		}

		// List
		{
			UIListColumn* pCol;
			UIAutoWidgetParams params = {0};

			fxTracker.pDebugDrawList = ui_ListCreate(NULL, &eaDynDrawTrackers, 14);

			ui_WidgetSetDimensionsEx((UIWidget*) fxTracker.pDebugDrawList, 1, 600, UIUnitPercentage, UIUnitFixed);
			//ui_WidgetSetPositionEx((UIWidget*) fxTracker.pDebugUpdateList, 0, 0, 0, 0, UITopLeft);
			//ui_WidgetSetPaddingEx((UIWidget*) fxTracker.pDebugUpdateList, 0, 0, 80, 0);


			// add columns
			pCol = ui_ListColumnCreateCallback("Name", drawDebugDisplayName, NULL);
			pCol->fWidth = 250.0f;
			ui_ListAppendColumn(fxTracker.pDebugDrawList, pCol);

			pCol = ui_ListColumnCreateCallback("Num", drawDebugDisplayNum, NULL);
			pCol->fWidth = 50.0f;
			ui_ListAppendColumn(fxTracker.pDebugDrawList, pCol);

			pCol = ui_ListColumnCreateCallback("SubObjects", drawDebugDisplaySubObjects, NULL);
			pCol->fWidth = 50.0f;
			ui_ListAppendColumn(fxTracker.pDebugDrawList, pCol);

			pCol = ui_ListColumnCreateCallback("Type", drawDebugDisplayType, NULL);
			pCol->fWidth = 100.0f;
			ui_ListAppendColumn(fxTracker.pDebugDrawList, pCol);

			pCol = ui_ListColumnCreateCallback("Priority", drawDebugDisplayPriority, NULL);
			pCol->fWidth = 100.0f;
			ui_ListAppendColumn(fxTracker.pDebugDrawList, pCol);

			FOR_EACH_IN_EARRAY_FORWARDS(fxTracker.pDebugDrawList->eaColumns, UIListColumn, pMyCol)
			{
				pMyCol->fWidth = GamePrefGetFloat(colWidthPrefName(ipMyColIndex, "Draw"), pMyCol->fWidth);
			}
			FOR_EACH_END;


			ui_ListSetMultiselect(fxTracker.pDebugDrawList, true);
			ui_RebuildableTreeAddWidget(pGroup, UI_WIDGET(fxTracker.pDebugDrawList), NULL, true, "DrawList", &params);
		}

		ui_RebuildableTreeDoneBuilding(fxTracker.pTree);
	}


	if (fxTracker.pFxDebugDevice)
	{
		ui_WindowSetMovable(fxTracker.pWindow, false);
		ui_WidgetSetDimensionsEx((UIWidget*)fxTracker.pWindow, 1, 1, UIUnitPercentage, UIUnitPercentage);
		ui_WindowSetResizable(fxTracker.pWindow, false);
		ui_WindowSetClosable(fxTracker.pWindow, false);
	}

	ui_RebuildableTreeDoneBuilding(fxTracker.pTree);
	ui_WindowAddToDevice(fxTracker.pWindow, fxTracker.pFxDebugDevice);
}

static void dfxTabChangedCB(UIAnyWidget *pWidget, UserData pUserData)
{
	int iTabIndex = ui_TabGroupGetActiveIndex(fxTracker.pTabGroup);
	if(iTabIndex == 1)
	{
		if(fxTracker.pTreeRoot == NULL)
		{
			dfxTreeInitData();
		}
	}
	else if(iTabIndex == 2)
	{
		if(eaSize(&fxTracker.ppPowerArtList) == 0)
		{
			dfxPowerArtInitData();
		}
	}
}

AUTO_COMMAND;
void dfxDebug(void)
{
	StashTable stDynFxTrackers = dynFxDebugGetTrackerTable();
	static FXTrackerSettings* pTrackerSettings;
	int iGroupIndex = 0;
	int iLastTab = 0;

	if (!pTrackerSettings)
	{
		pTrackerSettings = calloc(sizeof(FXTrackerSettings), 1);
		pTrackerSettings->fOpacity = GamePrefGetFloat("FXTracker\\Opacity", 255);
	}

	// If there is already a window, just close it and return
	if (fxTracker.pWindow)
	{
		shutdownWindow();
		return;
	}
	dynDebugState.bFxDebugOn = true;
	if (fxTracker.bPopOut)
	{
		fxTracker.pFxDebugDevice = gfxAuxDeviceAdd(NULL, "Fx Debug", popOutCloseCallback, popOutFrameCallback, NULL);
	}

	// Set up window
	if (fxTracker.pFxDebugDevice)
	{
		fxTracker.pWindow = ui_WindowCreate("DynFx Debug", 0, 0, 400, 700);
	}
	else
	{
		fxTracker.pWindow = ui_WindowCreate(
			"DynFx Debug",
			GamePrefGetInt("FXDebugX", 20),
			GamePrefGetInt("FXDebugY", 100),
			GamePrefGetInt("FXDebugW", 400),
			GamePrefGetInt("FXDebugH", 700)
			);
		iLastTab = GamePrefGetInt("FXDebug.tab", 0);
	}
	
	fxTracker.bWindowActive = true;

	assert(fxTracker.pWindow);
	ui_WindowSetCloseCallback(fxTracker.pWindow, dynFxDebugWindowClose, NULL);
	fxTracker.pWindow->widget.pOverrideSkin = fxTrackerGetSkin(pTrackerSettings, UI_GET_SKIN(fxTracker.pWindow));
	fxTracker.pTree = ui_RebuildableTreeCreate();
	//ui_RebuildableTreeInit(fxTracker.pTree, &UI_WIDGET(fxTracker.pWindow)->children, 0, 0, UIRTOptions_YScroll);


	// Create the Tab Group
	fxTracker.pTabGroup = ui_TabGroupCreate(0, 0, 250, 250);
	ui_WidgetSetDimensionsEx(UI_WIDGET(fxTracker.pTabGroup), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabGroupSetChangedCallback(fxTracker.pTabGroup, dfxTabChangedCB, NULL);

	// Instances Tab
	fxTracker.pInstancesTab = ui_TabCreate("Instances");
	ui_TabGroupAddTab(fxTracker.pTabGroup, fxTracker.pInstancesTab);

	dfxSetupInstancesTab(fxTracker.pInstancesTab, pTrackerSettings, iGroupIndex);

	// Hierarchy Tab
	fxTracker.pHierarchyTab = ui_TabCreate("Hierarchy");
	ui_TabGroupAddTab(fxTracker.pTabGroup, fxTracker.pHierarchyTab);

	dfxSetupHierarchyTab(fxTracker.pHierarchyTab);

	// Power Art Tab
	fxTracker.pPowerArtTab = ui_TabCreate("Power Art");
	ui_TabGroupAddTab(fxTracker.pTabGroup, fxTracker.pPowerArtTab);

	dfxSetupPowerArtTab(fxTracker.pPowerArtTab);

	// select the tab
	ui_TabGroupSetActiveIndex(fxTracker.pTabGroup, iLastTab);

	// add the tabs to the window
	ui_WindowAddChild(fxTracker.pWindow, fxTracker.pTabGroup);
}

static struct
{
	UIWindow* pWindow;
	char* pcRecord;
} fxRecordDisplay;

static bool dynFxRecordWindowClose(UIWindow* pCloseWindow, void* userData)
{
	estrClear(&fxRecordDisplay.pcRecord);
	assert( pCloseWindow == fxRecordDisplay.pWindow );
	return true;
}

static void forceCloseFxRecordDisplay(void)
{
	estrClear(&fxRecordDisplay.pcRecord);
	ui_WidgetQueueFree((UIWidget *) fxRecordDisplay.pWindow);
	fxRecordDisplay.pWindow = NULL;
}

F32 fDrawnPeakBudgets[3] = { 3, 6, 10 };
F32 fUpdatedPeakBudgets[3] = { 6, 12, 20 };
const char* pcFxQuality[3] = 
{
	"Low",
	"Medium",
	"High"
};

F32 getGradeForOverUnder(S32 iOverUnder)
{
	F32 fAdjustedGrade = ((F32)iOverUnder / (fDrawnPeakBudgets[dynDebugState.iFxQuality] + fUpdatedPeakBudgets[dynDebugState.iFxQuality]));
	if (fAdjustedGrade < 0.0f)
	{
		return CLAMP(-fAdjustedGrade * 0.1f + 0.9f, 0.0f, 1.0f);
	}
	else
		return CLAMP(0.9f - fAdjustedGrade * 0.4f, 0.0f, 1.0f);
}

const char* gradeSMFFont(F32 fGrade)
{
	if (fGrade > 0.9f)
		return "<font color=Green>";
	else if (fGrade > 0.8f)
		return "<font color=Black>";
	else
	{
		U8 cRedness = (char)round( CLAMP(1.0f - fGrade, 0.0f, 1.0f) * 0xFF);
		static char cResult[32];
		sprintf(cResult, "<font color=#%2x0000>", cRedness);
		return cResult;
	}
}

const char* gradeLetter(F32 fGrade)
{
	if (fGrade > 0.89f)
		return "A";
	else if (fGrade > 0.79f)
		return "B";
	else if (fGrade > 0.69f)
		return "C";
	else if (fGrade > 0.49f)
		return "D";
	else
		return "F";
}

static void copyFxRecordResults(UIButton* button, void* userData)
{
	char* pcToClipboard = NULL;
	S32 iDrawnOverUnder = fxRecording.uiNumDynFxDrawnPeak - fDrawnPeakBudgets[dynDebugState.iFxQuality];
	S32 iUpdatedOverUnder = fxRecording.uiNumDynFxUpdatedPeak - fDrawnPeakBudgets[dynDebugState.iFxQuality];

	estrCreate(&pcToClipboard);
	estrConcatf(&pcToClipboard, "Grade: %s\n", gradeLetter(getGradeForOverUnder( (iDrawnOverUnder + iUpdatedOverUnder)) ) );
	estrConcatf(&pcToClipboard,"FX Quality: %s\n", pcFxQuality[dynDebugState.iFxQuality]);
	estrConcatf(&pcToClipboard,"Peak Updated: %d / %d\n", fxRecording.uiNumDynFxUpdatedPeak, (S32)fUpdatedPeakBudgets[dynDebugState.iFxQuality]);
	estrConcatf(&pcToClipboard,"Peak Drawn: %d / %d\n", fxRecording.uiNumDynFxDrawnPeak, (S32)fDrawnPeakBudgets[dynDebugState.iFxQuality]);
	estrConcatf(&pcToClipboard,"Peak Created: %d\n", fxRecording.uiNumDynFxCreatedPeak);
	estrConcatf(&pcToClipboard,"Total Created: %d\n", fxRecording.uiNumDynFxCreatedTotal);
	estrConcatf(&pcToClipboard, "----------------------------------\n");
	estrConcatf(&pcToClipboard, "   %d Unique FX Updated\n", stashGetCount(stFxRecordNames));
	estrConcatf(&pcToClipboard, "----------------------------------\n");
	{
		StashTableIterator iter;
		StashElement elem;
		stashGetIterator(stFxRecordNames, &iter);
		while (stashGetNextElement(&iter, &elem))
			estrConcatf(&pcToClipboard, "%s\n", stashElementGetStringKey(elem));
	}
	estrConcatf(&pcToClipboard, "----------------------------------\n");

	winCopyToClipboard(pcToClipboard);
	estrDestroy(&pcToClipboard);
}

void dfxDisplayRecording(void)
{
	S32 iWindowWidth = 400;
	S32 iWindowHeight = 200;
	S32 iDrawnOverUnder, iUpdatedOverUnder;
	if (fxRecordDisplay.pWindow)
		forceCloseFxRecordDisplay();
	fxRecordDisplay.pWindow = ui_WindowCreate("FX Recording", 300, 300, iWindowWidth, iWindowHeight);
	ui_WindowSetCloseCallback(fxRecordDisplay.pWindow, dynFxRecordWindowClose, NULL);

	// Calculate grade
	iDrawnOverUnder = fxRecording.uiNumDynFxDrawnPeak - fDrawnPeakBudgets[dynDebugState.iFxQuality];
	iUpdatedOverUnder = fxRecording.uiNumDynFxUpdatedPeak - fDrawnPeakBudgets[dynDebugState.iFxQuality];

	estrCreate(&fxRecordDisplay.pcRecord);
	{
		F32 fTotalGrade = getGradeForOverUnder((iDrawnOverUnder + iUpdatedOverUnder));
		estrConcatf(&fxRecordDisplay.pcRecord,"%s<span align=center width=%d><scale 3.0>Grade: %s</scale></span></font><br><br>", gradeSMFFont(fTotalGrade), iWindowWidth, gradeLetter(fTotalGrade));
	}
	estrConcatf(&fxRecordDisplay.pcRecord,"FX Quality: %s<br><br>", pcFxQuality[dynDebugState.iFxQuality]);
	estrConcatf(&fxRecordDisplay.pcRecord,"%sPeak Updated: %d / %d</font><br>", gradeSMFFont(getGradeForOverUnder(iUpdatedOverUnder)), fxRecording.uiNumDynFxUpdatedPeak, (S32)fUpdatedPeakBudgets[dynDebugState.iFxQuality]);
	estrConcatf(&fxRecordDisplay.pcRecord,"%sPeak Drawn: %d / %d</font><br>", gradeSMFFont(getGradeForOverUnder(iDrawnOverUnder)), fxRecording.uiNumDynFxDrawnPeak, (S32)fDrawnPeakBudgets[dynDebugState.iFxQuality]);
	estrConcatf(&fxRecordDisplay.pcRecord,"Peak Created: %d<br>", fxRecording.uiNumDynFxCreatedPeak);
	estrConcatf(&fxRecordDisplay.pcRecord,"Total Created: %d<br>", fxRecording.uiNumDynFxCreatedTotal);

	{
		UISMFView* pView = ui_SMFViewCreate(0, 0, iWindowWidth, iWindowHeight);
		ui_SMFViewSetText(pView, fxRecordDisplay.pcRecord, NULL);
		ui_WindowAddChild(fxRecordDisplay.pWindow, UI_WIDGET(pView));
	}
	{
		UIButton* pButton = ui_ButtonCreate("Copy Results", 0, iWindowHeight - 25, copyFxRecordResults, NULL);
		ui_WindowAddChild(fxRecordDisplay.pWindow, UI_WIDGET(pButton));
	}
	ui_WindowShow(fxRecordDisplay.pWindow);
}

static struct
{
	UIWindow* pWindow;
	char* pcSeqDebugLog;
	char* pcWindowDisplayLog;
} seqDebugLogDisplay;

static void copySeqDebugLog(UIButton* button, void* userData)
{
	winCopyToClipboard(seqDebugLogDisplay.pcSeqDebugLog);
}

static bool dynSeqDebugLogWindowClose(UIWindow* pCloseWindow, void* userData)
{
	estrClear(&seqDebugLogDisplay.pcSeqDebugLog);
	estrClear(&seqDebugLogDisplay.pcWindowDisplayLog);
	assert( pCloseWindow == seqDebugLogDisplay.pWindow );
	return true;
}

static void forceCloseSeqDebugLogDisplay(void)
{
	estrClear(&seqDebugLogDisplay.pcSeqDebugLog);
	estrClear(&seqDebugLogDisplay.pcWindowDisplayLog);
	ui_WidgetQueueFree((UIWidget *) seqDebugLogDisplay.pWindow);
	ZeroStruct(&seqDebugLogDisplay);
}

void displaySeqDebugLog(void)
{
	S32 iWindowWidth = 600;
	S32 iWindowHeight = 400;
	if (seqDebugLogDisplay.pWindow)
		forceCloseSeqDebugLogDisplay();
	seqDebugLogDisplay.pWindow = ui_WindowCreate("Seq Data Debug", 200, 200, iWindowWidth, iWindowHeight);
	ui_WindowSetCloseCallback(seqDebugLogDisplay.pWindow, dynSeqDebugLogWindowClose, NULL);

	{
		UISMFView* pView = ui_SMFViewCreate(0, 0, iWindowWidth, iWindowHeight);
		estrCopy(&seqDebugLogDisplay.pcWindowDisplayLog, &seqDebugLogDisplay.pcSeqDebugLog);
		estrReplaceOccurrences_CaseInsensitive(&seqDebugLogDisplay.pcWindowDisplayLog, "\n", "<br>");
		estrReplaceOccurrences_CaseInsensitive(&seqDebugLogDisplay.pcWindowDisplayLog, "\t", "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
		ui_SMFViewSetText(pView, seqDebugLogDisplay.pcWindowDisplayLog, NULL);
		ui_WindowAddChild(seqDebugLogDisplay.pWindow, UI_WIDGET(pView));
	}
	{
		UIButton* pButton = ui_ButtonCreate("Copy Results", 0, iWindowHeight - 25, copySeqDebugLog, NULL);
		ui_WindowAddChild(seqDebugLogDisplay.pWindow, UI_WIDGET(pButton));
	}
	ui_WindowShow(seqDebugLogDisplay.pWindow);
}


AUTO_COMMAND ACMD_CATEGORY(dynAnimation);
void danimWhichSeq(ACMD_NAMELIST(stSeqDataCollection, STASHTABLE) const char* pcSequencer, ACMD_SENTENCE pcBits)
{

	char *strtokcontext = 0;

	//meaningless call to reset strTok
	char* pcBit = strTokWithSpacesAndPunctuation(NULL, NULL);
	DynBitField bitField;


	if (seqDebugLogDisplay.pcSeqDebugLog)
	{
		forceCloseSeqDebugLogDisplay();
	}


	dynBitFieldClear(&bitField);

	pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");

	while (pcBit)
	{
		if (stricmp(pcBit, "showbits")==0)
		{
			const DynBitField* pBF = dynSeqGetBits(dynDebugState.pDebugSkeleton->eaSqr[0]);
			dynBitFieldSetAllFromBitField(&bitField, pBF);
		}
		else if (pcBit[0] == '-')
		{
			pcBit++;
			if (!dynBitActOnByName(&bitField, pcBit, edba_Clear))
				Errorf("Could not find bit %s.", pcBit);
		}
		else
		{
			if (!dynBitActOnByName(&bitField, pcBit, edba_Set))
				Errorf("Could not find bit %s. Either a typo or you might have meant SHOWBITS.", pcBit);
		}

		pcBit = strTokWithSpacesAndPunctuation(pcBits, " ");
	}

	{
		estrCreate(&seqDebugLogDisplay.pcSeqDebugLog);
		if (debugDynSeqDataFromBits(allocAddString(pcSequencer), &bitField, &seqDebugLogDisplay.pcSeqDebugLog))
		{
			displaySeqDebugLog();
		}
		else
		{
			estrDestroy(&seqDebugLogDisplay.pcSeqDebugLog);
			ZeroStruct(&seqDebugLogDisplay);
		}
	}
}

#include "FxTracker_c_ast.c"
