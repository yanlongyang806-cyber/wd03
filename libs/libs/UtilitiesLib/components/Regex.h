#ifndef REGEX_H
#define REGEX_H

//Uses "PCRE-compliant" regular expressions, ie, Perl-format

#if defined(_WIN32) && !defined(GAMECLIENT) && !defined(PLATFORM_CONSOLE)
#define CRYPTIC_REGEX_SUPPORTED 1
#endif

// Perform a regular expression match
int regexMatch_s(SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pSubject, SA_PARAM_OP_VALID int * pMatches, int iMatchesSize,
				 char **pErrorMessage);

//NOTE NOTE NOTE NOTE
//This function returns zero if there IS a match, non-zero otherwise. So if you're doing an IF check just to see
//if a string matches an expression, you have to do your IF backwards from the way you expect.
#define regexMatch(pPattern, pSubject, pMatches) regexMatch_s(pPattern, pSubject, pMatches, ARRAY_SIZE(pMatches), NULL)

// Replace "[replace]" in pReplace with the inner match pPattern, then replace the outer match pPattern with that.
// Example: "Blah blah [program:blah]" could be come "Blah blah {blah}"
void regexFancyReplace(SA_PARAM_NN_STR char **pEstrSubject, SA_PARAM_NN_STR const char * pPattern, SA_PARAM_NN_STR const char *pReplace);

// Find an entry that matches any regular expression.
int eaFindRegex(CONST_STRING_EARRAY * eaArray, CONST_STRING_EARRAY * eaPatterns);


//returns true on match, false otherwise
static __forceinline bool RegExSimpleMatch(const char *pString, const char *pRegex) { return !regexMatch_s(pRegex, pString, NULL, 0, NULL); }

#endif
