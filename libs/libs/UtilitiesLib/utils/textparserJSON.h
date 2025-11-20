#pragma once


typedef enum WriteJsonFlags
{
	WRITEJSON_DONT_WRITE_EMPTY_OR_DEFAULT_FIELDS = 1 << 0,
	WRITEJSON_DONT_WRITE_NEWLINES = 1 << 1,
} WriteJsonFlags;

bool array_writejsonfile(ParseTable tpi[], int column, void* structptr, int index, FILE* out, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

void ParserWriteJSON(char **estr, ParseTable *tpi, void *struct_mem, WriteJsonFlags eFlags,
	StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

typedef struct cJSON cJSON;

void* json_convert_struct(cJSON *json, ParseTable tpi[], char **resultString);
