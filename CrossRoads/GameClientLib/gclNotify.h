#pragma once
GCC_SYSTEM

#include "chatCommon.h"
#include "NotifyCommon.h"
#include "UIGen.h"

typedef struct ChatData ChatData;
typedef struct DisplayMessage DisplayMessage;
typedef struct MessageStruct MessageStruct;
typedef U32 dtFx;

AST_PREFIX(WIKI(AUTO))

// No notify queue will ever have more than this many items in it.
#define NOTIFY_QUEUE_MAX 20

AUTO_STRUCT;
typedef struct NotifySettingUI
{
	const char* pchGroupName; AST(POOL_STRING)
		// The notify group name

	const char* pchDisplayName; AST(UNOWNED)
		// The display name of this setting

	const char* pchDescription; AST(UNOWNED)
		// The description of this setting

	NotifySettingFlags eFlags;
		// Flags

	U32 bIsHeader : 1;
		// Whether or not this is a header

	U32 bHasChat : 1;
		// Whether or not this setting affects a notification that goes to the chat log

	U32 bHasQueue : 1;
		// Whether or not this setting affects a notification that does a queue action

	U32 bHasTutorialPopup : 1;
		// Whether or not this setting affects a tutorial popup notification

} NotifySettingUI;

AUTO_STRUCT;
typedef struct NotifyActionGenMessage
{
	// the gen to send a message to
	REF_TO(UIGen) hGen; AST(STRUCTPARAM NON_NULL_REF REQUIRED)

	// the message that should be sent to the gen
	const char *pchMessage; AST(STRUCTPARAM POOL_STRING REQUIRED)
} NotifyActionGenMessage;

AUTO_STRUCT;
typedef struct NotifyActionGenState
{
	// the gen to apply the state change
	REF_TO(UIGen) hGen; AST(STRUCTPARAM NON_NULL_REF REQUIRED)

	// the list of states to apply
	UIGenState *eaiStates; AST(STRUCTPARAM REQUIRED)
} NotifyActionGenState;

AUTO_STRUCT;
typedef struct NotifyTutorialGen
{
	// the gen to apply this to (if there isn't a valid screen region)
	REF_TO(UIGen) hGen; AST(STRUCTPARAM NON_NULL_REF NAME(TutorialGen))

	// the preferred direction to apply the tutorial gen
	UIDirection ePopupDirection;

	// the pop up template gen to add to the target gen
	REF_TO(UIGen) hVerticalPopupTemplate; AST(NAME(VerticalPopupTemplate, PopupTemplate) NON_NULL_REF)

	// the pop up template gen to add to the target gen
	REF_TO(UIGen) hHorizontalPopupTemplate; AST(NAME(HorizontalPopupTemplate) NON_NULL_REF)
} NotifyTutorialGen;

AUTO_STRUCT;
typedef struct NotifyActionEnqueue
{
	const char *pchQueueName; AST(POOL_STRING STRUCTPARAM REQUIRED)
	F32 fLifetime; AST(STRUCTPARAM DEFAULT(5))
	bool bInfinite; AST(STRUCTPARAM DEFAULT(false))
	REF_TO(UIStyleFont) hFont; AST(NAME(Font) NON_NULL_REF STRUCTPARAM)
	U32 uiColor; AST(SUBTABLE(ColorEnum) STRUCTPARAM NAME(Color))
	F32 fDelay; AST(NAME(Delay))
	S32 iBatchLimit; AST(NAME(Batch))
	bool bMakeParseTableWork; // make the parsetable accept non-structparam args
} NotifyActionEnqueue;

AUTO_STRUCT;
typedef struct NotifyActionChat
{
	ChatLogEntryType eType; AST(REQUIRED STRUCTPARAM)
	bool bStripSMF;
	Expression *pExprDisplayStringOverride;	AST(NAME("DisplayStringOverride"), REDUNDANT_STRUCT("ExprDisplayStringOverride", parse_Expression_StructParam), LATEBIND)
} NotifyActionChat;

AUTO_ENUM;
typedef enum ENotifyFloatToInterp
{
	ENotifyFloatToInterp_LINEAR = 0,
	ENotifyFloatToInterp_SPLINE,
} ENotifyFloatToInterp;

