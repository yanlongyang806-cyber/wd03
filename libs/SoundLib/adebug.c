#include "adebug.h"

// Standard C Types
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// Cryptic Specific
#include "earray.h"
#include "estring.h"
#include "crypt.h"
#include "timing.h"
#include "file.h"
#include "mathutil.h"
#include "qsortG.h"
#include "wlTime.h"

// For UI stuff
#include "UIComboBox.h"
#include "UIExpander.h"
#include "UIScrollbar.h"
#include "UIWidgetTree.h"
#include "UIWindow.h"
#include "UITabs.h"
#include "UIPane.h"
#include "UILabel.h"
#include "UIProgressBar.h"
#include "UITree.h"
#include "UITextEntry.h"
#include "UIList.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UISlider.h"
#include "UIMenu.h"

#include "GraphicsLib.h"
#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "CBox.h"
#include "GfxPrimitive.h"
#include "UISkin.h"

// Preferences
#include "Prefs.h"

// Camera
#include "GfxCamera.h"

// Audio System
#include "sndLibPrivate.h"
#include "event_sys.h"
#include "sndsource.h"
#include "sndSpace.h"
#include "sndConn.h"
#include "sndLOD.h"
#include "sndCluster.h"
#include "sndMixer.h"
#include "sndVoice.h"

// 
#include "sysutil.h"

// Auto-structs
#include "adebug_c_ast.h"
#include "sndVoice_h_ast.h"
#include "sndVoice_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define AmpToDb(a) ((a) <= 0.0 ? -60 : log((a)) / 0.11512925)
#define DbToAmp(db) ( (dbVal) < -59 ? 0.0 : exp((db) * 0.11512925))



//
// Drawing
//

typedef void (*ADebugDrawFunc)(void *userData);
typedef void (*ADebugDestroyDrawFunc)(void *userData);

typedef struct ADebugDrawCB {
	ADebugDrawFunc drawFunc;
	ADebugDestroyDrawFunc destroyDrawFunc;
	void *userData;
} ADebugDrawCB;


enum {
	ADEBUG_NAME = 0,
	ADEBUG_PROJECT_FILENAME,
	ADEBUG_SOUND_TYPE,			// type of event
	ADEBUG_AUDIBILITY,			// current audibility of event.
	ADEBUG_VOLUME,
	ADEBUG_LENGTH,				// Length in milliseconds of this event.
	ADEBUG_INSTANCES_ACTIVE,	// Number of event instances currently in use
	ADEBUG_3D_MODE,				// 2D or 3D
	ADEBUG_3D_ROLLOFF_MODE,		// linear, log, custom...
	ADEBUG_3D_MIN,
	ADEBUG_3D_MAX,
	ADEBUG_MAX_PLAYBACKS,		// num max playbacks
	ADEBUG_DOPPLER_SCALE,		// doppler scale
	ADEBUG_3D_PAN_LEVEL,		// 3D pan level
	ADEBUG_FADEIN_TIME,			// fade in time
	ADEBUG_FADEOUT_TIME,		// fade out time
	ADEBUG_REVERB_DRY_LEVEL,	// dry level
	ADEBUG_REVERB_WET_LEVEL,	// wet level
	ADEBUG_MAX_WAVEBANKS,		// number of wavebanks refered to by this event.
	ADEBUG_PROJECT_ID,			// The runtime 'EventProject' wide unique identifier for this event.
	ADEBUG_SYSTEM_ID,			// The runtime 'EventSystem' wide unique identifier for this event.
	ADEBUG_MEMORY_USED,
	ADEBUG_EXCLUSIVE_GROUP,

	// User Properties
	ADEBUG_MOVEMENT,
	ADEBUG_ENABLEDSP,
	ADEBUG_2DALLOWED,
	ADEBUG_2DFADE,
	ADEBUG_IGNOREPOSITION,
	ADEBUG_IGNORE3D,
	ADEBUG_MUSIC, 
	ADEBUG_IGNORELOD,
	ADEBUG_DUCKABLE,
	ADEBUG_CLICKIE,

	ADEBUG_END_OF_LIST // marker for end of list
};


static const char *aDebugFmodPropertyNames[] = { 
	"Name", 
	"Project Filename",
	"Type",
	"Audibility",
	"Volume", 
	"Length", 
	"Instances Active", 


	"3D Mode",
	"3D Rolloff",
	"3D Min Distance",
	"3D Max Distance",
	"Max Playbacks",
	"Doppler Scale",
	"3D Pan Level",
	"Fadein Time",
	"Fadeout Time",
	"Reverb Dry Level",
	"Reverb Wet Level",
	"Max Wavebank References", 
	"Project Id", 
	"System Id",
	"Memory Used",
	"Exclusive Group",

	"Movement",
	"*Enable DSP",
	"*2d Allowed",
	"*2d Fade",
	"*Ignore Position",
	"*Ignore 3d",
	"*Music",
	"*Ignore LOD",
	"*Duckable",
	"*Clickie"
};


enum {
	REVERB_INSTANCE,
	REVERB_ENVIRONMENT,
	REVERB_ENVSIZE,
	REVERB_ENVDIFFUSION,
	REVERB_ROOM,
	REVERB_ROOMHF,
	REVERB_ROOMLF,
	REVERB_DECAYTIME,
	REVERB_DECAYHFRATIO,
	REVERB_DECAYLFRATIO,
	REVERB_REFLECTIONS,
	REVERB_REFLECTIONSDELAY,
	REVERB_REFLECTIONSPAN,
	REVERB_REVERB,
	REVERB_REVERBDELAY,
	REVERB_REVERBPAN,
	REVERB_ECHOTIME,
	REVERB_ECHODEPTH,
	REVERB_MODTIME,
	REVERB_MODDEPTH,
	REVERB_AIRABSORPTIONHF,
	REVERB_HFREF,
	REVERB_LFREF,
	REVERB_ROOMROLLOFFFACTOR,
	REVERB_DIFFUSION,
	REVERB_DENSITY,
	REVERB_FLAGS,

	ADEBUG_REVERB_END_OF_LIST
};

static const char *aDebugReverbPropertyNames[] = { 
	"Instance",
	"Environment",
	"EnvSize",
	"EnvDiffusion",
	"Room",
	"RoomHF",
	"RoomLF",
	"DecayTime",
	"DecayHFRatio",
	"DecayLFRatio",
	"Reflections",
	"Reflections",
	"ReflectionsDelay",
	"ReflectionsPan",
	"Reverb",
	"ReverbDelay",
	"ReverbPan",
	"EchoTime",
	"EchoDepth",
	"ModulationTime",
	"ModulationDepth",
	"AirAbsorptionHF",
	"HFReference",
	"LFReference",
	"RoomRolloffFactor",
	"Diffusion",
	"Density",
	"Flags",
};

//
// Space Properties
//
enum {
	ADEBUG_SPACE_FILENAME,
	ADEBUG_SPACE_DESC_NAME,
	ADEBUG_SPACE_ORIG_NAME,
	ADEBUG_SPACE_NUM_OWNED_SOURCES,
	ADEBUG_SPACE_NUM_LOCAL_SOURCES,
	ADEBUG_SPACE_TYPE,
	ADEBUG_SPACE_POSITION,
	ADEBUG_SPACE_MUSIC,
	ADEBUG_SPACE_DSP,
	ADEBUG_SPACE_AUDIBLE,
	ADEBUG_SPACE_CURRENT,

	ADEBUG_SPACE_PRIORITY,
	ADEBUG_SPACE_MULTIPLIER,

	ADEBUG_SPACE_END_OF_LIST
};

static const char *aDebugSpacePropertyNames[] = { 
	"Filename",
	"Desc Name",
	"Orig Name",
	"Owned Sources",
	"Local Sources",
	"Type",
	"Position",
	"Music",
	"DSP",
	"Audible",
	"Current",
	"Priority",
	"Multiplier"

};

//
// Sources
//
enum {
	ADEBUG_SOURCE_DESC_NAME,
	ADEBUG_SOURCE_CLUSTER,
	ADEBUG_SOURCE_ORIG_NAME,
	ADEBUG_SOURCE_TYPE,
	ADEBUG_SOURCE_ORIGIN,
	ADEBUG_SOURCE_ORIGIN_FILENAME,
	ADEBUG_SOURCE_ORIGIN_POS,
	ADEBUG_SOURCE_VIRTUAL_POS,
	ADEBUG_SOURCE_VELOCITY,
	ADEBUG_SOURCE_DIRECTIONALITY,
	ADEBUG_SOURCE_DIST_TO_LISTENER,
	ADEBUG_SOURCE_FADE_LEVEL,
	ADEBUG_SOURCE_NUM_LINES,
	ADEBUG_SOURCE_CURRENT_AMP,

	ADEBUG_SOURCE_STARTED,
	ADEBUG_SOURCE_STOPPED,
	ADEBUG_SOURCE_IN_AUDIBLE_SPACE,
	ADEBUG_SOURCE_IS_AUDIBLE,
	ADEBUG_SOURCE_STOLEN,
	ADEBUG_SOURCE_MUTED,
	ADEBUG_SOURCE_VIRTUAL,
	ADEBUG_SOURCE_HIDDEN,
	ADEBUG_SOURCE_CLEANUP,
	
	ADEBUG_SOURCE_DESTROYED,
	ADEBUG_SOURCE_IMMEDIATE,
	ADEBUG_SOURCE_MOVED,
	ADEBUG_SOURCE_UNMUTED,
	ADEBUG_SOURCE_NEEDSSTART,
	ADEBUG_SOURCE_NEEDSSTARTOFFSET,
	ADEBUG_SOURCE_NEEDSMOVE,
	ADEBUG_SOURCE_NEEDSCHANNELGROUP,
	ADEBUG_SOURCE_NEEDSSTOP,
	ADEBUG_SOURCE_HASEVENT,
	ADEBUG_SOURCE_DEAD,
	
	ADEBUG_SOURCE_INSTDELETED,
	ADEBUG_SOURCE_LODMUTED,
	ADEBUG_SOURCE_UPDATEPOSFROMENT,
	ADEBUG_SOURCE_LOCALPLAYER,
	ADEBUG_SOURCE_STATIONARY,

	ADEBUG_SOURCE_END_OF_LIST
};

static const char *aDebugSourcePropertyNames[] = {
	"Name",
	"Cluster",
	"Orig Name",
	"Type",
	"Origin",
	"Origin Filename",
	"Origin Pos",
	"Virtual Pos",
	"Velocity",
	"Directionality",
	"Distance to Listener",
	"Fade Level",
	"Num Lines",
	"CurrentAmp (Analysis)",

	"Started",
	"Stopped",
	"In Audible Space",
	"Is Audible",
	"Stolen",
	"Muted",
	"Virtual",
	"Hidden",
	"Clean Up",

	"Destroyed",
	"Immediate",
	"Moved",
	"Unmuted",
	"Needs start",
	"Needs start offset",
	"Needs move",
	"Needs Channel Group",
	"Needs Stop",
	"Has Event",
	"Dead",
	"Inst Deleted",
	"LOD Muted",
	"Update Pos From Ent",
	"Local Player",
	"Stationary"


};


typedef enum {
	ADEBUG_SOURCE_SORT_NONE,
	ADEBUG_SOURCE_SORT_NAME,
	ADEBUG_SOURCE_SORT_NAME_REV,
	ADEBUG_SOURCE_SORT_DISTANCE,
	ADEBUG_SOURCE_SORT_DISTANCE_REV,
	ADEBUG_SOURCE_SORT_STATE,
	ADEBUG_SOURCE_SORT_STATE_REV,
	ADEBUG_SOURCE_SORT_SPACE,
	ADEBUG_SOURCE_SORT_SPACE_REV
} ADebugSourceSortMode;


typedef struct ADebugFmodEventTreeData ADebugFmodEventTreeData;

AUTO_STRUCT;
struct ADebugFmodEventTreeData
{
	char *name;

	void *fmod_event;				NO_AST
	SoundTreeType type;				NO_AST
	SoundSource **previewSources;	NO_AST

	ADebugFmodEventTreeData **children;
	ADebugFmodEventTreeData *parent;

	U32 isVisible : 1; // determines whether the node will be displayed in the tree
};

typedef struct ADebugSpaceTreeData ADebugSpaceTreeData;

AUTO_STRUCT;
struct ADebugSpaceTreeData
{
	char *name;

	SoundSpace *space;				NO_AST

	ADebugSpaceTreeData **children; 
	ADebugSpaceTreeData *parent; 

	U32 isVisible : 1; // determines whether the node will be displayed in the tree
};


typedef struct ADebugFilter
{
	const char *name;

	char **pathComponents;

	U32 isPlaying : 1;

} ADebugFilter;


typedef struct ADebugLogEvent
{
	U32 startTimestamp;
	U32 endTimestamp;

	char *name;
	char *originFilename;
	char *originSpaceName;
	char *finalSpaceName;

	Vec3 initPosition;
	Vec3 finalPosition;
	SoundTreeType eventType;

	F32 distToListener;
	F32 volume;
	F32 audibility;

	U32 hadEvent : 1;
	U32 bLocalPlayer : 1;

	SoundSource *source; // if active, this will not be NULL
} ADebugLogEvent;

//
// Root Audio Debug UI Struct
//
struct {
	ADebugDrawCB **drawCallbacks;

	UIWindow *mainWindow;
	UISkin *mainSkin;

	// Tabs
	UITabGroup *mainTabs;

	UITab *performanceTab;

	struct {
		UIPane *rootPane;

		UILabel *cpuLabel;
		UILabel *cpuInfoLabel;
		UILabel *dspGraphMemTotal;
		UILabel *dspEnabled;
		UILabel *incombatLabel;

		UIProgressBar *cpuProgressBar;
		F32 cpuMax;

		UILabel *memoryLabel;
		UILabel *memoryInfoLabel;
		UIProgressBar *memoryProgressBar;

		UILabel *numEvents;
		UILabel *numEventsPlaying;
		UILabel *numInstances;

		UILabel *numSources;

		UILabel *timeOfDay;
		UILabel *listenerAttributes;

	} performance;
	UITab *fmodEventsTab;

	struct {
		UIMenu *contextMenu;
		ADebugFilter eventFilter;

		UIPane *rootPane;
		UIPane *playPane;

		ADebugFmodEventTreeData *completeTreeRoot; // all fmod event tree data

		UITextEntry *searchEntry;
		UICheckButton *isPlayingCheckButton;

		UITree *eventsTree;
		UICheckButton *expandAll;

		char *selectedEventPath;
		UILabel *selectedEventLabel;
		UILabel *eventsInfo;

		UIButton *copyEventPathButton;

		UIList *fmodParamsList;
		UIList *eventInstancesList;
		UIButton *playButton;
		UIButton *advancedPlayToggle; // hide/show advanced options
		U32 advancedDisplay;
		U32 playingEventsMemory;
		U32 eventSystemMemory;
		U32 eventsMemory;
		U32 systemMemory;
		UILabel *eventsMemoryLabel;
		UILabel *systemMemoryLabel;

		UICheckButton *customPosition; // enable custom position for sound preview
		UITextEntry *xPosPreview;
		UITextEntry *yPosPreview;
		UITextEntry *zPosPreview;
		UIButton *grabPosition;

		UIButton *stopAllButton;

		UICheckButton *playMultiple;
		UITextEntry *numInstances;
		UITextEntry *radius;
		UITextEntry *timeOffset;
		UIComboBox *multipleMode;

		UILabel *xLabel;
		UILabel *yLabel;
		UILabel *zLabel;

		UISlider *volumeSlider;
		UITextEntry *volumeTextEntry;
		UILabel *volumeDb;

		FmodEventInfo fmodEventInfo;

		ADebugFmodEventTreeData *selectedTreeNode;
		void *incomingFmodEventInstances[100];
		void **fmodEventInstances;

		UIButton *reloadButton;
	} fmodEvents;

	UITab *dspGraphTab;


	UITab *spacesTab;
	struct {

		UIList *globalSpacesList;
		UIList *nonExclusiveSpacesList;
		UIList *globalConnsList;

		UITabGroup *tabs;
		UITab *reverbSpaceTab;
		UITab *globalSpacesTab;
		UITab *nonExclusiveSpacesTab;
		UITab *globalConnsTab;

		UICheckButton *currentCheck;
		UITree *spacesTree;
		ADebugSpaceTreeData *spaceTreeRoot;
		UICheckButton *expandAll;

		SoundSpace *currentDisplaySpace;
		ADebugSpaceTreeData *selectedSpaceNode;
		SoundSpace *selectedSpace;
		UILabel *selectedName;
		UIList *paramsList;

		UILabel *currentSpaceName;
		UILabel *reverbName;

		UITabGroup *sourcesTabGroup;
		UITab *localSourcesTab;
		UITab *ownedSourcesTab;
		UITab *connectionsTab;
		UIList *localSourcesList;
		UIList *ownedSourcesList;
		UIList *connectionsList;
		UIList *reverbPropertyList;

		SoundSource **selectedLocalSources;
		SoundSource **selectedOwnedSources;
		SoundSpaceConnector **selectedConnections;

		ADebugDrawCB *drawAllCB; 
		ADebugDrawCB *drawSelectedCB; 
		//ADebugDrawCB *drawReverbSpheresCB;

		UICheckButton *displaySpaces;
		UICheckButton *displayReverbs;
		UICheckButton *bypassReverb;

		UIPane *rootPane;

		int numReverbParams;
		UILabel *reverbReturnLevel;
	} spaces;


	UITab *lodTab;
	struct {
		UICheckButton *enabledCheckButton;

		UILabel *currentStateLabel;
		UILabel *lastStateLabel;

		UILabel *currentMemory;
		UILabel *durationBelowThreshold;
		UILabel *ambientClipDistance;

		UITextEntry *durToRaiseLevelEntry;
		UITextEntry *thresholdEntry;

		SoundSource **activeSources;
		SoundSource **stoppedSources;

		UIList *activeSourceList;
		UIList *stoppedSourceList;
		UICheckButton *freezeCheckButton;

		UIPane *rootPane;
	} lod;

	UITab *sourcesTab;
	struct {
		UIPane *rootPane;

		UITabGroup *tabs;
		UITab *ungroupedTab;
		UITab *groupTab;

		// -- Group Tab -------------

		// Source Groups
		UITextEntry *sourceGroupFilter;
		UICheckButton *hasSourceCheckButton;

		UICheckButton *displaySources;
		UICheckButton *displayActiveSources;
		UICheckButton *displayInactiveSources;
		UICheckButton *displayDeadSources;
		ADebugDrawCB *drawAllCB; 
		ADebugDrawCB *drawSelectedCB; 

		UIList *sourceGroupsList; 

		// Selected Source Group
		UILabel *selectedSourceGroupLabel;

		SoundSourceGroup **sourceGroups; 
		int selectedSourceGroupId;

		// Sources
		UIList *activeSourcesList;
		SoundSource **activeSourcesModel;

		UIList *inactiveSourcesList;
		SoundSource **inactiveSourcesModel;

		UIList *deadSourcesList;
		SoundSource **deadSourcesModel;

		// Selected Source
		SoundSource *selectedSource;
		UILabel *selectedSourceLabel;

		UIList *sourceProperties;

		// Origin & Owner Spaces
		UITabGroup *spaceSelection; 

		UIList *spaceProperties;


		// -- UnGrouped Tab ------------------------
		struct {
			UITextEntry *sourceFilter;
			UIList *sourcesList;
			UIList *sourceProperties;
			UILabel *selectedSourceLabel; 
			SoundSource **filteredSources;
			ADebugSourceSortMode sortMode;
		} ungrouped;

	} sources;


	UITab *eventLogTab;
	struct {
		UIPane *rootPane;

		UITextEntry *nameFilter;
		UITextEntry *excludeFilter;

		UICheckButton *loggingEnabled;
		UICheckButton *audibleButton;

		UIButton *clearLogButton;

		ADebugLogEvent **events;
		ADebugLogEvent **filteredEvents;

		ADebugLogEvent *selectedLogEvent;
		ADebugDrawCB *drawCB;

		const char *filterStr;
		const char *excludeFilterStr;

		UILabel *eventCountLabel;

		StashTable sourceStash; // keep track of SoundSources (that are alive)

		UIList *eventList;
	} eventLog;

	UITab *duckingTab;
	struct {
		UIPane *rootPane;

		UICheckButton *enabledButton;
		UILabel *currentValue;

		UITextEntry *rate;
		UITextEntry *targetScaleFactor;
		UITextEntry *numEventThreshold;

	} ducking;

	struct {
		UIPane *rootPane;

		UISlider *windowOpacity;
	} settings;
	UITab *settingsTab;

} aDebugUI;

typedef enum {
	EVENTICONCOLOR_PROJECT,
	EVENTICONCOLOR_GROUP,
	EVENTICONCOLOR_INACTIVE_EVENT,
	EVENTICONCOLOR_ACTIVE_EVENT
} EventIconColor;

typedef enum {
	SPACEICONCOLOR_INAUDIBLE,
	SPACEICONCOLOR_AUDIBLE
} SpaceIconColor;


enum {
	ADEBUG_TAB_PERFORMANCE,
	ADEBUG_TAB_FMODEVENTS,
	ADEBUG_TAB_SPACES,
	ADEBUG_TAB_LOD,
	ADEBUG_TAB_SOURCES,
	ADEBUG_TAB_EVENTLOG,
	ADEBUG_TAB_DUCKING
};

struct {
	UIWindow *mainWnd;
	UIList *userList;
	UIList *chanList;
	UIList *channelUsers;
	UIList *userChannels;
} svDebugUI;

StashTable aDebugTreeStash = 0;

#ifdef STUB_SOUNDLIB


void aDebugOncePerFrame(F32 elapsed) { }
void aDebugDraw(void) { }

#else

extern SoundMixer *gSndMixer;

void aDebugSourceSetSortMode(ADebugSourceSortMode mode);
void aDebugSourcesSort();

void aDebugSetWindowOpacity(F32 opacity);

void aDebugPlayEvent(UIAnyWidget *widget, UserData userData);
void aDebugUpdatePreviewEventVolumeInDb(F32 dbVal);

void aDebugUpdateExpandAllSpacesTree();

void aDebugStopEventInstances(void *fmod_event);
int aDebugNumEventInstancesForEvent(void *fmod_event);

void aDebugSetVisibleAncestry(ADebugFmodEventTreeData *node);
void aDebugMarkAllNodesVisible(ADebugFmodEventTreeData *src, bool visible);

void aDebugFmodEventsUpdateFilter();
void aDebugUpdatePlayingMemory();

void aDebugUpdateExpandAllEventsTree();
void aDebugUpdateSpaceTree();

void aDebugReloadEvents(UIAnyWidget *widget, UserData userData);

//void aDebugSpaceUpdateReverbBypass();
SoundSpace *aDebugUpdateCurrentSpace();

int aDebugSourceStateAsInt(const SoundSource *source);

void aDebugUpdateDucking();

void aDebugDrawFmodEventsTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z);
void aDebugFillFmodEventsChild(UITreeNode *node, UserData fillData);
void aDebugFillFmodEventsTree(UITreeNode *node, UserData fillData);
void aDebugFmodEventSelected(UIAnyWidget *widget, UserData userData);
void aDebugFmodEventsTextSearch(UIAnyWidget *widget, UserData userData);
//void aDebugCopyFmodEventTreeDataProperties(ADebugFmodEventTreeData *dst, ADebugFmodEventTreeData *src);
void aDebugReleaseFmodEventTreeDataProperties(ADebugFmodEventTreeData *node);
void aDebugRecursivelyRemoveChildren(ADebugFmodEventTreeData **node);
//void aDebugCopyFmodEventChildren(ADebugFmodEventTreeData *dst, ADebugFmodEventTreeData *src);
void aDebugFilterFmodEvents(const char *filter);

void aDebugDrawAxes(F32 *p, int color);
void aDebugUpdateEventLog();

SoundSource* aDebugSoundSourceForEvent(void *fmod_instance);


const char *aDebugSourceStateAsString(SoundSource *source);

void aDebugUpdateLODInfo();
void aDebugEstrPrintVector(char **estrOutput, Vec3 vec);
void aDebugUpdateSources();
SoundSource *aDebugGetValidSelectedSource();
void aDebugMoveCameraToPosition(const Vec3 pos);

void aDebugActivateSpaceProperty(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData);

typedef int (*TraverseDSPGraphFunc)(void *dspNode, void *userData);

typedef struct ADebugDSPData {
	int totalMem;
	int visited;
} ADebugDSPData;


// Drawing ---
ADebugDrawCB* aDebugAddDrawCB(ADebugDrawCB ***drawCallbacks, ADebugDrawFunc drawFunc, ADebugDestroyDrawFunc destroyFunc, void *userData);
void aDebugRemoveDrawCB(ADebugDrawCB ***drawCallbacks, ADebugDrawCB *drawCB);
void aDebugRemoveAllDrawCB(ADebugDrawCB ***drawCallbacks);
// ---


void aDebugSplitStringWithDelimFast(char *str, const char *delim, char ***eaOutput)
{
	char *token;
	char *ptr = str;

	do {
		token = strsep2(&ptr, delim, NULL);
		if(token)
		{
			eaPush(eaOutput, token);
		}
	} while(token);
}

void aDebugSplitStringWithDelim(const char *str, const char *delim, char ***eaOutput)
{
	char *token;
	char *strCopy = strdup(str);
	char *ptr = strCopy;

	do {
		token = strsep2(&ptr, delim, NULL);
		if(token)
		{
			eaPush(eaOutput, strdup(token));
		}
	} while(token);

	free(strCopy);
}


int aDebugTraverseGraphMemTotal(void *dspNode, void *userData)
{
	ADebugDSPData *dspData = (ADebugDSPData*)userData;
	int result;
	unsigned int memUsed;

	result = fmodDSPGetMemoryInfo(dspNode, &memUsed, NULL);
	if(result == FMOD_OK)
	{
		dspData->totalMem += memUsed;
	}
	dspData->visited++;

	return result;
}

void aDebugTraverseDspGraph(void *dspNode, TraverseDSPGraphFunc func, StashTable nodeStash, void *userData)
{
	if(dspNode)
	{
		void **childNodes = NULL;

		fmodDSPGetChildren(dspNode, &childNodes);

		if(childNodes)
		{
			int i;
			int numChildren = eaSize(&childNodes);
			for(i = 0; i < numChildren; i++)
			{
				void *childNode = childNodes[i];
				void *node;

				if(!stashFindPointer(nodeStash, childNode, &node)) 
				{
					aDebugTraverseDspGraph(childNode, func, nodeStash, userData);

					stashAddPointer(nodeStash, childNode, childNode, true);
				}
			}
		}

		func(dspNode, userData);
	}
}

void aDebugUpdatePerformanceTab()
{
	char txt[256];
	int cur, mem;
	F32 cpu;
	F32 val;
	static ADebugDSPData dspData;
	static StashTable aDebugDSPNodeStash = NULL;

	FMOD_EventSystem_GetMemStats(&cur, &mem);
	cpu = fmodGetCPUUsage() / 100.0;
	MINMAX1(cpu, 0, 1);

	// Memory
	//ui_ProgressBarSet(g_audio_dbg.setting_pane.memory, (F32)cur/soundBufferSize);
	val = CLAMP( (F32)cur/soundBufferSize, 0.0, 1.0 );

	ui_ProgressBarSet(aDebugUI.performance.memoryProgressBar, val);
	{
		sprintf(txt, "%.1fk of %.1fk (max allocated: %.1fk)", cur/1024., soundBufferSize/1024., mem/1024.);
	}
	
	
	ui_LabelSetText(aDebugUI.performance.memoryInfoLabel, txt);

	// CPU
	if(cpu > aDebugUI.performance.cpuMax) aDebugUI.performance.cpuMax = cpu;

	ui_ProgressBarSet(aDebugUI.performance.cpuProgressBar, cpu);
	sprintf(txt, "%.1f%% (max: %.1f%%)", cpu*100.0, aDebugUI.performance.cpuMax*100.);
	ui_LabelSetText(aDebugUI.performance.cpuInfoLabel, txt);

	if(!aDebugDSPNodeStash)
	{
		aDebugDSPNodeStash = stashTableCreateAddress(100); // hold the events
	}
	

	// Traverse the DSP Graph (and accumulate memory total from each node)
	dspData.totalMem = 0; // reset
	dspData.visited = 0;
	stashTableClear(aDebugDSPNodeStash);
	aDebugTraverseDspGraph(fmodDSPGetSystemHead(), aDebugTraverseGraphMemTotal, aDebugDSPNodeStash, (void*)&dspData);

	sprintf(txt, "%.2fk (%d nodes)", dspData.totalMem / 1024.0, dspData.visited);
	ui_LabelSetText(aDebugUI.performance.dspGraphMemTotal, txt);

	if(!g_audio_state.dsp_enabled)
	{
		ui_LabelSetText(aDebugUI.performance.dspEnabled, "No");
	}
	else
	{
		ui_LabelSetText(aDebugUI.performance.dspEnabled, "Yes");
	}

	if(gSndMixer)
	{
		int numEvents = sndMixerNumEventsPlaying(gSndMixer);
		int maxPlaybacks = sndMixerMaxPlaybacks(gSndMixer);
		int ignored = sndMixerNumIgnoredEvents(gSndMixer);
		int skipped = sndMixerEventsSkippedDueToMemory(gSndMixer);

		if(maxPlaybacks <= 0)
		{
			sprintf(txt, "%d of unlimited (%d skipped due to num events) (%d skipped due to lack of mem)", numEvents, ignored, skipped);
		}
		else
		{
			sprintf(txt, "%d of %d (%d skipped due to num events) (%d skipped due to lack of mem)", numEvents, maxPlaybacks, ignored, skipped);
		}
		
		ui_LabelSetText(aDebugUI.performance.numEventsPlaying, txt);
	}

	sprintf(txt, "%d", eaSize(&space_state.sources));
	ui_LabelSetText(aDebugUI.performance.numSources, txt);

	sprintf(txt, "%f", wlTimeGet());
	ui_LabelSetText(aDebugUI.performance.timeOfDay, txt);

	sprintf(txt, "%s", g_audio_state.player_in_combat_func ? (g_audio_state.player_in_combat_func() ? "Yes" : "No") : "Unknown");
	ui_LabelSetText(aDebugUI.performance.incombatLabel, txt);

	{
		Vec3 pos, vel, forward, up;

		FMOD_EventSystem_get3DListenerAttributes(pos, vel, forward, up);

		sprintf(txt, "<%.2f, %.2f, %.2f> <%.2f, %.2f, %.2f>", pos[0], pos[1], pos[2], vel[0], vel[1], vel[2]);
		ui_LabelSetText(aDebugUI.performance.listenerAttributes, txt);
	}
	
}

