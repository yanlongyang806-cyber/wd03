#include"UGCBugReport.h"

#include"EditLibUIUtil.h"
#include"EntitySavedData.h"
#include"GameStringFormat.h"
#include"GfxSpriteText.h"
#include"GlobalTypes.h"
#include"MultiEditFieldContext.h"
#include"Player.h"
#include"Player.h"
#include"StringUtil.h"
#include"UIWindow.h"
#include"file.h"
#include"gclEntity.h"
#include"ticketnet.h"
#include"trivia.h"
#include"utilitiesLib.h"

#include"Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

// MJF: This seems like a silly way to get access to the trivia.  Why
// not declared in trivia.h?
extern TriviaData** g_ppTrivia;

typedef enum UGCBugReportWindowState {
	UGCBUG_NONE,
	UGCBUG_TICKET_SEARCH,
	UGCBUG_SUBMIT_NEW_BUG,
	UGCBUG_SUBMIT_DETAILS,
} UGCBugReportWindowState;

/// User-facing data for the bug window
AUTO_STRUCT;
typedef struct UGCBugReportModel
{
	char* strSearchText;		AST( NAME(SearchText) )

	char* strIssueSummary;		AST( NAME(IssueSummary) )
	char* strIssueDetails;		AST( NAME(IssueDetails) )

	TicketRequestResponse* ticketToAddDetails;
} UGCBugReportModel;
extern ParseTable parse_UGCBugReportModel[];
#define TYPE_parse_UGCBugReportModel UGCBugReportModel

typedef struct UGCBugReportWindow
{
	UGCBugReportWindowState state;
	UIWindow* window;
	UIList* ticketList;
	UIButton* submitButton;
	bool queueUpdate;

	UGCBugReportModel uiModel;
	TicketRequestResponseList resultModel;
} UGCBugReportWindow;

static UGCBugReportWindow g_ugcBugReport;

static void ugc_ReportBugUpdateUI( void );
static bool ugc_ReportBugFreeCB( UIWindow* ignored, UserData ignored2 );
static void ugc_ReportBugSearch( UIWidget* ignored, UserData ignored2 );
static void ugc_ReportBugQueueUpdateUICB( UIList* ignored, UserData ignored2 );
static void ugc_SubmitTicket( UIButton* ignored, UserData ignored2 );
static void ugc_SubmitTicketDetails( UIButton* ignored, UserData ignored2 );
static void ugc_SubmitNewTicket( UIButton* ignored, UserData ignored2 );
static void ugc_FinishSubmitTicket( UIButton* ignored, UserData ignored2 );

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ugc_ReportBugFieldChangedCB( MEField *pField, bool bFinished, UserData unused )
{
	if( bFinished ) {
		g_ugcBugReport.queueUpdate = true;
	}

	if( g_ugcBugReport.submitButton ) {
		ui_SetActive( UI_WIDGET( g_ugcBugReport.submitButton ),
					  !nullStr( g_ugcBugReport.uiModel.strIssueSummary )
					  && !nullStr( g_ugcBugReport.uiModel.strIssueDetails ));
	}
}

AUTO_COMMAND;
void ugc_ReportBug( void )
{
	if( g_ugcBugReport.window ) {
		return;
	}
	devassert( g_ugcBugReport.state == UGCBUG_NONE );
	g_ugcBugReport.state = UGCBUG_TICKET_SEARCH;

	g_ugcBugReport.window = ui_WindowCreate( NULL, 0, 0, 1, 1 );
	ui_WidgetSetTextMessage( UI_WIDGET( g_ugcBugReport.window ), "UGC_Bugs.Window_Title" );
	ui_WindowSetDimensions( g_ugcBugReport.window, 500, 500, 500, 500 );
	ui_WindowSetResizable( g_ugcBugReport.window, false );
	elUICenterWindow( g_ugcBugReport.window );
	ui_WindowSetModal( g_ugcBugReport.window, true );
	ui_WindowSetCloseCallback( g_ugcBugReport.window, ugc_ReportBugFreeCB, NULL );

	ugc_ReportBugSearch( NULL, NULL );
	ugc_ReportBugUpdateUI();

	ui_WindowShowEx( g_ugcBugReport.window, true );
}

