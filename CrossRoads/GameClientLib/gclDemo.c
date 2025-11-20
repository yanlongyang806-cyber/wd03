
#include "gclDemo.h"
#include "gclDemo_h_ast.h"
#include "GfxDebug.h"
#include "GameClientLib.h"
#include "gclCommandParse.h"
#include "structHist.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "EntityExtern.h"
#include "WorldColl.h"
#include "wlTime.h"
#include "gclCutscene.h"
#include "gclHandleMsg.h"
#include "gclEntityNet.h"
#include "EntityLib.h"
#include "utilitiesLib.h"
#include "Entity_h_ast.h"
#include "EntityIterator.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "EditLib.h"
#include "gclEntity.h"
#include "gfxCommandParse.h"
#include "gclCamera.h"
#include "gclUtils.h"
#include "structInternals.h"
#include "ResourceSystem_internal.h"
#include "inputKeyBind.h"
#include "UILib.h"
#include "TimedCallback.h"
#include "MemoryMonitor.h"
#include "GfxRecord_h_ast.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "cutscene_common.h"
#include "fileutil2.h"
#include "CutsceneDemoPlayEditor.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "EditLibUIUtil.h"
#include "EditorManagerPrivate.h"
#include "AnimList_Common.h"
#include "ResourceManager.h"
#include "GfxConsole.h"
#include "StringCache.h"
#include "mapstate_common.h"
#include "gclMapState.h"
#include "RdrState.h"
#include "wlPerf.h"
#include "ContinuousBuilderSupport.h"
#include "gclBaseStates.h"
#include "Sound_common.h"
#include "Soundlib.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_DemoPlayback););

typedef struct DemoHistory
{
	FrameCounts maxvalues;
	FrameCounts minvalues;
	FrameCounts sum;
	FrameCounts avg;
	int frames;

	FrameCounts * history;
	int history_length;
	int history_pos;
} DemoHistory;

static int demoEnableFullHistory = false;
AUTO_CMD_INT(demoEnableFullHistory, demoFullHistoryCSV) ACMD_CMDLINE;

static DemoHistory demo_history;

// Demo version of the parse table for SavedEntityData.
//
// Initialized at runtime.
ParseTable* parse_SavedEntityDataDemo;

//filename into which to write demo results
char gDemoResultsFileName[MAX_PATH]= "";
AUTO_CMD_STRING(gDemoResultsFileName, DemoResultsFileName) ACMD_CMDLINE;

//if true, quit when the demo playback completes
bool gbQuitOnDemoCompletion = false;
AUTO_CMD_INT(gbQuitOnDemoCompletion, QuitOnDemoCompletion) ACMD_CMDLINE;

static void demoEnsureLegacyMode( void );
static void demo_EntityUpdateRecordedPositions(void);
static void demo_Finished( void );
static void demo_playUber(
        CmdContext* context, SA_PARAM_NN_STR const char* file, U32 repeat, int fps,
        SA_PARAM_OP_STR const char* imageFilePrefix, SA_PARAM_OP_STR const char* imageFileExt );
void demo_playPlayback( void );
void demo_pausePlayback( void );
void demo_playPauseToggle( void );
void demo_slowPlayback( void );
void demo_ffPlayback( void );
void demo_quit( void );
static void demo_saveRecording(void);
static void demo_UpdateTimeTicker( void );

/// Total time playing back demos
/// -OR-
/// Timer while recording a demo.
static F32 s_timeElapsed;

/// Time that has elapsed in playing back a demo.  If a demo is
/// repeating, then this will be set to 0.0 multiple times.
static F32 s_demoPlaybackTimeElapsed;

/// The fixed number of frames per second to tick
/// -OR-
/// 0, to use a variable framerate.
U32 s_demoPlaybackFixedFps;
AUTO_CMD_INT( s_demoPlaybackFixedFps, demo_playback_fps ) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

/// NULL, when not saving out screenshots.
/// -OR-
/// The filename prefix for saving out screenshots.
static const char* s_demoScreenshotFileNamePrefix;

/// NULL, when not saving out screenshots.
/// -OR-
/// The filename extention for saving out screenshots.
/// -OR-
/// "depth", to save out the depth buffer
/// -OR-
/// "" (the empty string) to use the default format.
static const char* s_demoScreenshotFileNameExt;

/// The min depth to save out, when recording depth
static float s_demoDepthMin = 0.2;

/// The max depth to save out, when recording depth
static float s_demoDepthMax = 0.5;

/// If the last frame was absolute
///
/// This is needed because the current camera is flipped at the END of
/// the frame.
static bool recordingIsAbsolute;

static DemoRecording s_Demo;
static bool recording = false;

static int playbackTimesRemaining = 0;
static int playbackTimesTotal = 0;
static char* s_zonename = NULL;

/// The file name of the demo file being played.
///
/// NOTE: Before the demo file is loaded, this will just be the demo
/// name specified on the command line.  This might or might not be an
/// absolute path.  This might or might not have an extension.  This
/// might or might not exist.
static char* s_demo_filename = NULL;
static char s_demo_override_mapname[MAX_PATH];

/// If true, then record absolute camera positions, to allow precise playback comparisons.
static bool s_demoRecordExactCameraPositions = false;

static U32 s_start_frame;
static bool s_bDidDisplayResults=false;

/// If true, then show verbose dialogs and errors.
static bool demoVerbose = true;

/// The start time of playback from the demo.  This is done because
/// there are a whole bunch of bugs revealed at this time.  Out of
/// sight, out of mind.
static F32 demoPlaybackStart = 0.0f;

/// The end time of playback from the demo.  This is done to terminate
/// demo playback early for quick performance testing on long demos.
static F32 demoPlaybackEnd;

/// The time ticker, showing how far into the demo the playback is.
static UILabel* demoTimeTicker = NULL;

/// The root pane, needs to be available to make transparent
static UIPane* demoRootPane = NULL;

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
///
/// Skip this many seconds at the beginning of a demo.
AUTO_CMD_FLOAT(demoPlaybackStart,demo_playbackStart) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_FLOAT(demoPlaybackStart,demo_skipIntro) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
/// 
/// Cuts a demo short at the specified length (in seconds)
AUTO_CMD_FLOAT(demoPlaybackEnd, demo_playbackEnd) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_FLOAT(demoPlaybackEnd, demoLength) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
/// 
/// Show extra data.
AUTO_CMD_INT(demoVerbose, demoVerbose) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
///
/// Play the demo from START in seconds to END in seconds.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_playbackRange(float start, float end)
{
	demoPlaybackStart = start;
	demoPlaybackEnd = end;
}

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
///
/// Skip this many seconds at the beginning of a demo.
AUTO_CMD_STRING(s_demo_override_mapname, demo_override_map) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

/// SHOULD ONLY BE SPECIFIED ON THE COMMAND LINE.
///
/// If true, then record absolute camera positions, to allow precise playback comparisons.
AUTO_CMD_INT(s_demoRecordExactCameraPositions, demo_record_exact_camera_positions) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

static void demoErrorfCallback(ErrorMessage *msg, void *userdata);

bool demo_recording(void)
{
	return recording;
}
bool demo_playingBack(void)
{
	return (playbackTimesRemaining > 0) || (s_bDidDisplayResults);
}

DemoRecording* demo_GetInfo(char **demo_name/*out*/)
{
	if(!demo_playingBack() && !demo_recording())
		return NULL;
	if(demo_name)
		*demo_name = s_demo_filename;
	return &s_Demo;
}

F32 demo_GetPlaybackTimeElapsed()
{
	return s_demoPlaybackTimeElapsed;
}

static char *g_demo_save_timing;
// Sets the timer profile name to save timer results to
AUTO_COMMAND ACMD_CMDLINE;
void demoSaveTiming(const char *s)
{
	SAFE_FREE(g_demo_save_timing);
	if (s)
		g_demo_save_timing = strdup(s);
	else
		g_demo_save_timing = strdup("demo");
}


/// Start recording a demo, save it into FILE.
///
/// The demo will be saved in the demos/ data folder, as FILE.demo.
/// Note that not all events are saved into the demo, but most are.
AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 );
void demo_record(const char *file)
{
	char fname_buf[1024];

	if( isProductionEditMode() ) {
		// alert the user
		conPrintf("Demo Record only available when playing a published map.");
		return;
	}
	if( recording ) {
		conPrintf("Already recording -- run demo_record_stop to finish recording.");
		return;
	}
	
	recording = true;
	recordingIsAbsolute = (gGCLState.pPrimaryDevice->activecamera != &gGCLState.pPrimaryDevice->gamecamera);
	playbackTimesRemaining = 0;
    playbackTimesTotal = 0;

	// Construct the demo file's full path
	if(file && file[0])
		sprintf(fname_buf, "%s/%s", fileDemoDir(), file);
	else
		sprintf(fname_buf, "%s/%s", fileDemoDir(), "last_recording.demo");

	if (!strchr(fname_buf+1, '.'))
		strcat(fname_buf, ".demo");

	resSetNotificationHookForDemo(demo_RecordResourceUpdated, NULL);

	if(s_demo_filename)
		StructFreeString(s_demo_filename);
	s_demo_filename = StructAllocString(fname_buf);

	// See if there's already a loaded map.
	// If not, the game client will start recording when the map is done loading.  if so, we should start recording immediately
	if(s_zonename != NULL)
	{
		demo_startRecording();
	}
}

/// Start recording a demo, save it to a timestamp-named file.
AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(demo_record);
void demo_record_auto(void)
{
	char datestr[100];
	timeMakeLocalDateStringFromSecondsSince2000( datestr,timeSecondsSince2000() );
	{
		char* s;
		for(s=datestr;*s;s++) {
			if (*s == ':' || *s == ' ')
				*s = '-';
		}
	}
	

	demo_record( datestr );
}

/// Set off a cutscene and then start recording.
AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 9 );
void demo_record_cutscene(CmdContext *cmd_context, const char *file, const char *cutscene)
{
	ServerCmd_cutsceneStartFromClientCommand(cutscene);
	demo_record(file);
}

/// Stop recording any demos started earlier with DEMO-RECORD.
///
/// The demo will be saved into the filename previously specified.
AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 );
void demo_record_stop(CmdContext *cmd_context)
{
	s_Demo.endTime = timeSecondsSince2000();
	demo_saveRecording();

	resSetNotificationHookForDemo(NULL, NULL);
	
	recording = false;
}

// ------------------------------------------------------------------
// Old backward compatibility structures -- DO NOT EVER CHANGE THISE.
AUTO_STRUCT AST_IGNORE(hReferencedCostume) AST_NO_PREFIX_STRIP;
typedef struct CostumeRefV0
{
	DirtyBit dirtyBit;								AST(NAME(dirtyBit))
	U32 dirtiedCount;								AST(NAME(dirtiedCount))

	// The rule is to first look at the effective costume.  This is only present if powers/equipment alter the costume.
	// Then look at the stored costume, which is set for persisted entities
	// Then look at the substitute costume, which is used by special critters like Nemesis ones.
	// Then look at the referenced costume, which is what most critter entities will have
	// Destructible critters have no player costume.  They have a string name of the object it pulled geo from.
	PlayerCostumeV0 *pEffectiveCostume;				AST(NAME(pEffectiveCostume))
	PlayerCostumeV0 *pStoredCostume;				AST(NAME(pStoredCostume))
	PlayerCostumeV0 *pSubstituteCostume;			AST(NAME(pSubstituteCostume))
	REF_TO(PlayerCostume) hReferencedCostume;		AST(NAME(hReferencedCostume))
	const char *pcDestructibleObjectCostume;		AST(NAME(pcDestructibleObjectCostume))

	// Mood is tracked outside the costume and is applied during costume generate
	REF_TO(PCMood) hMood;							AST(NAME(hMood))

	// Additional FX can be set up in the world and are not persisted
	PCFXNoPersist **eaAdditionalFX;					AST(NAME(eaAdditionalFX))
} CostumeRefV0;

extern ParseTable parse_CostumeRefV0[];
#define TYPE_parse_CostumeRefV0 CostumeRefV0

