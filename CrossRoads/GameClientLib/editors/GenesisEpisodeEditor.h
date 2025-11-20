#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

//// An editor for Genesis Episodes.

#ifndef NO_EDITORS

#include "EditorManager.h"

typedef struct EpisodeEditDoc EpisodeEditDoc;
typedef struct GenesisEpisode GenesisEpisode;
typedef struct MEField MEField;
typedef struct UIGimmeButton UIGimmeButton;
typedef struct GEMissionLevelDefGroup GEMissionLevelDefGroup;

#define EPISODE_EDITOR "Episode Editor"
#define DEFAULT_EPISODE_NAME "New_Episode"

typedef struct GEPUndoData 
{
	GenesisEpisode *pPreEpisode;
	GenesisEpisode *pPostEpisode;
} GEPUndoData;

typedef struct GEPMissionInfoGroup
{
	EpisodeEditDoc *pDoc;

	// Infrastructure
	UIExpander* pExpander;

	// Editable fields
	UILabel *pDisplayNameLabel;
	MEField *pDisplayNameField;
	
	UILabel *pShortTextLabel;
	MEField *pShortTextField;
	
	UILabel *pDescriptionTextLabel;
	MEField *pDescriptionTextField;
	
	UILabel *pSummaryTextLabel;
	MEField *pSummaryTextField;
	
	UILabel *pCategoryLabel;
	MEField *pCategoryField;
	
	UILabel *pRequirementsLabel;
	MEField *pRequirementsField;
	
	UILabel *pRewardLabel;
	MEField *pRewardField;
	
	UILabel* pRewardScaleLabel;
	MEField *pRewardScaleField;

	GEMissionLevelDefGroup* pLevelDefGroup;
} GEPMissionInfoGroup;

typedef struct GEPMissionStartGroup
{
	EpisodeEditDoc *pDoc;

	// Infrastructure
	UIExpander* pExpander;

	// Editable fields
	UILabel *pNeedsReturnLabel;
	MEField *pNeedsReturnField;
	
	UILabel *pReturnTextLabel;
	MEField *pReturnTextField;
} GEPMissionStartGroup;

typedef struct GEPEpisodePartGroup
{
	EpisodeEditDoc *pDoc;

	// Infrastructure
	int index;
	UIExpanderGroup* pExpanderGroup;
	UIExpander* pExpander;
	UIExpander* pTransitionExpander;
	UIButton *pDelButton;

	// Editable fields (main expander)
	UILabel *pNameLabel;
	MEField *pNameField;
	
	UILabel *pMapDescLabel;
	MEField *pMapDescField;
	UIButton *pMapDescEditButton;
	
	UILabel *pMissionLabel;
	MEField *pMissionField;
	
	UILabel *pContinueFromLabel;
	MEField *pContinueFromField;
	
	UILabel *pContinueRoomLabel;
	MEField *pContinueRoomField;
	
	UILabel *pContinueChallengeLabel;
	MEField *pContinueChallengeField;

	UILabel *pContinueMissionTextLabel;
	MEField *pContinueMissionTextField;

	UILabel *pContinuePromptUsePetCostumeLabel;
	MEField *pContinuePromptUsePetCostumeField;

	UILabel *pContinuePromptCostumeLabel;
	MEField *pContinuePromptCostumeField;

	UILabel *pContinuePromptPetCostumeLabel;
	MEField *pContinuePromptPetCostumeField;
	
	UILabel *pContinuePromptButtonTextLabel;
	MEField *pContinuePromptButtonTextField;
	
	UILabel *pContinuePromptCategoryLabel;
	MEField *pContinuePromptCategoryField;
	
	UILabel *pContinuePromptPriorityLabel;
	MEField *pContinuePromptPriorityField;
	
	UILabel *pContinuePromptTitleTextLabel;
	MEField *pContinuePromptTitleTextField;
	
	UILabel *pContinuePromptBodyTextLabel;
	MEField **eaContinuePromptBodyTextField;
	UIButton **eaContinuePromptBodyTextAddRemoveButtons;

	// editable fields (transition expander)
	UILabel *pContinueTransitionOverrideLabel;
	MEField *pContinueTransitionOverrideField;
	
	UILabel *pNextStartTransitionOverrideLabel;
	MEField *pNextStartTransitionOverrideField;

	char** eaMissionNames;
	char** eaRoomNames;
	char** eaChallengeNames;
} GEPEpisodePartGroup;

typedef struct EpisodeEditDoc
{
	EMEditorDoc emDoc;

	GenesisEpisode *pEpisode;
	GenesisEpisode *pOrigEpisode;
	GenesisEpisode *pNextUndoEpisode;

	bool bIgnoreFieldChanges;
	bool bIgnoreFilenameChanges;

	// Main window controls
	UIWindow *pMainWindow;
	UILabel *pFilenameLabel;
	UIGimmeButton *pFileButton;
	UIScrollArea *pPartArea;
	UIExpanderGroup *pMissionExpanderGroup;
	UIButton *pAddPartButton;

	// Simple fields
	MEField **eaDocFields;

	// Groups
	GEPMissionInfoGroup *pMissionInfoGroup;
	GEPMissionStartGroup *pMissionStartGroup;
	GEPEpisodePartGroup **eaEpisodePartGroups;
} EpisodeEditDoc;

EpisodeEditDoc *GEPOpenEpisode(EMEditor *pEditor, char *pcName);
void GEPRevertEpisode(EpisodeEditDoc *pDoc);
void GEPCloseEpisode(EpisodeEditDoc *pDoc);
EMTaskStatus GEPSaveEpisode(EpisodeEditDoc* pDoc, bool bSaveAsNew);
void GEPInitData(EMEditor *pEditor);

#endif
