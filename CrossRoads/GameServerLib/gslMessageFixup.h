#ifndef GSLMESSAGEFIXUP_H__
#define GSLMESSAGEFIXUP_H__

AUTO_STRUCT;
typedef struct MsgKeyFixup {
	const char *pcOldKey;	AST(POOL_STRING)
	const char *pcNewKey;	AST(POOL_STRING)
	char *pcNewDescripton;	AST(INDEX_DEFINE)
	char *pFilename;		AST(ESTRING, INDEX_DEFINE)
} MsgKeyFixup;

void gslMsgFixupApplyTranslationFixups(MsgKeyFixup ***peaMsgKeyFixups, bool bDryRun);

#endif