bool ugc_ReportBugFreeCB( UIWindow* ignored, UserData ignored2 )
{
	MEContextDestroyByName( "UGCReportBug" );
	ui_WidgetQueueFreeAndNull( &g_ugcBugReport.window );
	g_ugcBugReport.ticketList = NULL;
	
	g_ugcBugReport.state = UGCBUG_NONE;
	return true;
}

static void ugc_TicketSummaryCB( UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput )
{
	TicketRequestResponse* ticket = (*pList->peaModel)[ iRow ];

	estrClear( estrOutput );
	StringStripTagsPrettyPrintEx( ticket->pSummary, estrOutput, true );
}

static void ugc_TicketLastSeenCB( UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput )
{
	TicketRequestResponse* ticket = (*pList->peaModel)[ iRow ];
	U32 curTime = timeSecondsSince2000();
	U32 diffTime = curTime < ticket->uLastTime ? 0 : curTime - ticket->uLastTime;

	if( ticket->uID == 0 ) {
		estrClear( estrOutput ); 
	} else {
		estrClear( estrOutput );

		if( diffTime >= SECONDS_PER_DAY ) {
			U32 time = diffTime / SECONDS_PER_DAY;
			FormatGameMessageKey( estrOutput, "Ticket.Time.Days", STRFMT_INT("Count", time), STRFMT_END );
		} else if( diffTime >= SECONDS_PER_HOUR ) {
			U32 time = diffTime / SECONDS_PER_HOUR;
			FormatGameMessageKey( estrOutput, "Ticket.Time.Hours", STRFMT_INT("Count", time), STRFMT_END );
		} else {
			U32 time = MAX( diffTime / 60, 1 );
			FormatGameMessageKey( estrOutput, "Ticket.Time.MinutesShort", STRFMT_INT("Count", time), STRFMT_END );
		}
	}
}

static void ugc_TicketCountCB( UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput )
{
	TicketRequestResponse* ticket = (*pList->peaModel)[ iRow ];

	if( ticket->uID == 0 ) {
		estrClear( estrOutput );
	} else {
		estrPrintf( estrOutput, "%d", ticket->uCount );
	}
}

static void ugc_TicketStatusCB( UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput )
{
	TicketRequestResponse* ticket = (*pList->peaModel)[ iRow ];

	if( ticket->uID == 0 ) {
		estrClear( estrOutput );
	} else {
		estrPrintf( estrOutput, "%s", TranslateMessageKey( ticket->pStatus ));
	}
}

