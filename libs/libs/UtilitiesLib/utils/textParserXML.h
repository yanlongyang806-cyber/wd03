#pragma once

//moved some stuff here so it can be accessed from textParserXML.c and textParserCallbacks_inline.h
bool array_writexmlfile(ParseTable tpi[], int column, void* structptr, int index, int level, FILE* out, StructFormatField iOptions);

void XMLWrapInCdata(char **estr);
