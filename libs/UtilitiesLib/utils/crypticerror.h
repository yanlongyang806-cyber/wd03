#ifndef CRYPTICERROR_H
#define CRYPTICERROR_H

/***************************************************************************



***************************************************************************/

/***************************************************************************
* CrypticError Functions
* 
* These functions allow us to convey locations of strings and raw integer 
* values to the external process CrypticError without invoking any file or 
* memory routines during an exception.
* 
* DO NOT INTRODUCE ANY NEW DEPENDENCIES INTO CRYPTICERROR.H/C. These files
* are intended to be redistributable.
* 
* Note: if you add stack variables here, they must exist when CrypticError 
*       actually reads it!
*
* Usage:
* 
* Inside of your program's exception handler, use these three functions 
* to populate an internal (crypticerror.c) variable with the locations
* of interesting values: 
* 
* ceAddStringPtr() / ceAddPtr() / ceAddInt()
* 
* Accepted values:
* 
* Type    | Name           | Description
* -----------------------------------------------
* String  | assertbuf      | --- To Be Written --
* String  | errortracker   | --- To Be Written --
* Integer | assertmode     | --- To Be Written --
* Integer | errortype      | --- To Be Written --
* String  | platformname   | --- To Be Written --
* String  | executablename | --- To Be Written --
* String  | productname    | --- To Be Written --
* String  | versionstring  | --- To Be Written --
* String  | svnbranch      | --- To Be Written --
* String  | expression     | --- To Be Written --
* String  | userwhogotit   | --- To Be Written --
* String  | errorstring    | --- To Be Written --
* String  | stackdata      | --- To Be Written --
* String  | sourcefile     | --- To Be Written --
* String  | sourcefileline | --- To Be Written --
* String  | clientcount    | --- To Be Written --
* String  | trivia         | --- To Be Written --
* String  | productionmode | --- To Be Written --
* String  | memorydump     | --- To Be Written --
* Pointer | debugme        | --- To Be Written --
*
* Then, spawn CrypticError.exe with the arguments to it calculated with
* the ceCalcArgs() function, and spin/sleep forever. 
* 
***************************************************************************/

void ceClear();

void ceAddStringPtr(const char *name, const char *str);
void ceAddPtr(const char *name, void *ptr);
void ceAddInt(const char *name, int i);

char * ceCalcArgs();

#endif