static void upgradeCostumeRefV0toV5(NOCONST(CostumeRef)* v1, CostumeRefV0* v0)
{
	v1->dirtyBit = v0->dirtyBit;
	v1->dirtiedCount = v0->dirtiedCount;

	v1->pEffectiveCostume = costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, v0->pEffectiveCostume));
	v1->pStoredCostume = costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, v0->pStoredCostume));
	v1->pSubstituteCostume = costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, v0->pSubstituteCostume));
	COPY_HANDLE(v1->hReferencedCostume, v0->hReferencedCostume);
	v1->pcDestructibleObjectCostume = StructAllocString(v0->pcDestructibleObjectCostume);
	
	COPY_HANDLE(v1->hMood, v0->hMood);

	eaCopyStructs(&v0->eaAdditionalFX, &v1->eaAdditionalFX, parse_PCFXNoPersist);
}

TextParserResult fixupRecordedEntity(RecordedEntity *ent, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_POST_TEXT_READ)
	{
		if (ent->entityTypeOld)
		{
			assert(!ent->entityTypeEnum);
			switch (ent->entityTypeOld)
			{
				xcase 19:
					ent->entityTypeEnum = GLOBALTYPE_ENTITYPLAYER;
					ent->entityTypeOld = 0;
				xcase 20:
					ent->entityTypeEnum = GLOBALTYPE_ENTITYCRITTER;
					ent->entityTypeOld = 0;
				xcase 21:
					ent->entityTypeEnum = GLOBALTYPE_ENTITYSAVEDPET;
					ent->entityTypeOld = 0;
				xdefault:
					Errorf("Unknown old entity type %d", ent->entityTypeOld);
			}
		}

		if( ent->costumeV0 ) {
			upgradeCostumeRefV0toV5(CONTAINER_NOCONST(CostumeRef, &ent->costumeV5), ent->costumeV0);
			StructDestroySafe(parse_CostumeRefV0, &ent->costumeV0);
		}
	}
	return PARSERESULT_SUCCESS;
}

TextParserResult fixupRecordedEntityCostumeChange(RecordedEntityCostumeChange *costumeChange, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_POST_TEXT_READ)
	{
		if( costumeChange->costumeV0 ) {
			upgradeCostumeRefV0toV5(CONTAINER_NOCONST(CostumeRef, &costumeChange->costume), costumeChange->costumeV0);
			StructDestroySafe(parse_CostumeRefV0, &costumeChange->costumeV0);
		}
	}
	return PARSERESULT_SUCCESS;
}

