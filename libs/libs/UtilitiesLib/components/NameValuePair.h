#pragma once
GCC_SYSTEM



//sticking this here because we don't want any allocations to come from stringUtil.h, which is where this used to be
AUTO_STRUCT;
typedef struct NameValuePair
{
	char *pName; AST(STRUCTPARAM KEY)
	char *pValue; AST(STRUCTPARAM FORMATSTRING(HTML_DONTUSENAME=1))
} NameValuePair;


AUTO_STRUCT;
typedef struct NameValuePairList
{
	NameValuePair **ppPairs;
} NameValuePairList;

extern ParseTable parse_NameValuePair[];
#define TYPE_parse_NameValuePair NameValuePair
extern ParseTable parse_NameValuePairList[];
#define TYPE_parse_NameValuePairList NameValuePairList




/*This function takes in a string and tries to parse name/value pairs out of it. It generally parses strings
that look like this: Name value Name value Name value

It can also parse strings that look like this: blah blah blah blah (Name value Name value) (ignores the blah blah)

It can also manage simple quotes, so 
Message "test message"
ends up with one parsed pair. (Use \q quoting, not \").

Each name must be only alphanumerics, underscore, dot and colon. Values can be anything that is not a space, parentheses or quote.
*/


#define GetNameValuePairsFromString(pString, pppPairs, pExtraWhiteSpaceChars) GetNameValuePairsFromString_dbg(pString, pppPairs, pExtraWhiteSpaceChars, __FILE__, __LINE__)
bool GetNameValuePairsFromString_dbg(const char *pString, NameValuePair ***pppPairs, char *pExtraWhiteSpaceChars, SA_PARAM_NN_STR const char *caller_fname, int line);
char *GetValueFromNameValuePairs(NameValuePair ***pppPairs, const char *pName);
void UpdateOrSetValueInNameValuePairList(NameValuePair ***pppPairs, const char *pName, const char *pValue);
void RemovePairFromNameValuePairList(NameValuePair ***pppPairs, const char *pName);