void aDebugUpdateFmodEventsTab()
{
	int numEvents;
	int numInstances;
	int numPlaying;
	char txt[128];
	int cur, mem;

	fmodEventSystemGetNumEvents(&numEvents, &numInstances, &numPlaying);

	FMOD_EventSystem_GetMemStats(&cur, &mem);

	sprintf(txt, "Count E=%d  I=%d  P=%d", numEvents, numInstances, numPlaying);
	ui_LabelSetText(aDebugUI.fmodEvents.eventsInfo, txt);

	sprintf(txt, "Mem ES=%.1fk E=%.1fk P=%.1fk", aDebugUI.fmodEvents.eventSystemMemory / 1024., aDebugUI.fmodEvents.eventsMemory / 1024.0, aDebugUI.fmodEvents.playingEventsMemory / 1024.0);
	ui_LabelSetText(aDebugUI.fmodEvents.eventsMemoryLabel, txt);

	sprintf(txt, "Mem Totals S=%.1fk Pool=%.1fk", aDebugUI.fmodEvents.systemMemory / 1024., cur / 1024.);
	ui_LabelSetText(aDebugUI.fmodEvents.systemMemoryLabel, txt);


	aDebugUpdatePlayingMemory();
	// does the tree need to be filtered?
	if( ui_CheckButtonGetState(aDebugUI.fmodEvents.isPlayingCheckButton) ) 
	{
		aDebugFmodEventsUpdateFilter();
	}
}

void aDebugOncePerFrame(F32 elapsed)
{
	if(aDebugUI.mainWindow)
	{
		static int frameCount = 0;

		if(frameCount > 0) // only update once per N frames
		{
			int activeTab = ui_TabGroupGetActiveIndex(aDebugUI.mainTabs);

			aDebugUpdateCurrentSpace();

			switch(activeTab)
			{
				case ADEBUG_TAB_PERFORMANCE:
					aDebugUpdatePerformanceTab();
					break;
				case ADEBUG_TAB_FMODEVENTS:
					aDebugUpdateFmodEventsTab();
					break;
				case ADEBUG_TAB_SPACES:
					// see above (aDebugUpdateCurrentSpace) called regardless of tab
					break;
				case ADEBUG_TAB_LOD:
					aDebugUpdateLODInfo();
					break;
				case ADEBUG_TAB_SOURCES:
					aDebugUpdateSources();
					break;
				case ADEBUG_TAB_EVENTLOG:
					aDebugUpdateEventLog();
					break;
				case ADEBUG_TAB_DUCKING:
					aDebugUpdateDucking();
					break;
			}

			frameCount = 0;
		}
		frameCount++;
	}
}

///////////////////////////////////////////////////////////////////////////////
// Fmod Events Tree
///////////////////////////////////////////////////////////////////////////////

void aDebugFmodEventTreeDisplayText(UITreeNode *node, EventIconColor colorIndex, const char *text1, const char *text2, int columnIndex, UI_MY_ARGS, F32 z)
{
	static Color projectColor = { 128, 0, 128, 128 };
	static Color groupColor = { 0, 0, 128, 128 };
	static Color nonActiveEventColor = { 0, 0, 255, 128 };
	static Color activeEventColor = { 0, 128, 0, 128 };
	Color color;

	int indent;
	AtlasTex *opened;
	F32 indentOffset;
	F32 icon_x1, icon_x2, icon_y1, icon_y2;
	int count_x;
	int columnOffset;
	int column_0_x;
	int column_1_x;

	UITree* tree = node->tree;
	CBox total_box = {x, y, x + w, y + h};
	
	UIStyleFont *font = GET_REF(UI_GET_SKIN(tree)->hNormal);
	CBox col_0_box = total_box;

	indent = ui_TreeNodeGetIndent(node);
	opened = (g_ui_Tex.minus);
	indentOffset = (UI_STEP + opened->width) * scale;

	column_0_x = 220;
	column_1_x = 240;

	columnOffset = (columnIndex == 0) ? column_0_x : column_1_x;

	count_x = x + (columnOffset - indent*indentOffset);
	col_0_box.hx = x + (column_0_x - 5);

	ui_StyleFontUse(font, node == tree->selected || (tree->multiselect && eaFind(&tree->multiselected, node) >= 0), UI_WIDGET(tree)->state);
	clipperPushRestrict(&col_0_box);

	// draw the 'icon'
	icon_x1 = x;
	icon_x2 = x+10;
	icon_y1 = y + (h / 2.0) - 5;
	icon_y2 = y + (h / 2.0) + 5;
	
	switch(colorIndex) {
		case EVENTICONCOLOR_PROJECT: color = projectColor; break;
		case EVENTICONCOLOR_GROUP: color = groupColor; break;		
		default:
		case EVENTICONCOLOR_INACTIVE_EVENT: color = nonActiveEventColor; break;
		case EVENTICONCOLOR_ACTIVE_EVENT: color = activeEventColor; break;
	}
	gfxDrawQuad(icon_x1, icon_y1, icon_x2, icon_y2, z, color);


	gfxfont_Printf(x+15, y + h/2, z, scale, scale, CENTER_Y, "%s", text1);
	clipperPop();
// (columnOffset - indent*indentOffset)
	if(text2 && text2[0]) {
		clipperPushRestrict(&total_box);
		gfxfont_Printf(x + w - 25, y + h/2, z, scale, scale, CENTER_Y, "%s", text2);
		clipperPop();
	}
}

void aDebugDrawFmodEventsTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	ADebugFmodEventTreeData *data = (ADebugFmodEventTreeData*)node->contents;
	int numChildren = eaSize(&data->children);
	int visibleChildrenCount = 0;
	int columnIndex = 0;
	static char str1[128];
	static char str2[16];
	EventIconColor colorIndex;

	sprintf(str1, "%s", data->name);

	if(numChildren > 0)
	{
		int i;
		// how many of the children are visible
		for(i = 0; i < numChildren; i++)
		{
			if(data->children[i]->isVisible) visibleChildrenCount++;
		}
		sprintf(str2, "%d", visibleChildrenCount);

		if(data->type == STT_PROJECT) {
			colorIndex = EVENTICONCOLOR_PROJECT;
		} else {
			colorIndex = EVENTICONCOLOR_GROUP;
		}
	} 
	else 
	{
		// most likely an event
		if(data->fmod_event)
		{
			int numInstances = aDebugNumEventInstancesForEvent(data->fmod_event);
			
			if(numInstances > 0){
				colorIndex = EVENTICONCOLOR_ACTIVE_EVENT;
				sprintf(str2, "%d", numInstances);	
			} else {
				colorIndex = EVENTICONCOLOR_INACTIVE_EVENT;
				str2[0] = '\0';
			}
		} 
		else 
		{
			colorIndex = EVENTICONCOLOR_INACTIVE_EVENT;
			str2[0] = '\0';
		}

		columnIndex = 1;
	}
	
	//ui_TreeDisplayText(node, str, UI_MY_VALUES, z);
	aDebugFmodEventTreeDisplayText(node, colorIndex, str1, str2, columnIndex, UI_MY_VALUES, z); // custom display func
}

void aDebugFillFmodEventsChild(UITreeNode *node, UserData fillData)
{
	ADebugFmodEventTreeData *parent = (ADebugFmodEventTreeData*)fillData;
	int i;

	for ( i = 0; i < eaSize(&parent->children); ++i )
	{
		ADebugFmodEventTreeData *data = parent->children[i];
		if(data->isVisible)
		{
			UITreeNode *newNode = ui_TreeNodeCreate(
				node->tree, cryptAdler32String(data->name), parse_ADebugFmodEventTreeData, data,
				eaSize(&data->children) ? aDebugFillFmodEventsChild : NULL, data,
				aDebugDrawFmodEventsTreeNode, NULL, 
				20);
			ui_TreeNodeAddChild(node, newNode);
		}
	}
}

void aDebugFillFmodEventsTree(UITreeNode *node, UserData fillData)
{
	ADebugFmodEventTreeData *data = (ADebugFmodEventTreeData*)fillData;
	if(data)
	{
		if(data->isVisible)
		{
			UITreeNode *newNode = ui_TreeNodeCreate(
				node->tree, cryptAdler32String(data->name), parse_ADebugFmodEventTreeData, fillData,
				aDebugFillFmodEventsChild, fillData, aDebugDrawFmodEventsTreeNode, "Fmod Event Tree", 20);
			ui_TreeNodeAddChild(node, newNode);
		}
	}
}

void aDebugExpandAllNodes(UITreeNode *treeNode)
{
	int i;
	int numChildren;

	ui_TreeNodeExpandAndCallback(treeNode);

	numChildren = eaSize(&treeNode->children);
	for(i = 0; i < numChildren; i++) 
	{
		UITreeNode *childNode = treeNode->children[i];
		aDebugExpandAllNodes(childNode);
	}
}

void aDebugUpdateExpandAllEventsTree()
{
	U32 state;

	state = ui_CheckButtonGetState(aDebugUI.fmodEvents.expandAll);
	if(state)
	{
		UITreeNode *rootTreeNode = &aDebugUI.fmodEvents.eventsTree->root;
		aDebugExpandAllNodes(rootTreeNode);
	}
}

void aDebugExpandAllEventsTree(UIAnyWidget *widget, UserData userData) 
{
	aDebugUpdateExpandAllEventsTree();
}

void aDebugCopyEventNameToClipboard(UIAnyWidget *widget, UserData userData) 
{
	if(aDebugUI.fmodEvents.selectedEventPath)
	{
		winCopyToClipboard(aDebugUI.fmodEvents.selectedEventPath);
	}
}

void aDebugFmodEventSelected(UIAnyWidget *widget, UserData userData) 
{
	UITree* tree = (UITree*)widget;
	UITreeNode* uiNode = tree->selected;
	if(uiNode)
	{
		char labelText[128];
		F32 vol;
		F32 dbVol;
		char volText[16];
		//char *eventPath = NULL;
		static char *typesText[] = { "Unknown", "Project", "Category", "Event Group", "Event", "Event Instance" };

		ADebugFmodEventTreeData *node_data = (ADebugFmodEventTreeData*)uiNode->contents;

		if(node_data->type < 1 || node_data->type > 5) 
		{
			node_data->type = 0; // unknown
		}
		
		if(node_data->fmod_event && node_data->type == 4)
		{
			fmodEventGetFullName(&aDebugUI.fmodEvents.selectedEventPath, node_data->fmod_event, false);
		}
		else
		{
			estrClear(&aDebugUI.fmodEvents.selectedEventPath);
			estrAppend2(&aDebugUI.fmodEvents.selectedEventPath, node_data->name);
		}

		sprintf(labelText, "[%s] %s", typesText[node_data->type], aDebugUI.fmodEvents.selectedEventPath);
		
		ui_LabelSetText(aDebugUI.fmodEvents.selectedEventLabel, labelText);

		
		aDebugUI.fmodEvents.selectedTreeNode = node_data;

		//if(eaSize(&node_data->previewSources))
		if(aDebugNumEventInstancesForEvent(node_data->fmod_event))
		{
			ui_ButtonSetText(aDebugUI.fmodEvents.playButton, "Stop");
		} 
		else 
		{
			ui_ButtonSetText(aDebugUI.fmodEvents.playButton, "Play");
		}

		vol = fmodEventSystemGetVolumeProperty(node_data->fmod_event);
		
		dbVol = AmpToDb(vol);
		sprintf(volText, "%.1f", dbVol);

		ui_SliderSetValue(aDebugUI.fmodEvents.volumeSlider, dbVol);
		ui_TextEntrySetText(aDebugUI.fmodEvents.volumeTextEntry, volText);
	}
}

void aDebugFmodEventsTextSearch(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *textEntry = (UITextEntry*)widget;
	const char *search_text = ui_TextEntryGetText(textEntry);

	ui_CheckButtonSetState(aDebugUI.fmodEvents.expandAll, true);
	aDebugUpdateExpandAllEventsTree();

	aDebugFilterFmodEvents(search_text);
}

void aDebugSetVisibleAncestry(ADebugFmodEventTreeData *node)
{
	node->isVisible = 1;
	if(node->parent)
	{
		aDebugSetVisibleAncestry(node->parent);
	}
}

void aDebugUpdatePlayingMemory()
{
	static void **events;
	int numEvents;
	int i;
	int totalMem;

	eaClear(&events);
	fmodEventSystemGetPlaying(&events);
	numEvents = eaSize(&events);

	totalMem = 0;
	for(i = 0; i < numEvents; i++)
	{
		int memUsed;
		void *info_event;
		void *fmod_event = events[i];

		fmodEventGetInfoEvent(fmod_event, &info_event);
		fmodEventGetMemoryInfo(fmod_event, &memUsed, NULL);

		totalMem += memUsed;
	}

	fmodEventSystemGetMemoryInfo(&aDebugUI.fmodEvents.eventSystemMemory, NULL);
	fmodSystemGetMemoryInfo(&aDebugUI.fmodEvents.systemMemory, NULL);

	aDebugUI.fmodEvents.playingEventsMemory = totalMem;
}

void aDebugFmodEventsUpdateFilter() //UIAnyWidget *widget, UserData userData)
{
	ADebugFmodEventTreeData *node;
	static void **events;
	int numEvents;
	int i;
	UITreeNode *selectedNode = NULL;

	eaClear(&events);
	fmodEventSystemGetPlaying(&events);
	numEvents = eaSize(&events);

	// TODO(gt): could optimize this later to keep track of differences between each call
	aDebugMarkAllNodesVisible(aDebugUI.fmodEvents.completeTreeRoot, false);

	for(i = 0; i < numEvents; i++)
	{
		void *info_event;
		void *fmod_event = events[i];

		fmodEventGetInfoEvent(fmod_event, &info_event);

		if(stashFindPointer(aDebugTreeStash, info_event, &node)) 
		{
			aDebugSetVisibleAncestry(node);
		}
	}

	// make sure selection is maintained...
	selectedNode = aDebugUI.fmodEvents.eventsTree->selected;

	ui_TreeRefresh(aDebugUI.fmodEvents.eventsTree);

	aDebugUI.fmodEvents.eventsTree->selected = selectedNode;

	aDebugUpdateExpandAllEventsTree();
}

//void aDebugCopyFmodEventTreeDataProperties(ADebugFmodEventTreeData *dst, ADebugFmodEventTreeData *src)
//{
//	dst->name = strdup(src->name);
//	dst->type = src->type;
//	dst->fmod_event = src->fmod_event;
//}

void aDebugReleaseFmodEventTreeDataProperties(ADebugFmodEventTreeData *node)
{
	free(node->name);
}

void aDebugRecursivelyRemoveChildren(ADebugFmodEventTreeData **node)
{
	if(*node)
	{
		if((*node)->children)
		{
			int numChildren = eaSize(&(*node)->children);
			int i;

			for(i = 0; i < numChildren; i++)
			{
				ADebugFmodEventTreeData *child = (*node)->children[i];
				aDebugRecursivelyRemoveChildren(&child);
			}
			eaDestroy(&(*node)->children);
		}

		aDebugReleaseFmodEventTreeDataProperties(*node);
		free(*node);

		*node = NULL;
	}
}


void aDebugMarkAllNodesVisible(ADebugFmodEventTreeData *src, bool visible)
{
	if(src)
	{
		if(src->children)
		{
			int numChildren = eaSize(&src->children);
			int i;

			for(i = 0; i < numChildren; i++)
			{	
				aDebugMarkAllNodesVisible(src->children[i], visible);
			}
		}
		src->isVisible = visible ? 1 : 0;
	}
}

void aDebugFmodEventsIsPlayingToggle(UIAnyWidget *widget, UserData userData)
{
	UICheckButton *checkButton = (UICheckButton*)widget;

	if(ui_CheckButtonGetState(checkButton))
	{
		aDebugMarkAllNodesVisible(aDebugUI.fmodEvents.completeTreeRoot, false);
		aDebugFmodEventsUpdateFilter();
	} 
	else 
	{
		const char *search_text = ui_TextEntryGetText(aDebugUI.fmodEvents.searchEntry);
		aDebugFilterFmodEvents(search_text);
	}
}

bool aDebugSetFmodEventTreeFilter(ADebugFmodEventTreeData *src, ADebugFilter *filter)
{
	bool isVisible = false;

	if(src)
	{	
		int numPathComponents = eaSize(&filter->pathComponents);

		// either this node contains the string or one of its children (etc...)
		if(filter->name && !filter->name[0]) 
		{
			isVisible = true;
		} 
		else if( filter->name ) 
		{
			// check each component
			if(numPathComponents > 1)
			{
				ADebugFmodEventTreeData *node = src;
				char *nodeName;
				int pathIndex = numPathComponents - 1;
				bool matchFailed = false;
				bool matched = false;

				while(pathIndex >= 0 && node)
				{
					char *lastPathComponent = filter->pathComponents[pathIndex];
					nodeName = node->name;

					if( strstri(nodeName, lastPathComponent) ) 
					{
						matched = true;
					} 
					else
					{
						matchFailed = true;
					}

					node = node->parent;
					pathIndex--;
				}

				if(matched && !matchFailed)
				{
					isVisible = true;
				}

			}
			else if(strstri(src->name, filter->name)) 
			{
				isVisible = true;
			}
		}

		if(src->children)
		{
			int numChildren = eaSize(&src->children);
			int i;

			for(i = 0; i < numChildren; i++)
			{	
				bool result;
				ADebugFmodEventTreeData *child = src->children[i];

				result = aDebugSetFmodEventTreeFilter(child, filter);
				if(result)
				{
					isVisible = true;
				}
			}
		}
		
		// set the flag
		src->isVisible = isVisible ? 1 : 0;
	}

	return isVisible;
}

//bool aDebugCopyNodesWithFlag(ADebugFmodEventTreeData **dst, ADebugFmodEventTreeData *src)
//{
//	bool copied = false;
//
//	if(src && src->isVisible) 
//	{
//		ADebugFmodEventTreeData *newNode;
//		
//		newNode = calloc(1, sizeof(ADebugFmodEventTreeData));
//
//		aDebugCopyFmodEventTreeDataProperties(newNode, src);
//
//		if(src->children)
//		{
//			int numChildren = eaSize(&src->children);
//			int i;
//
//			for(i = 0; i < numChildren; i++)
//			{
//				ADebugFmodEventTreeData *child = src->children[i];
//				ADebugFmodEventTreeData *newChildNode = NULL;
//
//				if(aDebugCopyNodesWithFlag(&newChildNode, child))
//				{
//					// it was copied, so add the child
//					eaPush(&newNode->children, newChildNode);
//				}
//			}
//		}
//
//		*dst = newNode; 
//		copied = true;
//	}
//	return copied;
//}

void aDebugFilterFmodEvents(const char *filter) 
{
	aDebugUI.fmodEvents.eventFilter.pathComponents = NULL; // make sure we init this
	aDebugUI.fmodEvents.eventFilter.name = filter;
	aDebugUI.fmodEvents.eventFilter.isPlaying = ui_CheckButtonGetState(aDebugUI.fmodEvents.isPlayingCheckButton);

	// clean out path
	FOR_EACH_IN_EARRAY(aDebugUI.fmodEvents.eventFilter.pathComponents, char, pchComponent)
	{
		SAFE_FREE(pchComponent);
	}
	FOR_EACH_END
	eaClear(&aDebugUI.fmodEvents.eventFilter.pathComponents);

	// check for path
	aDebugSplitStringWithDelim(filter, "/", &aDebugUI.fmodEvents.eventFilter.pathComponents);

	// first pass, determine which nodes we need to keep (using isVisible flag)
	aDebugSetFmodEventTreeFilter(aDebugUI.fmodEvents.completeTreeRoot, &aDebugUI.fmodEvents.eventFilter);

	ui_TreeNodeSetFillCallback(&aDebugUI.fmodEvents.eventsTree->root, aDebugFillFmodEventsTree, aDebugUI.fmodEvents.completeTreeRoot);

	ui_TreeRefresh(aDebugUI.fmodEvents.eventsTree);

	
}


U32 aDebugBuildEventTree(char *name, void *e_g_c, SoundTreeType type, void *p_userdata, void *userdata, void **new_p_userdata)
{
	ADebugFmodEventTreeData *node;

	node = calloc(1,sizeof(ADebugFmodEventTreeData));
	node->name = strdup(name);
	node->fmod_event = e_g_c;
	node->type = type;

	// add it to the lookup table
	if (node->fmod_event &&
		type == STT_EVENT)
	{
		FMOD_RESULT result = FMOD_OK;
		int memUsed = 0;

		stashAddPointer(aDebugTreeStash, node->fmod_event, node, 1);

		result = fmodEventGetMemoryInfo(node->fmod_event, &memUsed, NULL);
		
		if (result != FMOD_OK) {
			ErrorDetailsf("Result = %s", fmodGetErrorText(result));
			Errorf("%s : FMOD Error when attempting fmodEventGetMemoryInfo", __FUNCTION__);
		}
		
		aDebugUI.fmodEvents.eventsMemory += memUsed;
	}

	if( *new_p_userdata == NULL )
	{
		ADebugFmodEventTreeData *parent = (ADebugFmodEventTreeData*)p_userdata;
		if(parent) 
		{
			node->parent = parent;
			*new_p_userdata = node;

			// add as a child
			eaPush(&parent->children, node);
			
		}
	} else {
		ADebugFmodEventTreeData *parent = *new_p_userdata;
		node->parent = parent;

		// add as a child
		eaPush(&parent->children, node);
	}

	return 1;
}

///////////////////////////////////////////////////////////////////////////////
// Fmod Properties
///////////////////////////////////////////////////////////////////////////////


void aDebugFmodPropertyName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugFmodEventTreeData *nodeData = aDebugUI.fmodEvents.selectedTreeNode;
	void *fmod_event;
	if(!nodeData) 
	{
		estrPrintf(estrOutput, "");
		return;
	}

	fmod_event = nodeData->fmod_event;
	if(fmod_event && nodeData->type == STT_EVENT)
	{
		if(iRow <= ADEBUG_END_OF_LIST)
		{
			estrPrintf(estrOutput, "%s", aDebugFmodPropertyNames[iRow]);
		}
	}
}

void aDebugGetFmodEventInstances(void *fmod_event, void ***fmodEventInstances)
{
	U32 result;
	int i;

	// instances...
	aDebugUI.fmodEvents.fmodEventInfo.numinstances = 100; // max
	aDebugUI.fmodEvents.fmodEventInfo.instances = aDebugUI.fmodEvents.incomingFmodEventInstances;
	aDebugUI.fmodEvents.fmodEventInfo.wavebankinfo = NULL;
	aDebugUI.fmodEvents.fmodEventInfo.maxwavebanks = 0;

	eaClear(fmodEventInstances);

	result = fmodGetFmodEventInfo(fmod_event, &aDebugUI.fmodEvents.fmodEventInfo);
	if( !result )  // FMOD_ERR_MEMORY
	{
		// copy events
		for(i = 0; i < aDebugUI.fmodEvents.fmodEventInfo.numinstances; i++)
		{
			void *fmod_event_instance = aDebugUI.fmodEvents.incomingFmodEventInstances[i];
			eaPush(fmodEventInstances, fmod_event_instance);
		}
	}
}

void aDebugPrintSoundType(char **estrOutput, SoundType soundType)
{
	switch(soundType)
	{
		case SND_MAIN: estrPrintf(estrOutput, "Main"); break;
		case SND_FX: estrPrintf(estrOutput, "Fx"); break;
		case SND_AMBIENT: estrPrintf(estrOutput, "Ambient"); break;
		case SND_MUSIC: estrPrintf(estrOutput, "Music"); break;
		case SND_TEST: estrPrintf(estrOutput, "Test"); break;
		case SND_UI: estrPrintf(estrOutput, "UI"); break;
		case SND_VOICE: estrPrintf(estrOutput, "Voice"); break;
		case SND_NOTIFICATION: estrPrintf(estrOutput, "Notification"); break;
		default: estrPrintf(estrOutput, "<Unknown>"); break;
	}
}

SoundSourceGroup *aDebugGetSoundSourceGroup(int systemId)
{
	SoundSourceGroup *group = NULL;

	stashIntFindPointer(g_audio_state.sndSourceGroupTable, systemId, &group);

	return group;
}

void aDebugFmodPropertyValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugFmodEventTreeData *nodeData = aDebugUI.fmodEvents.selectedTreeNode;
	void *fmod_event;

	if(!nodeData)  
	{
		estrPrintf(estrOutput, "");
		return;
	}

	fmod_event = nodeData->fmod_event;
	if(fmod_event && nodeData->type == STT_EVENT)
	{
		char *name = NULL;
		F32 audibility, audibility_db;
		static Fmod3DRolloffType rolloffMode;
		static F32 minDistance, maxDistance;
		static const char *rolloffModeStrs[] = { "Custom", "Log", "Linear" };
		//static SoundSourceGroup *sndSourceGroup = NULL;
		static EventMetaData *eventMetaData = NULL;
		int userPropertyValue;
		static int lastRow = -1;

		// only read once per property refresh 
		// this prevents it from re-reading the structure for every data property
		if(iRow <= lastRow || lastRow == -1)
		{	
			fmodEventGet3DInfo(fmod_event, &rolloffMode, &minDistance, &maxDistance);

			aDebugGetFmodEventInstances(fmod_event, &aDebugUI.fmodEvents.fmodEventInstances);

			//sndSourceGroup = aDebugGetSoundSourceGroup(aDebugUI.fmodEvents.fmodEventInfo.systemid);
			eventMetaData = sndFindMetaData(fmod_event);
			
			lastRow = iRow;
		}

		switch(iRow) {
			case ADEBUG_NAME: // name
				switch(nodeData->type) {
					case STT_EVENT:
					case STT_EVENT_INSTANCE:
						FMOD_EventSystem_GetName(fmod_event, &name);
						estrPrintf(estrOutput, "%s", name);
						break;
					case STT_GROUP:
						FMOD_EventSystem_GetGroupName(fmod_event, &name);
						estrPrintf(estrOutput, "%s", name);
						break;
					default:
						estrPrintf(estrOutput, "");
						break;
				}
				break;

			case ADEBUG_SOUND_TYPE:
				if(eventMetaData)
				{
					aDebugPrintSoundType(estrOutput, eventMetaData->type);
					estrConcatf(estrOutput, " (%s)", eventMetaData->streamed ? "streamed" : "memory");
				}
				break;

			case ADEBUG_PROJECT_FILENAME:
				if(eventMetaData)
					estrPrintf(estrOutput, "%s", eventMetaData->project_filename);
				break;

			case ADEBUG_EXCLUSIVE_GROUP:
				if(eventMetaData)
					estrPrintf(estrOutput, "%s", eventMetaData->exclusive_group);
				break;

			case ADEBUG_MOVEMENT:
				if(eventMetaData)
				{
					if(eventMetaData->moving < 0)
					{
						estrPrintf(estrOutput, "ignore movement - always play source");
					}
					else if(eventMetaData->moving == 0)
					{
						estrPrintf(estrOutput, "stop playback when source moves");
					}		
					else if(eventMetaData->moving > 0)
					{
						estrPrintf(estrOutput, "only play when source moves");
					}		
				}
				break;
			case ADEBUG_ENABLEDSP:
				fmodEventGetPropertyByName(fmod_event, "EnableDSP", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;

			case ADEBUG_2DALLOWED:
				fmodEventGetPropertyByName(fmod_event, "2DAllowed", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;

			case ADEBUG_2DFADE:
				fmodEventGetPropertyByName(fmod_event, "2DFade", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;
			
			case ADEBUG_IGNOREPOSITION:
				fmodEventGetPropertyByName(fmod_event, "IgnorePosition", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;
			
			case ADEBUG_IGNORE3D:
				fmodEventGetPropertyByName(fmod_event, "Ignore3d", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;
			
			case ADEBUG_MUSIC:
				fmodEventGetPropertyByName(fmod_event, "Music", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;

			case ADEBUG_IGNORELOD:
				fmodEventGetPropertyByName(fmod_event, "IgnoreLOD", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;

			case ADEBUG_DUCKABLE:
				fmodEventGetPropertyByName(fmod_event, "Duckable", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;
			
			case ADEBUG_CLICKIE:
				fmodEventGetPropertyByName(fmod_event, "clickie", (void*)&userPropertyValue, false);
				if( userPropertyValue > 0 )
				{
					estrPrintf(estrOutput, "YES");
				}
				break;

			case ADEBUG_VOLUME: {
				F32 vol = fmodEventSystemGetVolumeProperty(fmod_event);
					
				estrPrintf(estrOutput, "%f (%.2f dB)", vol, AmpToDb(vol));
				break;
			}
			case ADEBUG_LENGTH: 
				

				if(fmodEventIsLooping(fmod_event)) //aDebugUI.fmodEvents.fmodEventInfo.lengthms == -1)
				{
					estrPrintf(estrOutput, "loops");// %.3fs", aDebugUI.fmodEvents.fmodEventInfo.lengthmsnoloop / 1000.0);
				} 
				else 
				{
					if(aDebugUI.fmodEvents.fmodEventInfo.lengthms > 0)
					{
						estrPrintf(estrOutput, "one-shot %.3fs", aDebugUI.fmodEvents.fmodEventInfo.lengthms / 1000.0);
					}
					else
					{
						estrPrintf(estrOutput, "One-shot (WARNING: UNKNOWN DURATION - MAKE SURE THE LOOP SETTINGS ARE CORRECT)");
					}
					
				}
				break;
			case ADEBUG_INSTANCES_ACTIVE:
				estrPrintf(estrOutput, "%d", aDebugUI.fmodEvents.fmodEventInfo.instancesactive);
				break;
			case ADEBUG_MAX_WAVEBANKS:
				estrPrintf(estrOutput, "%d", aDebugUI.fmodEvents.fmodEventInfo.maxwavebanks);
				//{
				//	int bankIndex;
				//	for(bankIndex = 0; bankIndex < aDebugUI.fmodEvents.fmodEventInfo.maxwavebanks; bankIndex++)
				//	{
				//		aDebugUI.fmodEvents.fmodEventInfo.wavebankinfo[bankIndex];

				//	}
				//}
				break;

			case ADEBUG_3D_MODE:
				estrPrintf(estrOutput, "%s", fmodEventIs2D(fmod_event) ? "2D" : "3D");
				break;
			case ADEBUG_3D_ROLLOFF_MODE: {
				int index;
				index = (int)rolloffMode;
				CLAMP(index, 0, 2);

				estrPrintf(estrOutput, "%s", rolloffModeStrs[index]);
				break;
			}
			case ADEBUG_3D_MIN:
				estrPrintf(estrOutput, "%.2f", minDistance);
				break;
			case ADEBUG_3D_MAX:
				estrPrintf(estrOutput, "%.2f", maxDistance);
				break;
			case ADEBUG_MAX_PLAYBACKS:
				estrPrintf(estrOutput, "%d", fmodEventGetMaxPlaybacks(fmod_event));
				break;
			case ADEBUG_DOPPLER_SCALE:
				estrPrintf(estrOutput, "%.2f", fmodEventGetDopplerScale(fmod_event));
				break;
			case ADEBUG_3D_PAN_LEVEL:
				estrPrintf(estrOutput, "%.2f", fmodEventGetPanLevel(fmod_event));
				break;
			case ADEBUG_FADEIN_TIME:
				estrPrintf(estrOutput, "%d ms", fmodEventGetFadeInTime(fmod_event));
				break;
			case ADEBUG_FADEOUT_TIME:
				estrPrintf(estrOutput, "%d ms", fmodEventGetFadeOutTime(fmod_event));
				break;
			case ADEBUG_REVERB_DRY_LEVEL:
				estrPrintf(estrOutput, "%.3f dB (unsupported property)", fmodEventGetReverbDryLevel(fmod_event));
				break;
			case ADEBUG_REVERB_WET_LEVEL:
				estrPrintf(estrOutput, "%.3f dB (unsupported property)", fmodEventGetReverbWetLevel(fmod_event));
				break;
			case ADEBUG_PROJECT_ID:
				estrPrintf(estrOutput, "%d", aDebugUI.fmodEvents.fmodEventInfo.projectid);
				break;
			case ADEBUG_SYSTEM_ID:
				estrPrintf(estrOutput, "%d", aDebugUI.fmodEvents.fmodEventInfo.systemid);
				break;
			case ADEBUG_AUDIBILITY:

				audibility = aDebugUI.fmodEvents.fmodEventInfo.audibility;
				audibility_db = AmpToDb(audibility);
		
				estrPrintf(estrOutput, "%f (%.2f dB)", audibility, audibility_db);
				break;
			case ADEBUG_MEMORY_USED: {
				unsigned int memUsed = 0;
				//static unsigned int memUsedArray[FMOD_MEMTYPE_MAX];

				fmodEventGetMemoryInfo(fmod_event, &memUsed, NULL);
				
				estrPrintf(estrOutput, "%d (%.2fk)", memUsed, memUsed / 1024.0);
				break;
			}
			default:
				estrPrintf(estrOutput, "");
				break;
		}
	} 
	else 
	{
		estrPrintf(estrOutput, "");
	}
}

void aDebugEventInstanceIndex(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_event_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_event_instance)
	{
		int memUsed;

		fmodEventGetMemoryInfo(fmod_event_instance, &memUsed, NULL);

		estrPrintf(estrOutput, "%d %.1fk", iRow, (float)memUsed / 1024.0);
	} 
	else
	{
		estrPrintf(estrOutput, "%d", iRow);
	}
}



void aDebugActivateEventInstance(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{ 
	void *fmod_event_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_event_instance)
	{
		void *userData = NULL;

		fmodEventGetUserData(fmod_event_instance, &userData);

		if(userData)
		{
			SoundSource *source = (SoundSource*)userData;
			aDebugMoveCameraToPosition(source->virtual_pos);
		}
	}
}


void aDebugEventInstancePosition(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_event_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_event_instance)
	{
		FmodEventInfo instanceInfo = {0};
		int pos;
		int length;
		float percent;

		fmodGetFmodEventInfo(fmod_event_instance, &instanceInfo);
		 
		pos = instanceInfo.positionms;
		//length = instanceInfo.lengthms == -1 ? instanceInfo.lengthmsnoloop : instanceInfo.lengthms;
		length = instanceInfo.lengthms == -1 ? 0 : instanceInfo.lengthms;
		if(length > 0)
		{
			percent = ((float)pos / (float)length) * 100.0;			
			if(percent > 100.0)
			{
				int numLoops;
				numLoops = (int)(percent / 100.0);
				percent = fmod(percent, 100); // keep it in range
			

				estrPrintf(estrOutput, "%.2f%% [%d %.3fs]", percent, numLoops, (float)pos / 1000.0);
			} else {
				estrPrintf(estrOutput, "%.2f%% [%.3fs]", percent, (float)pos/1000.0);
			}
		} else {
			estrPrintf(estrOutput, "%.3fs", (float)pos/1000.0);
		}
	}
}

SoundSource* aDebugSoundSourceForEvent(void *fmod_instance)
{
	FMOD_RESULT res;
	SoundSource *result = NULL;
	int systemId;

	res = FMOD_EventSystem_GetSystemID(fmod_instance, &systemId);
	if(!res)
	{
		SoundSourceGroup *group;

		if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, systemId, &group))
		{
			// check active sources for ptr
			int numActive = eaSize(&group->active_sources);
			int j;

			for(j = 0; j < numActive; j++)
			{
				SoundSource *source = group->active_sources[j];
				if(source->fmod_event == fmod_instance)
				{
					result = source;
					break;
				}
			}
		}
	}
	return result;
}

void aDebugEventInstanceDistance(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_instance)
	{
		SoundSource *source = aDebugSoundSourceForEvent(fmod_instance);
		if(source)
		{
			estrPrintf(estrOutput, "%.1f", source->distToListener);
		}
	}
}

void aDebugEventInstanceOrigin(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_instance)
	{
		SoundSource *source = aDebugSoundSourceForEvent(fmod_instance);
		if(source)
		{
			estrPrintf(estrOutput, "%s", source->obj.file_name);
		}
	}
}

void aDebugEventInstanceVolume(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_event_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_event_instance)
	{
		estrPrintf(estrOutput, "%.2f", fmodEventGetVolume(fmod_event_instance));
	}
}