// All angles are defined in degrees
AUTO_STRUCT;
typedef struct NotifyFloatToSplineDef
{
	// control point1- angle offset from the start to goal direction
	F32		fCtrl1_AngleOffset;	

	// control point1- a random offset from Ctrl1_AngleOffset, 0 for no randomization
	F32		fCtrl1_AngleOffsetMax;

	// control point1- minimum distance to create the control point 
	F32		fCtrl1_DistanceMin;
	
	// control point1- maximum distance to create the control point 
	F32		fCtrl1_DistanceMax;

	// control point2- angle offset from the start to goal direction
	F32		fCtrl2_AngleOffset;

	// control point2- a random offset from Ctrl1_AngleOffset, 0 for no randomization
	F32		fCtrl2_AngleOffsetMax;
	
	// control point2- minimum distance to create the control point 
	F32		fCtrl2_DistanceMin;
	
	// control point2- maximum distance to create the control point 
	F32		fCtrl2_DistanceMax;

	// control point1- whether the AngleOffset can be mirrored 
	bool	bCtrl1_AllowMirror;

	// control point2- whether the AngleOffset can be mirrored 
	bool	bCtrl2_AllowMirror;

} NotifyFloatToSplineDef;

AUTO_STRUCT;
typedef struct NotifyActionFloatToGen
{
	// the positional interpolation type
	ENotifyFloatToInterp eInterpType;		AST(NAME(InterpType))
	
	// optional. The gen that this FloatTo will go to
	REF_TO(UIGen) hGen;						AST(STRUCTPARAM NAME(Gen) NON_NULL_REF)

	// optional. if the Gen is not found, this is the UIGen to fall back to
	REF_TO(UIGen) hFallback;				AST(NAME(Fallback) NON_NULL_REF)

	// If not set, no text will display. the font to display the text in. 
	REF_TO(UIStyleFont) hFont;				AST(STRUCTPARAM NAME(Font) NON_NULL_REF)
	
	// how long the floater will last in seconds
	F32 fLifetime;							AST(STRUCTPARAM DEFAULT(2.5))

	// If set, the display string will be overridden by whatever this expression returns. Must return a string
	Expression *pExprDisplayStringOverride;	AST(NAME("DisplayStringOverride"), REDUNDANT_STRUCT("ExprDisplayStringOverride", parse_Expression_StructParam), LATEBIND)

	// optional. return 0 if this notification should ignore the floatTo
	Expression *pExprShowFloatTo;			AST(NAME("ShowFloatTo"), REDUNDANT_STRUCT("ExprShowFloatTo", parse_Expression_StructParam), LATEBIND)

	// optional. runs this expression when the floatTo expires
	Expression *pExprOnExpire;				AST(NAME("OnExpire"), REDUNDANT_STRUCT("ExprOnExpire", parse_Expression_StructParam), LATEBIND)


	// The offset in pixels from the goal location in the OffsetYaw direction. If no destination UIGen is specified, it will offset from the starting location
	F32 fOffsetMagnitude;

	// In degrees, 0 being to the right. Only valid if OffsetMagnitude is set. The offset direction the float will travel to.
	F32 fOffsetYaw;

	// the scale of the text	
	F32 fTextScale;							AST(DEFAULT(1))

	// the starting percentage X/Y on the screen
	F32 fStartX;							AST(DEFAULT(0.5))
	F32 fStartY;							AST(DEFAULT(0.5))
	
	// optional. the name of the sprite to use
	const char *pchIconName;				AST(NAME(IconName) NON_NULL_REF)

	// only valid for the IconName. The width of the icon
	F32 fIconWidth;

	// only valid for the IconName. The height of the icon
	F32 fIconHeight;		
	
	// optional. FX to attach to the FloatTo
	const char *pchAttachedFX;

	// optional. For spline, how to specify the control points. 
	// Otherwise hard-coded defaults are used.
	NotifyFloatToSplineDef *pSplineInfo;

	// the normalized percentage of time that will be spent scaling in
	F32		fScaleInNormTime;				AST(DEFAULT(0.25))
	
	// the normalized percentage of time that will be spent scaling out
	F32		fScaleOutNormTime;				AST(DEFAULT(0.25))

	// the normalized percentage of time that will be spent fading in
	F32		fFadeInNormTime;

	// the normalized percentage of time that will be spent fading out
	F32		fFadeOutNormTime;
	
	// for any notifications that have a world origin, this will anchor the floatTo to that point projected on the screen.
	bool	bAnchorToWorld;

} NotifyActionFloatToGen;

AUTO_STRUCT;
typedef struct NotifyActionFloatToGenItem
{
	AtlasTex *pSprite;				NO_AST
	char *pchString;

	F32 fLifetime;

	// if anchored to the world
	Vec3 vWorldOrigin;

	// if projected on the screen
	Vec2 vPoint1;
	Vec2 vPoint2;
	Vec2 vPoint3;
	Vec2 vPoint4;
	F32 fScreenScale;

	dtFx	hFX;					NO_AST
	
	NotifyActionFloatToGen	*pDef;	NO_AST
} NotifyActionFloatToGenItem;

