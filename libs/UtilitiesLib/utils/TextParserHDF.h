#pragma once

//putting some stuff here so it can be seen from both textParserHDF.c and TextParserCallbacks_inline.h
bool array_writehdf(ParseTable tpi[], int column, void* structptr, int index, HDF *hdf, char *name_override);