void aDebugEventInstanceAudibility(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	void *fmod_event_instance = aDebugUI.fmodEvents.fmodEventInstances[iRow];
	if(fmod_event_instance)
	{
		F32 audibility;
		F32 audibility_db;
		FmodEventInfo instanceInfo;

		fmodGetFmodEventInfo(fmod_event_instance, &instanceInfo);

		audibility = instanceInfo.audibility;
		audibility_db = AmpToDb(audibility);

		estrPrintf(estrOutput, "%.2f (%.2f dB)", audibility, audibility_db);
	}
}

void aDebugAdvancedPlayToggle(UIAnyWidget *widget, UserData userData)
{
	UIButton *button = (UIButton*)widget;
	F32 left_col_width = 275.0;
	F32 padding = 5.0;

	if(!aDebugUI.fmodEvents.advancedDisplay)
	{
		// display advanced options
		ui_WidgetSetHeightEx(UI_WIDGET(aDebugUI.fmodEvents.playPane), 100, UIUnitFixed);
		
		ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 0, 0, 120, 150);

		ui_ButtonSetText(button, "-");
	} else {

		// hide advanced options
		ui_WidgetSetHeightEx(UI_WIDGET(aDebugUI.fmodEvents.playPane), 37, UIUnitFixed);

		ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 0, 0, 75, 150);

		ui_ButtonSetText(button, "+");
	}

	aDebugUI.fmodEvents.advancedDisplay = !aDebugUI.fmodEvents.advancedDisplay;
}

SoundSource* aDebugCreateSoundSourceAtPos(const char *path, Vec3 pos)
{
	SoundSource *source;

	source = sndSourceCreate(__FILE__, NULL, path, pos, ST_POINT, SO_REMOTE, NULL, -1, false);
	
	return source;
}

void aDebugGetCustomPos(Vec3 pos)
{
	const unsigned char *str;

	str = ui_TextEntryGetText(aDebugUI.fmodEvents.xPosPreview);
	pos[0] = atof(str);
	str = ui_TextEntryGetText(aDebugUI.fmodEvents.yPosPreview);
	pos[1] = atof(str);
	str = ui_TextEntryGetText(aDebugUI.fmodEvents.zPosPreview);
	pos[2] = atof(str);
}

int aDebugNumEventInstancesForEvent(void *fmod_event)
{
	static void **instances;
	int numInstances;

	aDebugGetFmodEventInstances(fmod_event, &instances);

	numInstances = eaSize(&instances);
	
	return numInstances;
}

void aDebugStopAllEventInstances(UIAnyWidget *widget, UserData userData)
{
	static void **events;
	int numEvents;
	int i;

	eaClear(&events);
	
	fmodEventSystemGetPlaying(&events);
	
	numEvents = eaSize(&events);
	
	for(i = 0; i < numEvents; i++)
	{
		void *fmod_event = events[i];

		aDebugStopEventInstances(fmod_event);
	}
}

void aDebugStopEventInstances(void *fmod_event)
{
	static void **instances;
	FMOD_RESULT result;
	int i;
	int id;
	int numInstances;

	aDebugGetFmodEventInstances(fmod_event, &instances);

	numInstances = eaSize(&instances);
	for(i = 0; i < numInstances; i++)
	{
		void *fmod_instance = instances[i];

		result = FMOD_EventSystem_GetSystemID(fmod_instance, &id);
		if(!result)
		{
			SoundSourceGroup *group;

			if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, id, &group))
			{
				// check active sources for ptr
				int numActive = eaSize(&group->active_sources);
				int j;

				if(numActive == 0)
				{
					FMOD_EventSystem_StopEvent(fmod_instance, false);
				} else { 
					for(j = 0; j < numActive; j++)
					{
						SoundSource *source = group->active_sources[j];
						if(source->fmod_event == fmod_instance)
						{
							source->needs_stop = 1;
							source->immediate = false;
						}
					}
				}
			} else {
				FMOD_EventSystem_StopEvent(fmod_instance, false);
			}
		} else {
			FMOD_EventSystem_StopEvent(fmod_instance, false);
		}
	}

	//numInstances = eaSize(&nodeData->previewSources);
	//for(i = 0; i < numInstances; i++)
	//{
	//	nodeData->previewSources[i]->needs_stop = 1;
	//}
	//eaClear(&nodeData->previewSources);
	
}

void aDebugPlayEvent(UIAnyWidget *widget, UserData userData)
{
	UIButton *button = (UIButton*)widget;
	Vec3 pos;
	ADebugFmodEventTreeData *nodeData = aDebugUI.fmodEvents.selectedTreeNode;
	
	if(!nodeData)  
	{
		return;
	}
	
	
	if(aDebugNumEventInstancesForEvent(nodeData->fmod_event) == 0) // no live events
	{
		if(nodeData->type == STT_EVENT)
		{
			char *str = NULL;
			U32 playMultiple;
			U32 usePosition;
			SoundSource *source;

			usePosition = ui_CheckButtonGetState(aDebugUI.fmodEvents.customPosition);
			if(!usePosition) {
				sndGetListenerPosition(pos);
			} else {
				aDebugGetCustomPos(pos);
			}
			
			fmodEventGetFullName(&str, nodeData->fmod_event, true);

			playMultiple = ui_CheckButtonGetState(aDebugUI.fmodEvents.playMultiple);

			if(playMultiple) 
			{	
				const unsigned char *numInstancesStr;
				U32 numInstances, i;
				//U32 maxPlaybacks;

				numInstancesStr = ui_TextEntryGetText(aDebugUI.fmodEvents.numInstances);
				numInstances = atoi(numInstancesStr);

				// override max-playbacks param if necessary
				// unsupported feature in f-mod
				//maxPlaybacks = fmodEventGetMaxPlaybacks(nodeData->fmod_event);
				//if(numInstances > maxPlaybacks)
				//{
				//	// prompt user for override?

				//	fmodEventSetMaxPlaybacks(nodeData->fmod_event, numInstances);
				//}
				
				for(i = 0; i < numInstances; i++)
				{
					source = aDebugCreateSoundSourceAtPos(str, pos);
					if(source) eaPush(&nodeData->previewSources, source);
				}
			} 
			else
			{
				
				source = aDebugCreateSoundSourceAtPos(str, pos);
				if(source) eaPush(&nodeData->previewSources, source);
			}
			
			if(eaSize(&nodeData->previewSources) > 0)
			{
				ui_ButtonSetText(button, "Stop");
			}
			estrDestroy(&str);
		}
	} else {
		aDebugStopEventInstances(nodeData->fmod_event);
		eaClear(&nodeData->previewSources);

		ui_ButtonSetText(button, "Play");
	}
}

void aDebugUpdatePreviewEventVolumeInDb(F32 dbVal)
{
	// convert from dB to 0-1
	ADebugFmodEventTreeData *nodeData = aDebugUI.fmodEvents.selectedTreeNode;
	if(nodeData)
	{
		static void **instances;
		int i;
		int numInstances;

		F32 normalizedValue = DbToAmp(dbVal);
		aDebugGetFmodEventInstances(nodeData->fmod_event, &instances);

		numInstances = eaSize(&instances);
		for(i = 0; i < numInstances; i++)
		{
			void *fmod_instance = instances[i];
			fmodEventSetVolumeProperty(fmod_instance, normalizedValue);
		}
	}
}

void aDebugVolumeSliderChanged(UIAnyWidget *widget, bool bFinished, UserData userData)
{
	UISlider *volumeSlider = (UISlider *)widget;
	F32 value;
	char valStr[16];

	value = ui_FloatSliderGetValue(volumeSlider);
	sprintf(valStr, "%.1f", value);

	aDebugUpdatePreviewEventVolumeInDb(value);

	ui_TextEntrySetText(aDebugUI.fmodEvents.volumeTextEntry, valStr);
}

void aDebugGrabPosition(UIAnyWidget *widget, UserData userData)
{
	char txt[16];
	Vec3 pos;

	sndGetListenerPosition(pos);

	sprintf(txt, "%.1f", pos[0]);
	ui_TextEntrySetText(aDebugUI.fmodEvents.xPosPreview, txt);
	sprintf(txt, "%.1f", pos[1]);
	ui_TextEntrySetText(aDebugUI.fmodEvents.yPosPreview, txt);
	sprintf(txt, "%.1f", pos[2]);
	ui_TextEntrySetText(aDebugUI.fmodEvents.zPosPreview, txt);
}

void aDebugVolumeEntryChanged(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *volTextEntry = (UITextEntry*)widget;
	const unsigned char *input;
	F32 value;

	input = ui_TextEntryGetText(volTextEntry);

	value = atof(input);

	CLAMP(value, -60.0, 0.0);

	aDebugUpdatePreviewEventVolumeInDb(value);

	ui_FloatSliderSetValue(aDebugUI.fmodEvents.volumeSlider, value);
}



///////////////////////////////////////////////////////////////////////////////
// Main Debugger Window
///////////////////////////////////////////////////////////////////////////////

// The window is closing
bool aDebugUICloseCallback(UIAnyWidget *widget, UserData unused)
{
	GamePrefStoreFloat("ADebug.x", ui_WidgetGetX((UIWidget*)widget));
	GamePrefStoreFloat("ADebug.y", ui_WidgetGetY((UIWidget*)widget));
	GamePrefStoreFloat("ADebug.w", ui_WidgetGetWidth((UIWidget*)widget));
	GamePrefStoreFloat("ADebug.h", ui_WidgetGetHeight((UIWidget*)widget));

	if(aDebugUI.mainWindow)
	{
		F32 scaled = aDebugUI.mainSkin->background[0].a / 255.0;

		GamePrefStoreFloat("ADebug.windowOpacity", scaled);
	}

	if(aDebugUI.mainTabs)
		GamePrefStoreInt("ADebug.tab", ui_TabGroupGetActiveIndex(aDebugUI.mainTabs));

	ZeroStruct(&aDebugUI);

	return 1;
}

// Main Tab has been changed
void aDebugTabChanged(UIAnyWidget *widget, UserData unused)
{
}

// Setup the controls for the performance tab
void aDebugSetupPerformanceTab(UITab *tab)
{
	F32 x, y;
	F32 col_1_x, col_2_x;
	F32 y_spacing;
	//UILabel *label;

	aDebugUI.performance.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.performance.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	col_1_x = 5.0;
	col_2_x = 155.0;
	y_spacing = 2;
	y = 5.0;

	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_ButtonCreate("Reload", col_1_x, y, aDebugReloadEvents, NULL));
	
	y += 30.0;

	// Memory
	aDebugUI.performance.memoryLabel = ui_LabelCreate("Memory", col_1_x, y);
	aDebugUI.performance.memoryProgressBar = ui_ProgressBarCreate(col_2_x, y, 100);
	x = ui_WidgetGetNextX(UI_WIDGET(aDebugUI.performance.memoryProgressBar)) + 5;
	aDebugUI.performance.memoryInfoLabel = ui_LabelCreate("0k of 0k (0k)", x, y);

	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.memoryLabel)) + y_spacing;

	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.memoryLabel));
	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.memoryProgressBar));
	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.memoryInfoLabel));

	// CPU
	aDebugUI.performance.cpuLabel = ui_LabelCreate("CPU", col_1_x, y);
	aDebugUI.performance.cpuProgressBar = ui_ProgressBarCreate(col_2_x, y, 100);
	x = ui_WidgetGetNextX(UI_WIDGET(aDebugUI.performance.cpuProgressBar)) + 5;
	aDebugUI.performance.cpuInfoLabel = ui_LabelCreate("0.00% (0.00%)", x, y);
	aDebugUI.performance.cpuMax = 0.0;

	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.cpuLabel));
	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.cpuProgressBar));
	ui_PaneAddChild(aDebugUI.performance.rootPane, UI_WIDGET(aDebugUI.performance.cpuInfoLabel));

	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.cpuLabel)) + y_spacing;

	// DSP Graph
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("DSP Graph Mem", col_1_x, y));
	aDebugUI.performance.dspGraphMemTotal = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.dspGraphMemTotal);

	
	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.dspGraphMemTotal)) + y_spacing;

	// DSP Enabled
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("DSP Enabled", col_1_x, y));
	aDebugUI.performance.dspEnabled = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.dspEnabled);

	
	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.dspEnabled)) + y_spacing;

	// Events Playing
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("Events Playing", col_1_x, y));
	aDebugUI.performance.numEventsPlaying = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.numEventsPlaying);

	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.numEventsPlaying)) + y_spacing;
	
	// Total Num Sound Sources (active, inactive, etc)
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("Num Sources", col_1_x, y));
	aDebugUI.performance.numSources = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.numSources);

	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.numSources)) + y_spacing;

	// Time of Day
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("Time of Day", col_1_x, y));
	aDebugUI.performance.timeOfDay = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.timeOfDay);

	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.timeOfDay)) + y_spacing;

	// Listener Attributes
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("Listener Attributes", col_1_x, y));
	aDebugUI.performance.listenerAttributes = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.listenerAttributes);


	y = ui_WidgetGetNextY(UI_WIDGET(aDebugUI.performance.listenerAttributes)) + y_spacing;

	// In Combat?
	ui_PaneAddChild(aDebugUI.performance.rootPane, ui_LabelCreate("Player In Combat", col_1_x, y));
	aDebugUI.performance.incombatLabel = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.performance.rootPane, aDebugUI.performance.incombatLabel);

	
	ui_TabAddChild(tab, aDebugUI.performance.rootPane);
}

void aDebugInitFmodTreeData()
{
	if(aDebugUI.fmodEvents.completeTreeRoot)
	{
		// release the old tree
		aDebugRecursivelyRemoveChildren(&aDebugUI.fmodEvents.completeTreeRoot);
	}

	// init the tree data
	aDebugUI.fmodEvents.completeTreeRoot = calloc(1,sizeof(ADebugFmodEventTreeData));
	aDebugUI.fmodEvents.completeTreeRoot->name = strdup("Projects");

	if(!aDebugTreeStash)
	{
		aDebugTreeStash = stashTableCreateAddress(1000); // hold the events
	} 
	else 
	{
		stashTableClear(aDebugTreeStash); // make sure we clear this out
	}

	aDebugUI.fmodEvents.eventsMemory = 0; // reset this (the traversal will accumulate)
	FMOD_EventSystem_TreeTraverse(STT_PROJECT, aDebugBuildEventTree, aDebugUI.fmodEvents.completeTreeRoot, NULL);

	aDebugFilterFmodEvents("");
}

void aDebugReloadEvents(UIAnyWidget *widget, UserData userData)
{
	aDebugUI.fmodEvents.selectedTreeNode = NULL;
	sndReloadAll(NULL);
	aDebugInitFmodTreeData();
}

static void aDebugCopyTreeToClipboardCB(UIAnyWidget *widget, UserData userData) 
{
	char *estr = NULL;

	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(aDebugUI.fmodEvents.eventsTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;

		estrConcatf(&estr, "ADebug Selected Results : %d\n", numSelected);

		if(aDebugUI.fmodEvents.eventFilter.name && strlen(aDebugUI.fmodEvents.eventFilter.name))
		{
			estrConcatf(&estr, " filter by name : %s\n", aDebugUI.fmodEvents.eventFilter.name);	
		}
		if(aDebugUI.fmodEvents.eventFilter.isPlaying)
		{
			estrConcatf(&estr, " only if playing\n");	
		}
		estrConcatf(&estr, "\n========================\n\n");

		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				ADebugFmodEventTreeData *pTreeNodeData = (ADebugFmodEventTreeData*)pTreeNode->contents;
				if(pTreeNodeData)
				{
					ADebugFmodEventTreeData *pParentNode = pTreeNodeData->parent;
					while(pParentNode)
					{
						estrConcatf(&estr, "\t");
						pParentNode = pParentNode->parent;
					}

					estrConcatf(&estr, "%s\n", pTreeNodeData->name);
				}
			}
		}
		winCopyToClipboard(estr);

		estrDestroy(&estr);
	}
}

static void aDebugCopyValidPathsToClipboardCB(UIAnyWidget *widget, UserData userData) 
{
	char *estr = NULL;
	char *eventPath = NULL;

	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(aDebugUI.fmodEvents.eventsTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;

		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				ADebugFmodEventTreeData *pTreeNodeData = (ADebugFmodEventTreeData*)pTreeNode->contents;
				if(pTreeNodeData && pTreeNodeData->fmod_event)
				{
					estrClear(&eventPath);

					fmodEventGetFullName(&eventPath, pTreeNodeData->fmod_event, false);
					if(estrLength(&eventPath) > 0)
					{
						estrAppend(&estr, &eventPath);
						estrConcatf(&estr, "\n");
					}
				}
			}
		}
		winCopyToClipboard(estr);

		estrDestroy(&estr);
		estrDestroy(&eventPath);
	}
}

static void aDebugSelectAllCB(UIAnyWidget *tree, UserData pUserData)
{
	ui_TreeSelectAll(aDebugUI.fmodEvents.eventsTree);
}

static void aDebugShowAllChildren(ADebugFmodEventTreeData *pTreeNodeData)
{
	if(pTreeNodeData)
	{
		pTreeNodeData->isVisible = 1;

		FOR_EACH_IN_EARRAY(pTreeNodeData->children, ADebugFmodEventTreeData, pChildNode)
		{
			aDebugShowAllChildren(pChildNode);
		}
		FOR_EACH_END;
	}
}

static bool aDebugSelectNodeCondition(UITreeNode *pTreeNode, void *pUserData)
{
	bool result = false;
	ADebugFmodEventTreeData **ppPrevSelected = (ADebugFmodEventTreeData**)pUserData;
	FOR_EACH_IN_EARRAY(ppPrevSelected, ADebugFmodEventTreeData, pDfxTreeNodeData)
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

static void aDebugShowAllChildrenCB(UIAnyWidget *tree, UserData pUserData)
{
	ADebugFmodEventTreeData **ppPrevSelected = NULL;

	const UITreeNode * const * const *selectedNodes = ui_TreeGetSelectedNodes(aDebugUI.fmodEvents.eventsTree);
	if(selectedNodes)
	{
		int numSelected = eaSize(selectedNodes);
		int i;
		for(i = 0; i < numSelected; i++)
		{
			const UITreeNode* pTreeNode = (*selectedNodes)[i];
			if(pTreeNode)
			{
				ADebugFmodEventTreeData *pTreeNodeData = (ADebugFmodEventTreeData*)pTreeNode->contents;
				aDebugShowAllChildren(pTreeNodeData);
				aDebugExpandAllNodes((UITreeNode*)pTreeNode);
				eaPush(&ppPrevSelected, pTreeNodeData);
			}
		}
	}

	ui_TreeRefresh(aDebugUI.fmodEvents.eventsTree);
	//ui_TreeRefresh(fxTracker.pAssetTree);

	ui_TreeSelectFromBranchWithCondition(aDebugUI.fmodEvents.eventsTree, &aDebugUI.fmodEvents.eventsTree->root, aDebugSelectNodeCondition, ppPrevSelected);

	eaDestroy(&ppPrevSelected);
}

static void aDebugFmodEventsContextCB(UIAnyWidget *tree, UserData pUserData)
{
	if (!aDebugUI.fmodEvents.contextMenu)
		aDebugUI.fmodEvents.contextMenu = ui_MenuCreate("");

	eaDestroyEx(&aDebugUI.fmodEvents.contextMenu->items, ui_MenuItemFree);

	ui_MenuAppendItems(aDebugUI.fmodEvents.contextMenu, NULL);

	ui_MenuAppendItems(aDebugUI.fmodEvents.contextMenu,
		ui_MenuItemCreate("Select All (Ctrl+A)", UIMenuCallback, aDebugSelectAllCB, NULL, NULL),
		ui_MenuItemCreate("Copy Tree To Clipboard", UIMenuCallback, aDebugCopyTreeToClipboardCB, NULL, NULL),
		ui_MenuItemCreate("Copy Valid Paths To Clipboard", UIMenuCallback, aDebugCopyValidPathsToClipboardCB, NULL, NULL),
		ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
		ui_MenuItemCreate("Show All Children", UIMenuCallback, aDebugShowAllChildrenCB, NULL, NULL),
		NULL);

	ui_MenuPopupAtCursor(aDebugUI.fmodEvents.contextMenu);
}

// Setup the controls for the FMod Events Tab
void aDebugSetupFmodEventsTab(UITab *tab)
{
	F32 left_col_width = 275.0;
	F32 padding = 5.0;
	F32 filter_label_width = 45.0;
	F32 event_name_height = 25.0;
	F32 x, y;
	UILabel *label;
	UIListColumn *col;
	static void **fakeVarEArray = NULL;

	aDebugUI.fmodEvents.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.fmodEvents.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	// Filter By Name
	x = 5;
	y = 5;
	label = ui_LabelCreate("Filter", x, y);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, label);

	// Search Entry
	aDebugUI.fmodEvents.searchEntry = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.searchEntry), left_col_width - filter_label_width - x, UIUnitFixed);
	ui_TextEntrySetChangedCallback(aDebugUI.fmodEvents.searchEntry, aDebugFmodEventsTextSearch, aDebugUI.fmodEvents.searchEntry);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.searchEntry), 0.4, UIUnitPercentage);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, aDebugUI.fmodEvents.searchEntry);


	aDebugUI.fmodEvents.isPlayingCheckButton = ui_CheckButtonCreate(x, y + 25, "Is Playing", false);
	ui_CheckButtonSetToggledCallback(aDebugUI.fmodEvents.isPlayingCheckButton, aDebugFmodEventsIsPlayingToggle, NULL);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.isPlayingCheckButton));

	// Events Tree
	aDebugUI.fmodEvents.eventsTree = ui_TreeCreate(0.0, 0.0, left_col_width, 1.0);
	ui_TreeSetMultiselect(aDebugUI.fmodEvents.eventsTree, true);

	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.fmodEvents.eventsTree), 0, 0, 60, 105+25);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.fmodEvents.eventsTree), 0.4, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TreeSetSelectedCallback(aDebugUI.fmodEvents.eventsTree, aDebugFmodEventSelected, NULL);
	ui_TreeSetContextCallback(aDebugUI.fmodEvents.eventsTree, aDebugFmodEventsContextCB, NULL);

	aDebugUI.fmodEvents.expandAll = ui_CheckButtonCreate(0, 0, "Expand All", false);
	ui_CheckButtonSetToggledCallback(aDebugUI.fmodEvents.expandAll, aDebugExpandAllEventsTree, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.expandAll), 0, 78+25, 0, 0, UIBottomLeft);
	ui_CheckButtonSetState(aDebugUI.fmodEvents.expandAll, true);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.expandAll));

	
	aDebugUI.fmodEvents.stopAllButton = ui_ButtonCreate("Stop All", 0, 0, aDebugStopAllEventInstances, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.stopAllButton), 50, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.stopAllButton));

	aDebugUI.fmodEvents.eventsInfo = ui_LabelCreate("", 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.eventsInfo), 0, 50+25, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.eventsInfo));

	aDebugUI.fmodEvents.eventsMemoryLabel = ui_LabelCreate("", 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.eventsMemoryLabel), 0, 25+25, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.eventsMemoryLabel));

	aDebugUI.fmodEvents.systemMemoryLabel = ui_LabelCreate("", 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.systemMemoryLabel), 0, 25, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.systemMemoryLabel));


	aDebugUI.fmodEvents.reloadButton = ui_ButtonCreate("reload", 0, 0, aDebugReloadEvents, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.reloadButton), 0, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.reloadButton));


	//ui_TreeNodeSetFillCallback(&aDebugUI.fmodEvents.eventsTree->root, aDebugFillFmodEventsTree, aDebugUI.fmodEvents.filteredTreeRoot);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.eventsTree));
 
	aDebugInitFmodTreeData();


	//x = left_col_width + padding;
	y = 5;
	x = 0;

	// Selected Event Name
	aDebugUI.fmodEvents.selectedEventLabel = ui_LabelCreate("Selected Event Name", 0, y);
	//ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.selectedEventLabel), left_col_width, 0.0, 0.0, 0.0
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.fmodEvents.selectedEventLabel), 0.59, 25, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.selectedEventLabel), 0, 0, 0, 0, UITopRight);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.selectedEventLabel));

	// copy button
	aDebugUI.fmodEvents.copyEventPathButton = ui_ButtonCreate("copy", 0, 0, aDebugCopyEventNameToClipboard, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.copyEventPathButton), 5, y, 0, 0, UITopRight);
	//ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.copyEventPathButton), 0.59, UIUnitPercentage);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.copyEventPathButton));
		

	y = 5 + 25;
	x = 0;

	//
	// Create the Playback Options Pane
	//
	aDebugUI.fmodEvents.playPane = ui_PaneCreate(0, y, 0.59, 37, UIUnitPercentage, UIUnitFixed, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.playPane), 0, y, 0, 0, UITopRight);
	aDebugUI.fmodEvents.playPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.playPane));

	y = 5;

	// General Play & Volume Controls
	aDebugUI.fmodEvents.advancedPlayToggle = ui_ButtonCreate("+", x, y, aDebugAdvancedPlayToggle, NULL);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.advancedPlayToggle));				

	aDebugUI.fmodEvents.playButton = ui_ButtonCreate("Play", x+50, y, aDebugPlayEvent, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.playButton), 40, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.playButton));				

	// 350, 455, 525
	aDebugUI.fmodEvents.volumeSlider = ui_FloatSliderCreate(x + 100, y, 100, 0, 1, 1);
	ui_SliderSetRange(aDebugUI.fmodEvents.volumeSlider, -60.0, 0.0, 0.1);
	ui_SliderSetChangedCallback(aDebugUI.fmodEvents.volumeSlider, aDebugVolumeSliderChanged, NULL);
	ui_SliderSetPolicy(aDebugUI.fmodEvents.volumeSlider, UISliderContinuous);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.volumeSlider));				

	aDebugUI.fmodEvents.volumeTextEntry = ui_TextEntryCreate("", x + 205, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.volumeTextEntry), 60, UIUnitFixed);
	ui_TextEntrySetEnterCallback(aDebugUI.fmodEvents.volumeTextEntry, aDebugVolumeEntryChanged, NULL);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.volumeTextEntry));				

	aDebugUI.fmodEvents.volumeDb = ui_LabelCreate("dB", x + 278, y);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.volumeDb));


	// Position Control

	y += 30.0; // next line

	aDebugUI.fmodEvents.customPosition = ui_CheckButtonCreate(x, y, "Position", 0);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.customPosition));

	aDebugUI.fmodEvents.xLabel = ui_LabelCreate("X", x + 120, y);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.xLabel));				

	aDebugUI.fmodEvents.xPosPreview = ui_TextEntryCreate("", x + 135, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.xPosPreview), 50, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.xPosPreview));				

	aDebugUI.fmodEvents.yLabel = ui_LabelCreate("Y", x + 195, y);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.yLabel));

	aDebugUI.fmodEvents.yPosPreview = ui_TextEntryCreate("", x + 210, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.yPosPreview), 50, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.yPosPreview));				

	aDebugUI.fmodEvents.yLabel = ui_LabelCreate("Z", x + 270, y);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.yLabel));

	aDebugUI.fmodEvents.zPosPreview = ui_TextEntryCreate("", x + 285, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.zPosPreview), 50, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.zPosPreview));				

	aDebugUI.fmodEvents.grabPosition = ui_ButtonCreate("grab", x + 345, y, aDebugGrabPosition, NULL);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.grabPosition));				

	// Multiple Instances

	y += 30.0; // next line

	aDebugUI.fmodEvents.playMultiple = ui_CheckButtonCreate(x, y, "Multiple Instances", 0);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.playMultiple));

	aDebugUI.fmodEvents.numInstances = ui_TextEntryCreate("4", x + 150, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.numInstances), 50, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.numInstances));				

	//aDebugUI.fmodEvents.radius = ui_TextEntryCreate("2", x + 250, y);
	//ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.radius), 50, UIUnitFixed);
	//ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.radius));				

	//aDebugUI.fmodEvents.timeOffset = ui_TextEntryCreate("0", x + 350, y);
	//ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.fmodEvents.timeOffset), 50, UIUnitFixed);
	//ui_PaneAddChild(aDebugUI.fmodEvents.playPane, UI_WIDGET(aDebugUI.fmodEvents.timeOffset));				

	//UIComboBox *multipleMode;
 


	

	// Selected Fmod Event Properties Table
	if(!fakeVarEArray) 
	{
		int i;

		eaSetSize(&fakeVarEArray, ADEBUG_END_OF_LIST);
		for(i=0; eaSize(&fakeVarEArray) < ADEBUG_END_OF_LIST; i++)
		{
			eaPush(&fakeVarEArray, NULL);
		}
	}

	aDebugUI.fmodEvents.fmodParamsList = ui_ListCreate(NULL, &fakeVarEArray, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 1);
		
	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugFmodPropertyName, NULL);
	ui_ListAppendColumn(aDebugUI.fmodEvents.fmodParamsList, col);
	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugFmodPropertyValue, NULL);
	ui_ListAppendColumn(aDebugUI.fmodEvents.fmodParamsList, col);

	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 0, 0, 75, 150);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 0.59, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 0, 0, 0, 0, UITopRight);
	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList));

 
	// Event Instances
	aDebugUI.fmodEvents.eventInstancesList = ui_ListCreate(NULL, &aDebugUI.fmodEvents.fmodEventInstances, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	//ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.fmodEvents.fmodParamsList), 1);
	ui_ListSetCellActivatedCallback(aDebugUI.fmodEvents.eventInstancesList, aDebugActivateEventInstance, NULL);

	col = ui_ListColumnCreate(UIListTextCallback, "Mem", (intptr_t)aDebugEventInstanceIndex, NULL);
	ui_ListColumnSetWidth(col, false, 100);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Volume", (intptr_t)aDebugEventInstanceVolume, NULL);
	ui_ListColumnSetWidth(col, false, 75);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Audibility", (intptr_t)aDebugEventInstanceAudibility, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Playback", (intptr_t)aDebugEventInstancePosition, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Distance", (intptr_t)aDebugEventInstanceDistance, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Origin", (intptr_t)aDebugEventInstanceOrigin, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.fmodEvents.eventInstancesList, col);

	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.fmodEvents.eventInstancesList), 0, 0, 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.fmodEvents.eventInstancesList), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.fmodEvents.eventInstancesList), 0.59, 150.0, UIUnitPercentage, UIUnitFixed);

	ui_PaneAddChild(aDebugUI.fmodEvents.rootPane, UI_WIDGET(aDebugUI.fmodEvents.eventInstancesList));



	ui_TabAddChild(tab, aDebugUI.fmodEvents.rootPane);
}