void ugc_ReportBugUpdateUI( void )
{
	MEFieldContext* context = MEContextPush( "UGCReportBug", NULL, &g_ugcBugReport.uiModel, parse_UGCBugReportModel );
	MEContextSetParent( UI_WIDGET( g_ugcBugReport.window ));
	context->bTextEntryTrimWhitespace = true;
	context->cbChanged = ugc_ReportBugFieldChangedCB;
	
	g_ugcBugReport.submitButton = NULL;
	g_ugcBugReport.ticketList = NULL;

	if( g_ugcBugReport.state == UGCBUG_TICKET_SEARCH ) {
		TicketRequestResponse* selected = NULL;
			
		ui_WidgetSetTextMessage( UI_WIDGET( g_ugcBugReport.window ), "UGC_Bugs.Window_Title" );
		{
			MEFieldContextEntry* entry = MEContextAddTextMsg( false, NULL, "SearchText", "Ticket.Keyword.Text", NULL );
			MEContextEntryAddActionButtonMsg( entry, "Ticket.Keyword.Button", NULL, ugc_ReportBugSearch, NULL, -1, NULL );
			ui_TextEntrySetEnterCallback( ENTRY_FIELD( entry )->pUIText, ugc_ReportBugSearch, NULL );
		}
		{
			MEFieldContextEntry* entry = MEContextAddCustom( "SearchResults" );
			UIList* list = (UIList*)ENTRY_WIDGET( entry );
		
			if( !list ) {
				UIListColumn* column;
				
				list = ui_ListCreate( parse_TicketRequestResponse, &g_ugcBugReport.resultModel.ppTickets, 32 );
				ENTRY_WIDGET( entry ) = UI_WIDGET( list );
				ui_WindowAddChild( g_ugcBugReport.window, UI_WIDGET( list ));

				column = ui_ListColumnCreateTextMsg( "Ticket.Header.Top10Summary", ugc_TicketSummaryCB, NULL );
				ui_ListColumnSetWidth( column, false, 300 );
				ui_ListAppendColumn( list, column );

				column = ui_ListColumnCreateTextMsg( "Ticket.Header.Time", ugc_TicketLastSeenCB, NULL );
				ui_ListColumnSetWidth( column, true, 1 );
				ui_ListColumnSetResizable( column, false );
				ui_ListAppendColumn( list, column );

				column = ui_ListColumnCreateTextMsg( "Ticket.Header.Count", ugc_TicketCountCB, NULL );
				ui_ListColumnSetWidth( column, true, 1 );
				ui_ListColumnSetResizable( column, false );
				ui_ListAppendColumn( list, column );

				column = ui_ListColumnCreateText( "Ticket.Header.Status", ugc_TicketStatusCB, NULL );
				ui_ListColumnSetWidth( column, true, 1 );
				ui_ListColumnSetResizable( column, false );
				ui_ListAppendColumn( list, column );
			}
			selected = ui_ListGetSelectedObject( list );
			ui_ListSetSelectedCallback( list, ugc_ReportBugQueueUpdateUICB, NULL );
			g_ugcBugReport.ticketList = list;
			list->bLastRowHasSingleColumn = true;

			ui_WidgetSetPosition( UI_WIDGET( list ), 0, MEContextGetCurrent()->iYPos );
			ui_WidgetSetDimensionsEx( UI_WIDGET( list ), 1, 250, UIUnitPercentage, UIUnitFixed );
			MEContextGetCurrent()->iYPos += 250;
		}

		MEContextAddLabelMsg( "DescriptionHeader", "Ticket.Header.Description", NULL );
		
		if( selected ) {
			MEFieldContextEntry* entry = MEContextAddLabel( "DescriptionText",
															(nullStr( selected->pDescription ) ? TranslateMessageKey( "UGC_Bugs.NoDescription" ) : selected->pDescription),
															NULL );
			ui_LabelSetWordWrap( ENTRY_LABEL( entry ), true );
			ui_LabelSetWidthNoAutosize( ENTRY_LABEL( entry ), 1, UIUnitPercentage );
		} else {
			MEContextAddLabelMsg( "DescriptionText", "Ticket.NoTicketSelected", NULL );
		}
		MEContextStepDown();
		MEContextStepDown();
		MEContextStepDown();

		MEContextAddLabelMsg( "CSRHeader", "Ticket.Response.Title", NULL );
		if( selected ) {
			MEFieldContextEntry* entry = MEContextAddLabel( "CSRText",
															(nullStr( selected->pResponse ) ? TranslateMessageKey( "UGC_Bugs.NoCSRREsponse" ) : selected->pResponse),
															NULL );
			ui_LabelSetWordWrap( ENTRY_LABEL( entry ), true );
			ui_LabelSetWidthNoAutosize( ENTRY_LABEL( entry ), 1, UIUnitPercentage );
		} else {
			MEContextAddLabelMsg( "CSRText", "Ticket.NoTicketSelected", NULL );
		}

		if( selected && selected->uID == 0 ) {
			MEFieldContextEntry* entry = MEContextAddButtonMsg( "TIcket.Button.CreateNew", NULL, ugc_SubmitNewTicket, NULL, "AddInfo", NULL, NULL );
			ui_WidgetSetWidth( UI_WIDGET( ENTRY_BUTTON( entry )), 160 );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( entry )), 0, 0, 0, 0, UIBottomRight );
		} else {
			MEFieldContextEntry* entry;
			MEContextSetEnabled( selected != NULL );
				
			entry = MEContextAddButtonMsg( "Ticket.Button.SubmitSame", NULL, ugc_SubmitTicket, NULL, "HaveIssue", NULL, NULL );
			ui_WidgetSetWidth( UI_WIDGET( ENTRY_BUTTON( entry )), 160 );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( entry )), 170, 0, 0, 0, UIBottomRight );
				
			entry = MEContextAddButtonMsg( "Ticket.Button.AddMoreInfo", NULL, ugc_SubmitTicketDetails, NULL, "AddInfo", NULL, NULL );
			ui_WidgetSetWidth( UI_WIDGET( ENTRY_BUTTON( entry )), 160 );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( entry )), 0, 0, 0, 0, UIBottomRight );
			MEContextSetEnabled( true );
		}
	} else if( g_ugcBugReport.state == UGCBUG_SUBMIT_NEW_BUG ) {
		int oldXDataStart = context->iXDataStart;
		int oldTextAreaHeight = context->iTextAreaHeight;
		context->iXDataStart = 0;
		context->iTextAreaHeight = 7;
		
		ui_WidgetSetTextMessage( UI_WIDGET( g_ugcBugReport.window ), "Ticket.CreateNew.BugsTitle" );

		MEContextAddLabelMsg( "IssueSummaryLabel", "Ticket.Header.Summary", NULL );
		MEContextAddTextMsg( false, "Ticket.Summary.Empty", "IssueSummary", NULL, NULL );

		MEContextAddLabelMsg( "IssueDetailsLabel", "Ticket.Header.Description", NULL );
		MEContextAddTextMsg( true, "Ticket.Description.Empty", "IssueDetails", NULL, NULL );

		MEContextSetEnabled( !nullStr( g_ugcBugReport.uiModel.strIssueSummary )
							 && !nullStr( g_ugcBugReport.uiModel.strIssueDetails ) );
		{
			MEFieldContextEntry* entry = MEContextAddButtonMsg( "Ticket.Button.Submit", NULL, ugc_FinishSubmitTicket, NULL, "SubmitIssue", NULL, NULL );
			ui_WidgetSetWidth( UI_WIDGET( ENTRY_BUTTON( entry )), 160 );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( entry )), 0, 0, 0, 0, UIBottomRight );
			g_ugcBugReport.submitButton = ENTRY_BUTTON( entry );
		}
		MEContextSetEnabled( true );

		context->iXDataStart = oldXDataStart;
		context->iTextAreaHeight = oldTextAreaHeight;
	} else if( g_ugcBugReport.state == UGCBUG_SUBMIT_DETAILS ) {
		char* estr = NULL;
		int oldXDataStart = context->iXDataStart;
		int oldTextAreaHeight = context->iTextAreaHeight;
		context->iXDataStart = 0;
		context->iTextAreaHeight = 7;

		estrClear( &estr );
		FormatMessageKey( &estr, "UGC_Bugs.AddInfo_Title",
						  STRFMT_INT( "TicketID", g_ugcBugReport.uiModel.ticketToAddDetails->uID ),
						  STRFMT_END );
		ui_WindowSetTitle( g_ugcBugReport.window, estr );

		{
			MEFieldContextEntry* entry = MEContextAddLabelMsg( "Directions", "UGC_Bugs.AddInfo_Disclaimer", NULL );
			ui_LabelSetWordWrap( ENTRY_LABEL( entry ), true );
			MEContextStepDown();
		}

		estrClear( &estr );
		FormatMessageKey( &estr, "UGC_Bugs.AddInfo_Header",
						  STRFMT_INT( "TicketID", g_ugcBugReport.uiModel.ticketToAddDetails->uID ),
						  STRFMT_END );
		MEContextAddLabel( "IssueName", estr, NULL );

		MEContextAddTextMsg( true, "Ticket.AddInfo.Description", "IssueDetails", NULL, NULL );

		MEContextSetEnabled( !nullStr( g_ugcBugReport.uiModel.strIssueDetails ));
		{
			MEFieldContextEntry* entry = MEContextAddButtonMsg( "Ticket.Button.Submit", NULL, ugc_FinishSubmitTicket, NULL, "SubmitIssue", NULL, NULL );
			ui_WidgetSetWidth( UI_WIDGET( ENTRY_BUTTON( entry )), 160 );
			ui_WidgetSetPositionEx( UI_WIDGET( ENTRY_BUTTON( entry )), 0, 0, 0, 0, UIBottomRight );
			g_ugcBugReport.submitButton = ENTRY_BUTTON( entry );
		}
		MEContextSetEnabled( true );
		
		context->iXDataStart = oldXDataStart;
		context->iTextAreaHeight = oldTextAreaHeight;
	}
	
	MEContextPop( "UGCReportBug" );
}