static bool demo_prune(DemoRecording *demo)
{
	bool bPruned=false;
	FOR_EACH_IN_EARRAY_FORWARDS(demo->packets, RecordedMMPacket, packet)
	{
		FOR_EACH_IN_EARRAY(packet->updates, RecordedEntityUpdate, update)
		{
			// Look for duplicate positions
			int i;
			bool bFoundLastUpdate=false;
			bool bDeleteMe=false;
			for (i=eaSize(&update->positions)-1; i>=1; i--) 
			{
				if (0==StructCompare(parse_RecordedEntityPos, update->positions[i], update->positions[i-1], 0, 0, 0))
				{
					StructDestroySafe(parse_RecordedEntityPos, &(update->positions[i]));
					eaRemove(&update->positions, i);
					bPruned = true;
				}
			}
			// Check to make sure it's not created on this frame, if it was, we can't remove the update
			FOR_EACH_IN_EARRAY(packet->createdEnts, RecordedEntity, ent)
			{
				if ((EntityRef)ent->entityRef == update->entityRef)
					bFoundLastUpdate = true;
			}
			FOR_EACH_END;

			// Look back and find the last update for this entity, and if it's the same, remove this one
			for (i=ipacketIndex-1; i>=0 && !bFoundLastUpdate; i--)
			{
				FOR_EACH_IN_EARRAY(demo->packets[i]->createdEnts, RecordedEntity, ent)
				{
					if ((EntityRef)ent->entityRef == update->entityRef)
						bFoundLastUpdate = true;
				}
				FOR_EACH_END;
				if (bFoundLastUpdate)
					break;
				FOR_EACH_IN_EARRAY(demo->packets[i]->updates, RecordedEntityUpdate, update2)
				{
					if (update2->entityRef == update->entityRef)
					{
						bFoundLastUpdate = true;
						if (0==StructCompare(parse_RecordedEntityUpdate, update, update2, 0, 0, 0))
						{
							bDeleteMe = true;
						}
						break;
					}
				}
				FOR_EACH_END;
			}
			if (bDeleteMe)
			{
				StructDestroySafe(parse_RecordedEntityUpdate, &update);
				eaRemove(&packet->updates, iupdateIndex);
				bPruned = true;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	return bPruned;
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play(CmdContext *cmd_context, char *file)
{
    demo_playUber( cmd_context, file, 1, 0, NULL, NULL );
}



static void setDemoOptionsAtCmdParseTime()
{
	disableEditors(true);
	resClientDisableAllRequests();
	globCmdParse("noPopUps 1");
	globCmdParse("dontLogErrors 1");
	globCmdParse("HideChatBubbles 1");
	globCmdParse("HideEntityHUD 1");
	globCmdParse("HideDamage 1");
}


/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo, and repeat it REPEAT times.
/// All stats before the second playthrough will be discarded to
/// remove loading time seconds.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Profile ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play_repeat(CmdContext *cmd_context, char *file, U32 repeat)
{
    demo_playUber( cmd_context, file, repeat, 0, NULL, NULL );
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo, as in DEMO-PLAY, but lock the
/// logic to 60 fps.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Profile ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play_60fps( CmdContext* cmdContext, char* file )
{
    demo_playUber( cmdContext, file, 1, 60, NULL, NULL );
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo, as in DEMO-PLAY-REPEAT, but
/// lock the logic to 60fps.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Profile ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play_repeat_60fps(CmdContext *cmdContext, char *file, U32 repeatCount)
{
    demo_playUber( cmdContext, file, repeatCount, 60, NULL, NULL );
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo, as in DEMO-PLAY, but lock the
/// logic to 30 fps.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Profile ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play_30fps( CmdContext* cmdContext, char* file )
{
    demo_playUber( cmdContext, file, 1, 30, NULL, NULL );
}


/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Playback a previously recorded demo, as in DEMO-PLAY-REPEAT, but lock the
/// logic to 30 fps.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY( Profile ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_play_repeat_30fps(CmdContext *cmdContext, char *file, U32 repeatCount)
{
    demo_playUber( cmdContext, file, repeatCount, 30, NULL, NULL );
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Save out the demo file as a set of images.  Images will be named
/// as image_file_prefix_00001.jpg, image_file_prefix_00002.jpg, etc.
///
/// Images are saved at 30fps.
AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_save_images(
        CmdContext* cmdContext, SA_PARAM_NN_STR const char* demoFile,
		SA_PARAM_OP_STR char* imageFilePrefix, SA_PARAM_OP_STR char* imageFileExt ) {
    demo_playUber( cmdContext, demoFile, 1, 30,
                   (imageFilePrefix ? imageFilePrefix : ""),
                   (imageFileExt ? imageFileExt : "jpg") );
}

/// SHOULD ONLY BE USED ON THE COMMAND LINE.
///
/// Save out the demo file as a set of images.  Sets a lot of options
/// to make the demo look pretty.
AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_movie_save( CmdContext* cmdContext, const char* demoFile )
{
	char buffer[ 256 ];
	getFileNameNoExt( buffer, demoFile );
	demo_playUber( cmdContext, demoFile, 1, 30, buffer, "jpg" );
	globCmdParse( "-windowedSize 1620 975" );
	globCmdParse( "-renderSize 1620 975" );
	globCmdParse( "-showLessAnnoyingAccessLevelWarning 1");
	globCmdParse( "-renderScale 2" );
	globCmdParse( "-fov 65" );
	globCmdParse( "-screenshotAfterRenderscale 1" );
	globCmdParse( "-disableTexReduce 1" );
	globCmdParse( "-visscale 6" );
	globCmdParse( "-reduceMip 0" );
	globCmdParse( "-highQualityDOF" );
	globCmdParse( "-postprocessing 1" );
	globCmdParse( "-terrainDetail 9" );
	globCmdParse( "-shadows 1" );
	globCmdParse( "-ui_ToggleHUD 0" );
	globCmdParse( "-ShowChatBubbles 0" );
	globCmdParse( "-ShowFloatingText 0" );
	globCmdParse( "-ShowEntityUI 0" );
	globCmdParse( "-pssmShadowMapSize 2048" );
	globCmdParse( "-pssmNearShadowRes 64" );
	globCmdParse( "-bloomQuality 2" );
	globCmdParse( "-showfps 0" );
	globCmdParse( "-dfxoff 0" );
}

AUTO_COMMAND ACMD_CATEGORY( Standard ) ACMD_ACCESSLEVEL( 0 ) ACMD_EARLYCOMMANDLINE;
void demo_depth_range( float depth_min, float depth_max )
{
	s_demoDepthMin = depth_min;
	s_demoDepthMax = depth_max;
}

AUTO_COMMAND ACMD_CATEGORY( Debug ) ACMD_COMMANDLINE;
void demo_update_batch( char* root )
{
	char** demos = fileScanDir( root );
	int it;

	demoEnsureLegacyMode();
	for( it = 0; it != eaSize( &demos ); ++it ) {
		if (!ParserReadTextFile( demos[ it ], parse_DemoRecording, &s_Demo, PARSER_NOERRORFSONPARSE)) {
			printfColor( COLOR_RED, "[%2d/%2d] Demo %s -- unable to read for conversion.\n", it + 1, eaSize( &demos ), demos[ it ]);
		} else {
			ParserWriteTextFile(demos[ it ], parse_DemoRecording, &s_Demo, 0, 0 );
			printfColor( COLOR_GREEN, "[%2d/%2d] Demo %s -- updated successfully.\n", it + 1, eaSize( &demos ), demos[ it ]);

			StructReset( parse_DemoRecording, &s_Demo );
		}
	}

	eaDestroyEx( &demos, NULL );
}

AUTO_COMMAND ACMD_CATEGORY( Debug ) ACMD_COMMANDLINE;
void demo_test_batch( char* root )
{
	char** demos = fileScanDir( root );
	int it;

	for( it = 0; it != eaSize( &demos ); ++it ) {
		if (!ParserReadTextFile( demos[ it ], parse_DemoRecording, &s_Demo, 0 )) {
			printfColor( COLOR_RED, "[%2d/%2d] Demo %s -- unable to read - in old format or corrupt.\n", it + 1, eaSize( &demos ), demos[ it ]);
		} else {
			printfColor( COLOR_GREEN, "[%2d/%2d] Demo %s -- is fine.\n", it + 1, eaSize( &demos ), demos[ it ]);
			StructReset( parse_DemoRecording, &s_Demo );
		}
	}

	eaDestroyEx( &demos, NULL );
}

/// Playback the demo specified by FILE.
///
/// Will repeat the demo REPEAT times, and gather statistics about
/// every pass except the first.  Will playback the demo at FPS
/// frames-per-second.  If FPS = 0, then will use a variable framerate
/// instead of a fixed one.  If IMAGE-FILE-PREFIX is non-null, save
/// out the frames as in DEMO-SAVE-IMAGES.  When IMAGE-FILE-PREFIX is
/// the empty string, then use the default naming convention.
static void demo_playUber(
        CmdContext* context, SA_PARAM_NN_STR const char* file, U32 repeat, int fps,
        SA_PARAM_OP_STR const char* imageFilePrefix, SA_PARAM_OP_STR const char* imageFileExt )
{
	setDemoOptionsAtCmdParseTime();

    s_demoPlaybackFixedFps = fps;
    
	recording = false;
	playbackTimesRemaining = repeat;
    playbackTimesTotal = repeat;

    if( imageFilePrefix ) {
        assertmsgf( repeat == 1, "When saving out images, a demo can only repeat once, but REPEAT=%d.", repeat );
        repeat = 1;

        if( !imageFileExt || !imageFileExt[0] ) {
            Alertf( "Extention must be valid format, defaulting to JPG" );
            imageFileExt = "jpg";
        }
    }

	if( imageFilePrefix ) {
		s_demoScreenshotFileNamePrefix = strdup( imageFilePrefix[ 0 ] ? imageFilePrefix : "screenshot" );
		s_demoScreenshotFileNameExt = strdup( imageFileExt[ 0 ] ? imageFileExt : "jpg" );
	} else {
		s_demoScreenshotFileNamePrefix = NULL;
		s_demoScreenshotFileNameExt = NULL;
	}
    

	if(s_demo_filename)
		StructFreeString(s_demo_filename);
	s_demo_filename = StructAllocString(file);

	gfxSetRecordCamMatPostProcessFn( gclCamera_ApplyProperZDistanceForDemo );
}

void demo_startRecording(void)
{
	// Clear the recording, but spare the list of entities from destruction
	StructReset(parse_DemoRecording, &s_Demo);
	s_Demo.version = 1;

	// Copy the map name
	if( resExtractNameSpace_s( s_zonename, NULL, 0, NULL, 0 )) {
		// It should be the active zonemap
		ZoneMapInfo* activeInfo = zmapGetInfo( NULL );
		assert( activeInfo && stricmp( zmapInfoGetFilename( activeInfo ), s_zonename ) == 0 );
		s_Demo.zmInfo = StructClone( parse_ZoneMapInfo, activeInfo );
		s_Demo.zmInfoFilename = StructAllocString( zmapInfoGetFilename( activeInfo ));
	} else {
		s_Demo.zoneName = StructAllocString(s_zonename);
	}

	// Record the start time
	s_Demo.startTime = timeSecondsSince2000();
	s_timeElapsed = 0;
	s_Demo.startWorldTime = wlTimeGet();

    // Record the active player
    {
        const Entity* activePlayer = entActivePlayerPtr();
        s_Demo.activePlayerRef = activePlayer ? activePlayer->myRef : 0;
    }
    
	ZeroStruct(&demo_history);

	s_Demo.fovy = gfxGetActiveCameraFOV();

	demo_RecordMMHeader(mmNetGetLatestSPC());

	// Get the list of all entities on the server
	{
		EntityIterator * iter = entGetIteratorAllTypesAllPartitions(0,0);
		Entity* currEnt;
		while ((currEnt = EntityIteratorGetNext(iter)))
		{
			// Record each entity's existence and ask for a full update
			demo_RecordEntityCreation(currEnt);
		}
		ServerCmd_forceFullEntityUpdateForDemo();

		EntityIteratorRelease(iter);
	}

	// Record the current map state
	{
		ResourceIterator iter;
		const char* resName;
		void* object;
		
		resInitIterator( INTERACTION_DICTIONARY, &iter );
		while( resIteratorGetNext( &iter, &resName, &object )) {
			demo_RecordResourceUpdated( NULL, INTERACTION_DICTIONARY, resName, object, parse_WorldInteractionNode );
		}
		resFreeIterator(&iter);
		resReRequestMissingResources( INTERACTION_DICTIONARY );
	}
	mapState_InitialRecordForDemo();
}

static void demo_saveRecording(void)
{
	if(recording) {
		demo_prune(&s_Demo); // TODO: why is all the crap being recorded?  Stop it there, not here
		ParserWriteTextFile(s_demo_filename, parse_DemoRecording, &s_Demo, 0, 0);
	}
}

void demo_UpdateTimeTicker(void)
{
	if( demoTimeTicker ) {
		int numMinutes = (int)s_demoPlaybackTimeElapsed / 60;
		int numSeconds = (int)s_demoPlaybackTimeElapsed % 60;
		int numMs = (int)(s_demoPlaybackTimeElapsed * 1000) % 1000;
		char buffer[256];
		static char *last_buffer;

		sprintf( buffer, "%d:%02d.%03d", numMinutes, numSeconds, numMs );
		if (!last_buffer)
			last_buffer = calloc(256, 1);
		if (stricmp(last_buffer, buffer)!=0)
		{
			strcpy_s(last_buffer, 256, buffer);
			ui_LabelSetText(demoTimeTicker, buffer);
			ui_LabelResize(demoTimeTicker);
		}
	}
}

void demo_RecordMapName(const char* mapname)
{
	if(s_zonename)
		StructFreeString(s_zonename);

	s_zonename = StructAllocString(mapname);
}

static const char *demoGetMapNameToUse(void)
{
	static char buf[1024];
	char *nameToUse=s_Demo.zoneName;
	bool bGood=false;

	assert(s_Demo.zoneName);

#define CHECK(map_name) if (!bGood && map_name && worldGetZoneMapByPublicName(map_name)) { bGood = true; nameToUse = (map_name); }
	if (s_Demo.old_worldgridName)
	{
		// Old format, try to get public name from worldgrid name because zone filename probably points to something different
		getFileNameNoExt(buf, s_Demo.old_worldgridName);
		CHECK(buf);
	}
	CHECK(s_Demo.zoneName);
	CHECK(s_Demo.old_worldgridName);
	if (!bGood && s_Demo.old_worldgridName)
	{
		getFileNameNoExt(buf, s_Demo.old_worldgridName);
		CHECK(buf);
	}
	if (!bGood && s_Demo.old_worldgridName)
	{
		buf[0] = '_';
		getFileNameNoExt_s(buf+1, ARRAY_SIZE(buf)-1, s_Demo.old_worldgridName);
		CHECK(buf);
	}

	if (!bGood && s_Demo.zoneName)
	{
		getFileNameNoExt(buf, s_Demo.zoneName);
		CHECK(buf);
	}
	if (!bGood && s_Demo.zoneName)
	{
		buf[0] = '_';
		getFileNameNoExt_s(buf+1, ARRAY_SIZE(buf)-1, s_Demo.zoneName);
		CHECK(buf);
	}
#undef CHECK

	return nameToUse;
}

void demo_startMapPatching(void)
{
	if ( s_Demo.zoneName ){
		const char *nameToUse = demoGetMapNameToUse();
		if (!worldZoneMapStartPatchingByName(nameToUse))
		{
			// This can safely be ignored, but if the map isn't there, you probably don't
			//  actually want to be playing back the demo (certain for the perf builders
			//  we don't - we want to know it's messed up ASAP).
			FatalErrorf("Unable to load zone map \"%s\"", nameToUse);
		}
	} else if( s_Demo.zmInfo ) {
		if (!worldZoneMapStartPatching(s_Demo.zmInfo))
		{
			FatalErrorf("Unable to load zone map \"%s\"", zmapInfoGetPublicName(s_Demo.zmInfo));
		}
	} else {
		FatalErrorf( "Internal Error: Demo appears to have no relevant zone map." );
	}
}

void demo_loadMap(void)
{
	if( s_Demo.zoneName ) {
		const char *nameToUse = demoGetMapNameToUse();
		if (!worldLoadZoneMapByName(nameToUse))
		{
			// This can safely be ignored, but if the map isn't there, you probably don't
			//  actually want to be playing back the demo (certainly for the perf builders
			//  we don't - we want to know it's messed up ASAP).
			FatalErrorf("Unable to load zone map \"%s\"", nameToUse);
		}
	} else if( s_Demo.zmInfo ) {
		zmapInfoSetFilenameForDemo( s_Demo.zmInfo, s_Demo.zmInfoFilename );
		if( !worldLoadZoneMap( s_Demo.zmInfo, false, false )) {
			FatalErrorf("Unable to load zone map \"%s\"", zmapInfoGetPublicName(s_Demo.zmInfo));
		}
	} else {
		FatalErrorf( "Internal Error: Demo appears to have no relevant zone map." );
	}

	demo_LoadReplayLate();
}


void demo_RecordCamera(void)
{
	CameraMatRelative* camMat;
	if( !recording ) {
		return;
	}

	camMat = StructAlloc(parse_CameraMatRelative);

	gfxGetActiveCameraYPR(camMat->pyr);
	camMat->time = s_timeElapsed;

	if( recordingIsAbsolute ) {
		camMat->isAbsolute = true;
		gfxGetActiveCameraPos(camMat->pos);
	} else {
		camMat->dist = gclCamera_GetSettings( gGCLState.pPrimaryDevice->activecamera )->fDistance;
	}

	recordingIsAbsolute = (gGCLState.pPrimaryDevice->activecamera != &gGCLState.pPrimaryDevice->gamecamera) || s_demoRecordExactCameraPositions;

	eaPush(&s_Demo.relativeCameraViews, camMat);
    s_timeElapsed += gGCLState.frameElapsedTime;
}

static bool demo_ShouldReplayMessage(const char* msg)
{
	// Messages that shouldn't be replayed are ones that don't make sense for a recording.
	if(strStartsWith(msg, "Camera."))
		return false;
	if(strStartsWith(msg, "AddCommandNameToClientForAutoCompletion"))
		return false;
	if(strStartsWith(msg, "AddCommandArgsToClientForAutoCompletion"))
		return false;
    if(strStartsWith(msg, "LootInteraction"))
        return false;
    if(strStartsWith(msg, "GameDialogError"))
        return false;
	if(strStartsWith(msg, "mission_NotifyUpdate"))
		return false;
	if(strStartsWith(msg, "GameDialogGenericMessage"))
		return false;

	return true;
}

void demo_RecordMessage(const char* msg, U32 flags, int cmd, Entity *ent)
{
	RecordedMessage* message;

	if( !recording ) {
		return;
	}

	message = StructCreate(parse_RecordedMessage);
	message->time = s_timeElapsed;
	message->command = cmd;
	message->entityRef = (ent ? entGetRef(ent) : 0);
	message->message = StructAllocString(msg);
	message->flags = flags;
	eaPush(&s_Demo.messages, message);
}

void demo_saveMemoryUsage(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	const char *name = userData;
	char path[MAX_PATH];
	char newext[128];
	char *memory_dump=NULL;
	FILE *file;

	if (!gDemoResultsFileName[0])
	{
		char tempbuf[MAX_PATH];
		char *s;
		getFileNameNoExt(tempbuf, s_demo_filename);
		sprintf(gDemoResultsFileName, "%s_%s.results", tempbuf, timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
		strchrReplace(gDemoResultsFileName, ' ', '_');
		if (s = strchr(gDemoResultsFileName, '/'))
		{
			strchrReplace(s, ':', '_');
		} else {
			strchrReplace(gDemoResultsFileName, ':', '_');
		}
	}

	if (fileIsAbsolutePath(gDemoResultsFileName))
	{
		strcpy(path, gDemoResultsFileName);
	}
	else
	{
		sprintf(path, "%s/results/%s", fileDemoDir(), gDemoResultsFileName);
	}

	makeDirectoriesForFile(path);
	sprintf(newext, ".%s.mmds", name);
	changeFileExt(path, newext, path);

	errorSetTempVerboseLevel(1);
	memMonitorDisplayStatsInternal(estrConcatHandler, &memory_dump, 100000);
	errorSetTempVerboseLevel(0);

	// Save results
	file = fileOpen(path, "w");
	if (file) {
		fprintf(file, "%s", memory_dump);
		fclose(file);
	}

	estrDestroy(&memory_dump);
}

static void demoWritePerfHistoryCSV(FILE *file)
{
	F32 cyclesToMS = (float)(1000.0/wlPerfGetPerfCyclesPerSecond());
	int f;
	char cam_pos_pyr_str[ 64 ];
	fprintf(file, "%s,%s,%s, %s,%s,%s,%s,%s, %s,%s,%s,%s, %s,%s,%s,%s,  %s,%s,%s,%s, %s,%s,%s,%s,%s, %s,%s,%s,%s, %s,%s,%s, %s,%s,%s,%s,%s,%s,%s,%s,%s,%s, %s,%s,%s, %s,%s,%s, %s,%s,%s, %s,%s,%s, %s\n", 
		"world_animation_updates",

		"sprites_drawn",
		"sprite_primitives_drawn",

		"unique_shader_graphs_referenced",
		"unique_shaders_referenced",
		"unique_materials_referenced",
		"lights_drawn",
		"device_locks",

		"ms",
		"fps",
		"cpu_ms",
		"gpu_ms",

		"total_skeletons",
		"drawn_skeletons",
		"drawn_skeleton_shadows",
		"postprocess_calls",

		"misc ms",
		"draw ms",
		"queue ms",
		"queue_world ms",

		"anim ms",
		"wait_gpu ms",
		"net ms",
		"ui ms",
		"cloth ms",

		"skel ms",
		"fx ms",
		"sound",
		"physics",

		"z occluders",
		"zo tests",
		"zo culls",

		"gpu: idle",
		"shadows",
		"zprepass",
		"opaque 1pass",
		"shadow buf",
		"opaque",
		"alpha",
		"post",
		"2D",
		"other",

		"gpu_bound?",

		"over_budget_mem?",
		"over_budget_perf?",

		"mem_usage_(MB)",

		"stalled?",
		"stall_time",

		"triangles_in_scene",
		"opaque_triangles_drawn",
		"alpha_triangles_drawn",

		"objects_in_scene",
		"opaque_objects_drawn",
		"alpha_objects_drawn",

		"camera"
		);
	for (f = 0; f < demo_history.history_pos; ++f)
	{
		const FrameCounts * hf = &demo_history.history[f];
		sprintf(cam_pos_pyr_str, "%.2f %.2f %.2f %.1f %.1f %.1f", hf->cam_pos[0], hf->cam_pos[1], hf->cam_pos[2], DEG(hf->cam_pyr[0]), DEG(hf->cam_pyr[1]), DEG(hf->cam_pyr[2]));
		fprintf(file, "%d,%d,%d, %d,%d,%d,%d,%d, %.2f,%.2f,%.2f,%.2f, %d,%d,%d,%d,  %.2f,%.2f,%.2f,%.2f, %.2f,%.2f,%.2f,%.2f,%.2f, %.2f,%.2f,%.2f,%.2f, %d,%d,%d, %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f, %c,%c,%c, %d,%c,%.2f, %"FORM_LL"u,%"FORM_LL"u,%"FORM_LL"u, %d,%d,%d, %s\n", 
			hf->world_animation_updates,

			hf->sprites_drawn,
			hf->sprite_primitives_drawn,

			hf->unique_shader_graphs_referenced,
			hf->unique_shaders_referenced,
			hf->unique_materials_referenced,
			hf->lights_drawn,
			hf->device_locks,

			hf->ms,
			hf->fps,
			hf->cpu_ms,
			hf->gpu_ms,

			hf->total_skeletons,
			hf->drawn_skeletons,
			hf->drawn_skeleton_shadows,
			hf->postprocess_calls,

			hf->world_perf_counts.time_misc * cyclesToMS,
			hf->world_perf_counts.time_draw * cyclesToMS,
			hf->world_perf_counts.time_queue * cyclesToMS,
			hf->world_perf_counts.time_queue_world * cyclesToMS,

			hf->world_perf_counts.time_anim * cyclesToMS,
			hf->world_perf_counts.time_wait_gpu * cyclesToMS,
			hf->world_perf_counts.time_net * cyclesToMS,
			hf->world_perf_counts.time_ui * cyclesToMS,
			hf->world_perf_counts.time_cloth * cyclesToMS,

			hf->world_perf_counts.time_skel * cyclesToMS,
			hf->world_perf_counts.time_fx * cyclesToMS,
			hf->world_perf_counts.time_sound * cyclesToMS,
			hf->world_perf_counts.time_physics * cyclesToMS,

			hf->zo_occluders,
			hf->zo_cull_tests,
			hf->zo_culls,

			hf->world_perf_counts.time_gpu[EGfxPerfCounter_IDLE],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_SHADOWS],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_ZPREPASS],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_OPAQUE_ONEPASS],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_SHADOW_BUFFER],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_OPAQUE],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_ALPHA],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_POSTPROCESS],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_2D],
			hf->world_perf_counts.time_gpu[EGfxPerfCounter_MISC],

			hf->gpu_bound ? 'Y' : 'N',

			hf->over_budget_mem ? 'Y' : 'N',
			hf->over_budget_perf ? 'Y' : 'N',

			hf->mem_usage_mbs,

			hf->stalled ? 'Y' : 'N',
			hf->stall_time,

			hf->triangles_in_scene,
			hf->opaque_triangles_drawn,
			hf->alpha_triangles_drawn,

			hf->objects_in_scene,
			hf->opaque_objects_drawn,
			hf->alpha_objects_drawn,

			cam_pos_pyr_str
			);
	}
}

static void demo_displayResults(void)
{
	char *message=NULL;
	char *messageCSV=NULL;
	int frames = gfxGetFrameCount() - s_start_frame;

	if (g_demo_save_timing)
		timerRecordEnd();

	// Disable our errorf catching, and instead let it pop up the normal dialog box
	ErrorfPopCallback();
	gbQueueErrorsEvenInProductionMode = true;

	if( !demoVerbose ) {
		if( s_demoScreenshotFileNamePrefix ) {
			Alertf( "Demo image saving finished %d frames in %1.3fs (%1.3f fps, %1.1f ms/f).", frames, s_timeElapsed, frames/s_timeElapsed, s_timeElapsed*1000/frames );
		} else {
			Alertf( "Demo finished %d frames in %1.3fs (%1.3f fps, %1.1f ms/f).", frames, s_timeElapsed, frames/s_timeElapsed, s_timeElapsed*1000/frames );
		}
	} else {
		if( s_demoScreenshotFileNamePrefix ) {
			Alertf( "Demo image saving finished %d frames in %1.3fs (%1.3f fps, %1.1f ms/f).", frames, s_timeElapsed, frames/s_timeElapsed, s_timeElapsed*1000/frames );
		} else {
			char tempbuf[2048];

			systemSpecsGetString(SAFESTR(tempbuf));
			estrConcatf(&message, "%s\n", tempbuf);
			systemSpecsGetCSVString(SAFESTR(tempbuf));
			estrConcatf(&messageCSV, "%s\n", tempbuf);

			gfxGetSettingsString(SAFESTR(tempbuf));
			estrConcatf(&message, "%s\n", tempbuf);
			gfxGetSettingsStringCSV(SAFESTR(tempbuf));
			estrConcatf(&messageCSV, "%s\n", tempbuf);

			estrConcatf(&message, "Version: %s In-game Time:%1.1f\n", GetUsefulVersionString(), wlTimeGet());
			estrConcatf(&messageCSV, "Version,\"%s\",InGameTime,%1.1f\n", GetUsefulVersionString(), wlTimeGet());

			estrConcatf(&message, "Demo playback finished %d frames in %1.3fs (%1.3f fps, %1.1f ms/f).\n", frames, s_timeElapsed, frames/s_timeElapsed, s_timeElapsed*1000/frames);
			estrConcatf(&messageCSV, "Frames,%d\nSeconds,%1.3fs\nTotalFPS,%1.5f\nTotalMspf,%1.2f\n", frames, s_timeElapsed, frames/s_timeElapsed, s_timeElapsed*1000/frames);

			if (demo_history.frames) {
				shDoOperationSetInt(demo_history.frames);
				StructCopyAll(parse_FrameCounts, &demo_history.sum, &demo_history.avg);
				shDoOperation(STRUCTOP_DIV, parse_FrameCounts, &demo_history.avg, OPERAND_INT);
			}
			estrConcatf(&message, "Min/Max/Avg:\n");

			#define DOIT3(nicefmt, csvfmt, ...)	{						\
					estrConcatf(&message, nicefmt, __VA_ARGS__);		\
					estrConcatf(&messageCSV, csvfmt, __VA_ARGS__); }
			#define DOIT2(field, nicefmt, csvfmt, cast, mod)			\
				DOIT3("%22s: " nicefmt " /" nicefmt " /" nicefmt "\n",	\
					  "\"%s\"," csvfmt "," csvfmt "," csvfmt "\n",		\
					  #field, (cast)demo_history.minvalues.field mod, (cast)demo_history.maxvalues.field mod, (cast)demo_history.avg.field mod);
			#define DOIT(field, nicefmt, csvfmt)						\
				DOIT3("%22s: " nicefmt " /" nicefmt " /" nicefmt "\n",	\
					  "\"%s\"," csvfmt "," csvfmt "," csvfmt "\n",		\
					  #field, demo_history.minvalues.field, demo_history.maxvalues.field, demo_history.avg.field);
			#define DOITFULL(name, minvar, maxvar, avgvar, nicefmt, csvfmt)	\
				DOIT3("%22s: " nicefmt " /" nicefmt " /" nicefmt "\n",	\
					  "\"%s\"," csvfmt "," csvfmt "," csvfmt "\n",		\
					  #name, minvar, maxvar, avgvar );
			#define DOIT_INT(field) DOIT(field, "%6d", "%d")
			#define DOIT_INT64(field) DOIT(field, "%6"FORM_LL"d", "%"FORM_LL"d")
			#define DOITFULL_INT(name, minvar, maxvar, avgvar) DOITFULL(name, minvar, maxvar, avgvar, "%6d", "%d")
			#define DOITFULL_INT64(name, minvar, maxvar, avgvar) DOITFULL(name, minvar, maxvar, avgvar, "%6"FORM_LL"d", "%"FORM_LL"d")
			#define DOIT_FLOAT(field) DOIT(field, "%6.1f", "%0.1f")
			DOIT_INT(objects_in_scene);
			DOIT_INT(opaque_objects_drawn);
			DOIT_INT(alpha_objects_drawn);
			DOITFULL_INT(	total_objects_drawn, 
							demo_history.minvalues.opaque_objects_drawn + demo_history.minvalues.alpha_objects_drawn, 
							demo_history.maxvalues.opaque_objects_drawn + demo_history.maxvalues.alpha_objects_drawn, 
							demo_history.avg.opaque_objects_drawn + demo_history.avg.alpha_objects_drawn);
			DOIT_INT64(triangles_in_scene);
			DOIT_INT64(opaque_triangles_drawn);
			DOIT_INT64(alpha_triangles_drawn);
			DOITFULL_INT64(	total_triangles_drawn,
							demo_history.minvalues.opaque_triangles_drawn + demo_history.minvalues.alpha_triangles_drawn, 
							demo_history.maxvalues.opaque_triangles_drawn + demo_history.maxvalues.alpha_triangles_drawn, 
							demo_history.avg.opaque_triangles_drawn + demo_history.avg.alpha_triangles_drawn);
			DOIT_INT(total_skeletons);
			DOIT_INT(drawn_skeletons);
			DOIT_INT(drawn_skeleton_shadows);
			DOIT_INT(cell_zoculls);
			DOIT_INT(entry_zoculls);
			DOIT_INT(terrain_zoculls);
			DOIT_INT(welded_instance_culls);

			if( !s_demoPlaybackFixedFps ) {
				DOIT_FLOAT(fps);
				DOIT_FLOAT(ms);
			}
			{
				F32 mod = 1000/(F32)wlPerfGetPerfCyclesPerSecond();
				#define DOIT2_FLOAT(field) DOIT2(field, "%6.1f", "%0.1f", F32, *mod)
				DOIT2_FLOAT(world_perf_counts.time_misc);
				DOIT2_FLOAT(world_perf_counts.time_draw);
				DOIT2_FLOAT(world_perf_counts.time_queue);
				DOIT2_FLOAT(world_perf_counts.time_queue_world);
				DOIT2_FLOAT(world_perf_counts.time_anim);
				DOIT2_FLOAT(world_perf_counts.time_wait_gpu);
				DOIT2_FLOAT(world_perf_counts.time_net);
				DOIT2_FLOAT(world_perf_counts.time_cloth);
				DOIT2_FLOAT(world_perf_counts.time_skel);
				DOIT2_FLOAT(world_perf_counts.time_fx);
			}

			DOIT_INT(mem_usage_mbs);

			DOIT3("%22s: %6d (%1.1f fps)\n",
				  "\"%s\",%d,%0.1f\n",
				  "98th percentile ms/f", gfxGetMspfPercentile(0.98), 1000.f/gfxGetMspfPercentile(0.98));
			DOIT3("%22s: %6d / %1.2fs\n",
				  "\"%s\",%d,%0.2f\n",
				  "stalls", demo_history.sum.stalled, demo_history.sum.stall_time);
			DOIT3("%22s: %6d (%2.1f%%)\n",
				  "\"%s\",%d,%0.1f\n",
				  "frames GPU bound", demo_history.sum.gpu_bound, demo_history.sum.gpu_bound * 100.f/ (F32)demo_history.frames);
			DOIT3("%22s: %6d (%2.1f%%)\n",
				  "\"%s\",%d,%0.1f\n",
				  "perf budget frames over", demo_history.sum.over_budget_perf, demo_history.sum.over_budget_perf * 100.f/ (F32)demo_history.frames);
			DOIT3("%22s: %6d (%2.1f%%)\n",
				  "\"%s\",%d,%0.1f\n",
				  "mem budget frames over", demo_history.sum.over_budget_mem, demo_history.sum.over_budget_mem * 100.f/ (F32)demo_history.frames);
			estrConcatf(&message, "%22s: %1.2fs\n", "Load time", gGCLState.startupTime);
			estrConcatf(&messageCSV, "\"Load time\",%1.2fs\n", gGCLState.startupTime);

			// Write message to file
			{
				char path[MAX_PATH];
				FILE *file;

				if (!gDemoResultsFileName[0])
				{
					char *s;
					getFileNameNoExt(tempbuf, s_demo_filename);
					sprintf(gDemoResultsFileName, "%s_%s.results", tempbuf, timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
					strchrReplace(gDemoResultsFileName, ' ', '_');
					if (s = strchr(gDemoResultsFileName, '/'))
					{
						strchrReplace(s, ':', '_');
					} else {
						strchrReplace(gDemoResultsFileName, ':', '_');
					}
				}

				if (fileIsAbsolutePath(gDemoResultsFileName))
				{
					strcpy(path, gDemoResultsFileName);
				}
				else
				{
					sprintf(path, "%s/results/%s", fileDemoDir(), gDemoResultsFileName);
				}

				makeDirectoriesForFile(path);

				// Save results
				file = fileOpen(path, "w");
				if (file) {
					fprintf(file, "%s", message);
					fclose(file);
				}

				// Save .csv version of results
				changeFileExt(path, ".csv", path);
				file = fileOpen(path, "w");
				if (file) {
					char *strtokContext=NULL;
					char *line;
					char header[1024];
					getFileNameNoExt(tempbuf, s_demo_filename);
					sprintf(header, "\"%s\",\"%s\",", timeGetLocalDateString(), tempbuf);
					line = strtok_s(messageCSV, "\n", &strtokContext);
					while (line) {
						fprintf(file, "%s%s\n", header, line);
						line = strtok_s(NULL, "\n", &strtokContext);
					}
					fclose(file);
				}

				if (demo_history.history)
				{
					// Save .csv version of history
					changeFileExt(path, "history.csv", path);
					file = fileOpen(path, "w");
					if (file) {
						demoWritePerfHistoryCSV(file);
						fclose(file);
					}
				}


				// Save memory usage
				demo_saveMemoryUsage(NULL, 0, "END");
			}

			#if !PLATFORM_CONSOLE
			if (gbQuitOnDemoCompletion)
			{
				gclAboutToExit();
				exit(0);
			}
			#endif

			Alertf("%s", message);
			estrDestroy(&message);
			estrDestroy(&messageCSV);
		}
	}

	gbQueueErrorsEvenInProductionMode = false;
	ErrorfPushCallback(demoErrorfCallback, NULL);
}

static S32 useNextFramePos = 1;
AUTO_CMD_INT(useNextFramePos, demo_useNextFramePos);

static void demo_ReplayMessages(bool bFirstCall)
{
	int nextMessage;
	int origMessage = s_Demo.curMessage;
	int numMessages = eaSize(&s_Demo.messages);
	bool posChanged = false;

	if(NULL ==s_Demo.messages)
		return;

	// Find all the messages whose times have come
	nextMessage = origMessage + 1;
	while(nextMessage < numMessages && s_Demo.messages[nextMessage]->time <= s_demoPlaybackTimeElapsed)
	{
		posChanged = true;
		nextMessage++;
		s_Demo.curMessage++;
	}

	// If any messages' times have passed, send them to the client
	if(posChanged)
	{
		int i;
		for(i=origMessage + 1; i<nextMessage; i++)
		{
			RecordedMessage* mesg = s_Demo.messages[i];
			Entity* ent = entFromEntityRefAnyPartition(mesg->entityRef);

			if(demo_ShouldReplayMessage(mesg->message))
				gclHandleMsgFromReplay(mesg->message, mesg->flags, mesg->command, ent);
		}
	}

    // find the player, and set his position as the target pos
    {
        Entity* player = entActivePlayerPtr();
        Vec3 playerPos;

        if( player ) {
			if(useNextFramePos && !bFirstCall){
				entGetPosForNextFrame(player, playerPos);
				
				globMovementLog("[demo] Setting demo camera to next frame entity pos(%f, %f, %f).",
								vecParamsXYZ(playerPos));
			}else{
				entGetPos( player, playerPos );

				globMovementLog("[demo] Setting demo camera to current frame entity pos(%f, %f, %f).",
								vecParamsXYZ(playerPos));
			}
			playerPos[1] += entGetHeight( player );
            gfxSetRecordingTargetPos( playerPos );
        }
    }
}

void demo_getCutsceneCamMat(CutsceneDef* record, F32 time, F32 timestep)
{
	Mat4 mat;
	Vec3 pos, pyr;
	gfxGetActiveCameraMatrix(mat);
	getMat3YPR(mat, pyr);
	copyVec3(mat[3], pos);
	if(gclGetCutsceneCameraPathListPosPyr(record, time, pos, pyr, gfxGetActiveCameraView()->sky_data, false, timestep))
	{
		createMat3YPR(mat, pyr);
		copyVec3(pos, mat[3]);
		gfxSetActiveCameraMatrix(mat, false);
	}
}

static void demo_allocFullFrameHistory(DemoRecording * pDemo)
{
	U32 demoLengthSeconds = pDemo->endTime - pDemo->startTime;
	if (demoPlaybackEnd != 0 && demoPlaybackEnd > demoPlaybackStart)
		demoLengthSeconds = ceilf(demoPlaybackEnd - demoPlaybackStart);

	demo_history.history_pos = 0;

	demo_history.history_length = demoLengthSeconds * 60;
	demo_history.history = calloc(demo_history.history_length, sizeof(FrameCounts));
}

static void demo_setFirstDemoFrame(void)
{
	gclSetDemoCameraActive();
	gfxSetActiveProjection(s_Demo.fovy, -1);
    
    if(s_Demo.cutsceneDef && s_Demo.cutsceneDef->pPathList)
	{
		Vec3 pos, pyr;
		GfxCameraController *camera = gfxGetActiveCameraController();
		copyVec3(camera->camcenter, pos);
		copyVec3(camera->campyr, pyr);
		gclCutsceneLoadSplines(s_Demo.cutsceneDef->pPathList);
		gclGetCutsceneCameraPathListPosPyr(s_Demo.cutsceneDef, 0, pos, pyr, NULL, true, -1);
		gfxSetRecordPlayCutsceneCB(demo_getCutsceneCamMat);
		gfxLoadRecordingCutscene(&s_Demo.cutsceneDef);
	}
	else if( s_Demo.cameraViews )
    {
        gfxLoadRecording(&s_Demo.cameraViews);
    }
    else if( s_Demo.relativeCameraViews )
    {
        gfxLoadRecordingRelative(&s_Demo.relativeCameraViews);
    }
    else
    {
        Alertf( "Demo does not appear to have any saved camera positions." );
    }
}

void demo_restartInternal( bool noReloading )
{
	globCmdParsef("mmNetBufferSkipSeconds %f", demoPlaybackStart);

	s_Demo.curMessage = -1;
	if( !noReloading ) {
		s_Demo.curPacket = 0;
	}
	s_demoPlaybackTimeElapsed = demoPlaybackStart;

	if( !noReloading ) {
		objDestroyAllContainers();
		entClearLocalPlayers();
		entityLibResetState();
    
		demo_LoadReplay();
		worldReloadMap();
		demo_LoadReplayLate();
		wlTimeSet( s_Demo.startWorldTime );
	}

	
	gfxReplayRecording();
	demo_EntityUpdateRecordedPositions();
	keybind_PushProfileName( "gclDemo" );
}

/// Restart a currently playing demo.
///
/// Playback will start up back at the beginning of the demo.
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 );
void demo_restart( void )
{
	demo_restartInternal( false );
}

void demo_RecordResourceUpdated(void* ignored, const char* dictName, const char* resourceName, void* object, ParseTable* parseTable)
{
	if( !recording ) {
		return;
	}
	
	if( stricmp( dictName, "InteractionDictionary" ) == 0 ) {
		RecordedMMPacket* packet = 	s_Demo.packets[s_Demo.curPacket];
		
		RecordedWIN* recordedWin;
		assert(parseTable == parse_WorldInteractionNode);

		recordedWin = StructCreate(parse_RecordedWIN);
		recordedWin->resourceName = allocAddString(resourceName);
		recordedWin->node = StructClone( parse_WorldInteractionNode, object );
		eaPush(&packet->interactionUpdates, recordedWin);
	}
}

void demo_RecordEntityCreation(Entity* pEntity)
{
	RecordedMMPacket* packet;
	RecordedEntity* recEnt;

	if( !recording ) {
		return;
	}
	
	packet = s_Demo.packets[s_Demo.curPacket];
	recEnt = StructCreate(parse_RecordedEntity);
	
	// Copy the entity's entRef and container ID.  When we re-play this demo, we just use the same values
	recEnt->entityRef = pEntity->myRef;
	recEnt->containerID = pEntity->myContainerID;
	recEnt->entityTypeEnum = pEntity->myEntityType;
	recEnt->fEntitySendDistance = pEntity->fEntitySendDistance;

	// Copy the entity's costume
    StructCopyAll( parse_CostumeRef, &pEntity->costumeRef, &recEnt->costumeV5 );

	if (pEntity->pSaved)
	{
		recEnt->entityAttach = StructClone(parse_SavedEntityData, pEntity->pSaved);
	}

	// Add the entity to the current packet's list of entities
	eaPush(&packet->createdEnts, recEnt);
}

void demo_RecordEntityUpdate(Entity* pEntity, RecordedEntityUpdate* recUpdate)
{
	RecordedMMPacket* packet;

	if( !recording ) {
		return;
	}
	
	packet = s_Demo.packets[s_Demo.curPacket];

	// Add entref
	recUpdate->entityRef = pEntity->myRef;

	// Push this position to the current packet
	eaPush(&packet->updates, recUpdate);
}

void demo_RecordEntityCostumeChange(Entity* pEntity)
{
	RecordedMMPacket* packet;
	RecordedEntityCostumeChange* costumeChange;
	
	if( !recording ) {
		return;
	}

	packet = s_Demo.packets[s_Demo.curPacket];
	costumeChange = StructCreate( parse_RecordedEntityCostumeChange );
	costumeChange->entityRef = pEntity->myRef;
	StructCopyAll( parse_CostumeRef, &pEntity->costumeRef, &costumeChange->costume );

	eaPush(&packet->costumeChanges, costumeChange);
}

void demo_RecordEntityDestruction(int iEntRef, bool noFade)
{
	RecordedMMPacket* packet;
	RecordedEntityDestruction* record;

	if( !recording ) {
		return;
	}

	packet = s_Demo.packets[s_Demo.curPacket];
	record = StructCreate(parse_RecordedEntityDestruction);
	record->entityRef = iEntRef;
	record->noFade = noFade;

	// Add this entity ref to the list of destroyed entity refs in this packet
	eaPush(&packet->destroyedEnts, record);
}

void demo_RecordEntityDamage(Entity* pEntity, F32 hp, F32 maxHP)
{
	if( !recording || !pEntity ) {
		return;
	}
	
	{
		RecordedMMPacket* packet = 	s_Demo.packets[s_Demo.curPacket];
		RecordedEntityDamage* record = StructCreate(parse_RecordedEntityDamage);
		
		record->entityRef = pEntity->myRef;
		record->hp = hp;
		record->maxHP = maxHP;

		eaPush(&packet->entDamage, record);
	}
}

void demo_RecordMMHeader(int curProcessCount)
{
	RecordedMMPacket* packet;
	
	if(!demo_recording() || !curProcessCount) {
		return;
	}

	packet = StructCreate(parse_RecordedMMPacket);

	packet->serverProcessCount = curProcessCount;
	packet->time = s_timeElapsed;

	// We have a new "current" packet
	eaPush(&s_Demo.packets, packet);
	s_Demo.curPacket = eaSize(&s_Demo.packets) - 1;
}

void demo_RecordMapStateFull(MapState* mapState, U32 pakID)
{
	RecordedMMPacket* packet;
	RecordedMapStateUpdate* pUpdate;
	
	if( !recording ) {
		return;
	}
	
	packet = s_Demo.packets[s_Demo.curPacket];
	pUpdate = StructCreate( parse_RecordedMapStateUpdate );
	pUpdate->pakID = pakID;
	pUpdate->fullUpdate = StructClone( parse_MapState, mapState );
	eaPush( &packet->mapState, pUpdate );
}

static MapState* s_pMapStateBefore = NULL;

void demo_RecordMapStateDiffBefore(MapState* beforeDiffMapState)
{
	devassert( !s_pMapStateBefore );
	if( !recording ) {
		return;
	}
	
	StructDestroySafe( parse_MapState, &s_pMapStateBefore );
	s_pMapStateBefore = StructClone( parse_MapState, beforeDiffMapState );
}

void demo_RecordMapStateDiff(MapState* mapState, U32 pakID)
{
	RecordedMMPacket* packet;
	RecordedMapStateUpdate* pUpdate;
	
	if( !recording ) {
		return;
	}
	devassert( s_pMapStateBefore );
	if( !s_pMapStateBefore ) {
		return;
	}
	
	packet = s_Demo.packets[s_Demo.curPacket];
	pUpdate = StructCreate( parse_RecordedMapStateUpdate );
	pUpdate->pakID = pakID;
	StructWriteTextDiff( &pUpdate->diffUpdate, parse_MapState, s_pMapStateBefore, mapState, 0, 0, 0, 0 );
	if( estrLength( &pUpdate->diffUpdate )) {
		eaPush( &packet->mapState, pUpdate );
	} else {
		StructDestroySafe( parse_RecordedMapStateUpdate, &pUpdate );
	}

	StructDestroySafe( parse_MapState, &s_pMapStateBefore );
}

void demo_RecordMapStateDestroy(U32 pakID)
{
	RecordedMMPacket* packet;
	RecordedMapStateUpdate* pUpdate;

	if( !recording ) {
		return;
	}
	
	packet = s_Demo.packets[s_Demo.curPacket];
	pUpdate = StructCreate( parse_RecordedMapStateUpdate );
	pUpdate->pakID = pakID;
	pUpdate->deleteUpdate = true;
	eaPush( &packet->mapState, pUpdate );
}

static void demo_CreateRecordedEntity(RecordedEntity* recEnt)
{
    NOCONST(Entity) *entity;

    entity = CONTAINER_NOCONST(Entity, entCreateNewFromEntityRef(recEnt->entityTypeEnum, recEnt->entityRef, __FUNCTION__));

    entity->myContainerID = recEnt->containerID;
    entity->myRef = recEnt->entityRef;
    entity->myEntityType = recEnt->entityTypeEnum;
	entity->fEntitySendDistance = recEnt->fEntitySendDistance;
	assert(entity->fEntitySendDistance); // Should get defaulted to 300 in the parse table

	// Make sure the active player has a player ptr.
	if( s_Demo.activePlayerRef == (EntityRef)recEnt->entityRef ) {
		entSetPlayerRef(0, s_Demo.activePlayerRef);
		
		entity->pPlayer = StructCreateNoConst(parse_Player);
		entity->pChar = StructCreateNoConst(parse_Character);
		entity->pChar->pEntParent = CONTAINER_RECONST(Entity, entity);
		entity->pChar->pattrBasic = StructCreateNoConst(parse_CharacterAttribs);
	}

    REMOVE_HANDLE(entity->costumeRef.hReferencedCostume);
    if( recEnt->costumeName ) {
        SET_HANDLE_FROM_STRING("PlayerCostume",recEnt->costumeName,entity->costumeRef.hReferencedCostume);
    } else {
        StructResetNoConst( parse_CostumeRef, &entity->costumeRef );
        StructCopyAllDeConst( parse_CostumeRef, &recEnt->costumeV5, &entity->costumeRef );
    }
    
    if (!objAddExistingContainerToRepository(entity->myEntityType,entity->myContainerID,entity))
	{
		assertmsg(0, "Failed to create entity, possibly bad entity type.");
	}
}

static void demo_PostCreateUpdate(RecordedEntity* recEnt)
{
	Entity *entity;
	entity = entFromEntityRefAnyPartition(recEnt->entityRef);
	if (!entity)
	{
		return;
	}

	if (recEnt->entityAttach)
	{
		DECONST(void *, entity->pSaved) = StructClone(parse_SavedEntityData, recEnt->entityAttach);
		gclEntUpdateAttach(entity);
	}
}

static void demo_ReplayMMPacket(RecordedMMPacket* packet)
{
	int i, n;
	U32 serverProcessCount = packet->serverProcessCount;
	U32 clientProcessCount;

	frameLockedTimerGetProcesses(gGCLState.frameLockedTimer, &clientProcessCount, NULL, NULL, NULL);

	mmReceiveHeaderFromDemoReplay(serverProcessCount, clientProcessCount);

	// Get rid of any destroyed entities
	n = eaSize(&packet->destroyedEnts);
	for(i=0; i<n; i++)
	{
		gclEntityDeleteForDemo( packet->destroyedEnts[i]->entityRef, packet->destroyedEnts[i]->noFade );
		/*
		Entity *pEntity = entFromEntityRefAnyPartition();
		int iResult;
		if(pEntity)
		{
			gclEntityDelete( INDEX_FROM_REFERENCE(iRef))
			iResult = objRemoveContainerFromRepository(pEntity->myEntityType,pEntity->myContainerID);
			assert(iResult);
		}
		*/
	}

	// Create any entities that were created
	n = eaSize(&packet->createdEnts);
	for(i=0; i<n; i++)
	{
		demo_CreateRecordedEntity(packet->createdEnts[i]);
	}

	for(i=0; i<n; i++)
	{
		demo_PostCreateUpdate(packet->createdEnts[i]);
	}

	// Update any entity movement
	n = eaSize(&packet->updates);
	for(i=0; i<n; i++)
	{
		RecordedEntityUpdate *update = packet->updates[i];
		Entity *pEntity = entFromEntityRefAnyPartition(update->entityRef);
		assert(pEntity);
		mmReceiveFromDemoReplay(pEntity->mm.movement, update, s_Demo.version);
	}

	// Update any entity costumes
	n = eaSize(&packet->costumeChanges);
	for(i=0; i<n; i++)
	{
		RecordedEntityCostumeChange *costumeChange = packet->costumeChanges[i];
		Entity *pEntity = entFromEntityRefAnyPartition(costumeChange->entityRef);
		assert(pEntity);

		StructCopyAll(parse_CostumeRef, &costumeChange->costume, &pEntity->costumeRef);
		costumeGenerate_FixEntityCostume(pEntity);
	}

	// Update entity health
	n = eaSize(&packet->entDamage);
	for(i=0; i<n; i++)
	{
		RecordedEntityDamage *damage = packet->entDamage[i];
		Entity *pEntity = entFromEntityRefAnyPartition(damage->entityRef);
		assert(pEntity);

		// If there's no character here, can't do much
		if (!pEntity->pChar) {
			continue;
		}

		if (!pEntity->pChar->pattrBasic) {
			pEntity->pChar->pattrBasic = StructCreate(parse_CharacterAttribs);
		}
		pEntity->pChar->pattrBasic->fHitPointsMax = damage->maxHP;
		pEntity->pChar->pattrBasic->fHitPoints = damage->hp;
	}

	// Update the interaction nodes
	n = eaSize(&packet->interactionUpdates);
	for(i=0; i<n; i++)
	{
		RecordedWIN *update = packet->interactionUpdates[i];

		// MJF: A bunch of old demos were recorded in such a way that
		// they would cause interaction nodes to get deleted from the
		// dictionary.  Interaction nodes should NEVER get deleted
		// from the dictionary; this prevents that.
		if( !update->node ) {
			continue;
		}

		resUpdateObjectForDemo(INTERACTION_DICTIONARY, update->resourceName, StructClone(parse_WorldInteractionNode, update->node));
	}
	n = eaSize(&packet->mapState);
	for(i=0; i<n; i++)
	{
		RecordedMapStateUpdate* update = packet->mapState[i];

		if( update->fullUpdate ) {
			mapState_ClientReceiveMapStateFullFromDemo( update->fullUpdate, update->pakID );
		} else if( update->diffUpdate ) {
			mapState_ClientReceiveMapStateDiffFromDemo( update->diffUpdate, update->pakID );
		} else if( update->deleteUpdate ) {
			mapState_ClientReceiveMapStateDestroyFromDemo( update->pakID );
		}
	}
}

static void demo_EntityUpdateRecordedPositions(void)
{
	U32 orig_frame, next_frame, max_frames;
	U32 i;

	// Make the compiler happy; otherwise it complains that packets could be NULL.
	if(!s_Demo.packets)
		return;

	// Find how many (if any) packets should be sent
	orig_frame = s_Demo.curPacket;
	
	next_frame = orig_frame + 1;
	max_frames = eaSize(&s_Demo.packets);

	while(next_frame < max_frames && s_Demo.packets[next_frame]->time <= s_demoPlaybackTimeElapsed)
	{
		s_Demo.curPacket++;
		next_frame++;
	}

	// Special case: play the first two packets if we haven't yet
	if (s_Demo.curPacket < 2 && eaSize(&s_Demo.packets)>2)
		s_Demo.curPacket = 2;
	if(orig_frame == 0 && s_Demo.curPacket > 0)
		demo_ReplayMMPacket(s_Demo.packets[0]);

	// next_frame is the new current frame.  Send all frames up to and including it
	for(i=orig_frame + 1; i<= s_Demo.curPacket; i++)
	{
		RecordedMMPacket *packet = s_Demo.packets[i];
		assert(packet);
		demo_ReplayMMPacket(packet);
	}
}

static void demo_RecordFrameCounts(void)
{
#define NUM_FRAMES_TO_SKIP_ON_WARP 15 // Because the stalls values are 5 frames delayed, we're really skipping 5 less than this
	static int frame_countdown=NUM_FRAMES_TO_SKIP_ON_WARP;
	FrameCounts last_frame;

	// TODO: this doesn't yet work on new relative-offset demos
	if (gfxRecordDidPositionWarp()) {
		verbose_printf("Demo playback detected position warp, skipping the next few frames' performance numbers.\n");
		frame_countdown = NUM_FRAMES_TO_SKIP_ON_WARP;
	}

	if (frame_countdown) {
		frame_countdown--;
		if (!frame_countdown)
			printf("Done skipping frames' performance numbers.\n");
		return;
	}

	gfxGetFrameCounts(&last_frame);
	if (demo_history.frames == 0) {
		StructCopyAll(parse_FrameCounts, &last_frame, &demo_history.maxvalues);
		StructCopyAll(parse_FrameCounts, &last_frame, &demo_history.minvalues);
		StructCopyAll(parse_FrameCounts, &last_frame, &demo_history.sum);
	} else {
		shDoOperation(STRUCTOP_MAX, parse_FrameCounts, &demo_history.maxvalues, &last_frame);
		shDoOperation(STRUCTOP_MIN, parse_FrameCounts, &demo_history.minvalues, &last_frame);
		shDoOperation(STRUCTOP_ADD, parse_FrameCounts, &demo_history.sum, &last_frame);
	}
	demo_history.frames++;
	if (demo_history.history)
	{
		StructCopyAll(parse_FrameCounts, &last_frame, &demo_history.history[demo_history.history_pos]);
		++demo_history.history_pos;
		if (demo_history.history_pos >= demo_history.history_length)
			demo_history.history_pos = 0;
	}
}

static void demoErrorfCallback(ErrorMessage *errMsg, void *userdata)
{
	const char *errString = errorFormatErrorMessage(errMsg);
	// Do not call printf() as that creates significant stalls
	if( demoVerbose ) {
		gfxStatusPrintf("%s", errString);
	}
}

void gclDemoSndCameraMatCB(Mat4 mat)
{
	gfxGetActiveCameraMatrix(mat);
}

void gclDemoSndGetPlayerMatCB(Mat4 mat)
{
	Entity*	pTempPlayerEnt = entActivePlayerPtr();
	if (pTempPlayerEnt)
	{
		entGetBodyMat(pTempPlayerEnt, mat);
	}
}
void gclDemoSndVelCB(Vec3 vel)
{
	//EntityPhysics *physics;
	Entity*	pTempPlayerEnt = entActivePlayerPtr();
	// TODO: This correctly.
	//mmGetPhysics(pTempPlayerEnt->movement, &physics);
	//epGetTotalVel(physics, vel);
	zeroVec3(vel);
}

int gclDemoSndPlayerAlwaysExistsCB(void)
{
	return 1;
}

static void demo_ToggleTransparency(UICheckButton *check, UserData toggledData)
{
	CutsceneEditorState* cutEdState;

	cutEdOpenWindow( true );
	cutEdState = cutEdDemoPlaybackState();
	
	if( ui_CheckButtonGetState( check )) {
		// Make skin transparent
		g_ui_State.default_skin.background[0].a = g_ui_State.default_skin.background[1].a
			= g_ui_State.default_skin.button[0].a = g_ui_State.default_skin.button[1].a
			= g_ui_State.default_skin.button[2].a = g_ui_State.default_skin.button[3].a
			= g_ui_State.default_skin.button[4].a = g_ui_State.default_skin.button[5].a
			= g_ui_State.default_skin.titlebar[0].a = g_ui_State.default_skin.titlebar[1].a
			= g_ui_State.default_skin.entry[0].a = g_ui_State.default_skin.entry[1].a
			= g_ui_State.default_skin.entry[2].a = g_ui_State.default_skin.entry[3].a
			= g_ui_State.default_skin.entry[4].a
			= 0x40;

		#ifndef NO_EDITORS
		{
			if( cutEdState ) {
				cutEdState->pSkinRed->entry[0].a = cutEdState->pSkinRed->entry[1].a
					= cutEdState->pSkinRed->entry[2].a = cutEdState->pSkinRed->entry[3].a
					= cutEdState->pSkinRed->entry[4].a
					= cutEdState->pSkinBlue->entry[0].a = cutEdState->pSkinBlue->entry[1].a
					= cutEdState->pSkinBlue->entry[2].a = cutEdState->pSkinBlue->entry[3].a
					= cutEdState->pSkinBlue->entry[4].a
					= cutEdState->pSkinGreen->entry[0].a = cutEdState->pSkinGreen->entry[1].a
					= cutEdState->pSkinGreen->entry[2].a = cutEdState->pSkinGreen->entry[3].a
					= cutEdState->pSkinGreen->entry[4].a
					= 0x40;
			}
		}
		#endif
	} else {
		// Make skin opaque
		g_ui_State.default_skin.background[0].a = g_ui_State.default_skin.background[1].a
			= g_ui_State.default_skin.button[0].a = g_ui_State.default_skin.button[1].a
			= g_ui_State.default_skin.button[2].a = g_ui_State.default_skin.button[3].a
			= g_ui_State.default_skin.button[4].a = g_ui_State.default_skin.button[5].a
			= g_ui_State.default_skin.titlebar[0].a = g_ui_State.default_skin.titlebar[1].a
			= g_ui_State.default_skin.entry[0].a = g_ui_State.default_skin.entry[1].a
			= g_ui_State.default_skin.entry[2].a = g_ui_State.default_skin.entry[3].a
			= g_ui_State.default_skin.entry[4].a
			= 0xFF;

		#ifndef NO_EDITORS
		{
			if( cutEdState ) {
				cutEdState->pSkinRed->entry[0].a = cutEdState->pSkinRed->entry[1].a
					= cutEdState->pSkinRed->entry[2].a = cutEdState->pSkinRed->entry[3].a
					= cutEdState->pSkinRed->entry[4].a
					= cutEdState->pSkinBlue->entry[0].a = cutEdState->pSkinBlue->entry[1].a
					= cutEdState->pSkinBlue->entry[2].a = cutEdState->pSkinBlue->entry[3].a
					= cutEdState->pSkinBlue->entry[4].a
					= cutEdState->pSkinGreen->entry[0].a = cutEdState->pSkinGreen->entry[1].a
					= cutEdState->pSkinGreen->entry[2].a = cutEdState->pSkinGreen->entry[3].a
					= cutEdState->pSkinGreen->entry[4].a
					= 0xFF;
			}
		}
		#endif
	}
}

static void demo_ToggleCutsceneEd(UICheckButton* check, UserData toggledData)
{
	if( ui_CheckButtonGetState( check )) {
		cutEdOpenWindow( false );
	} else {
		cutEdCloseWindow();
	}
}

void demo_AddUI(void)
{
	// test new UI
	{
		UIButton* restartButton;
		UIButton* playButton;
		UIButton* pauseButton;
		UIButton* slowButton;
		UIButton* ffButton;
		UICheckButton* cameraEdCheck;
		UICheckButton* transparentCheck;
		UIButton* quitButton;
            
		restartButton = ui_ButtonCreateImageOnly( "restart_icon", 0, 0, (UIActivationFunc)demo_restart, NULL );
		playButton = ui_ButtonCreateImageOnly(
				"play_icon",
				UI_WIDGET( restartButton )->x + UI_WIDGET( restartButton )->width + 4,
				0, (UIActivationFunc)demo_playPlayback, NULL );
		pauseButton = ui_ButtonCreateImageOnly(
				"pause_icon",
				UI_WIDGET( playButton )->x + UI_WIDGET( playButton )->width + 4,
				0, (UIActivationFunc)demo_pausePlayback, NULL );
		slowButton = ui_ButtonCreateImageOnly(
				"slow_icon",
				UI_WIDGET( pauseButton )->x + UI_WIDGET( pauseButton )->width + 4,
				0, (UIActivationFunc)demo_slowPlayback, NULL );
		ffButton = ui_ButtonCreateImageOnly(
				"ff_icon",
				UI_WIDGET( slowButton )->x + UI_WIDGET( slowButton )->width + 4,
				0, (UIActivationFunc)demo_ffPlayback, NULL );

		cameraEdCheck = ui_CheckButtonCreate( 350, 0, "Camera Path Editor", false );
		#ifndef NO_EDITORS
		{
			ui_CheckButtonSetToggledCallback( cameraEdCheck, (UIActivationFunc)demo_ToggleCutsceneEd, NULL );
		}
		#endif
		transparentCheck = ui_CheckButtonCreate( 350, 16, "Transparent", false );
		ui_CheckButtonSetToggledCallback( transparentCheck, demo_ToggleTransparency, NULL );

		quitButton = ui_ButtonCreateImageOnly(
				"button_close_window_32x32",
				80, 0, (UIActivationFunc)demo_quit, NULL );
		quitButton->widget.offsetFrom = UITopRight;
		ui_WidgetUnskin( UI_WIDGET( quitButton ), CreateColorRGB( 231, 51, 56 ), ColorBlack, ColorBlack, ColorBlack );
				
		demoTimeTicker = ui_LabelCreate( "", 0, ui_WidgetGetNextY( UI_WIDGET( playButton )) + 2 );
		ui_LabelSetFont(demoTimeTicker, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
		ui_WidgetSetDimensionsEx( UI_WIDGET( demoTimeTicker ), 1, 1, UIUnitPercentage, UIUnitPercentage );
		demo_UpdateTimeTicker();
            
		// "Menu" pane
		demoRootPane = ui_PaneCreate( 0, 0, 1.f, 0, UIUnitPercentage, UIUnitFixed, UI_PANE_VP_BOTTOM );
		demoRootPane->widget.priority = 100;
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( restartButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( playButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( pauseButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( slowButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( ffButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( cameraEdCheck ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( transparentCheck ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( quitButton ));
		ui_WidgetAddChild( UI_WIDGET( demoRootPane ), UI_WIDGET( demoTimeTicker ));
		demoRootPane->widget.height = ui_WidgetGetNextY( UI_WIDGET( demoTimeTicker )) + 32;
		ui_WidgetGroupAdd(ui_PaneWidgetGroupForDevice(NULL), UI_WIDGET( demoRootPane ));

		// Modal skin
		ui_ModalDialogSetCustomSkin(ui_SkinCreate(NULL));
	}
}

void gclDemoPlayback_Enter(void)
{
	const char *runOnConnectString = gclGetRunOnConnectString();
	if(runOnConnectString) {
		if(strlen(runOnConnectString)) {
			globCmdParse(runOnConnectString);
		}
	}

	gfxDebugDisableAccessLevelWarnings(true);

	// Reduce stalls and better simulate production
	globCmdParse("disableLastAuthor 1");
	globCmdParse("dontReportErrorsToErrorTracker 1");
	globCmdParse("NoCallStacksOnErrors 1");

    //globCmdParse("ui_ToggleHUD 0"); // Can't run this, breaks things like being able to run /options
    globCmdParse("ForceHideBudget 1");
	//globCmdParse("showfps 0");
	globCmdParse("showcampos 0");

	globCmdParse("noPopUps 0"); // Disable this so run-time Alertfs still show up
	
	globCmdParse("mmDisableNetBufferAdjustment 1");

	ErrorfPushCallback(demoErrorfCallback, NULL);

	resLoadResourcesFromDisk(g_AnimListDict, "ai/animlists", ".al", "AnimLists.bin", PARSER_SERVERSIDE | RESOURCELOAD_SHAREDMEMORY);
	objectLibraryLoad();

	if (g_demo_save_timing)
		timerRecordStart(g_demo_save_timing);

	wlTimeSetForce(s_Demo.startWorldTime);

	gfxDebugDisableAccessLevelWarnings(true);

	demo_restartInternal( true );
	s_start_frame = gfxGetFrameCount();

	if (s_demoPlaybackFixedFps) {
		frameLockedTimerSetFixedRate(gGCLState.frameLockedTimer, 1.f / s_demoPlaybackFixedFps);
	}

	sndSetVelCallback(gclDemoSndVelCB);
	sndSetCameraMatCallback(gclDemoSndCameraMatCB);	
	sndSetPlayerMatCallback(gclDemoSndGetPlayerMatCB);
	sndSetPlayerExistsCallback(gclDemoSndPlayerAlwaysExistsCB);

	// // Save "initial" memory usage in 1 second
	//TimedCallback_Run(demo_saveMemoryUsage, "BEGIN", 1.f);
	// Saved it immediately to be more deterministic
	demo_saveMemoryUsage(NULL, 0, "BEGIN");

    if( !s_demoScreenshotFileNamePrefix ) {
		demo_AddUI();
    }
}

void gclDemoPlayback_BeginFrame(void)
{
    float oldFrameElapsedTime = gGCLState.frameElapsedTime;
    //if( s_demoPlaybackFixedFps ) { // Won't be on the first frame
    //	assert(nearSameF32(gGCLState.frameElapsedTime, 1.0f / s_demoPlaybackFixedFps));
    //}
    
	gGCLState.bDrawWorldThisFrame = true;

	s_timeElapsed += gGCLState.frameElapsedTime;
    s_demoPlaybackTimeElapsed += gGCLState.frameElapsedTime;
	if (demoPlaybackEnd != 0 && s_demoPlaybackTimeElapsed > demoPlaybackEnd)
		s_demoPlaybackTimeElapsed = demoPlaybackEnd;
	gfxRecordSetTimeElapsed(s_demoPlaybackTimeElapsed);
	wlPerfStartUIBudget();
	demo_UpdateTimeTicker();
	wlPerfEndUIBudget();

	demo_RecordFrameCounts();

	{
		/*
		float oldTimeScale = wlTimeGetStepScale();
		int numIterations = 1;
		int it;

		if( !s_demoPlaybackFixedFps ) {
		} else {
			
			wlTimeSetStepScaleDebug( 1.0f );
			numIterations = (int)floor( oldTimeScale );
		}
		numIterations = 1;
		for( it = 0; it < numIterations; ++it ) {
		*/
			mmCreateWorldCollIntegration();
			wcSwapSimulation(gGCLState.frameLockedTimer);
			gclUpdateRoamingCell();

			gclLibsOncePerFrame();
		/*
		}

		wlTimeSetStepScaleDebug( oldTimeScale );
		*/
	}

	wlPerfStartNetBudget();
	demo_EntityUpdateRecordedPositions();
	demo_ReplayMessages(false);
	wlPerfEndNetBudget();

    {
        F32 maxTime;

        if( s_Demo.cameraViews ) {
            maxTime = s_Demo.cameraViews[ eaSize( &s_Demo.cameraViews ) - 1 ]->time;
        } else if( s_Demo.relativeCameraViews ) {
            maxTime = s_Demo.relativeCameraViews[ eaSize( &s_Demo.relativeCameraViews ) - 1 ]->time;
        } else {
            maxTime = 0;
        }
        
        if ( (s_Demo.curMessage + 1 >= (unsigned)eaSize( &s_Demo.messages )
			  && s_demoPlaybackTimeElapsed >= maxTime)
			 || (demoPlaybackEnd != 0 && s_demoPlaybackTimeElapsed >= demoPlaybackEnd))
        {
            demo_Finished();
        }
    }

    if( s_demoScreenshotFileNamePrefix ) {
        char fname[ CRYPTIC_MAX_PATH ];

		assert( s_demoScreenshotFileNamePrefix[ 0 ] != '\0' );
		
        sprintf( fname, "%s_%05d.%s",
				 s_demoScreenshotFileNamePrefix, demo_history.frames,
				 s_demoScreenshotFileNameExt );

		if( stricmp( s_demoScreenshotFileNameExt, "depth" ) == 0 ) {
			gfxSaveScreenshotDepth( fname, s_demoDepthMin, s_demoDepthMax );
		} else if( stricmp( s_demoScreenshotFileNameExt, "jpg" ) == 0 ) {
            gfxSaveJPGScreenshotWithUI( fname );
        } else {
            gfxSaveScreenshotWithUI( fname );
        }
    }

	gfxCheckAutoFrameRateStabilizer();

    gGCLState.frameElapsedTime = oldFrameElapsedTime;
}

void gclDemoPlayback_Leave(void)
{
	gfxDebugDisableAccessLevelWarnings(false);
	ErrorfPopCallback();
}

// This function should get called while loading the demo playback (before gameplay starts).
void demo_LoadReplay(void)
{
	// Construct the demo file's full path
    char fname_buf[1024];

	StructReset( parse_DemoRecording, &s_Demo );

	if( !fileIsAbsolutePath( s_demo_filename ))
	{
		if(s_demo_filename && s_demo_filename[0])
			sprintf(fname_buf, "%s/%s", fileDemoDir(), s_demo_filename);
		else
			sprintf(fname_buf, "%s/%s", fileDemoDir(), "last_recording.demo");
	}
	else
	{
		strcpy( fname_buf, s_demo_filename );
	}

	if (!fileExists(fname_buf)) {
		if (!strchr(fname_buf+1, '.')) { // +1 so that ./demos/blarg works
			strcat(fname_buf, ".demo");
		}
	}
	if (!fileExists(fname_buf)) {
		FatalErrorf("Could not find demo %s.", s_demo_filename);
	}

	if (!ParserReadTextFile(fname_buf, parse_DemoRecording, &s_Demo, PARSER_NOERRORFSONPARSE)) {
		printf( "Demo %s in old format.  Attempting legacy format read.", s_demo_filename );
		StructReset( parse_DemoRecording, &s_Demo );
		demoEnsureLegacyMode();
		if (!ParserReadTextFile(fname_buf, parse_DemoRecording, &s_Demo, 0)) {
			FatalErrorf("Demo in old format or corrupt.  Failed to load demo file %s", s_demo_filename);
		}
	}

	if (strlen(s_demo_override_mapname))
	{
		ZoneMapInfo *demoOverrideMapZMI = NULL;
		StructFreeString(s_Demo.zoneName);
		demoOverrideMapZMI = worldGetZoneMapByPublicName(s_demo_override_mapname);

		s_Demo.zoneName = StructAllocString(zmapInfoGetFilename(demoOverrideMapZMI));
	}

	if (demoEnableFullHistory)
		demo_allocFullFrameHistory(&s_Demo);

	{
		size_t orig_size = StructGetMemoryUsage(parse_DemoRecording, &s_Demo, true);
		bool bPruned = demo_prune(&s_Demo);
		size_t new_size = StructGetMemoryUsage(parse_DemoRecording, &s_Demo, true);
		if (bPruned) {
			printf("Pruned from %d to %d bytes\n", orig_size, new_size);
			if (strStartsWith(fname_buf, "c:"))
			{
				// Definitely a local file, write out the pruned version
				ParserWriteTextFile(fname_buf, parse_DemoRecording, &s_Demo, 0, 0);
			}
		}
	}

	if(s_demo_filename)
		StructFreeString(s_demo_filename);
	s_demo_filename = StructAllocString(fname_buf);
	
	// Setup the first camera frame
	demo_setFirstDemoFrame();
}

// This function sholud be called after the zonemap has been loaded, in pair with demo_LoadReplay.
void demo_LoadReplayLate( void )
{
	// Make sure to get that callback registered.
	mapState_RegisterDictListeners();
	
	// Replay any messages that got sent during loading
	demo_EntityUpdateRecordedPositions();
	demo_ReplayMessages(true);
}

Entity* demo_GetActivePlayer()
{
	return entFromEntityRefAnyPartition(s_Demo.activePlayerRef);
}

static void demo_Finished( void )
{
//	StructReset( parse_DemoRecording, &s_Demo );
	
    --playbackTimesRemaining;

    if( playbackTimesRemaining > 0 ) {
        // The first playthrough of a demo contains a lot of shader
        // loading.  Ignoring this playthrough greatly enhances the
        // accuracy of stats.
        if( playbackTimesRemaining == playbackTimesTotal - 1 ) {
            s_timeElapsed = 0;
            s_start_frame = gfxGetFrameCount();
            ZeroStruct( &demo_history );
        }
        
        demo_restart();
    } else {
        // Demo playback finished
        if( !s_bDidDisplayResults ) {
            demo_displayResults();

            if( s_demoScreenshotFileNamePrefix ) {
                free( (char*)s_demoScreenshotFileNamePrefix );
                s_demoScreenshotFileNamePrefix = NULL;
            }
            if( s_demoScreenshotFileNameExt ) {
                free( (char*)s_demoScreenshotFileNameExt );
                s_demoScreenshotFileNameExt = NULL;
            }
            s_bDidDisplayResults = true;
        }
    }

	gfxSkyUnsetCustomDOF(gfxGetActiveCameraView()->sky_data, false, 0.0f);
}

/// Resume playback at normal speed for a currently playing demo.
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 ) ACMD_HIDE;
void demo_playPlayback( void ) {
    wlTimeSetStepScaleDebug( 1.0f );
	if( s_demoPlaybackFixedFps ) {
		frameLockedTimerSetFixedRate(gGCLState.frameLockedTimer, 1.f / s_demoPlaybackFixedFps);
	}
}

/// Pause playback for a currently playing demo. 
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 ) ACMD_HIDE;
void demo_pausePlayback( void ) {
    wlTimeSetStepScaleDebug( 0.0f );
	if( s_demoPlaybackFixedFps ) {
		frameLockedTimerSetFixedRate(gGCLState.frameLockedTimer, 0.0f );
	}
}

/// Toggle play/pause speed.
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 ) ACMD_HIDE;
void demo_playPauseToggle( void ) {
	float timeScale = wlTimeGetStepScaleDebug();

	if( timeScale == 0 ) {
		demo_playPlayback();
	} else {
		demo_pausePlayback();
	}
}

/// Slow playback for a currently playing demo.
///
/// Subsequent presses will slow down the demo playback more and more.
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 ) ACMD_HIDE;
void demo_slowPlayback( void ) {
	float timeScale = wlTimeGetStepScaleDebug();
	float newTimeScale;

	if( timeScale > 1 ) {
		newTimeScale = 1;
	} else if( timeScale > 1.0f / 32.0f ) {
		newTimeScale = timeScale / 2.0f;
	} else {
		newTimeScale = 1.0f;
	}

	wlTimeSetStepScaleDebug( newTimeScale );
	if( s_demoPlaybackFixedFps ) {
		frameLockedTimerSetFixedRate(gGCLState.frameLockedTimer, newTimeScale / s_demoPlaybackFixedFps );
	}
}

/// Fast-Forward playback for a currently playing demo.
///
/// Subsequent presses will speed up the demo playback more and more.
AUTO_COMMAND ACMD_CATEGORY( Interface ) ACMD_ACCESSLEVEL( 0 ) ACMD_HIDE;
void demo_ffPlayback( void ) {
    float timeScale = wlTimeGetStepScaleDebug();
	float newTimeScale;

    if( timeScale < 2.0f ) {
        newTimeScale = 2.0f;
    } else if( timeScale < 32.0f ) {
		newTimeScale = 2 * timeScale;
    } else {
		newTimeScale = 1.0f;
    }

	wlTimeSetStepScaleDebug( newTimeScale );
	if( s_demoPlaybackFixedFps ) {
		frameLockedTimerSetFixedRate(gGCLState.frameLockedTimer, newTimeScale / s_demoPlaybackFixedFps );
	}
}

void demo_quit( void )
{
	if( ui_ModalDialog( "Quit Playback", "Are you sure you want to quit playing back this demo file?", ColorBlack, UIYes | UINo ) == UIYes ) {
		// NOTE: if there is a better way to do this, feel free to
		// replace this code with the apropriate way to quit
		globCmdParse( "quit" );
	}
}

// Resets all graphical options to the default for the current hardware and sets
// some other options appropriate for a fixed demo playback.
AUTO_COMMAND ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);
void demoOptions(int enable)
{
	assertmsgf(demo_playingBack(), "Calling DemoOptions before calling DemoPlay.  This can happen if you have a demo filename with a space in it.");

	if (enable) {
		// DJR disabling to allow each perf tester to use gameprefs file
		//globCmdParse("resetOptions 1");
		//globCmdParse("fullscreen 1280 720"); // JE: nor forcing the resolution so that the default graphics settings can determine the resolution for us.
		globCmdParse("demoFullHistoryCSV 1");
		globCmdParse("vsync 0");
		globCmdParse("maxInactiveFPS 0");
		globCmdParse("frameRateStabilizer 0");
		globCmdParse("autoEnableFrameRateStabilizer 0");
		globCmdParse("maxFPS 0");
	}
}

extern ParseTable parse_SavedContainerRef[];
#define TYPE_parse_SavedContainerRef SavedContainerRef

static void demoEnsureLegacyMode( void )
{
	const ParseTable emptyPT = { "", 0, 0 };
	
	int readIt;
	int writeIt = 0;



	char *pFieldString = NULL;
	char **ppFieldNames = NULL;

	assertmsgf(GetStringFromTPIFormatString(&parse_SavedEntityData[0], "DEMO_NO_IGNORE_FIELDS", &pFieldString), "SavedEntityData doesn't have DEMO_NO_IGNORE_FIELDS file");

	DivideString(pFieldString, " ,", &ppFieldNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	FORALL_PARSETABLE(parse_SavedEntityData, readIt) {
		parse_SavedEntityDataDemo = realloc( parse_SavedEntityDataDemo, sizeof( ParseTable ) * (writeIt + 1) );

		if( eaFindString(&ppFieldNames, parse_SavedEntityData[readIt].name) == -1)
		{
			parse_SavedEntityDataDemo[ writeIt ] = emptyPT;
			parse_SavedEntityDataDemo[ writeIt ].name = parse_SavedEntityData[ readIt ].name;
			if(   TOK_GET_TYPE(parse_SavedEntityData[ readIt ].type) == TOK_STRUCT_X
				  && parse_SavedEntityData[ readIt ].subtable != parse_SavedContainerRef) {
				parse_SavedEntityDataDemo[ writeIt ].type = TOK_IGNORE | TOK_IGNORE_STRUCT;
			} else {
				parse_SavedEntityDataDemo[ writeIt ].type = TOK_IGNORE;
			}
		} else {
			parse_SavedEntityDataDemo[ writeIt ] = parse_SavedEntityData[ readIt ];
		}
		
		++writeIt;
	}

	eaDestroyEx(&ppFieldNames, NULL);

	// add removed fields, fTotalPlayTime
	parse_SavedEntityDataDemo = realloc( parse_SavedEntityDataDemo, sizeof( ParseTable ) * (writeIt + 1) );
	parse_SavedEntityDataDemo[ writeIt ] = emptyPT;
	parse_SavedEntityDataDemo[ writeIt ].name = "fTotalPlayTime";
	parse_SavedEntityDataDemo[ writeIt ].type = TOK_IGNORE;
	++writeIt;
	
	parse_SavedEntityDataDemo = realloc( parse_SavedEntityDataDemo, sizeof( ParseTable ) * (writeIt + 1) );
	parse_SavedEntityDataDemo[ writeIt ] = emptyPT;
	++writeIt;

	{
		int col;
		if( !ParserFindColumn( parse_RecordedEntity, "entityAttach", &col )) {
			FatalErrorf( "Could not find column %s.", "entityAttach" );
		}

		parse_RecordedEntity[ col ].subtable = parse_SavedEntityDataDemo;
	}
	
	ParserSetTableInfo(parse_SavedEntityDataDemo, sizeof(SavedEntityData), "SavedEntityDataDemo", NULL, "gclDemo.c", false, true);
	
}

#include "gclDemo_h_ast.c"
#include "gclDemo_c_ast.c"