///////////////////////////////////////////////////////////////////////////////
// Spaces
///////////////////////////////////////////////////////////////////////////////

void aDebugSpaceTreeDisplayText(UITreeNode *node, SpaceIconColor colorIndex, const char *text1, const char *text2, UI_MY_ARGS, F32 z)
{
	static Color audibleColor = { 0, 128, 0, 128 };
	static Color inaudibleColor = { 64, 64, 64, 128 };
	Color color;

	int indent;
	AtlasTex *opened;
	F32 indentOffset;
	F32 icon_x1, icon_x2, icon_y1, icon_y2;
	int count_x;
	int column_1_x;

	UITree* tree = node->tree;
	CBox total_box = {x, y, x + w, y + h};

	UIStyleFont *font = GET_REF(UI_GET_SKIN(tree)->hNormal);
	CBox col_0_box = total_box;

	indent = ui_TreeNodeGetIndent(node);
	opened = (g_ui_Tex.minus);
	indentOffset = (UI_STEP + opened->width) * scale;

	column_1_x = 240;

	count_x = x + (column_1_x - indent*indentOffset);
	col_0_box.right = x + (column_1_x - 5);

	ui_StyleFontUse(font, node == tree->selected || (tree->multiselect && eaFind(&tree->multiselected, node)), UI_WIDGET(tree)->state);
	clipperPushRestrict(&col_0_box);

	// draw the 'icon'
	icon_x1 = x;
	icon_x2 = x+10;
	icon_y1 = y + (h / 2.0) - 5;
	icon_y2 = y + (h / 2.0) + 5;

	switch(colorIndex) {
		default:		
		case SPACEICONCOLOR_INAUDIBLE: color = inaudibleColor; break;
		case SPACEICONCOLOR_AUDIBLE: color = audibleColor; break;		
	}
	gfxDrawQuad(icon_x1, icon_y1, icon_x2, icon_y2, z, color);


	gfxfont_Printf(x+15, y + h/2, z, scale, scale, CENTER_Y, "%s", text1);
	clipperPop();

	if(text2 && text2[0]) {
		clipperPushRestrict(&total_box);
		gfxfont_Printf(count_x, y + h/2, z, scale, scale, CENTER_Y, "%s", text2);
		clipperPop();
	}
}


void aDebugDrawSpacesTreeNode(UITreeNode *node, const char *field, UI_MY_ARGS, F32 z)
{
	ADebugSpaceTreeData *data = (ADebugSpaceTreeData*)node->contents;
	char textStr[128];
	char countStr[9];
	SpaceIconColor colorIndex;

	if( eaFind(&space_state.global_spaces, data->space) != -1 ) {
		colorIndex = data->space->is_audible ? SPACEICONCOLOR_AUDIBLE : SPACEICONCOLOR_INAUDIBLE;
		sprintf(textStr, "%s", data->name);
		sprintf(countStr, "%d", eaSize(&data->space->localSources));

		aDebugSpaceTreeDisplayText(node, colorIndex, textStr, countStr, UI_MY_VALUES, z);
	}
}

void aDebugFillSpacesChild(UITreeNode *node, UserData fillData)
{
	ADebugSpaceTreeData *parent = (ADebugSpaceTreeData*)fillData;
	int i;

	for ( i = 0; i < eaSize(&parent->children); ++i )
	{
		ADebugSpaceTreeData *data = parent->children[i];
		if(data->isVisible)
		{
			UITreeNode *newNode = ui_TreeNodeCreate(
				node->tree, cryptAdler32String(data->name), parse_ADebugSpaceTreeData, data,
				aDebugFillSpacesChild, parent->children[i],
				aDebugDrawSpacesTreeNode, "Spaces Tree", 20);
			ui_TreeNodeAddChild(node, newNode);
		}
	}
}

void aDebugFillSpacesTree(UITreeNode *node, UserData fillData)
{
	ADebugSpaceTreeData *data = (ADebugSpaceTreeData*)fillData;
	if(data)
	{
		if(data->isVisible)
		{
			UITreeNode *newNode = ui_TreeNodeCreate(
				node->tree, cryptAdler32String(data->name), parse_ADebugSpaceTreeData, fillData,
				aDebugFillSpacesChild, fillData, aDebugDrawSpacesTreeNode, "Spaces Tree", 20);
			ui_TreeNodeAddChild(node, newNode);
		}
	}
}

void aDebugBuildSpaceTree(SoundSpace *space, ADebugSpaceTreeData **node, ADebugSpaceTreeData *parent, StashTable traversedConns, void *filter, void *userData)
{
	SoundSpaceConnector *connector;
	ADebugSpaceTreeData *newNode;
	int i;
	int numConnectors;

	newNode = calloc(1, sizeof(ADebugSpaceTreeData));
	newNode->name = strdup(space->obj.desc_name);
	newNode->parent = parent;
	newNode->space = space;
	newNode->isVisible = 1;

	numConnectors = eaSize(&space->connectors);
	for(i = 0; i < numConnectors; i++)
	{
		ADebugSpaceTreeData *childNode;
		connector = space->connectors[i];
		
		if(!stashAddressFindInt(traversedConns, connector, NULL))
		{
			//eaPush(&toTraverse, c);
			stashAddressAddInt(traversedConns, connector, 1, 1);
			
			if(connector->space1 == space && connector->space2 == space)
			{
				// uh oh...
			}
			else if(connector->space1 && connector->space1 != space)
			{
				// follow this
				aDebugBuildSpaceTree(connector->space1, &childNode, newNode, traversedConns, filter, userData);
				eaPush(&newNode->children, childNode);
			} 
			else if(connector->space2 && connector->space2 != space)
			{
				// follow this
				aDebugBuildSpaceTree(connector->space2, &childNode, newNode, traversedConns, filter, userData);
				eaPush(&newNode->children, childNode);
			}
		}
		
	}

	*node = newNode;
}

void aDebugReleaseTreeNodes(ADebugSpaceTreeData *node)
{
	if(node)
	{
		int numChildren;
		int i;
		numChildren = eaSize(&node->children);
		for(i = 0; i < numChildren; i++)
		{
			aDebugReleaseTreeNodes(node->children[i]);
		}
		
		free(node->name);
		free(node);
	}
}

void aDebugUpdateSpaceTree()
{
	SoundSpace *space = aDebugUpdateCurrentSpace();

	//aDebugSpaceUpdateReverbBypass();


	//if(space != aDebugUI.spaces.currentDisplaySpace)
	//{
	//	if(space)
	//	{
	//		static StashTable traversedConns = NULL;

	//		if(!traversedConns)
	//		{
	//			traversedConns = stashTableCreateAddress(20);
	//		}
	//		
	//		//aDebugUI.spaces.selectedSpaceNode = NULL; // make sure to clear this before releasing nodes
	//		aDebugUI.spaces.selectedSpace = NULL;
	//		aDebugUI.spaces.selectedLocalSources = NULL;
	//		aDebugUI.spaces.selectedOwnedSources = NULL;
	//		aDebugUI.spaces.selectedConnections = NULL;
	//		ui_LabelSetText(aDebugUI.spaces.selectedName, "");

	//		aDebugSpaceUpdateReverbBypass();

	//		aDebugReleaseTreeNodes(aDebugUI.spaces.spaceTreeRoot);
	//		aDebugUI.spaces.spaceTreeRoot = NULL;
	//		
	//		stashTableClear(traversedConns);
	//		aDebugBuildSpaceTree(space, &aDebugUI.spaces.spaceTreeRoot, NULL, traversedConns, NULL, NULL);

	//		ui_TreeNodeSetFillCallback(&aDebugUI.spaces.spacesTree->root, aDebugFillSpacesTree, aDebugUI.spaces.spaceTreeRoot);

	//		ui_TreeRefresh(aDebugUI.spaces.spacesTree);

	//		aDebugUpdateExpandAllSpacesTree(); // re-expand all (if enabled)

	//		aDebugUI.spaces.currentDisplaySpace = space;
	//	}
	//}
}

SoundSpace *aDebugUpdateCurrentSpace()
{
	SoundSpace *currentSpace = NULL;
	if(gSndMixer)
	{
		if(gSndMixer->currentSpace)
		{
			char txt[384];
			currentSpace = gSndMixer->currentSpace;
			if(eaFind(&space_state.global_spaces, currentSpace) >= 0)
			{
				SoundDSP *soundDSP;
				static SoundSpace *lastSpace = NULL;
				char *emptyName = " ";
				char *dspName = emptyName;
				char *roomToneName = emptyName;
				char *musicName = emptyName;

				soundDSP = GET_REF(currentSpace->dsp_ref);
				if(soundDSP)
				{
					dspName = (char*)soundDSP->name;
				}

				if(eaSize(&currentSpace->ownedSources) > 0) {
					roomToneName = (char*)currentSpace->ownedSources[0]->obj.desc_name;
				}

				if(currentSpace->music_name && currentSpace->music_name[0])
				{
					musicName = (char*)currentSpace->music_name;
				}
			
				sprintf(txt, "Current Space: %s  DSP: %s  Room Tone: %s  Music: %s", currentSpace->obj.desc_name, dspName, roomToneName, musicName);

				ui_LabelSetText(aDebugUI.spaces.currentSpaceName, txt);

				if(lastSpace != currentSpace)
				{
					gfxStatusPrintf("Current Space : %s", currentSpace->obj.desc_name);
				}

				lastSpace = currentSpace;
			}
			else 
			{
				currentSpace = NULL;
			}

			
		}
	}
	return currentSpace;
}

void aDebugUpdateSpaceSelection(SoundSpace *soundSpace)
{
	char txt[64];

	if(soundSpace)
	{
		SoundDSP *soundDSP = GET_REF(soundSpace->dsp_ref);

		aDebugUI.spaces.selectedLocalSources = soundSpace->localSources;
		aDebugUI.spaces.selectedOwnedSources = soundSpace->ownedSources;
		aDebugUI.spaces.selectedConnections = soundSpace->connectors;

		if(soundSpace->obj.desc_name)
		{
			sprintf(txt, "%s", soundSpace->obj.desc_name);
		}
		else
		{
			sprintf(txt, "(%p)", soundSpace);
		}
		ui_LabelSetText(aDebugUI.spaces.selectedName, txt);

		if(soundDSP)
		{
			ui_LabelSetText(aDebugUI.spaces.reverbName, soundDSP->name);
		}
		else
		{
			ui_LabelSetText(aDebugUI.spaces.reverbName, "");
		}
		

	}
	else
	{
		aDebugUI.spaces.selectedLocalSources = NULL;
		aDebugUI.spaces.selectedOwnedSources = NULL;
		aDebugUI.spaces.selectedConnections = NULL;

		ui_LabelSetText(aDebugUI.spaces.selectedName, "");
	}

}

void aDebugSpaceListSelected(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	SoundSpace **spaces = (SoundSpace**)(*pList->peaModel);

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	if( spaces && iRow < eaSize(&spaces) )
	{
		SoundSpace *soundSpace = spaces[iRow];
	
		aDebugUpdateSpaceSelection(soundSpace);
		
		aDebugUI.spaces.selectedSpace = soundSpace;
		//aDebugSpaceUpdateReverbBypass();
	}
}

void aDebugSpaceSelected(UIAnyWidget *widget, UserData userData)
{
	UITree* tree = (UITree*)widget;
	UITreeNode* uiNode = tree->selected;
	if(uiNode)
	{ 
		ADebugSpaceTreeData *node_data = (ADebugSpaceTreeData*)uiNode->contents;
		//aDebugUI.spaces.selectedSpaceNode = node_data;
		aDebugUI.spaces.selectedSpace = node_data->space;

		aDebugUpdateSpaceSelection(node_data->space);

		

		//aDebugUI.spaces.selectedLocalSources = node_data->space->localSources;
		//aDebugUI.spaces.selectedOwnedSources = node_data->space->ownedSources;
		//aDebugUI.spaces.selectedConnections = node_data->space->connectors;

		//sprintf_s(txt, 64, "%s (%p)", node_data->name, node_data->space);

		//ui_LabelSetText(aDebugUI.spaces.selectedName, txt);

	}
}

void aDebugUpdateExpandAllSpacesTree()
{
	U32 state;

	state = ui_CheckButtonGetState(aDebugUI.spaces.expandAll);
	if(state)
	{
		UITreeNode *rootTreeNode = &aDebugUI.spaces.spacesTree->root;
		aDebugExpandAllNodes(rootTreeNode);
	}
}

void aDebugExpandAllSpacesTree(UIAnyWidget *widget, UserData userData) 
{
	aDebugUpdateExpandAllSpacesTree();
}



void aDebugActivateLocalSources(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{ 
	bool okToRead;

	okToRead = eaFind(&space_state.global_spaces, aDebugUI.spaces.selectedSpace) >= 0;
	if(!okToRead)
	{
		okToRead = eaFind(&space_state.non_exclusive_spaces, aDebugUI.spaces.selectedSpace) >= 0;
	}

	if(okToRead && aDebugUI.spaces.selectedSpace && aDebugUI.spaces.selectedSpace->localSources)
	{
		if( iRow < eaSize(&aDebugUI.spaces.selectedSpace->localSources) )
		{
			SoundSource *source = aDebugUI.spaces.selectedSpace->localSources[iRow];
			
			aDebugMoveCameraToPosition(source->virtual_pos);
		}
	}
}


void aDebugSpaceLocalSources(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	static bool okToRead = false;

	if(iRow == 0)
	{
		okToRead = eaFind(&space_state.global_spaces, aDebugUI.spaces.selectedSpace) >= 0;
		if(!okToRead)
		{
			okToRead = eaFind(&space_state.non_exclusive_spaces, aDebugUI.spaces.selectedSpace) >= 0;
		}
	}

	if(okToRead)
	{
		if( iRow < eaSize(&aDebugUI.spaces.selectedSpace->localSources) )
		{
			SoundSource *source = aDebugUI.spaces.selectedSpace->localSources[iRow];
			estrPrintf(estrOutput, "[%d] %s <%.2f, %.2f, %.2f>", iRow+1, source->obj.desc_name, source->virtual_pos[0], source->virtual_pos[1], source->virtual_pos[2]);
		}
	}
}

//void aDebugSpaceBypassReverbChanged(UIAnyWidget *widget, UserData userData) 
//{
//	bool okToRead;
//	okToRead = eaFind(&space_state.global_spaces, aDebugUI.spaces.selectedSpace) >= 0;
//	if(okToRead)
//	{
//		SoundSpace *soundSpace = aDebugUI.spaces.selectedSpace;
//		if(soundSpace->fmodEventReverb)
//		{
//			bool state = ui_CheckButtonGetState((UICheckButton*)widget);
//
//			FMOD_EventReverbSetActive(soundSpace->fmodEventReverb, !state);
//		}
//	}
//}

//void aDebugSpaceUpdateReverbBypass()
//{
//	bool okToRead;
//	okToRead = eaFind(&space_state.global_spaces, aDebugUI.spaces.selectedSpace) >= 0;
//	if(okToRead)
//	{
//		SoundSpace *soundSpace = aDebugUI.spaces.selectedSpace;
//		if(soundSpace->fmodEventReverb)
//		{
//			bool active;
//			FMOD_EventReverbGetActive(soundSpace->fmodEventReverb, &active);
//
//			ui_CheckButtonSetState(aDebugUI.spaces.bypassReverb, !active);
//		}
//
//		ui_SetActive(UI_WIDGET(aDebugUI.spaces.bypassReverb), true);
//	} else {
//		ui_SetActive(UI_WIDGET(aDebugUI.spaces.bypassReverb), false);
//	}
//}

void aDebugSpaceOwnedSources(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	static bool okToRead = false;


	if(iRow == 0)
	{
		okToRead = eaFind(&space_state.global_spaces, aDebugUI.spaces.selectedSpace) >= 0;
		if(!okToRead)
		{
			okToRead = eaFind(&space_state.non_exclusive_spaces, aDebugUI.spaces.selectedSpace) >= 0;
		}
	}

	if(okToRead)
	{
		if( iRow < eaSize(&aDebugUI.spaces.selectedSpace->ownedSources) )
		{
			SoundSource *source = aDebugUI.spaces.selectedSpace->ownedSources[iRow];
			estrPrintf(estrOutput, "[%d] %s", iRow+1, source->obj.desc_name);
		}
	}
}

void aDebugSpaceConnections(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	static const char *types[] = {"Door", "Thin Wall"};
	SoundSpaceConnector *connector;
	int typeIndex;
	SoundDSP *soundDSP;
	SoundSpace *otherSpace;

	connector = aDebugUI.spaces.selectedConnections[iRow];
	typeIndex = connector->type;
	if(typeIndex < 0) typeIndex = 0;
	if(typeIndex > 1) typeIndex = 1;
	
	otherSpace = sndConnGetOther(connector, aDebugUI.spaces.selectedSpace);

	soundDSP = GET_REF(connector->dsp_ref);
	if(soundDSP)
	{
		estrPrintf(estrOutput, "[%d] %s <%.2f> [%s] (%s) => %s", iRow+1, connector->obj.desc_name, connector->audibility, soundDSP->name, types[typeIndex], otherSpace->obj.desc_name);	
	}
	else
	{
		estrPrintf(estrOutput, "[%d] %s <%.2f> (%s) => %s", iRow+1, connector->obj.desc_name, connector->audibility, types[typeIndex], otherSpace->obj.desc_name);
	}
}

void aDebugDisplaySpaceGain(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	SoundSpace **spaces = (SoundSpace**)(*pList->peaModel);

	if( spaces && iRow < eaSize(&spaces) )
	{
		SoundSpace *soundSpace = spaces[iRow];

		if(soundSpace->soundMixerChannel)
		{
			estrPrintf(estrOutput, "%.3f", soundSpace->soundMixerChannel->audibility);
		}
	}
}

void aDebugDisplaySpaceName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	SoundSpace **spaces = (SoundSpace**)(*pList->peaModel);

	if( spaces && iRow < eaSize(&spaces) )
	{
		SoundSpace *soundSpace = spaces[iRow];

		if(soundSpace->obj.desc_name)
		{
			estrPrintf(estrOutput, "[%d] %s", iRow+1, soundSpace->obj.desc_name);
		}
		else if(soundSpace->type == SST_SPHERE)
		{
			if(soundSpace->ownedSources && eaSize(&soundSpace->ownedSources) > 0)
			{
				SoundSource *ownedSource = soundSpace->ownedSources[0];
				estrPrintf(estrOutput, "[%d] %s (%.0f)", iRow+1, ownedSource->obj.desc_name, soundSpace->sphere.radius);
			}
			else
			{
				estrPrintf(estrOutput, "[%d] Sphere <%.0f, %.0f, %.0f> (%.0f)", iRow+1, soundSpace->sphere.mid[0], soundSpace->sphere.mid[1], soundSpace->sphere.mid[2], soundSpace->sphere.radius);
			}
			
		}
		else
		{
			estrPrintf(estrOutput, "{ Unknown SoundSpace }");
		}
	}
}

void aDebugSpaceOwnedSourcesCount(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	SoundSpace **spaces = (SoundSpace**)(*pList->peaModel);

	if( spaces && iRow < eaSize(&spaces) )
	{
		SoundSpace *soundSpace = spaces[iRow];
		estrPrintf(estrOutput, "%d", eaSize(&soundSpace->ownedSources));
	}
}

void aDebugSpaceLocalSourcesCount(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	SoundSpace **spaces = (SoundSpace**)(*pList->peaModel);

	if( spaces && iRow < eaSize(&spaces) )
	{
		SoundSpace *soundSpace = spaces[iRow];
		estrPrintf(estrOutput, "%d", eaSize(&soundSpace->localSources));
	}
}

void aDebugDisplayConnectionName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	SoundSpaceConnector **connectors = (SoundSpaceConnector**)(*pList->peaModel);

	if( connectors && iRow < eaSize(&connectors) )
	{
		SoundSpaceConnector *connector = connectors[iRow];
		bool oneWayConnection = false;
		if(!connector->space1 || !connector->space2)
		{
			oneWayConnection = true; 
		}

		if(connector->obj.desc_name)
		{
			estrPrintf(estrOutput, "[%d] %s (%p) %s", iRow+1, connector->obj.desc_name, connector, oneWayConnection ? "invalid" : "*");
		}
	}
}

void aDebugSpacePropertyName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	if(!aDebugUI.spaces.selectedSpace) return;

	//ADebugSpaceTreeData *nodeData = aDebugUI.spaces.selectedSpaceNode;

	//if(!nodeData) 
	//{
	//	estrPrintf(estrOutput, "");
	//	return;
	//}

	if(iRow < ADEBUG_SPACE_END_OF_LIST)
	{
		estrPrintf(estrOutput, "%s", aDebugSpacePropertyNames[iRow]);
	}
}

void aDebugGetDSPStringForSpace(SoundSpace *soundSpace, char **estrOutput)
{
	SoundDSP *soundDSP = GET_REF(soundSpace->dsp_ref);
	if(soundDSP)
	{
		estrPrintf(estrOutput, "%s (%s)", soundDSP->name, soundDSP->filename);
	}
	else
	{
		estrPrintf(estrOutput, "");
	}
}





void aDebugDisplaySoundSpaceValues(SoundSpace *soundSpace, S32 iRow, char **estrOutput)
{
	if(iRow < ADEBUG_SPACE_END_OF_LIST)
	{
		switch(iRow) {
			case ADEBUG_SPACE_FILENAME:
				if(soundSpace->obj.file_name) estrPrintf(estrOutput, "%s", soundSpace->obj.file_name);
				break;
			case ADEBUG_SPACE_DESC_NAME:
				if(soundSpace->obj.desc_name) estrPrintf(estrOutput, "%s", soundSpace->obj.desc_name);
				break;
			case ADEBUG_SPACE_ORIG_NAME:
				if(soundSpace->obj.orig_name) estrPrintf(estrOutput, "%s", soundSpace->obj.orig_name);
				break;
			case ADEBUG_SPACE_NUM_OWNED_SOURCES:
				estrPrintf(estrOutput, "%d", eaSize(&soundSpace->ownedSources));
				break;
			case ADEBUG_SPACE_NUM_LOCAL_SOURCES:
				estrPrintf(estrOutput, "%d", eaSize(&soundSpace->localSources));
				break;
			case ADEBUG_SPACE_PRIORITY:
				estrPrintf(estrOutput, "%d", soundSpace->priority);
				break;
			case ADEBUG_SPACE_MULTIPLIER:
				estrPrintf(estrOutput, "%d", soundSpace->multiplier);
				break;
			case ADEBUG_SPACE_POSITION:
				if(soundSpace->type == SST_VOLUME)
				{
					aDebugEstrPrintVector(estrOutput, soundSpace->volume.world_mid);
				}
				else if(soundSpace->type == SST_SPHERE)
				{
					aDebugEstrPrintVector(estrOutput, soundSpace->sphere.mid);
				}
				break;

			case ADEBUG_SPACE_TYPE: {
				switch(soundSpace->type){
			case SST_VOLUME: estrPrintf(estrOutput, "Volume"); break;
			case SST_SPHERE: estrPrintf(estrOutput, "Sphere"); break;
			case SST_NULL: estrPrintf(estrOutput, "NULL"); break;
				}
				break;
									}
			case ADEBUG_SPACE_MUSIC:
				estrPrintf(estrOutput, "%s", soundSpace->music_name);
				break;
			case ADEBUG_SPACE_DSP:{
				aDebugGetDSPStringForSpace(soundSpace, estrOutput);
				break;
								  }
			case ADEBUG_SPACE_AUDIBLE:
				estrPrintf(estrOutput, "%c", soundSpace->is_audible ? 'Y' : 'N');
				break;
			case ADEBUG_SPACE_CURRENT:
				estrPrintf(estrOutput, "%c", soundSpace->is_current ? 'Y' : 'N');
				break;
		}
	}
}

void aDebugDisplayReverbProperty(FMOD_REVERB_PROPERTIES *properties, S32 iRow, char **estrOutput)
{
	if(iRow < ADEBUG_REVERB_END_OF_LIST)
	{
		switch(iRow) {
			case REVERB_INSTANCE: estrPrintf(estrOutput, "%d", properties->Instance); break;
			case REVERB_ENVIRONMENT: estrPrintf(estrOutput, "%d", properties->Environment); break;
			//case REVERB_ENVSIZE: estrPrintf(estrOutput, "%.3f", properties->EnvSize); break;
			case REVERB_ENVDIFFUSION: estrPrintf(estrOutput, "%.3f", properties->EnvDiffusion); break;
			case REVERB_ROOM: estrPrintf(estrOutput, "%d", properties->Room); break;
			case REVERB_ROOMHF: estrPrintf(estrOutput, "%d", properties->RoomHF); break;
			case REVERB_ROOMLF: estrPrintf(estrOutput, "%d", properties->RoomLF); break;
			case REVERB_DECAYTIME: estrPrintf(estrOutput, "%.3f", properties->DecayTime); break;
			case REVERB_DECAYHFRATIO: estrPrintf(estrOutput, "%.3f", properties->DecayHFRatio); break;
			case REVERB_DECAYLFRATIO: estrPrintf(estrOutput, "%.3f", properties->DecayLFRatio); break;
			case REVERB_REFLECTIONS: estrPrintf(estrOutput, "%d", properties->Reflections); break;
			case REVERB_REFLECTIONSDELAY: estrPrintf(estrOutput, "%.3f", properties->ReflectionsDelay); break;
			//case REVERB_REFLECTIONSPAN: estrPrintf(estrOutput, "<%.2f, %.2f, %.2f>", properties->ReflectionsPan[0], properties->ReflectionsPan[1], properties->ReflectionsPan[2]); break;
			case REVERB_REVERB: estrPrintf(estrOutput, "%d", properties->Reverb); break;
			case REVERB_REVERBDELAY: estrPrintf(estrOutput, "%.3f", properties->ReverbDelay); break;
			//case REVERB_REVERBPAN: estrPrintf(estrOutput, "<%.2f, %.2f, %.2f>", properties->ReverbPan[0], properties->ReverbPan[1], properties->ReverbPan[2]); break;
			//case REVERB_ECHOTIME: estrPrintf(estrOutput, "%.3f", properties->EchoTime); break;
			//case REVERB_ECHODEPTH: estrPrintf(estrOutput, "%.3f", properties->EchoDepth); break;
			case REVERB_MODTIME: estrPrintf(estrOutput, "%.3f", properties->ModulationTime); break;
			case REVERB_MODDEPTH: estrPrintf(estrOutput, "%.3f", properties->ModulationDepth); break;
			//case REVERB_AIRABSORPTIONHF: estrPrintf(estrOutput, "%.3f", properties->AirAbsorptionHF); break;
			case REVERB_HFREF: estrPrintf(estrOutput, "%.3f", properties->HFReference); break;
			case REVERB_LFREF: estrPrintf(estrOutput, "%.3f", properties->LFReference); break;
			//case REVERB_ROOMROLLOFFFACTOR: estrPrintf(estrOutput, "%.3f", properties->RoomRolloffFactor); break;
			case REVERB_DIFFUSION: estrPrintf(estrOutput, "%.3f", properties->Diffusion); break;
			case REVERB_DENSITY: estrPrintf(estrOutput, "%.3f", properties->Density); break;
			case REVERB_FLAGS: estrPrintf(estrOutput, "%d", properties->Flags); break;
		}
	}
}

void aDebugSpaceReverbPropertyName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	//if(iRow < ADEBUG_REVERB_END_OF_LIST)
	//{
	//	estrPrintf(estrOutput, "%s", aDebugReverbPropertyNames[iRow]);
	//}

	F32 minValue, maxValue;
	int descriptionLen = 32;
	char description[32];
	char paramName[16];
	char label[16];

	fmodDSPGetParameterInfo(sndMixerReverbDSP(gSndMixer), iRow, paramName, label, description, descriptionLen, &minValue, &maxValue); 
		
	estrPrintf(estrOutput, "%s", paramName);

}

void aDebugSpaceReverbPropertyValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	F32 val;
	char valStr[32];
	int valStrLen = 32;

	if(iRow == 0)
	{
		char txt[32];

		F32 reverbReturn = sndMixerReverbReturnLevel(gSndMixer);

		sprintf(txt, "Reverb Return %.3f", reverbReturn);
		ui_LabelSetText(aDebugUI.spaces.reverbReturnLevel, txt);
	}

	fmodDSPGetParameter(sndMixerReverbDSP(gSndMixer), iRow, &val, valStr, valStrLen);

	estrPrintf(estrOutput, "%.3f", val);
}

void aDebugSpacePropertyValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	static bool okToRead = false;
	SoundSpace *soundSpace = aDebugUI.spaces.selectedSpace;
	if(!soundSpace) return;

	//ADebugSpaceTreeData *nodeData = aDebugUI.spaces.selectedSpaceNode;

	//SoundSpace *soundSpace;

	//if(!nodeData) return;
	//soundSpace = nodeData->space;
	if(iRow == 0)
	{
		okToRead = eaFind(&space_state.global_spaces, soundSpace) >= 0;
		if(!okToRead)
		{
			okToRead = eaFind(&space_state.non_exclusive_spaces, soundSpace) >= 0;
		}

	}
	if(okToRead)
	{
		aDebugDisplaySoundSpaceValues(soundSpace, iRow, estrOutput);
	}
}

void aDebugDrawSpace(SoundSpace *soundSpace)
{
	if(soundSpace->type == SST_VOLUME)
	{
		Mat4 world_mat;
		Color color;
		
		if(soundSpace->is_audible)
		{
			color = ARGBToColor(0x3300FF00);
		}
		else
		{
			color = ARGBToColor(0x33FF0000);
		}
		identityMat4(world_mat);

		gfxDrawBox3D(soundSpace->volume.world_min, soundSpace->volume.world_max, world_mat, color, 0.0); // fill
		gfxDrawBox3D(soundSpace->volume.world_min, soundSpace->volume.world_max, world_mat, color, 1.0); // outline
	}
}

//void aDebugDrawReverbSphere(SoundSpace *soundSpace)
//{
//	if(soundSpace->type == SST_VOLUME)
//	{
//		if(soundSpace->fmodEventReverb)
//		{
//			Vec3 pos;
//			F32 minRadius, maxRadius;
//			int color;
//			bool active;
//
//			FMOD_EventReverbGetActive(soundSpace->fmodEventReverb, &active);
//
//			color = active ? 0xFFFF9900 : 0xFF330000;
//
//			FMOD_EventReverbGet3DAttributes(soundSpace->fmodEventReverb, &pos, &minRadius, &maxRadius);
//
//			//gfxDrawSphere3DARGB(pos, minRadius, 24.0, color, 1.0);
//			gfxDrawSphere3DARGB(pos, maxRadius, 24.0, color, 1.0);
//		}
//	}
//}

void aDebugDrawConnector(SoundSpaceConnector *conn)
{
	F32 line_width = 0.0;
	Color color;

	if(conn->audibility > 0.0)
	{
		color = ARGBToColor(0x33AA00FF);
	}
	else
	{
		color = ARGBToColor(0x3300AAFF);
	}

	gfxDrawBox3D(conn->local_min, conn->local_max, conn->world_mat, color, 0.0); // fill
	gfxDrawBox3D(conn->local_min, conn->local_max, conn->world_mat, color, 1.0); // outline

	//for(j=0; j<eaSize(&conn->props1.audibleConns); j++)
	//{
	//	int k;
	//	int bidir = 0;
	//	SoundSpaceConnectorTransmission *trans = conn->props1.audibleConns[j];
	//	SoundSpaceConnectorProperties *otherProps = NULL;

	//	otherProps = sndConnGetSpaceProperties(trans->conn, conn->space1);

	//	for(k=0; k<eaSize(&otherProps->audibleConns); k++)
	//	{
	//		SoundSpaceConnectorTransmission *otherTrans = otherProps->audibleConns[k];

	//		if(otherTrans->conn==conn)
	//		{
	//			bidir = 1;
	//			break;
	//		}
	//	}

	//	if(bidir)
	//	{
	//		gfxDrawLine3DARGB(conn->world_mid, trans->conn->world_mid, 0xFFFF0000);
	//	}
	//	else
	//	{
	//		gfxDrawLine3D_2ARGB(conn->world_mid, trans->conn->world_mid, 0xFF00FF00, 0xFF0000FF);
	//	}
	//}

	//for(j=0; j<eaSize(&conn->props2.audibleConns); j++)
	//{
	//	int k;
	//	int bidir = 0;
	//	SoundSpaceConnectorTransmission *trans = conn->props2.audibleConns[j];
	//	SoundSpaceConnectorProperties *otherProps = NULL;

	//	otherProps = sndConnGetSpaceProperties(trans->conn, conn->space2);

	//	if(otherProps)
	//	{
	//		for(k=0; k<eaSize(&otherProps->audibleConns); k++)
	//		{
	//			SoundSpaceConnectorTransmission *otherTrans = otherProps->audibleConns[k];

	//			if(otherTrans->conn==conn)
	//			{
	//				bidir = 1;
	//				break;
	//			}
	//		}
	//	}			

	//	if(bidir)
	//	{
	//		gfxDrawLine3DARGB(conn->world_mid, trans->conn->world_mid, 0xFFFF0000);
	//	}
	//	else
	//	{
	//		gfxDrawLine3D_2ARGB(conn->world_mid, trans->conn->world_mid, 0xFF00FF00, 0xFF0000FF);
	//	}
	//}
}

//void aDebugDrawReverbSpheres(void *userData)
//{
//	int numSpaces = eaSize(&space_state.global_spaces);
//	int i;
//
//	for(i = 0; i < numSpaces; i++)
//	{
//		aDebugDrawReverbSphere(space_state.global_spaces[i]);
//	}
//}

void aDebugDrawSpaces(void *userData)
{
	int numConnectors = eaSize(&space_state.global_conns);
	int numSpaces = eaSize(&space_state.global_spaces);
	int i;

	for(i = 0; i < numSpaces; i++)
	{
		aDebugDrawSpace(space_state.global_spaces[i]);
	}

	for(i = 0; i < numConnectors; i++)
	{
		aDebugDrawConnector(space_state.global_conns[i]);
	}
}

void aDebugUpdateDisplaySpacesState()
{
	if(ui_CheckButtonGetState(aDebugUI.spaces.displaySpaces))
	{
		aDebugUI.spaces.drawAllCB = aDebugAddDrawCB(&aDebugUI.drawCallbacks, aDebugDrawSpaces, NULL, NULL);
	}
	else
	{
		aDebugRemoveDrawCB(&aDebugUI.drawCallbacks, aDebugUI.spaces.drawAllCB);
	}
}

void aDebugDisplaySpacesChanged(UIAnyWidget *widget, UserData userData)
{
	aDebugUpdateDisplaySpacesState();
}

void aDebugRemoveSpaces(UIAnyWidget *widget, UserData userData) 
{
	// destroy all but Nullspace
	FOR_EACH_IN_EARRAY(space_state.global_spaces, SoundSpace, space)
		if( strcmp(space->obj.desc_name, "Nullspace") ) 
		{
			sndSpaceDestroy(space);
		}
	FOR_EACH_END;
}

void aDebugSetupSpacesTab(UITab *tab)
{
	F32 left_col_width = 300.0;
	F32 x, y;
	F32 padding = 5.0;
	static void **fakeVarEArray = NULL;
	static void **fakeReverbVarEArray = NULL;
	UIListColumn *col;
	F32 rowHeight = gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP;
	UIButton *removeButton;

	aDebugUI.spaces.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.spaces.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	x = 5;
	y = 5;

	// Current Label
	aDebugUI.spaces.currentSpaceName = ui_LabelCreate("Current Space: ", x, y);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.currentSpaceName));

	y += 25;


	// Create the Tab Group ///////////////////////////////////////////////////
	aDebugUI.spaces.tabs = ui_TabGroupCreate(0, y, left_col_width, 250);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.tabs), left_col_width, 1.0, UIUnitFixed, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.spaces.tabs), 0, 0, 0, 25);


	// Display All Spaces
	aDebugUI.spaces.displaySpaces = ui_CheckButtonCreate(x, 0, "Display Spaces", true);
	ui_CheckButtonSetToggledCallback(aDebugUI.spaces.displaySpaces, aDebugDisplaySpacesChanged, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.spaces.displaySpaces), x, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, aDebugUI.spaces.displaySpaces);


	// Remove All Spaces
	removeButton = ui_ButtonCreate("Remove Spaces", x, 0, aDebugRemoveSpaces, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(removeButton), left_col_width - 110, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, removeButton);

	aDebugUpdateDisplaySpacesState();


	// Display Reverbs
	//aDebugUI.spaces.displayReverbs = ui_CheckButtonCreate(x + 125, 0, "Display Reverb Spheres", false);
	//ui_CheckButtonSetToggledCallback(aDebugUI.spaces.displayReverbs, aDebugDisplayReverbsChanged, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.spaces.displayReverbs), x + 125, 0, 0, 0, UIBottomLeft);
	//ui_PaneAddChild(aDebugUI.spaces.rootPane, aDebugUI.spaces.displayReverbs);

	//aDebugUpdateDisplayReverbSpheresState();




	

	//// Current Space Tree
	//aDebugUI.spaces.spacesTree = ui_TreeCreate(0.0, 0.0, left_col_width, 1.0);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.spaces.spacesTree ), 0, 0, 0, 25);
	//ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.spacesTree ), left_col_width, 1.0, UIUnitFixed, UIUnitPercentage);
	//ui_TreeSetSelectedCallback(aDebugUI.spaces.spacesTree, aDebugSpaceSelected, NULL);
	//aDebugUI.spaces.spaceTreeRoot = NULL;
	//ui_TabAddChild(aDebugUI.spaces.currentSpaceTab, UI_WIDGET(aDebugUI.spaces.spacesTree));				

	//// Expand All
	//aDebugUI.spaces.expandAll = ui_CheckButtonCreate(5, 0, "Expand All", false);
	//ui_CheckButtonSetToggledCallback(aDebugUI.spaces.expandAll, aDebugExpandAllSpacesTree, NULL);
	//ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.spaces.expandAll), 5, 0, 0, 0, UIBottomLeft);
	//ui_TabAddChild(aDebugUI.spaces.currentSpaceTab, UI_WIDGET(aDebugUI.spaces.expandAll));


	// Global Spaces Tab --
	aDebugUI.spaces.globalSpacesTab = ui_TabCreate("Spaces"); 
	ui_TabGroupAddTab(aDebugUI.spaces.tabs, aDebugUI.spaces.globalSpacesTab);

	aDebugUI.spaces.globalSpacesList = ui_ListCreate(NULL, &space_state.global_spaces, rowHeight);
	//aDebugUI.spaces.globalSpacesList->fHeaderHeight = 0.0;
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.globalSpacesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.spaces.globalSpacesList, aDebugSpaceListSelected, NULL);
	
	col = ui_ListColumnCreate(UIListTextCallback, "Space", (intptr_t)aDebugDisplaySpaceName, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.globalSpacesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Gain", (intptr_t)aDebugDisplaySpaceGain, NULL);
	ui_ListColumnSetWidth(col, false, 50);
	ui_ListAppendColumn(aDebugUI.spaces.globalSpacesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "L", (intptr_t)aDebugSpaceLocalSourcesCount, NULL);
	ui_ListColumnSetWidth(col, false, 25);
	ui_ListAppendColumn(aDebugUI.spaces.globalSpacesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "O", (intptr_t)aDebugSpaceOwnedSourcesCount, NULL);
	ui_ListColumnSetWidth(col, false, 25);
	ui_ListAppendColumn(aDebugUI.spaces.globalSpacesList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.globalSpacesList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.globalSpacesTab, UI_WIDGET(aDebugUI.spaces.globalSpacesList));


	// Current Reverb Tab --
	aDebugUI.spaces.reverbSpaceTab = ui_TabCreate("Reverb");
	ui_TabGroupAddTab(aDebugUI.spaces.tabs, aDebugUI.spaces.reverbSpaceTab);

	// Space Properties List
	if(!fakeReverbVarEArray) 
	{
		int i, numParams = 0;
		if(gSndMixer)
		{
			void *fmodWetDSP = sndMixerReverbDSP(gSndMixer);
			if(fmodWetDSP)
			{
				fmodDSPGetNumParameters(fmodWetDSP, &numParams);
				aDebugUI.spaces.numReverbParams = numParams;

				eaSetSize(&fakeReverbVarEArray, numParams);
				for(i=0; eaSize(&fakeReverbVarEArray) < numParams; i++) eaPush(&fakeReverbVarEArray, NULL);
			}
		}

	}

	aDebugUI.spaces.reverbReturnLevel = ui_LabelCreate("Reverb Return", 5, 5);
	ui_TabAddChild(aDebugUI.spaces.reverbSpaceTab, UI_WIDGET(aDebugUI.spaces.reverbReturnLevel));

	aDebugUI.spaces.reverbPropertyList = ui_ListCreate(NULL, &fakeReverbVarEArray, rowHeight);
	//aDebugUI.spaces.globalSpacesList->fHeaderHeight = 0.0;
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.reverbPropertyList), 1);
	//ui_ListSetCellClickedCallback(aDebugUI.spaces.reverbPropertyList, aDebugSpaceListSelected, NULL);

	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugSpaceReverbPropertyName, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.reverbPropertyList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugSpaceReverbPropertyValue, NULL);
	ui_ListColumnSetWidth(col, false, 100);
	ui_ListAppendColumn(aDebugUI.spaces.reverbPropertyList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.reverbPropertyList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.spaces.reverbPropertyList), 0, 0, 30, 0);
	ui_TabAddChild(aDebugUI.spaces.reverbSpaceTab, UI_WIDGET(aDebugUI.spaces.reverbPropertyList));


	// Non Exclusive Spaces Tab --
	aDebugUI.spaces.nonExclusiveSpacesTab = ui_TabCreate("Spheres");
	ui_TabGroupAddTab(aDebugUI.spaces.tabs, aDebugUI.spaces.nonExclusiveSpacesTab);

	aDebugUI.spaces.nonExclusiveSpacesList = ui_ListCreate(NULL, &space_state.non_exclusive_spaces, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.nonExclusiveSpacesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.spaces.nonExclusiveSpacesList, aDebugSpaceListSelected, NULL);

	col = ui_ListColumnCreate(UIListTextCallback, "Space", (intptr_t)aDebugDisplaySpaceName, NULL);
	ui_ListColumnSetWidth(col, false, 750);
	ui_ListAppendColumn(aDebugUI.spaces.nonExclusiveSpacesList, col);
	//col = ui_ListColumnCreate(UIListTextCallback, "L", (intptr_t)aDebugSpaceLocalSourcesCount, NULL);
	//ui_ListColumnSetWidth(col, false, 25);
	//ui_ListAppendColumn(aDebugUI.spaces.nonExclusiveSpacesList, col);
	//col = ui_ListColumnCreate(UIListTextCallback, "O", (intptr_t)aDebugSpaceOwnedSourcesCount, NULL);
	//ui_ListColumnSetWidth(col, false, 25);
	//ui_ListAppendColumn(aDebugUI.spaces.nonExclusiveSpacesList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.nonExclusiveSpacesList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.nonExclusiveSpacesTab, UI_WIDGET(aDebugUI.spaces.nonExclusiveSpacesList));


	// Global Connections Tab --
	aDebugUI.spaces.globalConnsTab = ui_TabCreate("Connections");
	ui_TabGroupAddTab(aDebugUI.spaces.tabs, aDebugUI.spaces.globalConnsTab);

	aDebugUI.spaces.globalConnsList = ui_ListCreate(NULL, &space_state.global_conns, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.globalConnsList), 1);

	col = ui_ListColumnCreate(UIListTextCallback, "Connection", (intptr_t)aDebugDisplayConnectionName, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.globalConnsList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.globalConnsList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.globalConnsTab, UI_WIDGET(aDebugUI.spaces.globalConnsList));


	ui_TabGroupSetActiveIndex(aDebugUI.spaces.tabs, 0); // select first tab

	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.tabs));
	///////////////////////////////////////////////////////////////////////////

	x = left_col_width + padding;
	y = 25;


	// Selection Label
	aDebugUI.spaces.selectedName = ui_LabelCreate("", x, y);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.selectedName));

	y += 20;

	// Reverb Controls
	//aDebugUI.spaces.bypassReverb = ui_CheckButtonCreate(x, y, "Bypass", true);
	//ui_CheckButtonSetToggledCallback(aDebugUI.spaces.bypassReverb, aDebugSpaceBypassReverbChanged, NULL);
	//ui_PaneAddChild(aDebugUI.spaces.rootPane, aDebugUI.spaces.bypassReverb);

	aDebugUI.spaces.reverbName = ui_LabelCreate("", x, y);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.reverbName));

	y += 25;

	// Space Properties List
	if(!fakeVarEArray) 
	{
		int i;
		eaSetSize(&fakeVarEArray, ADEBUG_SPACE_END_OF_LIST);
		for(i=0; eaSize(&fakeVarEArray) < ADEBUG_SPACE_END_OF_LIST; i++) eaPush(&fakeVarEArray, NULL);
	}

	aDebugUI.spaces.paramsList = ui_ListCreate(NULL, &fakeVarEArray, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_ListSetCellActivatedCallback(aDebugUI.spaces.paramsList, aDebugActivateSpaceProperty, NULL);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.paramsList), 1);

	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugSpacePropertyName, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.paramsList, col);
	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugSpacePropertyValue, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.paramsList, col);

	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.spaces.paramsList), left_col_width+padding, 0, y, 300);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.paramsList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.paramsList));

	//
	// Sources Tabs
	//
 
	// Create the Tab Group
	aDebugUI.spaces.sourcesTabGroup = ui_TabGroupCreate(0, 0, 500, 300);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.sourcesTabGroup), 1.0, 300, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.spaces.sourcesTabGroup), 0, 0, 0, 0, UIBottomLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.spaces.sourcesTabGroup), left_col_width+padding, 0, 0, 0);
	ui_PaneAddChild(aDebugUI.spaces.rootPane, UI_WIDGET(aDebugUI.spaces.sourcesTabGroup));

	

	// Local Sources
	aDebugUI.spaces.localSourcesTab = ui_TabCreate("Local Sources");
	ui_TabGroupAddTab(aDebugUI.spaces.sourcesTabGroup, aDebugUI.spaces.localSourcesTab);

	// Local Sources
	aDebugUI.spaces.ownedSourcesTab = ui_TabCreate("Owned Sources");
	ui_TabGroupAddTab(aDebugUI.spaces.sourcesTabGroup, aDebugUI.spaces.ownedSourcesTab);

	// Local Sources
	aDebugUI.spaces.connectionsTab = ui_TabCreate("Connections");
	ui_TabGroupAddTab(aDebugUI.spaces.sourcesTabGroup, aDebugUI.spaces.connectionsTab);

	// Local Sound Sources
	aDebugUI.spaces.localSourcesList = ui_ListCreate(NULL, &aDebugUI.spaces.selectedLocalSources, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_ListSetCellActivatedCallback(aDebugUI.spaces.localSourcesList, aDebugActivateLocalSources, NULL);
	//ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.localSourcesList), 1);
	aDebugUI.spaces.localSourcesList->fHeaderHeight = 0;

	col = ui_ListColumnCreate(UIListTextCallback, "Local Sources", (intptr_t)aDebugSpaceLocalSources, NULL);
	
	ui_ListAppendColumn(aDebugUI.spaces.localSourcesList, col);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.localSourcesList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.localSourcesTab, aDebugUI.spaces.localSourcesList);
	

	// Owned Sound Sources
	aDebugUI.spaces.ownedSourcesList = ui_ListCreate(NULL, &aDebugUI.spaces.selectedOwnedSources, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.ownedSourcesList), 1);
	aDebugUI.spaces.ownedSourcesList->fHeaderHeight = 0;

	col = ui_ListColumnCreate(UIListTextCallback, "Owned Sources", (intptr_t)aDebugSpaceOwnedSources, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.ownedSourcesList, col);
	
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.ownedSourcesList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.ownedSourcesTab, aDebugUI.spaces.ownedSourcesList);


	// Connections
	aDebugUI.spaces.connectionsList = ui_ListCreate(NULL, &aDebugUI.spaces.selectedConnections, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.spaces.connectionsList), 1);
	aDebugUI.spaces.connectionsList->fHeaderHeight = 0;

	col = ui_ListColumnCreate(UIListTextCallback, "Connections", (intptr_t)aDebugSpaceConnections, NULL);
	ui_ListAppendColumn(aDebugUI.spaces.connectionsList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.spaces.connectionsList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_TabAddChild(aDebugUI.spaces.connectionsTab, aDebugUI.spaces.connectionsList);

	ui_TabGroupSetActiveIndex(aDebugUI.spaces.sourcesTabGroup, 0); // select first tab

	ui_TabAddChild(tab, aDebugUI.spaces.rootPane);
}


///////////////////////////////////////////////////////////////////////////////
// LoD
///////////////////////////////////////////////////////////////////////////////
void aDebugToggleLODEnabled(UIAnyWidget *widget, UserData userData)
{
	sndLODSetIsEnabled(&gSndLOD, ui_CheckButtonGetState((UICheckButton*)widget)); 
}

void aDebugUpdateLODMemoryThreshold(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *entry = (UITextEntry*)widget;
	const unsigned char *input;
	F32 threshold;
	char validatedStr[9];

	// get input
	input = ui_TextEntryGetText(entry);
	threshold = atof(input);
	
	// make the change to the system
	sndLODSetThreshold(&gSndLOD, threshold);

	// make sure we update display 
	sprintf(validatedStr, "%.2f", sndLODThreshold(&gSndLOD));
	ui_TextEntrySetText(entry, validatedStr);
}

void aDebugUpdateLODWaitDuration(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *entry = (UITextEntry*)widget;
	const unsigned char *input;
	F32 durationInSecs;
	char validatedStr[9];

	// get input
	input = ui_TextEntryGetText(entry);
	durationInSecs = atof(input);

	// make the change to the system
	sndLODSetWaitDuration(&gSndLOD, durationInSecs);

	// make sure we update display 
	sprintf(validatedStr, "%.2f", sndLODWaitDuration(&gSndLOD));
	ui_TextEntrySetText(entry, validatedStr);
}

void sndLODUpdateActiveSources(SoundSource ***sources)
{
	static void **events;
	int numEvents, i;

	eaClear(sources);

	eaClear(&events);
	fmodEventSystemGetPlaying(&events);
	numEvents = eaSize(&events);

	for(i = 0; i < numEvents; i++)
	{
		FMOD_RESULT result;
		int systemId;
		void *fmod_event = events[i];

		result = FMOD_EventSystem_GetSystemID(fmod_event, &systemId);
		if(!result)
		{
			SoundSourceGroup *group;

			if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, systemId, &group))
			{
				// check active sources for ptr
				int numActive = eaSize(&group->active_sources);
				int j;

				for(j = 0; j < numActive; j++)
				{
					SoundSource *source = group->active_sources[j];
					if(source->fmod_event == fmod_event)
					{
						eaPush(sources, source);
					}
				}
			}
		}
	}
}

void aDebugUpdateLODInfo()
{
	bool isEnabled;
	SndLODState currentState;
	SndLODState lastState;
	F32 durAtCurrentState;
	F32 durAtLastState;
	F32 durAboveThreshold;
	F32 durBelowThreshold;
//	SoundSource **stoppedSources;
	SndLOD *sndLOD;
	static char txt[64];
	int currentMemoryUsed, maxMemoryUsed;

	FMOD_EventSystem_GetMemStats(&currentMemoryUsed, &maxMemoryUsed);

	sndLOD = &gSndLOD;
	
	// updated enabled
	isEnabled = sndLODIsEnabled(sndLOD);
	if(isEnabled != ui_CheckButtonGetState(aDebugUI.lod.enabledCheckButton)) 
	{
		ui_CheckButtonSetState(aDebugUI.lod.enabledCheckButton, isEnabled);
	}

	// update current state
	currentState = sndLODState(sndLOD);
	durAtCurrentState = sndLODDurationAtCurrentState(sndLOD);

	sprintf(txt, "%s (%.2fs)", sndLODStateAsString(currentState), durAtCurrentState);
	ui_LabelSetText(aDebugUI.lod.currentStateLabel, txt);

	// update last state
	lastState = sndLODLastState(sndLOD);
	durAtLastState = sndLODDurationAtLastState(sndLOD);

	sprintf(txt, "%s (%.2fs)", sndLODStateAsString(lastState), durAtLastState);
	ui_LabelSetText(aDebugUI.lod.lastStateLabel, txt);

	// update current memory & above threshold
	durAboveThreshold = sndLODDurationAboveThreshold(sndLOD);

	sprintf(txt, "used: %.2f", (float)currentMemoryUsed / (float)soundBufferSize);
	ui_LabelSetText(aDebugUI.lod.currentMemory, txt);

	// update below threshold
	durBelowThreshold = sndLODDurationBelowThreshold(sndLOD);

	sprintf(txt, "below: %.2fs", durBelowThreshold);
	ui_LabelSetText(aDebugUI.lod.durationBelowThreshold, txt);

	sprintf(txt, "%.2f", sndLOD->ambientClipDistance);
	ui_LabelSetText(aDebugUI.lod.ambientClipDistance, txt);

	// update active sources
	sndLODUpdateActiveSources(&aDebugUI.lod.activeSources);


	// update stopped sources
	//sndLODGetStoppedSources(sndLOD, &aDebugUI.lod.stoppedSources);
}


void aDebugLODActiveSourceEvent(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = aDebugUI.lod.activeSources;
	if(sources)
	{
		SoundSource *source;
		if(iRow == 0) // tmp hack to make sure the frame is up to date
		{	
			sndLODUpdateActiveSources(&aDebugUI.lod.activeSources);
		}

		if(iRow < eaSize(&sources))
		{
			source = sources[iRow];
			if(source->emd->ignoreLOD)
			{
				estrPrintf(estrOutput, "%d %s (IgnoreLOD)", iRow+1, source->obj.desc_name);
			}
			else
			{
				estrPrintf(estrOutput, "%d %s", iRow+1, source->obj.desc_name);
			}
		}
	}
}
void aDebugLODActiveSourceType(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = aDebugUI.lod.activeSources;
	if(sources)
	{
		if(iRow < eaSize(&sources))
		{
			SoundSource *source = sources[iRow];
			aDebugPrintSoundType(estrOutput, source->emd->type);
		}
	}
}

void aDebugLODActiveSourceMem(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	estrPrintf(estrOutput, "");
}

void aDebugLODActiveSourceDist(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = aDebugUI.lod.activeSources;
	if(sources)
	{
		if(iRow < eaSize(&sources))
		{

			SoundSource *source = sources[iRow];
			estrPrintf(estrOutput, "%.2f", source->distToListener);
		}
	}
}

void aDebugLODActiveSourcePriority(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	estrPrintf(estrOutput, "");
}




void aDebugLODInactiveSourceEvent(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = aDebugUI.lod.stoppedSources;
	if(sources)
	{
		//if(iRow < eaSize(&sources))
		//{
		//	SoundSource *source = sources[iRow];
		//	estrPrintf(estrOutput, "%d %s", iRow+1, source->obj.desc_name);
		//}
	}
}
void aDebugLODInactiveSourceType(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = aDebugUI.lod.stoppedSources;
	if(sources)
	{
		//if(iRow < eaSize(&sources))
		//{
		//	SoundSource *source = sources[iRow];
		//	aDebugPrintSoundType(estrOutput, source->emd->type);
		//}
	}
}

//void aDebugLODInactiveSourceMem(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
//{
//	estrPrintf(estrOutput, "");
//}
//
//void aDebugLODInactiveSourceDist(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
//{
//	SoundSource **sources = aDebugUI.lod.stoppedSources;
//	if(sources)
//	{
//		estrPrintf(estrOutput, "");
//
//		//SoundSource *source = sources[iRow];
//		//estrPrintf(estrOutput, "%.2f", source->distToListener);
//	}
//}
//
//void aDebugLODInactiveSourcePriority(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
//{
//	estrPrintf(estrOutput, "");
//}