void ugc_ReportBugSearch( UIWidget* ignored, UserData ignored2 )
{
	TicketRequestData ticketData = { 0 };
	Entity* ent = entActivePlayerPtr();
	
	ticketData.pMainCategory = "Cbug.Categorymain.Gamesupport";
	ticketData.pCategory = "Cbug.Category.Gamesupport.Ugc";

	if( ent && ent->pPlayer ) {
		ticketData.pAccountName = StructAllocString( ent->pPlayer->publicAccountName );
		ticketData.accessLevel = ent->pPlayer->accessLevel;
	}
	ticketData.pKeyword = g_ugcBugReport.uiModel.strSearchText;
	
	ServerCmd_sendTicketLabelRequest( &ticketData );
}

void ugc_ReportBugSearchResult( const char* ticketResponse )
{
	TicketRequestResponseWrapper wrapper = { 0 };

	if( !g_ugcBugReport.window ) {
		return;
	}

	StructReset( parse_TicketRequestResponseList, &g_ugcBugReport.resultModel );

	if(   !ParserReadText( ticketResponse, parse_TicketRequestResponseWrapper, &wrapper, 0 )
		  || !ParserReadTextSafe( wrapper.pListString, wrapper.pTPIString, wrapper.uCRC, parse_TicketRequestResponseList, &g_ugcBugReport.resultModel, 0 )) {
		// ERROR STATE, make sure the list stays empty then.
		StructReset( parse_TicketRequestResponseList, &g_ugcBugReport.resultModel );
	}

	{
		TicketRequestResponse *pResponse = StructCreate(parse_TicketRequestResponse);
		pResponse->pSummary = StructAllocString( TranslateMessageKey( "Ticket.CreateNew.Text" ));
		eaPush( &g_ugcBugReport.resultModel.ppTickets, pResponse );
	}

	StructReset( parse_TicketRequestResponseWrapper, &wrapper );

	ugc_ReportBugUpdateUI();
}

