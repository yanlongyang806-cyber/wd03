//// Shared interface for the UGC project chooser.
#pragma once

typedef struct PossibleUGCProject PossibleUGCProject;
typedef struct PossibleUGCProjects PossibleUGCProjects;
typedef struct UGCProjectReviews UGCProjectReviews;

//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in STUGCProjectChooser.c or NNOUGCProjectChooser.c
void ugcProjectChooserInit(void);
void ugcProjectChooserFree(void);
bool ugcProjectChooser_IsOpen(void);
void ugcProjectChooserShow(void);
void ugcProjectChooserHide(void);

void ugcProjectChooserSetPossibleProjects(PossibleUGCProjects* pProjects);
void ugcProjectChooserSetImportProjects(PossibleUGCProject **eaProjects);
void ugcProjectChooserReceiveMoreReviews(U32 iProjectID, U32 iSeriesID, int iPageNumber, UGCProjectReviews *pReviews);
void ugcProjectChooser_FinishedLoading(void);

typedef enum UGCProjectChooserMode
{
	UGC_PROJECT_CHOOSER_MODE_CHOOSE_PROJECT,
	UGC_PROJECT_CHOOSER_MODE_EDIT_SERIES,
} UGCProjectChooserMode;

void ugcProjectChooser_SetMode( UGCProjectChooserMode eMode );
