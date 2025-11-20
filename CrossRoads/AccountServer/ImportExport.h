#pragma once

typedef struct Export Export;

SA_RET_OP_VALID Export * CreateExport(void);

void DestroyExport(SA_PRE_NN_NN_VALID SA_POST_NN_NULL Export **pExport);

// Returns an estring the caller must destroy
SA_RET_OP_STR char * SerializeExport(SA_PARAM_NN_VALID Export *pExport,
									SA_PARAM_OP_STR const char *pAuthor,
									SA_PARAM_OP_STR const char *pDescription,
									int iFormatVersion);

SA_RET_OP_VALID Export * UnserializeExport(SA_PARAM_NN_STR const char *pString,
										int iFormatVersion);

void AddExportEntry(SA_PARAM_NN_VALID Export *pExport,
					SA_PARAM_NN_VALID ParseTable *tpi,
					SA_PARAM_NN_VALID const void *pStruct);

int GetNumExportEntries(SA_PARAM_NN_VALID Export *pExport);

SA_RET_NN_VALID void * GetExportEntry(SA_PARAM_NN_VALID Export *pExport,
								  SA_PARAM_NN_VALID ParseTable *tpi,
								  int iIndex);