void ugc_ReportBugOncePerFrame( void )
{
	if( g_ugcBugReport.window && g_ugcBugReport.queueUpdate ) {
		printf( "UGC ReportBugUpdateUI\n" );
		ugc_ReportBugUpdateUI();
	}
	g_ugcBugReport.queueUpdate = false;
}

void ugc_ReportBugQueueUpdateUICB( UIList* ignored, UserData ignored2 )
{
	g_ugcBugReport.queueUpdate = true;
}

/// For saying "I have the same problem, no extra details to add."
void ugc_SubmitTicket( UIButton* ignored, UserData ignored2 )
{
	TicketData ticketData = { 0 };
	Entity* ent = entActivePlayerPtr();
	TicketRequestResponse* selectedTicket = ui_ListGetSelectedObject( g_ugcBugReport.ticketList );

	ticketData.pPlatformName = StructAllocString( PLATFORM_NAME );
	ticketData.pProductName = StructAllocString( GetProductName() );
	ticketData.pVersionString = StructAllocString( GetUsefulVersionString() );
	if( ent && ent->pPlayer ) {
		ticketData.pAccountName = StructAllocString( ent->pPlayer->privateAccountName );
		ticketData.pDisplayName = StructAllocString( ent->pPlayer->publicAccountName );
		ticketData.pCharacterName = StructAllocString( ent->pSaved->savedName );
		ticketData.uIsInternal = (ent->pPlayer->accessLevel >= ACCESS_GM);
	}

	estrPrintf( &ticketData.pMainCategory, "Cbug.Categorymain.Gamesupport" );
	estrPrintf( &ticketData.pCategory, "Cbug.Category.Gamesupport.Ugc" );
	ticketData.pSummary = StructAllocString( selectedTicket->pSummary );
	ticketData.pUserDescription = StructAllocString( selectedTicket->pDescription );
	ticketData.iProductionMode = isProductionMode();
	ticketData.pTriviaList = StructCreate( parse_TriviaList );
	eaCopyStructs( &g_ppTrivia, &ticketData.pTriviaList->triviaDatas, parse_TriviaData );
	ticketData.iMergeID = selectedTicket->uID;
	ticketData.eLanguage = entGetLanguage( ent );
	ticketData.eVisibility = TICKETVISIBLE_UNKNOWN;

	ServerCmd_sendTicket( &ticketData, NULL );
	ui_WindowClose( g_ugcBugReport.window );
	
	StructReset( parse_TicketData, &ticketData );
}

