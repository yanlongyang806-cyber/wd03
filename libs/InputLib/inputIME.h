#pragma once
GCC_SYSTEM
/***************************************************************************
 
 
 
 ***************************************************************************/

#ifndef INPUTIME_H
#define INPUTIME_H

#if !PLATFORM_CONSOLE

#include <wininclude.h>
#include "stdtypes.h"

// ------------------------------------------------------------
// types
// ------------------------------------------------------------

typedef enum InputOrientation
{
	kInputOrientation_Vertical,
	kInputOrientation_Horizontal,
	kInputOrientation_Count	
} InputOrientation;


typedef enum IndicatorType
{ 
	INDICATOR_NON_IME, 
	INDICATOR_CHS, 
	INDICATOR_CHT, 
	INDICATOR_KOREAN, 
	INDICATOR_JAPANESE,
	kIndicatorType_Count
} IndicatorType;

#define MAX_UIIMESTRING_LEN 256
#define MAX_CANDLIST 9

//------------------------------------------------------------
//  Info about the IME input language
//
// for conversion mode: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/ime_1xo3.asp
// for sentence mode: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/ime_3n3n.asp
// for closing windows: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/ime_48kz.asp
// character sets: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/intl/unicode_0a5v.asp
//----------------------------------------------------------
typedef struct uiIMEInput
{
	bool visible;
	DWORD langCharset; // aka the current code page
	HKL keyboardLayout;
	InputOrientation eOrient;
	wchar_t *indicator;
	struct IndicatorDims
	{
		// unscaled
		F32 usWd;
		F32 usHt;
	} indicatorDims;

	DWORD conversionMode; // IME_CMODE_NATIVE, IME_CMODE_KATAKANA, ...
	DWORD sentenceMode; // IME_SMODE_AUTOMATIC, IME_SMODE_NONE, ...
	// 	UINT (WINAPI * _GetReadingString)( HIMC, UINT, LPWSTR, PINT, BOOL*, PUINT );
	// all I really need is ImmGetCompositionString(GCS_COMPREADSTR)
	// 	BOOL (WINAPI * CDXUTIMEEditBox::_ShowReadingWindow)( HIMC, BOOL );
	// looks like I can post 'WM_IME_CONTROL' message with 'IMC_CLOSESTATUSWINDOW'
} uiIMEInput;

typedef struct uiIMEComposition
{
	bool visible;
	BYTE attributes[ MAX_UIIMESTRING_LEN ];
	DWORD clauses[ MAX_UIIMESTRING_LEN ];
	// 	S32 iCursorStart; // where the composition started in the UIEdit
	U32 lenStr; // length of the composition string.
} uiIMEComposition;


typedef enum CandidateWindowVisibility
{
	kCandidateWindowVisibility_None,
	kCandidateWindowVisibility_Reading,
	kCandidateWindowVisibility_Candidate,
	kCandidateWindowVisibility_Count
} CandidateWindowVisibility;

typedef struct uiIMECandidateReading
{
	CandidateWindowVisibility visible;

	char pszCandidates[MAX_CANDLIST][MAX_UIIMESTRING_LEN];
	DWORD candidatesSize;  // number of candidates

	// --------------------
	// candidate info only

	DWORD selection; // current selection
	DWORD count; // the total number of candidates (including those not displayed). 	
} uiIMECandidateReading;

// This structure is used to allow the ime functions to modify a text
// input structure without exposing the ui library to the input library
// Add more functions here as needed
typedef struct IMEContext {

	void (*addString)(void *edit, WCHAR* str, int length);
	void (*delString)(void *edit, int length);

	void *data;
} IMEContext;

typedef struct uiIMEState
{
	uiIMEInput input;
	uiIMEComposition composition;
	uiIMECandidateReading candidate;
	IMEContext *pEdit; // the edit associated with this IME
	HIMC hImc;
} uiIMEState;

bool uiIME_MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

// --------------------
// display
#endif
#endif //INPUTIME_H
