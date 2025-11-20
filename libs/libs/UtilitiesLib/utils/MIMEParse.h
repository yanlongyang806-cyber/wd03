#pragma once

/*in my terminology, this 

Content-Type: multipart/mixed; boundary=0016e6434bd68e34aa0498f90975

is a MimeHeaderLine with Name "Content-Type", Value "multipart/mixed",
and a MimeHeaderLineSegment with NameOrEntireString "boundary" and 
Value "0016e6434bd68e34aa0498f90975"
*/

AUTO_STRUCT;
typedef struct MIMEHeaderLineSegment
{
	char *pNameOrEntireString; AST(ESTRING)
	char *pValue; AST(ESTRING)
} MIMEHeaderLineSegment;

AUTO_STRUCT;
typedef struct MIMEHeaderLine
{
	char *pName; AST(ESTRING)
	char *pValue; AST(ESTRING)
	MIMEHeaderLineSegment **ppSegments;
	char *pErrors; AST(ESTRING)
} MIMEHeaderLine;

//returns the number of source lines the header took up
int ReadMIMEHeader(MIMEHeaderLine ***pppOutHeader, char **ppInLines); 

//pass in "Content-Type", get out "multipart/mixed". Returns first one it finds
//if there are multiple with the same name
char *GetMIMEHeaderLineValue(MIMEHeaderLine ***pppHeader, char *pLineName);

//pass in "Content-Type" and "boundary", get out "0016e6434bd68e34aa0498f90975"
char *GetMIMEHeaderLineSegmentValue(MIMEHeaderLine ***pppHeader, char *pLineName, char *pSegmentName);