void aDebugSetupLODTab(UITab *tab)
{
	//UICheckButton *freezeCheckButton;

	F32 col_1_x, col_2_x, col_3_x, y;
	F32 line_height = 25;
	F32 padding = 5;
	F32 text_width = 100.0;
	UIListColumn *col;
	char txt[16];

	aDebugUI.lod.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.lod.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	col_1_x = 5.0;
	col_2_x = 200.0;
	col_3_x = 320.0;
	y = 5.0;

	// Enabled
	aDebugUI.lod.enabledCheckButton = ui_CheckButtonCreate(col_1_x, y, "Enabled", false);
	ui_CheckButtonSetToggledCallback(aDebugUI.lod.enabledCheckButton, aDebugToggleLODEnabled, NULL);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.enabledCheckButton));

	y += line_height;

	// Current State
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(ui_LabelCreate("Current State", col_1_x, y)));

	aDebugUI.lod.currentStateLabel = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.currentStateLabel));

	y += line_height;

	// Last State
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(ui_LabelCreate("Last State", col_1_x, y)));

	aDebugUI.lod.lastStateLabel = ui_LabelCreate("Last State", col_2_x, y);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.lastStateLabel));

	y += line_height;

	// Memory Threshold
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(ui_LabelCreate("Memory Threshold", col_1_x, y)));

	aDebugUI.lod.thresholdEntry = ui_TextEntryCreate("", col_2_x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.lod.thresholdEntry), text_width, UIUnitFixed);
	ui_TextEntrySetFinishedCallback(aDebugUI.lod.thresholdEntry, aDebugUpdateLODMemoryThreshold, NULL);
	ui_PaneAddChild(aDebugUI.lod.rootPane, aDebugUI.lod.thresholdEntry);

	sprintf(txt, "%.2f", sndLODThreshold(&gSndLOD));
	ui_TextEntrySetText(aDebugUI.lod.thresholdEntry, txt);

	aDebugUI.lod.currentMemory = ui_LabelCreate("Current: 0.00", col_3_x, y);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.currentMemory));

	y += line_height;

	// Wait Below Threshold
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(ui_LabelCreate("Ambient Clip Distance", col_1_x, y)));

	//aDebugUI.lod.durToRaiseLevelEntry = ui_TextEntryCreate("", col_2_x, y);
	//ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.lod.durToRaiseLevelEntry), text_width, UIUnitFixed);
	//ui_TextEntrySetFinishedCallback(aDebugUI.lod.durToRaiseLevelEntry, aDebugUpdateLODWaitDuration, NULL);
	//ui_PaneAddChild(aDebugUI.lod.rootPane, aDebugUI.lod.durToRaiseLevelEntry);

	aDebugUI.lod.ambientClipDistance = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.ambientClipDistance));

	aDebugUI.lod.durationBelowThreshold = ui_LabelCreate("Time Below Threshold: 0.0s", col_3_x, y);
	ui_PaneAddChild(aDebugUI.lod.rootPane, UI_WIDGET(aDebugUI.lod.durationBelowThreshold));

	//sprintf_s(txt, 16, "%.2f", sndLODWaitDuration(&gSndLOD));
	//ui_TextEntrySetText(aDebugUI.lod.durToRaiseLevelEntry, txt);

	y += line_height;


	// Active Sources
	aDebugUI.lod.activeSourceList = ui_ListCreate(NULL, &aDebugUI.lod.activeSources, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.lod.activeSourceList), 1);
	//aDebugUI.lod.activeSourceList->fHeaderHeight = 0;

	col = ui_ListColumnCreate(UIListTextCallback, "Event", (intptr_t)aDebugLODActiveSourceEvent, NULL);
	ui_ListAppendColumn(aDebugUI.lod.activeSourceList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)aDebugLODActiveSourceType, NULL);
	ui_ListAppendColumn(aDebugUI.lod.activeSourceList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Distance", (intptr_t)aDebugLODActiveSourceDist, NULL);
	ui_ListAppendColumn(aDebugUI.lod.activeSourceList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Memory", (intptr_t)aDebugLODActiveSourceMem, NULL);
	ui_ListAppendColumn(aDebugUI.lod.activeSourceList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Priority", (intptr_t)aDebugLODActiveSourcePriority, NULL);
	ui_ListAppendColumn(aDebugUI.lod.activeSourceList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.lod.activeSourceList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.lod.activeSourceList), 0, 0, y, 40);

	ui_PaneAddChild(aDebugUI.lod.rootPane, aDebugUI.lod.activeSourceList);



	// Stopped Sources
	aDebugUI.lod.stoppedSourceList = ui_ListCreate(NULL, &aDebugUI.lod.stoppedSources, gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.lod.stoppedSourceList), 1);
	//aDebugUI.lod.activeSourceList->fHeaderHeight = 0;

	col = ui_ListColumnCreate(UIListTextCallback, "Event", (intptr_t)aDebugLODInactiveSourceEvent, NULL);
	ui_ListAppendColumn(aDebugUI.lod.stoppedSourceList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)aDebugLODInactiveSourceType, NULL);
	ui_ListAppendColumn(aDebugUI.lod.stoppedSourceList, col);

	//col = ui_ListColumnCreate(UIListTextCallback, "Memory", (intptr_t)aDebugLODInactiveSourceMem, NULL);
	//ui_ListAppendColumn(aDebugUI.lod.stoppedSourceList, col);

	//col = ui_ListColumnCreate(UIListTextCallback, "Distance", (intptr_t)aDebugLODInactiveSourceDist, NULL);
	//ui_ListAppendColumn(aDebugUI.lod.stoppedSourceList, col);

	//col = ui_ListColumnCreate(UIListTextCallback, "Priority", (intptr_t)aDebugLODInactiveSourcePriority, NULL);
	//ui_ListAppendColumn(aDebugUI.lod.stoppedSourceList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.lod.stoppedSourceList), 1.0, 35, UIUnitPercentage, UIUnitFixed);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.lod.stoppedSourceList), 0, 0, 250, 0));
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.lod.stoppedSourceList), 0, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.lod.rootPane, aDebugUI.lod.stoppedSourceList);


	ui_TabAddChild(tab, aDebugUI.lod.rootPane);
}

///////////////////////////////////////////////////////////////////////////////
// Sources
///////////////////////////////////////////////////////////////////////////////

SoundSourceGroup* aDebugSelectedSourceGroup()
{
	SoundSourceGroup *sourceGroup = NULL;
	if(!stashIntFindPointer(g_audio_state.sndSourceGroupTable, aDebugUI.sources.selectedSourceGroupId, &sourceGroup))
	{
		sourceGroup = NULL; // make sure it's NULL
	}
	return sourceGroup;
}

//void aDebugFilterSourceGroup(UIAnyWidget *widget, UserData userData)
//{
//
//}

static void aDebugSourceGroup(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int iRow, void *drawData)
{
	SoundSourceGroup **groups = (SoundSourceGroup**)(*pList->peaModel);

	if( groups && iRow < eaSize(&groups) )
	{
		SoundSourceGroup *sourceGroup = groups[iRow];
		
		if( eaSize(&sourceGroup->active_sources) > 0 ) 
		{ 
			if( !ui_ListIsSelected(pList, col, iRow) )
			{
				gfxfont_SetColorRGBA(0x005500FF, 0x005500FF);
			}
		}
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", sourceGroup->name);
	}
}

static void aDebugSourceName(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int iRow, void *drawData)
{
	SoundSource **sources = (SoundSource**)(*pList->peaModel);

	if( sources && iRow < eaSize(&sources) )
	{
		SoundSource *source = sources[iRow];

		if(eaFind(&space_state.sources, source) != -1) 
		{
			if( aDebugSourceStateAsInt(source) == 0 ) // is it active?
			{
				if( !ui_ListIsSelected(pList, col, iRow) )
				{
					gfxfont_SetColorRGBA(0x005500FF, 0x005500FF);
				}
			}

			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", source->obj.desc_name);
		}
	}
}

void aDebugSourceDistance(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = (SoundSource**)(*pList->peaModel);

	if( sources && iRow < eaSize(&sources) )
	{
		SoundSource *source = sources[iRow];

		if(eaFind(&space_state.sources, source) != -1) 
		{
			if(source->distToListener == FLT_MAX)
			{
				estrPrintf(estrOutput, "inf");
			}
			else
			{
				estrPrintf(estrOutput, "%.1f", source->distToListener);
			}
		}
	}
}

void aDebugSourceState(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = (SoundSource**)(*pList->peaModel);

	if( sources && iRow < eaSize(&sources) )
	{
		SoundSource *source = sources[iRow];

		if(eaFind(&space_state.sources, source) != -1) 
		{
			const char *sourceState = aDebugSourceStateAsString(source);

			if(sourceState)
			{
				estrPrintf(estrOutput, "%s", sourceState);
			}
		}
	}
}

void aDebugSourceSpace(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource **sources = (SoundSource**)(*pList->peaModel);

	if( sources && iRow < eaSize(&sources) )
	{
		SoundSource *source = sources[iRow];

		if(eaFind(&space_state.sources, source) != -1) 
		{
			if(source->originSpace)
			{
				estrPrintf(estrOutput, "%s", source->originSpace->obj.desc_name);
			}
		}
	}
}


//void aDebugSourceGroup(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
//{
//	SoundSourceGroup **groups = aDebugUI.sources.sourceGroups;
//
//	if( groups && iRow < eaSize(&groups) )
//	{
//		SoundSourceGroup *sourceGroup = groups[iRow];
//		estrPrintf(estrOutput, "%s", sourceGroup->name);
//	}
//}

void aDebugSourceGroupActiveCount(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSourceGroup **groups = aDebugUI.sources.sourceGroups;

	if( groups && iRow < eaSize(&groups) )
	{
		SoundSourceGroup *sourceGroup = groups[iRow];
		estrPrintf(estrOutput, "%d", eaSize(&sourceGroup->active_sources));
	}
}

void aDebugSourceGroupInactiveCount(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSourceGroup **groups = aDebugUI.sources.sourceGroups;

	if( groups && iRow < eaSize(&groups) )
	{
		SoundSourceGroup *sourceGroup = groups[iRow];
		estrPrintf(estrOutput, "%d", eaSize(&sourceGroup->inactive_sources));
	}
}

void aDebugSourceGroupDeadCount(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSourceGroup **groups = aDebugUI.sources.sourceGroups;

	if( groups && iRow < eaSize(&groups) )
	{
		SoundSourceGroup *sourceGroup = groups[iRow];
		estrPrintf(estrOutput, "%d", eaSize(&sourceGroup->dead_sources));
	}
}



void aDebugSource(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	if(pList && pList->peaModel) 
	{
		SoundSource **sources = (SoundSource**)*(pList->peaModel);
		if(sources && iRow < eaSize(&sources))
		{
			SoundSource *source = sources[iRow];
			if( eaFind(&space_state.sources, source) != -1 )
			{
				char clusterStr;
				char soundingStr;

				clusterStr = source->cluster ? 'C' : ' ';
				soundingStr = source->cluster ? (source->cluster->soundingSource == source ? '*' : ' ') : ' ';

				if(source->distToListener == FLT_MAX)
				{
					estrPrintf(estrOutput, "%02d %p %c%c [inf]", iRow+1, source, clusterStr, soundingStr);
				}
				else
				{
					estrPrintf(estrOutput, "%02d %p %c%c [%.1f ft]", iRow+1, source, clusterStr, soundingStr, source->distToListener);
				}
			}
		}
	}
}

int aDebugSourceStateAsInt(const SoundSource *source)
{
	int subGroupIndex = -1;
	if(source)
	{
		SoundSourceGroup *group = source->group;
		if(group)
		{
			if( eaFind(&group->active_sources, source) != -1 )
			{
				subGroupIndex = 0;
			}
			else if( eaFind(&group->inactive_sources, source) != -1 )
			{
				subGroupIndex = 1;
			}
			else if( eaFind(&group->dead_sources, source) != -1 )
			{
				subGroupIndex = 2;
			}
		}
	}

	return subGroupIndex;
}

const char *aDebugSourceStateAsString(SoundSource *source)
{
	const char *subGroupNames[] = { "Active", "Inactive", "Dead" };
	
	int subGroupIndex = aDebugSourceStateAsInt(source);

	if(subGroupIndex >= 0 && subGroupIndex < 3)
	{
		return subGroupNames[subGroupIndex];
	} 
	else
	{
		return NULL;
	}
}

void aDebugUpdateSelectedSourceLabel(SoundSource *source)
{
	static char txt[128];
	
	if(source && eaFind(&space_state.sources, source) != -1) 
	{
		const char *sourceState = aDebugSourceStateAsString(source);
		if(sourceState)
		{
			sprintf(txt, "Selected Source: %s [%s] (%p)", source->obj.desc_name, sourceState, source);
		}
	} 
	else 
	{
		sprintf(txt, "");
	}
	
	ui_LabelSetText(aDebugUI.sources.selectedSourceLabel, txt);
}

void aDebugUpdateSelectedSourceGroupInfo()
{
	SoundSourceGroup *sourceGroup;
	if(stashIntFindPointer(g_audio_state.sndSourceGroupTable, aDebugUI.sources.selectedSourceGroupId, &sourceGroup))
	{
		ui_LabelSetText(aDebugUI.sources.selectedSourceGroupLabel, sourceGroup->name);

		aDebugUI.sources.activeSourcesModel = sourceGroup->active_sources;
		aDebugUI.sources.inactiveSourcesModel = sourceGroup->inactive_sources;
		aDebugUI.sources.deadSourcesModel = sourceGroup->dead_sources;
	} 
	else 
	{
		ui_LabelSetText(aDebugUI.sources.selectedSourceGroupLabel, "");
		aDebugUI.sources.activeSourcesModel = NULL;
		aDebugUI.sources.inactiveSourcesModel = NULL;
		aDebugUI.sources.deadSourcesModel = NULL;
	}
}

void aDebugSourceGroupSelected(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	SoundSourceGroup **groups = aDebugUI.sources.sourceGroups;
	S32 selectedRow;

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	selectedRow = ui_ListGetSelectedRow(pList);
	if( selectedRow >= 0 && selectedRow < eaSize(&groups) )
	{
		SoundSourceGroup *sourceGroup = groups[selectedRow];
		if(sourceGroup->fmod_id != aDebugUI.sources.selectedSourceGroupId)
		{
			ui_ListClearSelected(aDebugUI.sources.activeSourcesList);
			ui_ListClearSelected(aDebugUI.sources.inactiveSourcesList);
			ui_ListClearSelected(aDebugUI.sources.deadSourcesList);
			ui_ListClearSelected(aDebugUI.sources.sourceProperties);
			ui_ListClearSelected(aDebugUI.sources.spaceProperties);
			aDebugUI.sources.selectedSource = NULL;
		}

		aDebugUI.sources.selectedSourceGroupId = sourceGroup->fmod_id;
		aDebugUpdateSelectedSourceGroupInfo();

	}
}


void aDebugSourceSelected(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	SoundSource *source = NULL;
	static char txt[128];

	ui_ListClearSelected(aDebugUI.sources.activeSourcesList);
	ui_ListClearSelected(aDebugUI.sources.inactiveSourcesList);
	ui_ListClearSelected(aDebugUI.sources.deadSourcesList);
	ui_ListClearSelected(aDebugUI.sources.sourceProperties);
	ui_ListClearSelected(aDebugUI.sources.spaceProperties);

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	aDebugUI.sources.selectedSource = NULL; // assume failure

	if(pList && pList->peaModel) 
	{
		SoundSource **sources = (SoundSource**)*(pList->peaModel);
		if(sources && iRow < eaSize(&sources))
		{
			source = sources[iRow];
			aDebugUI.sources.selectedSource = source;
		}
	}

	if(source)
	{
		if( eaFind(&space_state.sources, source) != -1 )
		{
			const char *sourceState = aDebugSourceStateAsString(source);
			if(sourceState)
			{
				sprintf(txt, "Selected Source: %s [%s] (%p)", source->obj.desc_name, sourceState, source);
			}
		}
	} 
	else 
	{
		sprintf(txt, "");
	}

	ui_LabelSetText(aDebugUI.sources.ungrouped.selectedSourceLabel, txt);
}

void aDebugSourceSelectedFromGroup(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	SoundSourceGroup *sourceGroup;

	ui_ListClearSelected(aDebugUI.sources.activeSourcesList);
	ui_ListClearSelected(aDebugUI.sources.inactiveSourcesList);
	ui_ListClearSelected(aDebugUI.sources.deadSourcesList);
	ui_ListClearSelected(aDebugUI.sources.sourceProperties);
	ui_ListClearSelected(aDebugUI.sources.spaceProperties);

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	aDebugUI.sources.selectedSource = NULL; // assume failure

	sourceGroup = aDebugSelectedSourceGroup(); // make sure we still have a valid group
	if(sourceGroup)
	{
		if(pList && pList->peaModel) 
		{
			SoundSource **sources = (SoundSource**)*(pList->peaModel);
			if(sources && iRow < eaSize(&sources))
			{
				SoundSource *source = sources[iRow];
				aDebugUI.sources.selectedSource = source;
			}
		}
	}

	aDebugUpdateSelectedSourceLabel(aDebugUI.sources.selectedSource);
}

void aDebugSourceSpaceProperty(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	if(aDebugUI.sources.selectedSource && iRow <= ADEBUG_SPACE_END_OF_LIST)
	{
		estrPrintf(estrOutput, "%s", aDebugSpacePropertyNames[iRow]);
	}
}

void aDebugSourceSpaceValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource *source = aDebugUI.sources.selectedSource;
	static bool okToRead = false;
	static SoundSource* readSource = NULL;

	if(readSource!=source)
	{
		okToRead = false;
		readSource = source;
	}

	if(iRow == 0)
	{
		okToRead = eaFind(&space_state.sources, source) != -1;	
	}

	if(okToRead)
	{
		S32 selectedTab = ui_TabGroupGetActiveIndex(aDebugUI.sources.spaceSelection);
		SoundSpace *space = NULL;
		switch(selectedTab)
		{
			case 0: space = source->ownerSpace; break;
			case 1: space = source->originSpace; break;
		}

		if(space)
		{
			aDebugDisplaySoundSpaceValues(space, iRow, estrOutput);
		}
	}
}

void aDebugMoveCameraToPosition(const Vec3 pos)
{
	//gclSetFreeCameraActive();
	GfxCameraController *camera = gfxGetActiveCameraController();
	gfxCameraControllerSetTarget(camera, pos);
	//camera->camdist = dist;
}