AUTO_STRUCT;
typedef struct NotifyActionDialog
{
	REF_TO(Message) hTitle;	AST(NAME(Title))
	NotifyType *eaiClose;	AST(NAME(Close))
	bool bOnlyInUGC;		AST(NAME(OnlyInUGC))
} NotifyActionDialog;

AUTO_STRUCT WIKI("NotifyAction");
typedef struct NotifyAction
{
	// the type of notification this action handles
	NotifyType eType;							AST(STRUCTPARAM KEY REQUIRED)

	// optional. list of strings to filter the notification this action handles, by the notification's LogicalString
	const char **ppchLogicalStringFilters;		AST(POOL_STRING NAME(LogicalStringFilter))

	// optional. logical string is a tutorial screen region
	S8 bLogicalStringIsTutorial;				AST(NAME(LogicalStringIsTutorial) DEFAULT(-1))

	// optional. list of commands to run when the notify occurs
	const char **eaCommands;					AST(POOL_STRING NAME(Command))

	// optional. list of gen states to enter when notification hits
	UIGenState *eaiGenEnterGlobalState;			AST(NAME(GenEnterGlobalState))

	// optional. list of gen states to exit when notification 
	UIGenState *eaiGenExitGlobalState;			AST(NAME(GenExitGlobalState))

	// optional. list of gens with states to enter
	NotifyActionGenState **eaGenEnterState;		AST(NAME(GenEnterState))

	// optional. list of gens with states to exit
	NotifyActionGenState **eaGenExitState;		AST(NAME(GenExitState))

	// optional. list of gens to send messages to 
	NotifyActionGenMessage **eaGenMessage;		AST(NAME(GenMessage))

	// optional. list of queue actions
	NotifyActionEnqueue **eaQueue;				AST(NAME(Queue))

	// optional. if set, notification will perform a tutorial operation on a gen
	NotifyTutorialGen **eaTutorialGen;			AST(NAME(TutorialGen))

	// optional. list of sounds to play
	const char **eaSound;						AST(POOL_STRING NAME(Sound))

	// optional. text/icons that will fly around on the UI
	NotifyActionFloatToGen *pFloatTo;

	// optional. popup dialog to trigger
	NotifyActionDialog *pPopUpDialog;

	// optional. if set, notification will be sent to chat with settings
	NotifyActionChat *pChat;

	// optional. list of other notifications that are sent from this notification
	NotifyType *eaiChainNotify;					AST(NAME(ChainNotify))

	const char *pchFilename;					AST(CURRENTFILE)
} NotifyAction;

AUTO_STRUCT;
typedef struct NotifyActions
{
	NotifyAction **eaActions;					AST(NAME(Action) REQUIRED)
	const char *pchFilename;					AST(CURRENTFILE)

	NotifyAction **aeaActions[kNotifyType_COUNT];	NO_AST
} NotifyActions;

AUTO_STRUCT WIKI("NotifyAction"); 
typedef struct NotifyQueueItem
{
	// the type of notification
	NotifyType eType; AST(STRUCTPARAM)

	// the display string
	const char *pchDisplayString;

	// the logical string
	const char *pchLogicalString; AST(POOL_STRING)

	// the sound
	const char *pchSound; AST(POOL_STRING NAME(Sound))

	// texture
	const char *pchTexture; AST(POOL_STRING)

	// use this for whatever you want, you can clear a group of notifies by tag
	const char *pchTag; AST(POOL_STRING) 

	// the queue this is going to
	const char *pchQueue; AST(POOL_STRING)

	// optional headShot data
	ContactHeadshotData *pHeadshotData;

	// the font to be used
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))
	
	// color
	U32 uiColor; AST(SUBTABLE(ColorEnum) NAME(Color))

	char achColorString[10]; AST(NAME(ColorString))

	// the lifetime left
	F32 fLifetime;

	// the maximum lifetime
	F32 fLifetimeMax;

	F32 fDelay;

	// if the notification send an itemID 
	S64 itemID; AST(NAME("ItemID"))

	// if the notification sent a value along with it
	S32 iValue;

	Vec3 vOrigin;

	EntityRef erEntity;

	bool bInfinite;

	S32 iCount;
} NotifyQueueItem;

typedef struct ItemDef ItemDef;

AUTO_STRUCT;
typedef struct NotifyQueueItemWithItemDef
{
	ItemDef *pItemDef;				AST(UNOWNED)
	NotifyQueueItem *pQueueItem;	AST(UNOWNED)
} NotifyQueueItemWithItemDef;

AUTO_STRUCT;
typedef struct NotifyQueue
{
	const char *pchName; AST(POOL_STRING KEY)
	NotifyQueueItem **eaItems;
} NotifyQueue;