/// Adding additional details to an existing ticket
void ugc_SubmitTicketDetails( UIButton* ignored, UserData ignored2 )
{
	TicketRequestResponse* selectedTicket = ui_ListGetSelectedObject( g_ugcBugReport.ticketList );
	char buffer[ 256 ];
	g_ugcBugReport.state = UGCBUG_SUBMIT_DETAILS;
	StructDestroySafe( parse_TicketRequestResponse, &g_ugcBugReport.uiModel.ticketToAddDetails );
	g_ugcBugReport.uiModel.ticketToAddDetails = StructClone( parse_TicketRequestResponse, selectedTicket );

	sprintf( buffer, "I have more info on ticket #%d", SAFE_MEMBER( selectedTicket, uID ));
	g_ugcBugReport.uiModel.strIssueSummary = StructAllocString( buffer );
	StructFreeStringSafe( &g_ugcBugReport.uiModel.strIssueDetails );
	
	g_ugcBugReport.queueUpdate = true;
}

/// Create a new ticket
void ugc_SubmitNewTicket( UIButton* ignored, UserData ignored2 )
{
	g_ugcBugReport.state = UGCBUG_SUBMIT_NEW_BUG;
	
	StructFreeStringSafe( &g_ugcBugReport.uiModel.strIssueSummary );
	StructFreeStringSafe( &g_ugcBugReport.uiModel.strIssueDetails );
	
	g_ugcBugReport.queueUpdate = true;
}

/// Actually submit the ticket
void ugc_FinishSubmitTicket( UIButton* ignored, UserData ignored2 )
{
	TicketData ticketData = { 0 };
	Entity* ent = entActivePlayerPtr();

	ticketData.pPlatformName = StructAllocString( PLATFORM_NAME );
	ticketData.pProductName = StructAllocString( GetProductName() );
	ticketData.pVersionString = StructAllocString( GetUsefulVersionString() );
	if( ent && ent->pPlayer ) {
		ticketData.pAccountName = StructAllocString( ent->pPlayer->privateAccountName );
		ticketData.pDisplayName = StructAllocString( ent->pPlayer->publicAccountName );
		ticketData.pCharacterName = StructAllocString( ent->pSaved->savedName );
		ticketData.uIsInternal = (ent->pPlayer->accessLevel >= ACCESS_GM);
	}

	estrPrintf( &ticketData.pMainCategory, "Cbug.Categorymain.Gamesupport" );
	estrPrintf( &ticketData.pCategory, "Cbug.Category.Gamesupport.Ugc" );
	ticketData.pSummary = StructAllocString( g_ugcBugReport.uiModel.strIssueSummary );
	ticketData.pUserDescription = StructAllocString( g_ugcBugReport.uiModel.strIssueDetails );
	ticketData.iProductionMode = isProductionMode();
	ticketData.pTriviaList = StructCreate( parse_TriviaList );
	eaCopyStructs( &g_ppTrivia, &ticketData.pTriviaList->triviaDatas, parse_TriviaData );
	ticketData.eLanguage = entGetLanguage( ent );
	ticketData.eVisibility = TICKETVISIBLE_UNKNOWN;
	
	ServerCmd_sendTicket( &ticketData, NULL );
	ui_WindowClose( g_ugcBugReport.window );
}

#include"UGCBugReport_c_ast.c"