void aDebugActivateSpaceProperty(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{ 
	if( iRow == ADEBUG_SPACE_POSITION )
	{
		SoundSpace *space = aDebugUI.spaces.selectedSpace;

		if(space)
		{
			if(space->type == SST_VOLUME)
			{
				aDebugMoveCameraToPosition(space->volume.world_mid);
			}
			else if(space->type == SST_SPHERE)
			{
				aDebugMoveCameraToPosition(space->sphere.mid);
			}
		}
	} 
}


void aDebugSourceProperty(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	if(aDebugUI.sources.selectedSource && iRow <= ADEBUG_SOURCE_END_OF_LIST)
	{
		estrPrintf(estrOutput, "%s", aDebugSourcePropertyNames[iRow]);
	}
}

void aDebugEstrPrintVector(char **estrOutput, Vec3 vec)
{
	estrPrintf(estrOutput, "< %.2f, %.2f, %.2f >", vec[0], vec[1], vec[2]);
}

void aDebugSourceValue(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData userData, char **estrOutput)
{
	SoundSource *source = aDebugUI.sources.selectedSource;
	static const char *sourceTypes[] = { "Music", "Point", "Room", "UI" };
	static const char *origTypes[] = { "FX", "World", "Remote", "UI" };
	static bool okToRead = false;

	if(source) 
	{
		if(iRow == 0)
		{	
			// verify source still exists
			okToRead = eaFind(&space_state.sources, source) != -1;
		}

		if(okToRead)
		{
			switch(iRow)
			{
			case ADEBUG_SOURCE_CURRENT_AMP:
				estrPrintf(estrOutput, "%f", source->currentAmp);
				break;

			case ADEBUG_SOURCE_NUM_LINES:
				if(eaSize(&source->line.spaces))
				{
					estrPrintf(estrOutput, "%d (Group: %s)", eaSize(&source->line.spaces), source->line.group_str);
				}
				else
				{
					estrPrintf(estrOutput, "0");
				}
				break;
			case ADEBUG_SOURCE_TYPE:
				if(source->type >= 0 && source->type < 4) {
					estrPrintf(estrOutput, "%s", sourceTypes[source->type]);
				}
				break;
			case ADEBUG_SOURCE_ORIGIN:
				if(source->orig >= 0 && source->orig < 4) {
					estrPrintf(estrOutput, "%s", origTypes[source->orig]);
				}
				break;
			case ADEBUG_SOURCE_DESC_NAME:
				if(source->obj.desc_name) estrPrintf(estrOutput, "%s", source->obj.desc_name);
				break;
			case ADEBUG_SOURCE_CLUSTER:
				if(source->cluster)
				{
					SoundSourceCluster *cluster = source->cluster;
					char buffer[16];
					int j;
					int numSources = eaSize(&cluster->sources);

					estrPrintf(estrOutput, "Clustered (%d) <%.0f, %.0f, %.0f>", numSources, cluster->midPoint[0], cluster->midPoint[1], cluster->midPoint[2]);
					for(j = 0; j < numSources; j++)
					{
						if(cluster->sources[j] == cluster->soundingSource)
						{
							sprintf(buffer, " : %p*", cluster->sources[j]);
						}
						else
						{
							sprintf(buffer, " : %p", cluster->sources[j]);
						}

						estrConcat(estrOutput, buffer, (unsigned int)strlen(buffer));
					}
				}
				break;
			case ADEBUG_SOURCE_ORIG_NAME:
				if(source->obj.orig_name) estrPrintf(estrOutput, "%s", source->obj.orig_name);
				break;
			case ADEBUG_SOURCE_ORIGIN_FILENAME:
				if(source->obj.file_name) estrPrintf(estrOutput, "%s", source->obj.file_name);
				break;
			case ADEBUG_SOURCE_ORIGIN_POS:
				aDebugEstrPrintVector(estrOutput, source->origin_pos);
				break;
			case ADEBUG_SOURCE_VELOCITY:
				if(source->type==ST_POINT)
				{
					aDebugEstrPrintVector(estrOutput, source->point.vel);
				}
				break;
			case ADEBUG_SOURCE_VIRTUAL_POS:
				aDebugEstrPrintVector(estrOutput, source->virtual_pos);
				break;
			case ADEBUG_SOURCE_DIRECTIONALITY:
				estrPrintf(estrOutput, "%.3f", source->directionality);
				break;
			case ADEBUG_SOURCE_DIST_TO_LISTENER:
				estrPrintf(estrOutput, "%.2f", source->distToListener);
				break;
			case ADEBUG_SOURCE_STARTED:
				estrPrintf(estrOutput, "%c", source->started ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_STOPPED:
				estrPrintf(estrOutput, "%c", source->stopped ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_IN_AUDIBLE_SPACE:
				estrPrintf(estrOutput, "%c", source->in_audible_space ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_IS_AUDIBLE:
				estrPrintf(estrOutput, "%c", source->is_audible ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_STOLEN:
				estrPrintf(estrOutput, "%c", source->stolen ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_DEAD:
				estrPrintf(estrOutput, "%c", source->dead ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_CLEANUP:
				estrPrintf(estrOutput, "%c", source->clean_up ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_MUTED:
				estrPrintf(estrOutput, "%c", source->is_muted ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_VIRTUAL:
				estrPrintf(estrOutput, "%c", source->is_virtual ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_HIDDEN:
				estrPrintf(estrOutput, "%c", source->hidden ? 'Y' : 'N');
				break;


			case ADEBUG_SOURCE_DESTROYED:
				estrPrintf(estrOutput, "%c", source->destroyed ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_IMMEDIATE:
				estrPrintf(estrOutput, "%c", source->immediate ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_MOVED:
				estrPrintf(estrOutput, "%c", source->moved ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_UNMUTED:
				estrPrintf(estrOutput, "%c", source->unmuted ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_NEEDSSTART:
				estrPrintf(estrOutput, "%c", source->needs_start ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_NEEDSSTARTOFFSET:
				estrPrintf(estrOutput, "%c", source->needs_start_offset ? 'Y' : 'N');
				break;
			case ADEBUG_SOURCE_NEEDSMOVE:
				estrPrintf(estrOutput, "%c", source->needs_move ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_NEEDSCHANNELGROUP:
				estrPrintf(estrOutput, "%c", source->needs_channelgroup ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_NEEDSSTOP:
				estrPrintf(estrOutput, "%c", source->needs_stop ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_HASEVENT:
				estrPrintf(estrOutput, "%c", source->has_event ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_INSTDELETED:
				estrPrintf(estrOutput, "%c", source->inst_deleted ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_LODMUTED:
				estrPrintf(estrOutput, "%c", source->lod_muted ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_UPDATEPOSFROMENT:
				estrPrintf(estrOutput, "%c", source->updatePosFromEnt ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_LOCALPLAYER:
				estrPrintf(estrOutput, "%c", source->bLocalPlayer ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_STATIONARY:
				estrPrintf(estrOutput, "%c", source->stationary ? 'Y' : 'N');
				break;

			case ADEBUG_SOURCE_FADE_LEVEL:
				estrPrintf(estrOutput, "%.3f", source->fade_level);
				break;
			}
		}
	}
}


void aDebugSourcesSpaceTabChanged(UIAnyWidget *widget, UserData userData)
{

}

void aDebugUpdateSources()
{
	const unsigned char *filterStr;
	int i;
	bool hasFilter;
	bool addToList;
	int tabIndex;

	tabIndex = ui_TabGroupGetActiveIndex(aDebugUI.sources.tabs);
	switch(tabIndex)
	{
		case 1: 
		{
			eaClear(&aDebugUI.sources.ungrouped.filteredSources);

			filterStr = ui_TextEntryGetText(aDebugUI.sources.ungrouped.sourceFilter);
			hasFilter = filterStr && filterStr[0];

			for(i = eaSize(&space_state.sources) - 1; i >= 0; i--)
			{
				SoundSource *source = space_state.sources[i];

				addToList = true;

				if(hasFilter)
				{
					if(!strstri(source->obj.desc_name, filterStr)) // does not contain str
					{ 
						addToList = false;
					}
				}

				if(addToList)
				{
					eaPush(&aDebugUI.sources.ungrouped.filteredSources, source);
				}
			}

			aDebugSourcesSort();
		}
			break;

		case 0: // Groups
		{
			int numGroups;
			bool mustHaveSource;

			eaClear(&aDebugUI.sources.sourceGroups);
			
			mustHaveSource = ui_CheckButtonGetState(aDebugUI.sources.hasSourceCheckButton);

			filterStr = ui_TextEntryGetText(aDebugUI.sources.sourceGroupFilter);
			hasFilter = filterStr && filterStr[0];
			numGroups = eaSize(&space_state.source_groups);

			for(i = 0; i < numGroups; i++)
			{
				SoundSourceGroup *group;
				
				addToList = true;

				group = space_state.source_groups[i];
				
				if(hasFilter)
				{
					if(!strstri(group->name, filterStr)) // does not contain str
					{ 
						addToList = false;
					}
				}

				if(mustHaveSource)
				{
					if(eaSize(&group->active_sources) == 0 && eaSize(&group->inactive_sources) == 0 && eaSize(&group->dead_sources) == 0)
					{
						addToList = false;
					}
				}

				if(addToList)
				{
					eaPush(&aDebugUI.sources.sourceGroups, group);
				}
			} // end for
			break;
		} // end case
	} // end switch

	aDebugUpdateSelectedSourceGroupInfo();

	aDebugUpdateSelectedSourceLabel(aDebugGetValidSelectedSource());
}

SoundSource *aDebugGetValidSelectedSource()
{
	SoundSource *source = aDebugUI.sources.selectedSource;
	bool okToRead = eaFind(&space_state.sources, source) != -1;
	return okToRead ? source : NULL;
}

void aDebugActivateSourceProperty(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	SoundSource *source;
	if( iRow == ADEBUG_SOURCE_VIRTUAL_POS )
	{
		if(source = aDebugGetValidSelectedSource()) 
		{
			aDebugMoveCameraToPosition(source->virtual_pos);
		}
	} 
	else if( iRow == ADEBUG_SOURCE_ORIGIN_POS )
	{
		if(source = aDebugGetValidSelectedSource()) 
		{
			aDebugMoveCameraToPosition(source->origin_pos);
		}
	}
}

void aDebugDrawSource(SoundSource *source, int color, bool isActive)
{
	F32 *p = source->virtual_pos;

	aDebugDrawAxes(p, color);

	if(source->cluster)
	{
		// draw a line to the cluster's mid-point
		gfxDrawLine3DARGB(p, source->cluster->midPoint, 0xFF0099FF);

		if(source->cluster->soundingSource == source) 
		{
			gfxDrawSphere3DARGB(source->cluster->midPoint, 0.2, 24.0, 0xFF00CCFF, 1.0);
		}
	}

	if(aDebugUI.sources.selectedSource == source)
	{
		int selectedColor = isActive ? 0xFF009900 : 0xFF990000;
		Vec3 playerPos;
		sndGetPlayerPosition(playerPos);
		// draw a line to the cluster's mid-point
		gfxDrawLine3DARGB(playerPos, p, selectedColor);

		if(source->info_event)
		{
			F32 minRadius, maxRadius;

			minRadius = fmodEventGetMinRadius(source->info_event);
			maxRadius = fmodEventGetMaxRadius(source->info_event);
			
			gfxDrawSphere3DARGB(p, minRadius, 24.0, selectedColor, 1.0);
			gfxDrawSphere3DARGB(p, maxRadius, 24.0, selectedColor, 1.0);
		}
	}

	if(eaSize(&source->line.spaces) > 1)
	{
		Vec3 vMidPoint1 = {0};
		Vec3 vMidPoint2 = {0};
		SoundSpace *pSoundSpace;
		int iNumSpaces;
		int i;

		pSoundSpace = source->line.spaces[0];
		sndSpaceGetCenter(pSoundSpace, vMidPoint1);

		iNumSpaces = eaSize(&source->line.spaces);
		for(i = 1; i < iNumSpaces; i++)
		{
			pSoundSpace = source->line.spaces[i];
			
			sndSpaceGetCenter(pSoundSpace, vMidPoint2);
			
			gfxDrawLine3DARGB(vMidPoint1, vMidPoint2, 0xFFFFFF00);

			copyVec3(vMidPoint2, vMidPoint1);
		}
	}
	//gfxfont_SetColorRGBA(0xFFFFFFFF, 0x000000FF);
	//gfxfont_Printf(p[0], p[1], p[2], 1.0, 1.0, CENTER_Y, "%s", source->obj.desc_name);
}

void aDebugDrawSources(SoundSource **sources, int color, bool isActive)
{
	int i, numSources;
	numSources = eaSize(&sources);
	for(i = 0; i < numSources; i++) {
		aDebugDrawSource(sources[i], color, isActive);
	}
}
	

void aDebugDrawAllSources(void *userData)
{
	int i;
	bool drawActive, drawInactive, drawDead;

	int numGroups = eaSize(&space_state.source_groups);

	drawActive = ui_CheckButtonGetState(aDebugUI.sources.displayActiveSources);
	drawInactive = ui_CheckButtonGetState(aDebugUI.sources.displayInactiveSources);
	drawDead = ui_CheckButtonGetState(aDebugUI.sources.displayDeadSources);

	for(i = 0; i < numGroups; i++)
	{
		SoundSourceGroup *group = space_state.source_groups[i];

		if(drawActive) aDebugDrawSources(group->active_sources, 0xFF00FF00, true);
		if(drawInactive) aDebugDrawSources(group->inactive_sources, 0xFFFF0000, false);
		if(drawDead) aDebugDrawSources(group->dead_sources, 0xFFFF0000, false);
	}
}

void aDebugUpdateDisplaySourcesState()
{
	if(ui_CheckButtonGetState(aDebugUI.sources.displaySources))
	{
		aDebugUI.sources.drawAllCB = aDebugAddDrawCB(&aDebugUI.drawCallbacks, aDebugDrawAllSources, NULL, NULL);
	}
	else
	{
		aDebugRemoveDrawCB(&aDebugUI.drawCallbacks, aDebugUI.sources.drawAllCB);
	}
}


void aDebugDisplaySourcesChanged(UIAnyWidget *widget, UserData userData)
{
	// TODO(gt): enable / disable checkboxes
	//aDebugUI.sources.displayActiveSources
	//aDebugUI.sources.displayInactiveSources
	//aDebugUI.sources.displayDeadSources
	
	aDebugUpdateDisplaySourcesState();
}



// Distance ---

void aDebugSourceSortByDistanceDesc(UIListColumn* column, void* userData);
int aDebugSourceCompareDistance(const SoundSource** a, const SoundSource** b)
{
	if( (*b)->distToListener == FLT_MAX && (*a)->distToListener == FLT_MAX )
	{
		return 0;
	}
	if( (*b)->distToListener == FLT_MAX && (*a)->distToListener != FLT_MAX )
	{
		return 1;
	}
	if( (*b)->distToListener != FLT_MAX && (*a)->distToListener == FLT_MAX )
	{
		return -1;
	}

	return (*b)->distToListener - (*a)->distToListener;
}

int aDebugSourceCompareDistanceReverse(const SoundSource** a, const SoundSource** b)
{
	if( (*b)->distToListener == FLT_MAX && (*a)->distToListener == FLT_MAX )
	{
		return 0;
	}
	if( (*b)->distToListener == FLT_MAX && (*a)->distToListener != FLT_MAX )
	{
		return -1;
	}
	if( (*b)->distToListener != FLT_MAX && (*a)->distToListener == FLT_MAX )
	{
		return 1;
	}

	return (*a)->distToListener - (*b)->distToListener;
}

void aDebugSourceSortByDistance(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_DISTANCE);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortByDistanceDesc, NULL);
}

void aDebugSourceSortByDistanceDesc(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_DISTANCE_REV);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortByDistance, NULL);
}

// State ---

void aDebugSourceSortByStateDesc(UIListColumn* column, void* userData);
int aDebugSourceCompareState(const SoundSource** a, const SoundSource** b)
{
	return aDebugSourceStateAsInt(*b) - aDebugSourceStateAsInt(*a);
}

int aDebugSourceCompareStateReverse(const SoundSource** a, const SoundSource** b)
{
	return aDebugSourceStateAsInt(*a) - aDebugSourceStateAsInt(*b);
}

void aDebugSourceSortByState(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_STATE);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortByStateDesc, NULL);
}

void aDebugSourceSortByStateDesc(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_STATE_REV);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortByState, NULL);
}

// Space ---

void aDebugSourceSortBySpaceDesc(UIListColumn* column, void* userData);
int aDebugSourceCompareSpace(const SoundSource** a, const SoundSource** b)
{
	if((*a)->originSpace == NULL)
	{
		return 1;
	}
	if((*b)->originSpace == NULL)
	{
		return -1;
	}

	return strcmp( (*a)->originSpace->obj.desc_name, (*b)->originSpace->obj.desc_name );
}

int aDebugSourceCompareSpaceReverse(const SoundSource** a, const SoundSource** b)
{
	if((*a)->originSpace == NULL)
	{
		return -1;
	}
	if((*b)->originSpace == NULL)
	{
		return 1;
	}

	return strcmp( (*b)->originSpace->obj.desc_name, (*a)->originSpace->obj.desc_name );
}

void aDebugSourceSortBySpace(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_SPACE);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortBySpaceDesc, NULL);
}

void aDebugSourceSortBySpaceDesc(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_SPACE_REV);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortBySpace, NULL);
}

// Source Name ---

void aDebugSourceSortBySourceNameDesc(UIListColumn* column, void* userData);
int aDebugSourceCompareSourceName(const SoundSource** a, const SoundSource** b)
{
	return strcmp((*a)->obj.desc_name, (*b)->obj.desc_name);
}

int aDebugSourceCompareSourceNameReverse(const SoundSource** a, const SoundSource** b)
{
	return strcmp((*b)->obj.desc_name, (*a)->obj.desc_name);
}

void aDebugSourceSortBySourceName(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_NAME);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortBySourceNameDesc, NULL);
}

void aDebugSourceSortBySourceNameDesc(UIListColumn* column, void* userData)
{
	aDebugSourceSetSortMode(ADEBUG_SOURCE_SORT_NAME_REV);
	ui_ListColumnSetClickedCallback(column, aDebugSourceSortBySourceName, NULL);
}


void aDebugSourceSetSortMode(ADebugSourceSortMode mode)
{
	aDebugUI.sources.ungrouped.sortMode = mode;
}

void aDebugSourcesSort()
{
	switch(aDebugUI.sources.ungrouped.sortMode)
	{
	case ADEBUG_SOURCE_SORT_NAME:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareSourceName);
		break;
	case ADEBUG_SOURCE_SORT_NAME_REV:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareSourceNameReverse);
		break;
	case ADEBUG_SOURCE_SORT_DISTANCE:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareDistance);
		break;
	case ADEBUG_SOURCE_SORT_DISTANCE_REV:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareDistanceReverse);
		break;
	case ADEBUG_SOURCE_SORT_STATE:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareState);
		break;
	case ADEBUG_SOURCE_SORT_STATE_REV:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareStateReverse);
		break;
	case ADEBUG_SOURCE_SORT_SPACE:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareSpace);
		break;
	case ADEBUG_SOURCE_SORT_SPACE_REV:
		eaQSort(aDebugUI.sources.ungrouped.filteredSources, aDebugSourceCompareSpaceReverse);
		break;
	}
}


void aDebugSetupSourcesTab(UITab *tab)
{
	UIListColumn *col;
	F32 x, y;
	F32 source_list_x;
	F32 filter_label_width = 45; 
	F32 left_col_width = 275.0;
	F32 padding = 5.0;
	F32 tabWidth = 150.0;
	F32 rowHeight = gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP;
	F32 source_list_width = 200.0;
	F32 source_list_height = 200.0;
	F32 space_properties_height = 200.0;
	static void **fakeVarEArray = NULL;
	static void **fakeSpacePropertiesVarEArray = NULL;

	aDebugUI.sources.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.sources.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;


	x = 5;
	y = 5;


	// Create the Tab Group ///////////////////////////////////////////////////
	// Tabs [ Ungrouped | Grouped ]
	aDebugUI.sources.tabs = ui_TabGroupCreate(0, y, left_col_width, 250);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.tabs), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.tabs), 0, 0, 0, 50);//25); // pad bottom

	ui_PaneAddChild(aDebugUI.sources.rootPane, aDebugUI.sources.tabs);




	// Sources By Group Tab --
	aDebugUI.sources.groupTab = ui_TabCreate("By Group"); 
	ui_TabGroupAddTab(aDebugUI.sources.tabs, aDebugUI.sources.groupTab);

	// Filter (label)
	ui_TabAddChild(aDebugUI.sources.groupTab, ui_LabelCreate("Filter", x, y));

	// Search Entry
	aDebugUI.sources.sourceGroupFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.sources.sourceGroupFilter), left_col_width - filter_label_width - x, UIUnitFixed);
	//ui_TextEntrySetChangedCallback(aDebugUI.sources.sourceGroupFilter, aDebugFilterSourceGroup, aDebugUI.sources.sourceGroupFilter);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.sourceGroupFilter);
 
	// Has Source 'check'
	aDebugUI.sources.hasSourceCheckButton = ui_CheckButtonCreate(x, y + 25, "Has Source", true);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.hasSourceCheckButton);


	
	// Source Groups
	aDebugUI.sources.sourceGroupsList = ui_ListCreate(NULL, &aDebugUI.sources.sourceGroups, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.sourceGroupsList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.sources.sourceGroupsList, aDebugSourceGroupSelected, NULL);
	//aDebugUI.sources.sourceGroupsList->fHeaderHeight = 0;

	//col = ui_ListColumnCreate(UIListTextCallback, "Source Groups", (intptr_t)aDebugSourceGroup, NULL);
	col = ui_ListColumnCreateCallback("Source Groups", aDebugSourceGroup, NULL);
	ui_ListAppendColumn(aDebugUI.sources.sourceGroupsList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "A", (intptr_t)aDebugSourceGroupActiveCount, NULL);
	ui_ListColumnSetWidth(col, false, 20);
	ui_ListAppendColumn(aDebugUI.sources.sourceGroupsList, col);
	
	col = ui_ListColumnCreate(UIListTextCallback, "I", (intptr_t)aDebugSourceGroupInactiveCount, NULL);
	ui_ListColumnSetWidth(col, false, 20);
	ui_ListAppendColumn(aDebugUI.sources.sourceGroupsList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "D", (intptr_t)aDebugSourceGroupDeadCount, NULL);
	ui_ListColumnSetWidth(col, false, 20);
	ui_ListAppendColumn(aDebugUI.sources.sourceGroupsList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.sourceGroupsList), left_col_width, 1.0, UIUnitFixed, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.sourceGroupsList), x, 0, 0, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.sourceGroupsList), 0, 0, 60, 0);

	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.sourceGroupsList);




	// Display All Sources
	aDebugUI.sources.displaySources = ui_CheckButtonCreate(x, 0, "Display Sources", true);
	ui_CheckButtonSetToggledCallback(aDebugUI.sources.displaySources, aDebugDisplaySourcesChanged, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.displaySources), x, 25, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.sources.rootPane, aDebugUI.sources.displaySources);

	// active filter
	aDebugUI.sources.displayActiveSources = ui_CheckButtonCreate(x, 0, "Active", true);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.displayActiveSources), x, 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.sources.rootPane, aDebugUI.sources.displayActiveSources);

	// inactive filter
	aDebugUI.sources.displayInactiveSources = ui_CheckButtonCreate(x, 0, "Inactive", true);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.displayInactiveSources), x + left_col_width * (1.0/3.0), 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.sources.rootPane, aDebugUI.sources.displayInactiveSources);

	// dead filter
	aDebugUI.sources.displayDeadSources = ui_CheckButtonCreate(x, 0, "Dead", true);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.displayDeadSources), x + left_col_width * (2.0/3.0), 0, 0, 0, UIBottomLeft);
	ui_PaneAddChild(aDebugUI.sources.rootPane, aDebugUI.sources.displayDeadSources);

	aDebugUpdateDisplaySourcesState(); // inits







	// Selected Source Group Label
	aDebugUI.sources.selectedSourceGroupLabel = ui_LabelCreate("", x + left_col_width + padding, y);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.selectedSourceGroupLabel);


	source_list_x = x + left_col_width + padding;

	// Active Sources
	aDebugUI.sources.activeSourcesList = ui_ListCreate(NULL, &aDebugUI.sources.activeSourcesModel, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.activeSourcesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.sources.activeSourcesList, aDebugSourceSelectedFromGroup, NULL);
	col = ui_ListColumnCreate(UIListTextCallback, "Active Sources", (intptr_t)aDebugSource, NULL);
	ui_ListAppendColumn(aDebugUI.sources.activeSourcesList, col);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.activeSourcesList), source_list_width, source_list_height, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.activeSourcesList), source_list_x, y+25, 0, 0, UITopLeft);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.activeSourcesList), 0, 0, y + 30, 0);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.activeSourcesList);

	// Inactive Sources
	aDebugUI.sources.inactiveSourcesList = ui_ListCreate(NULL, &aDebugUI.sources.inactiveSourcesModel, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.inactiveSourcesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.sources.inactiveSourcesList, aDebugSourceSelectedFromGroup, NULL);
	col = ui_ListColumnCreate(UIListTextCallback, "Inactive Sources", (intptr_t)aDebugSource, NULL);
	ui_ListAppendColumn(aDebugUI.sources.inactiveSourcesList, col);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.inactiveSourcesList), source_list_width, source_list_height, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.inactiveSourcesList), source_list_x + source_list_width, y+25, 0, 0, UITopLeft);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.activeSourcesList), 0, 0, y + 30, 0);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.inactiveSourcesList);

	// Dead Sources
	aDebugUI.sources.deadSourcesList = ui_ListCreate(NULL, &aDebugUI.sources.deadSourcesModel, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.deadSourcesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.sources.deadSourcesList, aDebugSourceSelectedFromGroup, NULL);
	col = ui_ListColumnCreate(UIListTextCallback, "Dead Sources", (intptr_t)aDebugSource, NULL);
	ui_ListAppendColumn(aDebugUI.sources.deadSourcesList, col);
	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.deadSourcesList), source_list_width, source_list_height, UIUnitFixed, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.deadSourcesList), source_list_x + source_list_width*2, y+25, 0, 0, UITopLeft);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.activeSourcesList), 0, 0, y + 30, 0);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.deadSourcesList);

	// Selected Source Label
	aDebugUI.sources.selectedSourceLabel = ui_LabelCreate("", source_list_x, y + 30 + source_list_height);
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.selectedSourceLabel);


	// General Source Properties
	if(!fakeVarEArray) 
	{
		int i;
		eaSetSize(&fakeVarEArray, ADEBUG_SOURCE_END_OF_LIST);
		for(i=0; eaSize(&fakeVarEArray) < ADEBUG_SOURCE_END_OF_LIST; i++) eaPush(&fakeVarEArray, NULL);
	}

	aDebugUI.sources.sourceProperties = ui_ListCreate(NULL, &fakeVarEArray, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.sourceProperties), 1);
	ui_ListSetCellActivatedCallback(aDebugUI.sources.sourceProperties, aDebugActivateSourceProperty, NULL);



	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugSourceProperty, NULL);
	ui_ListAppendColumn(aDebugUI.sources.sourceProperties, col);
	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugSourceValue, NULL);
	ui_ListAppendColumn(aDebugUI.sources.sourceProperties, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.sourceProperties), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	//ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.sourceProperties), 0, 0, 0, 0, UIBottomLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.sourceProperties), source_list_x, 0, y + 55 + source_list_height, space_properties_height + 35);
 
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.sourceProperties);


 
	// Space Choice Tab
	aDebugUI.sources.spaceSelection = ui_TabGroupCreate(0, 0, 300, 30);
	//ui_TabGroupSetChangedCallback(aDebugUI.sources.spaceSelection, aDebugSourcesSpaceTabChanged, NULL);
	ui_TabGroupAddTab(aDebugUI.sources.spaceSelection, ui_TabCreate("Owner Space"));
	ui_TabGroupAddTab(aDebugUI.sources.spaceSelection, ui_TabCreate("Origin/Current Space"));
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.spaceSelection), source_list_x, space_properties_height, 0, 0, UIBottomLeft);
	//ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.spaceSelection), source_list_x, 0, 0, space_properties_height);
	ui_TabGroupSetActiveIndex(aDebugUI.sources.spaceSelection, 0); // select first tab
	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.spaceSelection);

	// Space Properties
	
	if(!fakeSpacePropertiesVarEArray) 
	{
		int i;
		eaSetSize(&fakeSpacePropertiesVarEArray, ADEBUG_SPACE_END_OF_LIST);
		for(i=0; eaSize(&fakeSpacePropertiesVarEArray) < ADEBUG_SPACE_END_OF_LIST; i++) eaPush(&fakeSpacePropertiesVarEArray, NULL);
	}


	aDebugUI.sources.spaceProperties = ui_ListCreate(NULL, &fakeSpacePropertiesVarEArray, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.spaceProperties), 1);
	ui_ListSetCellActivatedCallback(aDebugUI.sources.spaceProperties, aDebugActivateSpaceProperty, NULL);

	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugSourceSpaceProperty, NULL);
	ui_ListAppendColumn(aDebugUI.sources.spaceProperties, col);
	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugSourceSpaceValue, NULL);
	ui_ListAppendColumn(aDebugUI.sources.spaceProperties, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.spaceProperties), 1.0, space_properties_height, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.spaceProperties), 0, 0, 0, 0, UIBottomLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.spaceProperties), source_list_x, 0, 0, 0);

	ui_TabAddChild(aDebugUI.sources.groupTab, aDebugUI.sources.spaceProperties);





	// Sources By Group Tab --
	aDebugUI.sources.ungroupedTab = ui_TabCreate("All Sources"); 
	ui_TabGroupAddTab(aDebugUI.sources.tabs, aDebugUI.sources.ungroupedTab);


	// Filter (label)
	ui_TabAddChild(aDebugUI.sources.ungroupedTab, ui_LabelCreate("Filter", x, y));

	// Search Entry
	aDebugUI.sources.ungrouped.sourceFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourceFilter), left_col_width - filter_label_width - x, UIUnitFixed);
	//ui_TextEntrySetChangedCallback(aDebugUI.sources.sourceGroupFilter, aDebugFilterSourceGroup, aDebugUI.sources.sourceGroupFilter);
	ui_TabAddChild(aDebugUI.sources.ungroupedTab, aDebugUI.sources.ungrouped.sourceFilter);



	// Sources
	aDebugUI.sources.ungrouped.sourcesList = ui_ListCreate(NULL, &aDebugUI.sources.ungrouped.filteredSources, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.ungrouped.sourcesList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.sources.ungrouped.sourcesList, aDebugSourceSelected, NULL);
	//aDebugUI.sources.sourceGroupsList->fHeaderHeight = 0;

	//col = ui_ListColumnCreate(UIListTextCallback, "Source Groups", (intptr_t)aDebugSourceGroup, NULL);
	col = ui_ListColumnCreateCallback("Source", aDebugSourceName, NULL);
	ui_ListColumnSetSortable(col, true);
	ui_ListColumnSetWidth(col, false, 400);
	ui_ListColumnSetClickedCallback(col, aDebugSourceSortBySourceName, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourcesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Distance", (intptr_t)aDebugSourceDistance, NULL);
	ui_ListColumnSetWidth(col, false, 100);
	ui_ListColumnSetClickedCallback(col, aDebugSourceSortByDistance, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourcesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "State", (intptr_t)aDebugSourceState, NULL);
	ui_ListColumnSetWidth(col, false, 100);
	ui_ListColumnSetClickedCallback(col, aDebugSourceSortByState, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourcesList, col);

	col = ui_ListColumnCreate(UIListTextCallback, "Space", (intptr_t)aDebugSourceSpace, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListColumnSetClickedCallback(col, aDebugSourceSortBySpace, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourcesList, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourcesList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourcesList), x, 0, 0, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourcesList), 0, 0, 35, 300 + 30);

	ui_TabAddChild(aDebugUI.sources.ungroupedTab, aDebugUI.sources.ungrouped.sourcesList);


	source_list_x = x + left_col_width + padding;

	// Selected Source Label
	aDebugUI.sources.ungrouped.selectedSourceLabel = ui_LabelCreate("", 0, 0);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.ungrouped.selectedSourceLabel), x, 300, 0, 0, UIBottomLeft);
	ui_TabAddChild(aDebugUI.sources.ungroupedTab, aDebugUI.sources.ungrouped.selectedSourceLabel);

	// General Source Properties
	if(!fakeVarEArray) 
	{
		int i;
		eaSetSize(&fakeVarEArray, ADEBUG_SOURCE_END_OF_LIST);
		for(i=0; eaSize(&fakeVarEArray) < ADEBUG_SOURCE_END_OF_LIST; i++) eaPush(&fakeVarEArray, NULL);
	}

	aDebugUI.sources.ungrouped.sourceProperties = ui_ListCreate(NULL, &fakeVarEArray, rowHeight);
	ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.sources.ungrouped.sourceProperties), 1);
	ui_ListSetCellActivatedCallback(aDebugUI.sources.ungrouped.sourceProperties, aDebugActivateSourceProperty, NULL);

	col = ui_ListColumnCreate(UIListTextCallback, "Property", (intptr_t)aDebugSourceProperty, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourceProperties, col);
	col = ui_ListColumnCreate(UIListTextCallback, "Value", (intptr_t)aDebugSourceValue, NULL);
	ui_ListAppendColumn(aDebugUI.sources.ungrouped.sourceProperties, col);

	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourceProperties), 1.0, 300, UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPositionEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourceProperties), 0, 0, 0, 0, UIBottomLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.sources.ungrouped.sourceProperties), 0, 0, 0, 0);

	ui_TabAddChild(aDebugUI.sources.ungroupedTab, aDebugUI.sources.ungrouped.sourceProperties);



	ui_TabGroupSetActiveIndex(aDebugUI.sources.tabs, 0); // select first tab


	ui_TabAddChild(tab, aDebugUI.sources.rootPane);
}

///////////////////////////////////////////////////////////////////////////////
// Event Log
///////////////////////////////////////////////////////////////////////////////
bool aDebugLogEventPassesFilter(ADebugLogEvent *logEvent)
{
	bool result = true;
	
	//if(aDebugUI.eventLog.filterStr && aDebugUI.eventLog.filterStr[0]) 
	//{
	//	result = strstri(logEvent->name, aDebugUI.eventLog.filterStr) != NULL;
	//}

	//if(result)
	//{
	//	if(aDebugUI.eventLog.excludeFilterStr && aDebugUI.eventLog.excludeFilterStr[0])
	//	{
	//		result = strstri(logEvent->name, aDebugUI.eventLog.excludeFilterStr) == NULL; // must NOT contain string
	//	}
	//}

	if(ui_CheckButtonGetState(aDebugUI.eventLog.audibleButton))
	{
		if(logEvent->volume <= 0.0)
		{
			result = false;
		}
	}

	if(result)
	{
		static char **eaSearchStrings = NULL;
		static char **eaExcludeStrings = NULL;
		char searchStrings[128];
		char excludeStrings[128];
		char *name = logEvent->name;

		if(aDebugUI.eventLog.filterStr)
		{
			strncpy(searchStrings, aDebugUI.eventLog.filterStr, 127);
			if(searchStrings && searchStrings[0])
			{
				eaClear(&eaSearchStrings);
				aDebugSplitStringWithDelimFast(searchStrings, ",", &eaSearchStrings); 

				if(name == NULL)
				{
					result = false;
				}
				else
				{
					bool found = false;
					int j;
					for(j = eaSize(&eaSearchStrings)-1; j >= 0; j--)
					{
						char *str = eaSearchStrings[j];
						if(strstri(name, str))
						{
							found = true;
							break;
						}
					}
					if(!found) result = false;
				}

				//eaDestroy(&eaSearchStrings);

			}
		}

		if(aDebugUI.eventLog.excludeFilterStr)
		{
			strncpy(excludeStrings, aDebugUI.eventLog.excludeFilterStr, 127);
			
			if(excludeStrings && excludeStrings[0] && name)
			{
				bool found = false;
				int j;

				eaClear(&eaExcludeStrings);
				aDebugSplitStringWithDelim(excludeStrings, ",", &eaExcludeStrings);

				for(j = eaSize(&eaExcludeStrings)-1; j >= 0; j--)
				{
					char *str = eaExcludeStrings[j];
					if(strstri(name, str))
					{
						found = true;
						break;
					}
				}
				if(found) result = false;

				//eaDestroy(&eaExcludeStrings);
			}
		}
	}

	return result;
}

void aDebugFilterNewLogEvent(ADebugLogEvent *logEvent)
{
	if(ui_CheckButtonGetState(aDebugUI.eventLog.audibleButton))
	{
		// gotta continuously scan them... (given that volume changes)
		int numEntries = eaSize(&aDebugUI.eventLog.events);
		int i;

		eaClear(&aDebugUI.eventLog.filteredEvents);

		for(i = 0; i < numEntries; i++)
		{
			logEvent = aDebugUI.eventLog.events[i];
			if(aDebugLogEventPassesFilter(logEvent))
			{
				eaInsert(&aDebugUI.eventLog.filteredEvents, logEvent, 0);
			}
		}
	}
	else
	{
		if(aDebugLogEventPassesFilter(logEvent))
		{
			eaInsert(&aDebugUI.eventLog.filteredEvents, logEvent, 0);
		}
	}
}

void aDebugFilterEventLog()
{
	int numEntries = eaSize(&aDebugUI.eventLog.events);
	int i;
	
	eaClear(&aDebugUI.eventLog.filteredEvents);

	for(i = 0; i < numEntries; i++)
	{
		ADebugLogEvent *logEvent = aDebugUI.eventLog.events[i];
		aDebugFilterNewLogEvent(logEvent);
	}
}

void aDebugUpdateEventLogAudible(UIAnyWidget *widget, UserData userData)
{
	aDebugFilterEventLog();
}

void aDebugUpdateEventLogFilter(UIAnyWidget *widget, UserData userData)
{
	aDebugUI.eventLog.filterStr = ui_TextEntryGetText((UITextEntry*)widget);
	aDebugFilterEventLog();
}

void aDebugUpdateExcludeLogFilter(UIAnyWidget *widget, UserData userData)
{
	aDebugUI.eventLog.excludeFilterStr = ui_TextEntryGetText((UITextEntry*)widget);
	aDebugFilterEventLog();
}

void aDebugSoundSourceCreated(SoundSource *source, void *userData)
{
	if(aDebugUI.mainWindow && ui_CheckButtonGetState(aDebugUI.eventLog.loggingEnabled))
	{
		ADebugLogEvent *logEvent = calloc(1, sizeof(ADebugLogEvent));
		
		// copy what we can
		logEvent->source = source;
		logEvent->name = strdup(source->obj.desc_name);
		logEvent->startTimestamp = timerCpuMs();
		logEvent->eventType = source->emd ? source->emd->type : -1;
		logEvent->originFilename = strdup(source->obj.orig_name);
		logEvent->distToListener = source->distToListener;
		if(source->type == ST_POINT)
		{
			copyVec3(source->point.pos, logEvent->initPosition);
		}
		
		
		//if(source->originSpace)
		//{
		//	logEvent->originSpaceName = strdup(source->originSpace->obj.desc_name);
		//}
		

		stashAddPointer(aDebugUI.eventLog.sourceStash, source, logEvent, true);

		// run event through filter
		aDebugFilterNewLogEvent(logEvent);

		eaPush(&aDebugUI.eventLog.events, logEvent);
	}
}

void aDebugSoundSourceDestroyed(SoundSource *source, void *userData)
{
	ADebugLogEvent *logEvent;
	
	if(!aDebugUI.mainWindow) return;

	// check selected source
	if(aDebugUI.sources.selectedSource == source)
	{
		aDebugUI.sources.selectedSource = NULL;
	}

	if(stashFindPointer(aDebugUI.eventLog.sourceStash, source, &logEvent))
	{
		// SoundSource is being released, so copy what we want from it
		logEvent->source = NULL;
		logEvent->endTimestamp = timerCpuMs();
		copyVec3(source->virtual_pos, logEvent->finalPosition);
		logEvent->distToListener = source->distToListener;
		logEvent->volume = source->volume;
		logEvent->hadEvent = source->fmod_event != NULL;

		logEvent->bLocalPlayer = source->bLocalPlayer;

		//if(source->fmod_event)
		//{
		//	FmodEventInfo instanceInfo;
		//	fmodGetFmodEventInfo(source->fmod_event, &instanceInfo);
		//	logEvent->audibility = instanceInfo.audibility;
		//}

		if(source->originSpace)
		{
			logEvent->finalSpaceName = strdup(source->originSpace->obj.desc_name);
		}
		if(!logEvent->originSpaceName) 
		{
			logEvent->originSpaceName = logEvent->finalSpaceName;
		}
		stashRemovePointer(aDebugUI.eventLog.sourceStash, source, NULL);
	}
}

void aDebugReleaseLogEvent(ADebugLogEvent *logEvent)
{
	free(logEvent->name);
	free(logEvent->originFilename);
	//free(logEvent->originSpaceName);3
	free(logEvent->finalSpaceName);

	free(logEvent);
}

void aDebugClearLog(UIAnyWidget *widget, UserData userData)
{
	int numEvents = eaSize(&aDebugUI.eventLog.events);
	int i;

	for(i = 0; i < numEvents; i++)
	{
		aDebugReleaseLogEvent(aDebugUI.eventLog.events[i]);
	}
	eaClear(&aDebugUI.eventLog.events);
	stashTableClear(aDebugUI.eventLog.sourceStash);

	aDebugUI.eventLog.selectedLogEvent = NULL;

	aDebugFilterEventLog();
}

void aDebugEventLogStartTimestamp(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	estrPrintf(estrOutput, "%.2f", logEvent->startTimestamp*0.001f);
}

void aDebugEventLogDuration(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	if(logEvent->endTimestamp > 0)
	{
		estrPrintf(estrOutput, "%.2f", (logEvent->endTimestamp-logEvent->startTimestamp)*0.001f);
	}
	else
	{
		estrPrintf(estrOutput, "%.2f", (timerCpuMs() - logEvent->startTimestamp)*0.001f);
	}
}

static void aDebugEventLogName(UIList *pList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int iRow, void *drawData)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	if( !logEvent->source )
	{
		if( !ui_ListIsSelected(pList, col, iRow) )
		{
			gfxfont_SetColorRGBA(0x880000FF, 0x880000FF);
		}
	}
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", logEvent->name);
}

void aDebugEventLogType(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	aDebugPrintSoundType(estrOutput, logEvent->eventType);

	if(logEvent->eventType == SND_FX)
	{
		estrPrintf(estrOutput, "FX (%d)", logEvent->bLocalPlayer ? 1 : 0);
	}
}

void aDebugEventLogOrigin(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	estrPrintf(estrOutput, "%s", logEvent->originFilename);
}

void aDebugEventLogStartPos(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	const char *spaceName = NULL;

	if(logEvent->source && logEvent->source->originSpace)
	{
		spaceName = logEvent->source->originSpace->obj.desc_name;

		if(!logEvent->originSpaceName) 
		{
			logEvent->originSpaceName = strdup(spaceName); 
		}
	}
	
	if(spaceName)
	{
		estrPrintf(estrOutput, "%s <%.0f, %.0f, %.0f>", spaceName, logEvent->initPosition[0], logEvent->initPosition[1], logEvent->initPosition[2]);
	}
	else
	{
		estrPrintf(estrOutput, "%s <%.0f, %.0f, %.0f>", logEvent->originSpaceName, logEvent->initPosition[0], logEvent->initPosition[1], logEvent->initPosition[2]);
	}
}

void aDebugEventLogEndPos(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	if(!logEvent->source)
	{
		if(logEvent->finalSpaceName)
		{
			estrPrintf(estrOutput, "%s <%.0f, %.0f, %.0f>", logEvent->finalSpaceName, logEvent->finalPosition[0], logEvent->finalPosition[1], logEvent->finalPosition[2]);
		}
		else
		{
			estrPrintf(estrOutput, "<%.0f, %.0f, %.0f>", logEvent->finalPosition[0], logEvent->finalPosition[1], logEvent->finalPosition[2]);
		}
	} 
	else 
	{
		Vec3 pos;
		copyVec3(logEvent->source->virtual_pos, pos);
		//sndSourceGetOrigin(logEvent->source, pos);
		estrPrintf(estrOutput, "<%.0f, %.0f, %.0f>", pos[0], pos[1], pos[2]);
	}
}


void aDebugEventLogDistToListener(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	if(!logEvent->source)
	{
		estrPrintf(estrOutput, "%.3f", logEvent->distToListener);
	} 
	else 
	{
		estrPrintf(estrOutput, "%.3f", logEvent->source->distToListener);
	}
}

void aDebugEventLogVolume(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	if(!logEvent->source)
	{
		//estrPrintf(estrOutput, "%.3f  [%.2f]", logEvent->volume, logEvent->audibility);
		estrPrintf(estrOutput, "%.3f", logEvent->volume);
	} 
	else 
	{
		//if(logEvent->source->fmod_event)
		//{
		//	FmodEventInfo instanceInfo;
		//	F32 audibility;
		//	//F32 audibility_db;

		//	fmodGetFmodEventInfo(logEvent->source->fmod_event, &instanceInfo);

		//	audibility = instanceInfo.audibility;
		//	//audibility_db = AmpToDb(audibility);

		//	logEvent->audibility = audibility;
		//	//estrPrintf(estrOutput, "%.3f (%.2f)", logEvent->source->volume, audibility);
		//}
		estrPrintf(estrOutput, "%.3f", logEvent->source->volume);
	}
}


void aDebugDrawAxes(F32 *p, int color)
{
	int i;
	Vec3 s, e;

	for(i = 0; i < 3; i++)
	{
		copyVec3(p, s);
		copyVec3(p, e);

		s[i] += 0.2;	
		e[i] -= 0.2;

		gfxDrawLine3DARGB(s, e, color);
	}
}
void aDebugDrawEventSource(void *userData)
{
	ADebugLogEvent **logEventPtr = (ADebugLogEvent**)userData;
	ADebugLogEvent *logEvent = *logEventPtr;
	Vec3 player_pos;

	if(!logEvent) return;

	sndGetPlayerPosition(player_pos);

	gfxDrawLine3DARGB(player_pos, logEvent->initPosition, 0xAA00FF00);

	aDebugDrawAxes(logEvent->initPosition, 0xFF00FF00);

	if(!logEvent->source)
	{
		gfxDrawLine3DARGB(player_pos, logEvent->finalPosition, 0xAAFF0000);
		aDebugDrawAxes(logEvent->finalPosition, 0xAAFF0000);
	}
}

void aDebugLogEventSelected(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];

	// call the default handler
	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, pCellData);

	aDebugUI.eventLog.selectedLogEvent = logEvent;
}