AUTO_STRUCT;
typedef struct NotifyQueues
{
	NotifyQueue **eaQueues;
} NotifyQueues;

AUTO_STRUCT;
typedef struct NotifyAudioEventData
{
	S32		iInt;
	F32		fFloat;
	char*	pchString;
} NotifyAudioEventData;

AUTO_STRUCT;
typedef struct NotifyAudioEvent
{
	NotifyType eType;				AST(NAME(Type))
	DisplayMessage DisplayMsg;		AST(NAME(DisplayString) STRUCT(parse_DisplayMessage))
	const char* pchSound;			AST(NAME(Sound))
	const char* pchSuggestionSound;	AST(NAME(SuggestionSound))
	const char* pchTexture;			AST(NAME(Texture))
	
	Expression* pUpdateExpr;		AST(NAME(UpdateExpr) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
	Expression* pActivateExpr;		AST(NAME(ActivateExpr) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
	Expression* pResetExpr;			AST(NAME(ResetExpr) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
	F32			fResetTime;			AST(NAME(ResetTime) DEFAULT(1.0f))
	F32			fDuration;			AST(NAME(Duration) DEFAULT(1.0f))
	F32			fSuggestDuration;	AST(NAME(SuggestionDuration) DEFAULT(2.0f))
	F32			fActivateHoldTime;	AST(NAME(ActivateHoldTime))

	//These fields aren't read from file
	F32			fActivateTime;		NO_AST
	S32			iActivateCount;		NO_AST
	bool		bActivateHold;		NO_AST
	bool		bActivated;			NO_AST
	bool		bCanActivate;		AST(DEFAULT(true))
	
	NotifyAudioEventData* pData;
} NotifyAudioEvent;

AUTO_STRUCT;
typedef struct NotifyAudioEventHistory
{
	NotifyAudioEvent*	pEvent;			AST(UNOWNED)
	F32					fActivateTime;
	bool				bIsSuggestion;
} NotifyAudioEventHistory;

AUTO_STRUCT;
typedef struct NotifyAudioEventGroup
{
	NotifyAudioEvent** eaList;  AST(NAME(NotifyAudioEvent) REQUIRED)
	Expression* pRequiresExpr;	AST(NAME(RequiresExpr) REDUNDANT_STRUCT(Expression, parse_Expression_StructParam) LATEBIND)
	NotifyAudioEventHistory** eaHistory;
	bool bUpdatedLastFrame;
} NotifyAudioEventGroup;

AUTO_STRUCT;
typedef struct NotifyAudioEventList
{
	NotifyAudioEventGroup** eaGroups; AST(NAME(NotifyAudioEventGroup))
	const char *pchFilename; AST(CURRENTFILE)
} NotifyAudioEventList;

bool gclNotifyAudioHasAnyEvents(void);

void gclNotifyReceive(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture);
void gclNotifyReceiveMessageStruct(NotifyType eType, MessageStruct *pFmt);
void gclNotifyReceiveAudio(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture);
void gclNotifyReceiveWithData(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const char *pchTexture, const ChatData *pData);
void gclNotifyReceiveWithHeadshot(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchSound, const ContactHeadshotData *pHeadshotData);
void gclNotifyReceiveWithTag(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture, const char *pchTag);

void gclNotifyReceiveWithOrigin(NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchTag, 
								S32 iValue,
								const Vec3 vOrigin);

void gclNotifyReceiveWithItemID(NotifyType eType, 
								const char *pcDisplayString, 
								const char *pcLogicalString, 
								const char *pcTexture,
								U64 itemID,
								S32 iCount);

void gclNotifyReceiveWithEntityRef(	NotifyType eType, 
									const char *pcDisplayString, 
									const char *pchLogicalString, 
									S32 iValue,
									EntityRef erEntity);

bool gclNotifyIsHandled(NotifyType eType, SA_PARAM_OP_STR const char *pchLogicalString);

// Only sends the notification if there is a handler for the notification
bool gclNotifySendIfHandled(NotifyType eType, const char *pchDisplayString, const char *pchLogicalString, const char *pchTexture);

void gclNotifySettingsFillFromEntity(Entity* pEnt);
bool gclNotify_CheckSettingFlags(NotifyType eType, NotifySettingFlags eFlags);
bool gclNotify_ChangeSetting(const char* pchNotifyGroupName, NotifySettingFlags eFlags);
void gclNotify_HandleUpdate(void);

void gclNotifyUpdate(F32 fElapsed);
void gclNotifyQueueClearTag(const char *pchQueue, const char * pchTag);

void gclNotify_NotifyAction_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);
void gclNotify_NotifyAudioEvent_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);