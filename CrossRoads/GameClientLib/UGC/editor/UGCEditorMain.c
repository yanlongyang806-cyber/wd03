#include "UGCEditorMain.h"

#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "GameClientLib.h"
#include "GfxCamera.h"
#include "GlobalStateMachine.h"
#include "Message.h"
#include "MultiEditField.h"
#include "StringFormat.h"
#include "UGCCommon.h"
#include "UGCProjectChooser.h"
#include "UGCProjectCommon.h"
#include "UIButton.h"
#include "UICore.h"
#include "UISMFView.h"
#include "UIScrollbar.h"
#include "UIWindow.h"
#include "gclBaseStates.h"
#include "textparser.h"
#include "gclEntity.h"
#include "UGCAchievements.h"

#include "UGCProjectCommon_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

int ugcNeverTimeout = false;
AUTO_CMD_INT(ugcNeverTimeout, ugcNeverTimeout);

typedef struct UGCEditorModalDialog
{
	// Modal dialog
	UIWindow* modalWindow;
	UIWidget** modalWidgets;
	MEField** modalFields;

	UIButton* button1;
	UIButton* button2;
} UGCEditorModalDialog;

static UGCEditorModalDialog g_UGCModalDialog;

void ugcEditorShowEULA(UIActivationFunc yesCB)
{
	UISMFView* pSMFView = NULL;
	UIScrollArea* pScrollArea = NULL;
	const char *eulaTxt = NULL;

	if( yesCB ) {
		ugcModalDialogPrepare( TranslateMessageKey( "UGC.CreateProjectEULA_Title" ),
							   TranslateMessageKey( "UGC.Yes" ), yesCB, TranslateMessageKey( "UGC.No" ), NULL, NULL );
	} else {
		ugcModalDialogPrepare( TranslateMessageKey( "UGC.CreateProjectEULA_Title" ),
							   TranslateMessageKey( "UGC.Ok" ), NULL, NULL, NULL, NULL );
	}

	pScrollArea = ui_ScrollAreaCreate(0,0,1,1,1,1,false,true);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pScrollArea), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pScrollArea), 0,0,0,40);
	pScrollArea->autosize = 1;
	ugcModalDialogAddWidget( UI_WIDGET( pScrollArea ));

	pSMFView = ui_SMFViewCreate(0, 0, 0, 0);
	ui_SMFViewSetText(pSMFView, TranslateMessageKey("UGC.CreateProjectEULA"), NULL);
	ui_WidgetSetWidth(UI_WIDGET(pSMFView), 390);
	ui_ScrollAreaAddChild(pScrollArea, pSMFView);

	ugcModalDialogShow( 410, 450 );
}

void ugcEditorSetCamera(void)
{
	gfxInitCameraController(emGetWorldCamera(), NULL, NULL);
}