void aDebugEventLogEventActivated(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ADebugLogEvent **logEvents = (ADebugLogEvent**)(*pList->peaModel);
	ADebugLogEvent *logEvent = logEvents[iRow];
	
	if(logEvent)
	{
		// note: Make sure the column matches

		if(iColumn == 6) // origin file
		{
			if(logEvent->originFilename && strstr(logEvent->originFilename, "/") != NULL)
			{
				char cFileNameBuf[512];
				if (fileLocateWrite(logEvent->originFilename, cFileNameBuf))
				{
					fileOpenWithEditor(cFileNameBuf);
				}
				else
				{
					Errorf("Could not find file %s for editing", logEvent->originFilename);
				}
			}
		}
		else if(iColumn == 2) // event name
		{
			// switch tab
			ui_TabGroupSetActiveIndex(aDebugUI.mainTabs, 1); // SELECT FMod Events Tab

			// enter search params
			ui_TextEntrySetText(aDebugUI.fmodEvents.searchEntry, logEvent->name);
			ui_CheckButtonSetState(aDebugUI.fmodEvents.isPlayingCheckButton, false);
			ui_CheckButtonSetState(aDebugUI.fmodEvents.expandAll, true);

			aDebugFilterFmodEvents(logEvent->name);

			aDebugUpdateExpandAllEventsTree(); // expand all
		}
		else if(iColumn == 7) // start point
		{
			aDebugMoveCameraToPosition(logEvent->initPosition);
		}
		else if(iColumn == 8) // end point
		{
			aDebugMoveCameraToPosition(logEvent->finalPosition);
		}
	}
}


void aDebugUpdateEventLog()
{
	char txt[64];
	int numEvents = eaSize(&aDebugUI.eventLog.events);
	int numFilteredEvents = eaSize(&aDebugUI.eventLog.filteredEvents);

	sprintf(txt, "Displaying %d of %d events", numFilteredEvents, numEvents);
	ui_LabelSetText(aDebugUI.eventLog.eventCountLabel, txt);
}

void aDebugSetupEventLogTab(UITab *tab)
{
	UIListColumn *col;
	F32 x, y;
	F32 filter_label_width = 60.0;
	F32 clear_button_width = 75.0;
	F32 padding = 5.0;
	F32 rowHeight = gfxfont_FontHeight(&g_font_Sans, 1.f) + UI_HSTEP;
	F32 name_entry_width = 200.0;

	aDebugUI.eventLog.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.eventLog.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	x = 5;
	y = 5;

	
	// Logging Enabled checkbox
	aDebugUI.eventLog.loggingEnabled = ui_CheckButtonCreate(x, y, "Enable Logging", false);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.loggingEnabled);

	// clear log button
	aDebugUI.eventLog.clearLogButton = ui_ButtonCreate("Clear Log", x + 130, y, aDebugClearLog, NULL);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.eventLog.clearLogButton), clear_button_width, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.clearLogButton);


	y += 28;

	// Filter (label)
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, ui_LabelCreate("Search", x, y));

	// Name Entry
	aDebugUI.eventLog.nameFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_TextEntrySetChangedCallback(aDebugUI.eventLog.nameFilter, aDebugUpdateEventLogFilter, aDebugUI.eventLog.nameFilter);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.eventLog.nameFilter), name_entry_width, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.nameFilter);

	x += filter_label_width + name_entry_width + 20;

	// Filter (label)
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, ui_LabelCreate("Exclude", x, y));

	// Name Entry
	aDebugUI.eventLog.excludeFilter = ui_TextEntryCreate("", x + filter_label_width, y);
	ui_TextEntrySetChangedCallback(aDebugUI.eventLog.excludeFilter, aDebugUpdateExcludeLogFilter, aDebugUI.eventLog.excludeFilter);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.eventLog.excludeFilter), name_entry_width, UIUnitFixed);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.excludeFilter);


	y += 28;
	x = 5;

	// Audible
	aDebugUI.eventLog.audibleButton = ui_CheckButtonCreate(x, y, "Is Audible (i.e. Volume > 0.0)", false);
	ui_CheckButtonSetToggledCallback(aDebugUI.eventLog.audibleButton, aDebugUpdateEventLogAudible, NULL);	
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.audibleButton);

	y += 28;
	x = 5;

	aDebugUI.eventLog.eventCountLabel = ui_LabelCreate("Displaying 0 of 0 events", x, y);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.eventCountLabel);

	y += 28;
	
	// Event List
	aDebugUI.eventLog.eventList = ui_ListCreate(NULL, &aDebugUI.eventLog.filteredEvents, rowHeight);
	//ui_WidgetSetClickThrough(UI_WIDGET(aDebugUI.eventLog.eventList), 1);
	ui_ListSetCellClickedCallback(aDebugUI.eventLog.eventList, aDebugLogEventSelected, NULL);
	ui_ListSetCellActivatedCallback(aDebugUI.eventLog.eventList, aDebugEventLogEventActivated, NULL);

	// COL 0
	col = ui_ListColumnCreate(UIListTextCallback, "Start Time", (intptr_t)aDebugEventLogStartTimestamp, NULL);
	ui_ListColumnSetWidth(col, false, 75);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 1
	col = ui_ListColumnCreate(UIListTextCallback, "Duration", (intptr_t)aDebugEventLogDuration, NULL);
	ui_ListColumnSetWidth(col, false, 75);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 2
	col = ui_ListColumnCreateCallback("Name", aDebugEventLogName, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 3
	col = ui_ListColumnCreate(UIListTextCallback, "Distance", (intptr_t)aDebugEventLogDistToListener, NULL);
	ui_ListColumnSetWidth(col, false, 80);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 4
	col = ui_ListColumnCreate(UIListTextCallback, "Volume", (intptr_t)aDebugEventLogVolume, NULL);
	ui_ListColumnSetWidth(col, false, 60);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 5
	col = ui_ListColumnCreate(UIListTextCallback, "Type", (intptr_t)aDebugEventLogType, NULL);
	ui_ListColumnSetWidth(col, false, 60);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 6
	col = ui_ListColumnCreate(UIListTextCallback, "Origin", (intptr_t)aDebugEventLogOrigin, NULL);
	ui_ListColumnSetWidth(col, false, 200);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 7
	col = ui_ListColumnCreate(UIListTextCallback, "Start", (intptr_t)aDebugEventLogStartPos, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);

	// COL 8
	col = ui_ListColumnCreate(UIListTextCallback, "End", (intptr_t)aDebugEventLogEndPos, NULL);
	ui_ListColumnSetWidth(col, false, 150);
	ui_ListAppendColumn(aDebugUI.eventLog.eventList, col);


	ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.eventLog.eventList), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(aDebugUI.eventLog.eventList), 0, 0, y, 0);
	ui_PaneAddChild(aDebugUI.eventLog.rootPane, aDebugUI.eventLog.eventList);

	ui_TabAddChild(tab, aDebugUI.eventLog.rootPane);

}

void aDebugSetupEventLogCallbacks()
{
	// make sure we have a source stash
	if(!aDebugUI.eventLog.sourceStash)
	{
		aDebugUI.eventLog.sourceStash = stashTableCreateAddress(100);
	}
	
	aDebugUI.eventLog.selectedLogEvent = NULL;
	if(!aDebugUI.eventLog.drawCB)
	{
		aDebugUI.eventLog.drawCB = aDebugAddDrawCB(&aDebugUI.drawCallbacks, aDebugDrawEventSource, NULL, &aDebugUI.eventLog.selectedLogEvent);
	}


	sndSourceSetCreatedCB(aDebugSoundSourceCreated, NULL);
	sndSourceSetDestroyedCB(aDebugSoundSourceDestroyed, NULL);
}

void aDebugReleaseEventLogCallbacks()
{
	sndSourceSetCreatedCB(NULL, NULL);
	sndSourceSetDestroyedCB(NULL, NULL);
	
	aDebugRemoveDrawCB(&aDebugUI.drawCallbacks, aDebugUI.eventLog.drawCB);
	aDebugUI.eventLog.drawCB = NULL;

	stashTableDestroy(aDebugUI.eventLog.sourceStash);
	aDebugUI.eventLog.sourceStash = NULL;
}


///////////////////////////////////////////////////////////////////////////////
// Settings
///////////////////////////////////////////////////////////////////////////////

void aDebugWindowOpacityChanged(UIAnyWidget *widget, bool bFinished, UserData userData)
{
	F32 val = ui_FloatSliderGetValue((UISlider*)widget);

	aDebugSetWindowOpacity(val);
}

// Setup the controls for the performance tab
void aDebugSetupSettingsTab(UITab *tab)
{
	F32 y;
	F32 col_1_x, col_2_x;
	F32 y_spacing;
	
	aDebugUI.settings.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.settings.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	col_1_x = 5.0;
	col_2_x = 155.0;
	y_spacing = 2;
	y = 5.0;

	ui_PaneAddChild(aDebugUI.settings.rootPane, ui_LabelCreate("Window Opacity", col_1_x, y));

	aDebugUI.settings.windowOpacity = ui_FloatSliderCreate(col_2_x, y, 100, 0, 1, 1);
	ui_SliderSetRange(aDebugUI.settings.windowOpacity, 0.0, 1.0, 0.01);
	ui_SliderSetChangedCallback(aDebugUI.settings.windowOpacity, aDebugWindowOpacityChanged, NULL);
	ui_SliderSetPolicy(aDebugUI.settings.windowOpacity, UISliderContinuous);
	ui_PaneAddChild(aDebugUI.settings.rootPane, UI_WIDGET(aDebugUI.settings.windowOpacity));				
	ui_SliderSetValue(aDebugUI.settings.windowOpacity, aDebugUI.mainSkin->background[0].a);
	


	ui_TabAddChild(tab, aDebugUI.settings.rootPane);
}




///////////////////////////////////////////////////////////////////////////////
// Drawing
///////////////////////////////////////////////////////////////////////////////


ADebugDrawCB* aDebugAddDrawCB(ADebugDrawCB ***drawCallbacks, ADebugDrawFunc drawFunc, ADebugDestroyDrawFunc destroyFunc, void *userData)
{
	ADebugDrawCB *drawCB;
	devassert(drawFunc);

	drawCB = calloc(1, sizeof(ADebugDrawCB));
	drawCB->drawFunc = drawFunc;
	drawCB->destroyDrawFunc = destroyFunc;
	drawCB->userData = userData;
	eaPush(drawCallbacks, drawCB);

	return drawCB;
}

void aDebugRemoveDrawCB(ADebugDrawCB ***drawCallbacks, ADebugDrawCB *drawCB)
{
	if(drawCB)
	{
		if(drawCB->destroyDrawFunc) drawCB->destroyDrawFunc(drawCB->userData);

		eaFindAndRemove(drawCallbacks, drawCB);
	}
}

void aDebugRemoveAllDrawCB(ADebugDrawCB ***drawCallbacks)
{
	int numDrawCB = eaSize(drawCallbacks);
	int i;

	for(i = 0; i < numDrawCB; i++)
	{
		aDebugRemoveDrawCB(drawCallbacks, (*drawCallbacks)[i]);
	}
}

void aDebugDraw(void) 
{
	if(aDebugUI.mainWindow)
	{
		int numDrawCB = eaSize(&aDebugUI.drawCallbacks);
		int i;

		for(i = 0; i < numDrawCB; i++)
		{
			ADebugDrawCB *drawCB = aDebugUI.drawCallbacks[i];
			drawCB->drawFunc(drawCB->userData);
		}
	}
}


// Ducking ----

void aDebugToggleFxDuckingEnabled(UIAnyWidget *widget, UserData userData)
{
	sndMixerSetFxDuckingEnabled(gSndMixer, ui_CheckButtonGetState((UICheckButton*)widget)); 
}

void aDebugUpdateFxDuckingRate(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *entry = (UITextEntry*)widget;
	const unsigned char *input;
	F32 val;
	char validatedStr[9];

	// get input
	input = ui_TextEntryGetText(entry);
	val = atof(input);

	// make the change to the system
	sndMixerSetFxDuckRate(gSndMixer, val);
	
	// make sure we update display 
	sprintf(validatedStr, "%.2f", sndMixerFxDuckRate(gSndMixer));
	ui_TextEntrySetText(entry, validatedStr);
}

void aDebugUpdateFxDuckingTarget(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *entry = (UITextEntry*)widget;
	const unsigned char *input;
	F32 val;
	char validatedStr[9];

	// get input
	input = ui_TextEntryGetText(entry);
	val = atof(input);

	// make the change to the system
	sndMixerSetDuckScaleTarget(gSndMixer, val);

	// make sure we update display 
	sprintf(validatedStr, "%.2f", sndMixerFxDuckScaleTarget(gSndMixer));
	ui_TextEntrySetText(entry, validatedStr);
}


void aDebugUpdateFxDuckingEventThreshold(UIAnyWidget *widget, UserData userData)
{
	UITextEntry *entry = (UITextEntry*)widget;
	const unsigned char *input;
	int val;
	char validatedStr[9];

	// get input
	input = ui_TextEntryGetText(entry);
	val = atoi(input);

	// make the change to the system
	sndMixerSetDuckNumEventThreshold(gSndMixer, val);

	// make sure we update display 
	sprintf(validatedStr, "%d", sndMixerFxDuckNumEventThreshold(gSndMixer));
	ui_TextEntrySetText(entry, validatedStr);
}

void aDebugUpdateDucking() 
{
	char txt[16];
	sprintf(txt, "%.3f", g_audio_state.fxDuckScaleFactor);
	ui_LabelSetText(aDebugUI.ducking.currentValue, txt);
}

void aDebugSetupDuckingTab(UITab *tab)
{
	F32 col_1_x, col_2_x, col_3_x, y;
	F32 line_height = 25;
	F32 padding = 5;
	F32 text_width = 100.0;
	
	char txt[16];

	aDebugUI.ducking.rootPane = ui_PaneCreate(0.0, 0.0, 1.0, 1.0, UIUnitPercentage, UIUnitPercentage, 0);
	aDebugUI.ducking.rootPane->widget.pOverrideSkin = aDebugUI.mainSkin;

	col_1_x = 5.0;
	col_2_x = 200.0;
	col_3_x = 320.0;
	y = 5.0;

	// Enabled
	aDebugUI.ducking.enabledButton = ui_CheckButtonCreate(col_1_x, y, "Fx Ducking Enabled", sndMixerIsFxDuckingEnabled(gSndMixer));
	ui_CheckButtonSetToggledCallback(aDebugUI.ducking.enabledButton, aDebugToggleFxDuckingEnabled, NULL);
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(aDebugUI.ducking.enabledButton));

	y += line_height;

	// Current Value
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(ui_LabelCreate("Fx Ducking Value", col_1_x, y)));

	aDebugUI.ducking.currentValue = ui_LabelCreate("", col_2_x, y);
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(aDebugUI.ducking.currentValue));

	y += line_height;

	// Rate
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(ui_LabelCreate("Fx Ducking Rate", col_1_x, y)));

	aDebugUI.ducking.rate = ui_TextEntryCreate("", col_2_x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.ducking.rate), text_width, UIUnitFixed);
	ui_TextEntrySetFinishedCallback(aDebugUI.ducking.rate, aDebugUpdateFxDuckingRate, NULL);
	ui_PaneAddChild(aDebugUI.ducking.rootPane, aDebugUI.ducking.rate);

	sprintf(txt, "%.2f", sndMixerFxDuckRate(gSndMixer));
	ui_TextEntrySetText(aDebugUI.ducking.rate, txt);

	y += line_height;

	// Target Value
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(ui_LabelCreate("Fx Ducking Target", col_1_x, y)));

	aDebugUI.ducking.targetScaleFactor = ui_TextEntryCreate("", col_2_x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.ducking.targetScaleFactor), text_width, UIUnitFixed);
	ui_TextEntrySetFinishedCallback(aDebugUI.ducking.targetScaleFactor, aDebugUpdateFxDuckingTarget, NULL);
	ui_PaneAddChild(aDebugUI.ducking.rootPane, aDebugUI.ducking.targetScaleFactor);

	sprintf(txt, "%.2f", sndMixerFxDuckScaleTarget(gSndMixer));
	ui_TextEntrySetText(aDebugUI.ducking.targetScaleFactor, txt);

	y += line_height;

	// Event Threshold
	ui_PaneAddChild(aDebugUI.ducking.rootPane, UI_WIDGET(ui_LabelCreate("Num Event Threshold", col_1_x, y)));

	aDebugUI.ducking.numEventThreshold = ui_TextEntryCreate("", col_2_x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(aDebugUI.ducking.numEventThreshold), text_width, UIUnitFixed);
	ui_TextEntrySetFinishedCallback(aDebugUI.ducking.numEventThreshold, aDebugUpdateFxDuckingEventThreshold, NULL);
	ui_PaneAddChild(aDebugUI.ducking.rootPane, aDebugUI.ducking.numEventThreshold);

	sprintf(txt, "%d", sndMixerFxDuckNumEventThreshold(gSndMixer));
	ui_TextEntrySetText(aDebugUI.ducking.numEventThreshold, txt);

	
	ui_TabAddChild(tab, aDebugUI.ducking.rootPane);
}


///////////////////////////////////////////////////////////////////////////////
// Main Window Setup & Functions
///////////////////////////////////////////////////////////////////////////////

void aDebugSetWindowOpacity(F32 opacity)
{
	if(aDebugUI.mainWindow)
	{
		U8 scaled;
		CLAMP(opacity, 0.0, 1.0);

		scaled = (U8)(opacity * 255.0); 
		aDebugUI.mainSkin->background[0].a = scaled;
	}
}


// Toggle command to hide or show the Audio Debugger
void aDebugUIToggle(void)
{
	if(aDebugUI.mainWindow)
	{
		aDebugReleaseEventLogCallbacks();

		aDebugRemoveAllDrawCB(&aDebugUI.drawCallbacks);

		// Destroy it all
		ui_WindowClose(aDebugUI.mainWindow);
	}
	else
	{
		F32 x, y, w, h, opacity;
		int lastTab;
		char title[] = "Audio Debugger";

		// Create the main window
		x = GamePrefGetFloat("ADebug.x", 5);
		y = GamePrefGetFloat("ADebug.y", 5);
		w = GamePrefGetFloat("ADebug.w", 600);
		h = GamePrefGetFloat("ADebug.h", 600);
		opacity = GamePrefGetFloat("ADebug.windowOpacity", 0.5);

		lastTab = GamePrefGetInt("ADebug.tab", 0);

		//sprintf_s(title, 64, "Audio Debugger [%s]", __DATE__);
		aDebugUI.mainWindow = ui_WindowCreate(title, x, y, w, h);
		aDebugUI.mainSkin = ui_SkinCreate(NULL); 
		aDebugUI.mainWindow->widget.pOverrideSkin = aDebugUI.mainSkin;

		aDebugSetWindowOpacity(opacity);
		ui_WindowSetCloseCallback(aDebugUI.mainWindow, aDebugUICloseCallback, NULL);
		

		// Create the Tab Group
		aDebugUI.mainTabs = ui_TabGroupCreate(0, 0, w, 250);
		ui_WidgetSetDimensionsEx(UI_WIDGET(aDebugUI.mainTabs), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);


		ui_TabGroupSetChangedCallback(aDebugUI.mainTabs, aDebugTabChanged, NULL);
	
		// Performance Tab
		aDebugUI.performanceTab = ui_TabCreate("Performance");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.performanceTab);

		aDebugSetupPerformanceTab(aDebugUI.performanceTab);

		// FMod Events Tab
		aDebugUI.fmodEventsTab = ui_TabCreate("FMod Events");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.fmodEventsTab);

		aDebugSetupFmodEventsTab(aDebugUI.fmodEventsTab);

		//aDebugUI.dspGraphTab = ui_TabCreate("DSP Graph");
		//ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.dspGraphTab);

		// Spaces
		aDebugUI.spacesTab = ui_TabCreate("Spaces");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.spacesTab);

		aDebugSetupSpacesTab(aDebugUI.spacesTab);


		// LOD
		aDebugUI.lodTab = ui_TabCreate("LOD");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.lodTab);

		aDebugSetupLODTab(aDebugUI.lodTab);


		// Sources
		aDebugUI.sourcesTab = ui_TabCreate("Sources");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.sourcesTab);

		aDebugSetupSourcesTab(aDebugUI.sourcesTab);


		// Event Logging
		aDebugUI.eventLogTab = ui_TabCreate("Event Log");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.eventLogTab);

		aDebugSetupEventLogTab(aDebugUI.eventLogTab);
		aDebugSetupEventLogCallbacks();
		

		// Event Logging
		aDebugUI.duckingTab = ui_TabCreate("Ducking");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.duckingTab);

		aDebugSetupDuckingTab(aDebugUI.duckingTab);
		


		// Settings
		aDebugUI.settingsTab = ui_TabCreate("Settings");
		ui_TabGroupAddTab(aDebugUI.mainTabs, aDebugUI.settingsTab);

		aDebugSetupSettingsTab(aDebugUI.settingsTab);
		

		// select the tab
		ui_TabGroupSetActiveIndex(aDebugUI.mainTabs, lastTab);

		
		// add the tabs to the window
		ui_WindowAddChild(aDebugUI.mainWindow, aDebugUI.mainTabs);



		// Display the window
		ui_WindowShowEx(aDebugUI.mainWindow, true);

	}
}

#endif

void svDebugUserGetName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceUser *user = eaGet(&g_VoiceState.users, iRow);

	if(!user)
	{
		estrPrintf(estrOutput, "OutOfBounds");
		return;
	}

	if(g_VoiceState.nameFromID && g_VoiceState.nameFromID(user->id))
	{
		estrPrintf(estrOutput, "%s", g_VoiceState.nameFromID(user->id));
		return;
	}

	estrPrintf(estrOutput, "%d", user->id);
}

extern StaticDefineInt RequestStateEnum[];

void svDebugChanGetState(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceChannel *chan = eaGet(&g_VoiceState.channels, iRow);
	const char* c;
	const char* d;

	if(!chan)
	{
		estrPrintf(estrOutput, "OutOfBounds");
		return;
	}

	c = StaticDefineIntRevLookup(RequestStateEnum, chan->connectState);
	d = StaticDefineIntRevLookup(RequestStateEnum, chan->disconnectState);
	estrPrintf(estrOutput, "C: %s - D: %s", c, d);
}

void svDebugChanGetVol(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceChannel *chan = eaGet(&g_VoiceState.channels, iRow);

	if(!chan)
	{
		estrPrintf(estrOutput, "OOB");
		return;
	}

	estrPrintf(estrOutput, "%f", chan->ext_state.volume);
}

void svDebugChanGetTransmit(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceChannel *chan = eaGet(&g_VoiceState.channels, iRow);

	if(!chan)
	{
		estrPrintf(estrOutput, "OOB");
		return;
	}

	estrPrintf(estrOutput, "%s", chan->ext_state.transmitting ? "Yes" : "No");
}

void svDebugUserChannelGetName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceChannelUser ***model = (VoiceChannelUser***)ui_ListGetModel(pList);
	VoiceChannelUser *vcu = eaGet(model, iRow);

	if(!vcu)
	{
		estrPrintf(estrOutput, "OUTOFBOUNDS");
		return;
	}

	estrPrintf(estrOutput, "%s", vcu->chan->internName);
}

void svDebugChannelUserGetName(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput)
{
	VoiceChannelUser ***model = (VoiceChannelUser***)ui_ListGetModel(pList);
	VoiceChannelUser *vcu = eaGet(model, iRow);

	if(!vcu)
	{
		estrPrintf(estrOutput, "OutOfBounds");
		return;
	}

	if(g_VoiceState.nameFromID && g_VoiceState.nameFromID(vcu->user->id))
	{
		estrPrintf(estrOutput, "%s", g_VoiceState.nameFromID(vcu->user->id));
		return;
	}

	estrPrintf(estrOutput, "%d", vcu->user->id);
}

//typedef void (*UIActivationFunc)(UIAnyWidget *, UserData);
void svDebugUIUserSelected(UIAnyWidget *widget, UserData data)
{
	VoiceUser *user;
	if(!svDebugUI.userChannels)
	{
		int col = 0;
		UIListColumn *uicol = NULL;
		svDebugUI.userChannels = ui_ListCreate(NULL, NULL, 20);
		ui_WidgetSetPositionEx(UI_WIDGET(svDebugUI.userChannels), 0, 0, 0, 0, UITopRight);
		ui_WidgetSetPaddingEx(UI_WIDGET(svDebugUI.userChannels), 0, 0, 40, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(svDebugUI.userChannels), 0.5, 0.5, UIUnitPercentage, UIUnitPercentage);
		
		uicol = ui_ListColumnCreate(UIListTextCallback, "Channel", (intptr_t)svDebugUserChannelGetName, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userChannels, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "isMutedByMe", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "MutedByMe", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userChannels, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "isMutedByOp", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "MutedByOp", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userChannels, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "volume", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Volume", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userChannels, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "energy", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Energy", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userChannels, uicol);
	}

	user = eaGet(&g_VoiceState.users, ui_ListGetSelectedRow(svDebugUI.userList));

	if(!user)
	{
		ui_ListSetModel(svDebugUI.userChannels, NULL, NULL);
		ui_WindowRemoveChild(svDebugUI.mainWnd, svDebugUI.userChannels);
		return;
	}

	ui_ListSetModel(svDebugUI.userChannels, parse_VoiceChannelUser, &user->channels);

	ui_WindowAddChild(svDebugUI.mainWnd, svDebugUI.userChannels);
}

void svDebugUIChannelSelected(UIAnyWidget *widget, UserData data)
{
	VoiceChannel *chan;
	if(!svDebugUI.channelUsers)
	{
		int col = 0;
		UIListColumn *uicol = NULL;
		svDebugUI.channelUsers = ui_ListCreate(NULL, NULL, 20);
		ui_WidgetSetPositionEx(UI_WIDGET(svDebugUI.channelUsers), 0, 0, 0, 0, UIBottomRight);
		ui_WidgetSetDimensionsEx(UI_WIDGET(svDebugUI.channelUsers), 0.5, 0.5, UIUnitPercentage, UIUnitPercentage);

		uicol = ui_ListColumnCreate(UIListTextCallback, "User", (intptr_t)svDebugChannelUserGetName, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.channelUsers, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "isMutedByMe", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "MutedByMe", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.channelUsers, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "isMutedByOp", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "MutedByOp", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.channelUsers, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "volume", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Volume", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.channelUsers, uicol);

		ParserFindColumn(parse_VoiceChannelUser, "energy", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Energy", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.channelUsers, uicol);
	}

	chan = eaGet(&g_VoiceState.channels, ui_ListGetSelectedRow(svDebugUI.chanList));

	if(!chan)
	{
		ui_ListSetModel(svDebugUI.channelUsers, NULL, NULL);
		ui_WindowRemoveChild(svDebugUI.mainWnd, svDebugUI.channelUsers);
		return;
	}

	ui_ListSetModel(svDebugUI.channelUsers, parse_VoiceChannelUser, &chan->allUsers);

	ui_WindowAddChild(svDebugUI.mainWnd, svDebugUI.channelUsers);
}

void svDebugUIUserLost(VoiceUser *user)
{
	if(svDebugUI.userList && ui_ListGetSelectedObject(svDebugUI.userList)==user)
		ui_ListClearSelected(svDebugUI.userList);
}

void svDebugUIChanLost(VoiceChannel *chan)
{
	if(svDebugUI.chanList && ui_ListGetSelectedObject(svDebugUI.chanList)==chan)
		ui_ListClearSelected(svDebugUI.chanList);
}

void svDebugUIToggle(void)
{
	if(svDebugUI.mainWnd)
	{
		ui_WidgetQueueFree(UI_WIDGET(svDebugUI.mainWnd));
		svDebugUI.mainWnd = NULL;
		svDebugUI.userList = NULL;
		svDebugUI.chanList = NULL;
		svDebugUI.userChannels = NULL;
		svDebugUI.channelUsers = NULL;
	}
	else
	{
		int col = 0;
		UIListColumn *uicol = NULL;
		svDebugUI.mainWnd = ui_WindowCreate("Voice Debug", 0, 0, 500, 500);

		svDebugUI.userList = ui_ListCreate(parse_VoiceUser, &g_VoiceState.users, 20);
		ui_ListSetSelectedCallback(svDebugUI.userList, svDebugUIUserSelected, NULL);
		ui_WidgetSetPositionEx(UI_WIDGET(svDebugUI.userList), 0, 0, 0, 0, UITopLeft);
		ui_WidgetSetPaddingEx(UI_WIDGET(svDebugUI.userList), 0, 0, 40, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(svDebugUI.userList), 0.5, 0.5, UIUnitPercentage, UIUnitPercentage);

		uicol = ui_ListColumnCreate(UIListTextCallback, "Name", (intptr_t)svDebugUserGetName, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userList, uicol);

		ParserFindColumn(parse_VoiceUser, "externName", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Vivox", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, false, 100);
		ui_ListAppendColumn(svDebugUI.userList, uicol);

		ParserFindColumn(parse_VoiceUser, "externURI", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "URI", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, false, 100);
		ui_ListAppendColumn(svDebugUI.userList, uicol);

		ParserFindColumn(parse_VoiceUser, "volume", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Volume", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userList, uicol);

		ParserFindColumn(parse_VoiceUser, "isMutedByMe", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Muted", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.userList, uicol);
		
		ui_WindowAddChild(svDebugUI.mainWnd, UI_WIDGET(svDebugUI.userList));

		//------

		svDebugUI.chanList = ui_ListCreate(parse_VoiceChannel, &g_VoiceState.channels, 20);
		ui_ListSetSelectedCallback(svDebugUI.chanList, svDebugUIChannelSelected, NULL);
		ui_WidgetSetPositionEx(UI_WIDGET(svDebugUI.chanList), 0, 0, 0, 0, UIBottomLeft);
		ui_WidgetSetDimensionsEx(UI_WIDGET(svDebugUI.chanList), 0.5, 0.5, UIUnitPercentage, UIUnitPercentage);

		ParserFindColumn(parse_VoiceChannel, "internName", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Name", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		uicol = ui_ListColumnCreate(UIListTextCallback, "Vol", (intptr_t)svDebugChanGetVol, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		uicol = ui_ListColumnCreate(UIListTextCallback, "Trans", (intptr_t)svDebugChanGetTransmit, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		uicol = ui_ListColumnCreate(UIListTextCallback, "State", (intptr_t)svDebugChanGetState, NULL);
		ui_ListColumnSetWidth(uicol, true, 0);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		ParserFindColumn(parse_VoiceChannel, "handle", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "handle", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, false, 100);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		ParserFindColumn(parse_VoiceChannel, "externName", &col);
		uicol = ui_ListColumnCreate(UIListPTIndex, "Vivox", (intptr_t)col, NULL);
		ui_ListColumnSetWidth(uicol, false, 100);
		ui_ListAppendColumn(svDebugUI.chanList, uicol);

		ui_WindowAddChild(svDebugUI.mainWnd, UI_WIDGET(svDebugUI.chanList));

		ui_WindowShow(svDebugUI.mainWnd);
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(7) ACMD_CLIENTONLY;
void aDebug(void)
{
#ifndef STUB_SOUNDLIB
	if(!g_audio_state.noaudio)
	{
		aDebugUIToggle();
	}
#endif
}

AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CLIENTONLY;
void svDebug(void)
{
	if(!g_audio_state.noaudio)
		svDebugUIToggle();
}

#include "adebug_c_ast.c"


