#pragma once
GCC_SYSTEM

typedef struct DynAnimGraphUpdater DynAnimGraphUpdater;
typedef struct DynAnimGraphUpdaterNode DynAnimGraphUpdaterNode;
typedef struct DynAnimChartRunTime DynAnimChartRunTime;
typedef struct DynMovementBlock DynMovementBlock;
typedef struct DynMoveTransition DynMoveTransition;
typedef struct DynAnimTimedStance DynAnimTimedStance;
typedef struct DynAnimChartStack DynAnimChartStack;

const DynAnimChartStack* dynAnimGraphUpdaterGetChartStack(const DynAnimGraphUpdater* pUpdater);
S32 dynAnimGraphUpdaterGetChartStackSize(const DynAnimGraphUpdater *pUpdater);
const DynAnimChartRunTime *dynAnimGraphUpdaterGetChartStackChart(const DynAnimGraphUpdater *pUpdater, S32 i);

const char* dynAnimGraphUpdaterGetCurrentGraphName(DynAnimGraphUpdater* pUpdater);

bool dynAnimGraphUpdaterIsOnADefaultGraph(const DynAnimGraphUpdater* pUpdater);
bool dynAnimGraphUpdaterIsOnADeathGraph(const DynAnimGraphUpdater *pUpdater);
bool dynAnimGraphUpdaterIsInPostIdle(const DynAnimGraphUpdater* pUpdater);
bool dynAnimGraphUpdaterIsOverlay(const DynAnimGraphUpdater *pUpdater);

DynAnimGraphUpdaterNode* dynAnimGraphUpdaterGetCurrentNode(DynAnimGraphUpdater* pUpdater);
DynAnimGraphUpdaterNode* dynAnimGraphUpdaterGetNode(DynAnimGraphUpdater* pUpdater, int index);

F32 dynAnimGraphUpdaterGetOverlayBlend(const DynAnimGraphUpdater *pUpdater);

const char *dynAnimGraphNodeGetName(const DynAnimGraphUpdaterNode *pNode);
const char *dynAnimGraphNodeGetGraphName(const DynAnimGraphUpdaterNode *pNode);
const char* dynAnimGraphNodeGetMoveName(const DynAnimGraphUpdaterNode* pNode);

F32 dynAnimGraphNodeGetFrameTime(const DynAnimGraphUpdaterNode* pNode);
F32 dynAnimGraphNodeGetBlendFactor(const DynAnimGraphUpdaterNode* pNode);
F32 dynAnimGraphNodeGetMoveTotalTime(const DynAnimGraphUpdaterNode* pNode);
F32 dynAnimGraphNodeGetBlendTime(const DynAnimGraphUpdaterNode *pNode);
F32 dynAnimGraphNodeGetBlendTotalTime(const DynAnimGraphUpdaterNode *pNode);
const char *dynAnimGraphNodeGetReason(const DynAnimGraphUpdaterNode* pNode);
const char *dynAnimGraphNodeGetReasonDetails(const DynAnimGraphUpdaterNode *pNode);

const char* dynMovementBlockGetMovementType(const DynMovementBlock* pBlock);
const char* dynMovementBlockGetMoveName(const DynMovementBlock* pBlock);
F32 dynMovementBlockGetBlendFactor(const DynMovementBlock* pBlock);
F32 dynMovementBlockGetFrameTime(const DynMovementBlock* pBlock);
const DynAnimChartRunTime* dynMovementBlockGetChart(const DynMovementBlock* pBlock);
F32 dynMovementBlockGetTotalTime(const DynMovementBlock* pBlock);
S32 dynMovementBlockIsInTransition(const DynMovementBlock* pBlock);
const DynMoveTransition *dynMovementBlockGetTransition(const DynMovementBlock* pBlock);
void dynMovementBlockGetTransitionString(	const DynMoveTransition *pTran,
											char *buffer,
											U32 bufferSize);

const DynMoveTransition *dynMoveTransitionGet(const char *pcName);

const DynAnimChartRunTime *dynAnimChartGet(const char *pcName);
const char* dynAnimChartGetName(const DynAnimChartRunTime* pChart);
F32 dynAnimChartGetPriority(const DynAnimChartRunTime* pChart);
void dynAnimGetStanceWordsString(	const char*const* eaStanceWords,
									const DynAnimTimedStance* const*eaTimedStances,
									const char* separator,
									char* buffer,
									U32 bufferSize);
void dynAnimChartGetStanceWords(const DynAnimChartRunTime* pChart,
								char* buffer,
								U32 bufferSize);

typedef struct NameList NameList;
extern NameList *dynAnimKeywordList;
extern NameList *dynAnimStanceList;
extern NameList *dynAnimFlagList;

bool dynAnimStanceValid(const char *pcStance);