bool ugcEditorUpdateReviews(NOCONST(UGCProjectReviews)* pReviews, int* piPageNumber, const UGCProjectReviews* pNewReviews, int iNewPageNumber)
{
	if(pReviews && pNewReviews && iNewPageNumber > *piPageNumber)
	{
		UGCSingleReview** pNewSingleReviews = NULL;
		
		StructCopyDeConst(parse_UGCProjectReviews, pNewReviews, pReviews, 0, 0, TOK_EARRAY);
		eaiCopy( &pReviews->piNumRatings, &pNewReviews->piNumRatings );
		eaPushEArray(&pNewSingleReviews, &pNewReviews->ppReviews);
		eaQSort( pNewSingleReviews, ugcReviews_SortByTimestamp );
		eaPushStructsDeConst(&pReviews->ppReviews, &pNewReviews->ppReviews, parse_UGCSingleReview);
		eaDestroy(&pNewSingleReviews);
		
		*piPageNumber = iNewPageNumber;
		return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////
// Modal dialogs
void ugcModalDialogClose( UIButton* ignored, UserData ignored2 )
{
	if( g_UGCModalDialog.modalWindow ) {
		ui_WindowClose( g_UGCModalDialog.modalWindow );
	}
}

void ugcModalDialogPrepare( const char* title,
							const char* button1Text, UIActivationFunc button1Fn, const char* button2Text, UIActivationFunc button2Fn,
							UserData data )
{	
	eaClearEx( &g_UGCModalDialog.modalWidgets, ui_WidgetQueueFree );
	eaClearEx( &g_UGCModalDialog.modalFields, MEFieldDestroy );
	if( !g_UGCModalDialog.modalWindow ) {
		g_UGCModalDialog.modalWindow = ui_WindowCreate( "Dialog Text", 0, 0, 400, 400 );
		ui_WindowSetModal( g_UGCModalDialog.modalWindow, true );
		ui_WindowSetResizable( g_UGCModalDialog.modalWindow, false );
	}
	ui_WidgetGroupQueueFree( &g_UGCModalDialog.modalWindow->widget.children );
	ui_WindowSetTitle( g_UGCModalDialog.modalWindow, title );

	if( !button1Text & !button2Text ) {
		g_UGCModalDialog.button1 = ui_ButtonCreate( "OK", 0, 0, ugcModalDialogClose, data );
		ui_WidgetSetPositionEx( UI_WIDGET( g_UGCModalDialog.button1 ), 0, 0, 0, 0, UIBottomRight );
		ui_WidgetSetWidth( UI_WIDGET( g_UGCModalDialog.button1 ), 80 );
		ui_WindowAddChild( g_UGCModalDialog.modalWindow, g_UGCModalDialog.button1 );
		g_UGCModalDialog.button2 = NULL;
	} else if( !button2Text ) {
		g_UGCModalDialog.button1 = ui_ButtonCreate( button1Text, 0, 0, button1Fn ? button1Fn : ugcModalDialogClose, data );
		ui_WidgetSetPositionEx( UI_WIDGET( g_UGCModalDialog.button1 ), 0, 0, 0, 0, UIBottomRight );
		ui_WidgetSetWidth( UI_WIDGET( g_UGCModalDialog.button1 ), 80 );
		ui_WindowAddChild( g_UGCModalDialog.modalWindow, g_UGCModalDialog.button1 );
		g_UGCModalDialog.button2 = NULL;
	} else {
		g_UGCModalDialog.button1 = ui_ButtonCreate( button1Text, 0, 0, button1Fn ? button1Fn : ugcModalDialogClose, data );
		ui_WidgetSetPositionEx( UI_WIDGET( g_UGCModalDialog.button1 ), 82, 0, 0, 0, UIBottomRight );
		ui_WidgetSetWidth( UI_WIDGET( g_UGCModalDialog.button1 ), 80 );
		ui_WindowAddChild( g_UGCModalDialog.modalWindow, g_UGCModalDialog.button1 );
		
		g_UGCModalDialog.button2 = ui_ButtonCreate( button2Text, 0, 0, button2Fn ? button2Fn : ugcModalDialogClose, data );
		ui_WidgetSetPositionEx( UI_WIDGET( g_UGCModalDialog.button2 ), 0, 0, 0, 0, UIBottomRight );
		ui_WidgetSetWidth( UI_WIDGET( g_UGCModalDialog.button2 ), 80 );
		ui_WindowAddChild( g_UGCModalDialog.modalWindow, g_UGCModalDialog.button2 );
	}

	ui_WindowHide( g_UGCModalDialog.modalWindow );
}

void ugcModalDialogAddWidget( UIWidget* widget )
{
	ui_WindowAddChild( g_UGCModalDialog.modalWindow, widget );
	eaPush( &g_UGCModalDialog.modalWidgets, widget );
}

void ugcModalDialogAddField( MEField* field )
{
	ui_WindowAddChild( g_UGCModalDialog.modalWindow, field->pUIWidget );
	eaPush( &g_UGCModalDialog.modalFields, field );
}

void ugcModalDialogShow( int width, int height )
{
	ui_WidgetSetDimensions( UI_WIDGET( g_UGCModalDialog.modalWindow ), width, height + 50 );
	elUICenterWindow( g_UGCModalDialog.modalWindow );
	ui_WindowShowEx( g_UGCModalDialog.modalWindow, true );
}

UIWidget* ugcModalDialogContentParent( void )
{
	return UI_WIDGET( g_UGCModalDialog.modalWindow );
}

UIWidget* ugcModalDialogButton1( void )
{
	return UI_WIDGET( g_UGCModalDialog.button1 );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclUGCPublishDisabled(bool bUGCPublishDisabled)
{
	UGCEditorDoUGCPublishDisabled(bUGCPublishDisabled);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclUGCProcessPlayResult(UGCPlayResult *result)
{
	gclUGCDoProcessPlayResult( result );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void UGCEditorAutosaveDeletionCompleted(int iAutosaveType)
{
	UGCEditorDoAutosaveDeletionCompleted( iAutosaveType );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void UGCEditorUpdateSaveStatus(bool succeeded, char *error)
{
	UGCEditorDoUpdateSaveStatus( succeeded, error );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void UGCEditorWaitForResourcesComplete(void)
{
	UGCEditorDoWaitForResourcesComplete();
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void ReceiveCryptKeyForSafeProjectExport(char *pKey, int iExportID)
{
	DoReceiveCryptKeyForSafeProjectExport( pKey, iExportID );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_HIDE ACMD_PRIVATE ACMD_ACCESSLEVEL(0); 
void SafeImportBufferResult(int iID, int iResult)
{
	DoSafeImportBufferResult( iID, iResult );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void UGCEditorUpdatePublishStatus(bool succeeded, const char *pDisplayString)
{
	UGCEditorDoUpdatePublishStatus( succeeded, pDisplayString );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void ugcEditorAuthorAllowsFeaturedChanged( bool bSucceeded )
{
	ugcEditorDoAuthorAllowsFeaturedChanged( bSucceeded );
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void UGCProjectJobStatus(UGCProjectStatusQueryInfo *pInfo)
{
	DoUGCProjectJobStatus( pInfo );

}

void gclUGC_SendAchievementEvent(UGCAchievementEvent *pUGCAchievementEvent)
{
	UGCAccount *pUGCAccount = entGetUGCAccount(entActivePlayerPtr());
	if(pUGCAccount) // Pre-filter events against achievements, if the account data already exists
	{
		// We first see if any account achievement will end up matching the event before flooding the network with events that are never cared about.
		FOR_EACH_IN_EARRAY_FORWARDS(pUGCAccount->author.ugcAccountAchievements.eaAchievements, UGCAchievement, ugcAchievement)
		{
			UGCAchievementDef *pUGCAchievementDef = ugcAchievement_DefFromRefString(ugcAchievement->ugcAchievementName);
			if(pUGCAchievementDef)
				// NOTE - to support our ability to increase/decrease the target required to gain an achievement, we support the ability for someone to have reached an
				// achievement at the original target. Therefore, we do not check if count is less than target; we just check the achieved status.
				if(!ugcAchievement->uGrantTime)
				{
					U32 increment = ugcAchievement_EventFilter(&pUGCAchievementDef->ugcAchievementFilter, pUGCAchievementEvent);
					if(increment)
					{
						ServerCmd_gslUGC_SendAchievementEvent(pUGCAchievementEvent);
						return;
					}
				}
		}
		FOR_EACH_END;
	}
	else // If the account does not exist, just send the event so that the account can be created and achievements synchronized
		ServerCmd_gslUGC_SendAchievementEvent(pUGCAchievementEvent);
}
