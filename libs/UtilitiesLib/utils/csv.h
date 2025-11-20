#pragma once
GCC_SYSTEM

typedef struct FileWrapper FileWrapper;

typedef void (*CsvHeaderFileWrite_CB) (FileWrapper *file, ParseTable *pti);
typedef void (*CsvFileWrite_CB) (FileWrapper*file, void *data);
typedef void (*CsvHeaderEstrWrite_CB) (char **estr, ParseTable *pti);
typedef void (*CsvEstrWrite_CB) (char **estr, void *data);

void csvWriteToFile (SA_PARAM_NN_STR const char *fileName, void ** ppData, ParseTable *pti, 
					 CsvHeaderFileWrite_CB header_cb, CsvFileWrite_CB element_cb);

void csvWriteToEstr (char **estr, void ** ppData, ParseTable *pti, 
					 CsvHeaderEstrWrite_CB header_cb, CsvEstrWrite_CB element_cb);

bool csvReadLine(FileWrapper *file, STRING_EARRAY *eaFields, int iMaxLength);