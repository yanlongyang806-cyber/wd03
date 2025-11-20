/* File SimpleParser.h
 *	Contains simple string parsing utilities.
 *
 *
 */

#ifndef SIMPLEPARSER_H
#define SIMPLEPARSER_H
#pragma once
GCC_SYSTEM

#include <ctype.h>

typedef const char *constCharPtr;

/********************************************************************
 * Begin string parsing utilties
 */
void beginParse(char* string);
int getInt(int* output);
int getFloat(float* output);
int getString(char** output);
void endParse();
const char *removeLeadingWhiteSpaces(const char *str);
void removeTrailingWhiteSpaces(char* str);
int isEmptyStr(const char* str);
int isAlphaNumericStr(const char* str);

// JS:	Stupid isspace() asserts if a negative value is passed to it even though:
//		1) It requests a "signed" integer to be passed to it.
//		2) All c library functions use "signed" characters to specify strings.
//		DIE, stupid c library, DIE!
__forceinline int characterIsSpace(unsigned char c)
{
	return isspace(c);
}
/* 
 * End string parsing utilties
 ********************************************************************/

#endif