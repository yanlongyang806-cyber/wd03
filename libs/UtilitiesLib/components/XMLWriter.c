// Write streaming XML to an EString.

#include <stdlib.h>

#include "earray.h"
#include "EString.h"
#include "StringUtil.h"
#include "timing.h"
#include "XMLWriter.h"

// Internal XML writer state.
struct XmlWriterInternal
{
	char *pBuffer;					// Estring output buffer
	char **ppTagStack;				// EArray stack of unclosed tags
	bool bInStartTag;				// True if the generator is in an unclosed start tag
};

// Replace any characters problematic in attribute values.
static void EscapeForXmlAttributes(char **estrEscapedString, const char *pString)
{
	PERFINFO_AUTO_START_FUNC();

	if (pString)
		estrCopy2(estrEscapedString, pString);
	estrReplaceOccurrences(estrEscapedString, "&", "&amp;");
	estrReplaceOccurrences(estrEscapedString, "'", "&apos;");
	estrReplaceOccurrences(estrEscapedString, "<", "&lt;");
	estrReplaceOccurrences(estrEscapedString, ">", "&gt;");
	estrReplaceOccurrences(estrEscapedString, "\"", "&quot;");
	// Note that XMLWriter.c uses ' for attribute quoting.

	PERFINFO_AUTO_STOP_FUNC();
}

// Replace any characters problematic in text blocks.
static void EscapeForXmlCharacters(char **estrEscapedString, const char *pString)
{
	PERFINFO_AUTO_START_FUNC();

	if (pString)
		estrCopy2(estrEscapedString, pString);
	estrReplaceOccurrences(estrEscapedString, "&", "&amp;");
	estrReplaceOccurrences(estrEscapedString, "<", "&lt;");

	PERFINFO_AUTO_STOP_FUNC();
}

// Check that a writer object is basically valid.
static void ValidateWriter(const XmlWriter *pWriter)
{
	devassert(pWriter->pInternal->pBuffer);
	devassert(pWriter->pInternal->ppTagStack);
	devassert(pWriter->pInternal->bInStartTag == true || pWriter->pInternal->bInStartTag == false);
}

// Check that a tag is valid.
static void ValidateName(const char *pTag)
{
	const char *i;
	wchar_t c;
	bool foundColon;

	PERFINFO_AUTO_START_FUNC();

	// General validation and validation of first character.
	devassert(UTF8StringIsValid(pTag, NULL));
	c = UTF8ToWideCharConvert(pTag);
	devassert(c == L':' || (c >= 'A' && c <= 'Z') || c == L'_' || (c >= L'a' && c <= L'z') || (c >= 0xC0 && c <= 0xD6)
		|| (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) || (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF)
		|| (c >= 0x200C && c <= 0x200D) || (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF)
		|| (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF));

	// Validate subsequent characters.
	foundColon = c == L':';
	for (i = UTF8GetNextCodepoint(pTag); *i; i = UTF8GetNextCodepoint(i))
	{
		c = UTF8ToWideCharConvert(i);
		if (c == L':')
		{
			devassert(!foundColon);
			foundColon = true;
		}
		devassert(c == L':' || (c >= 'A' && c <= 'Z') || c == L'_' || (c >= L'a' && c <= L'z') || (c >= 0xC0 && c <= 0xD6)
			|| (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) || (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF)
			|| (c >= 0x200C && c <= 0x200D) || (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF)
			|| (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF) || L'-' || L'.'
			|| (c >= '0' && c <= '9') || c == 0xB7 || (c >= 0x300 && c <= 0x36F) || (c >= 0x203F && c <= 0x2040));
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Check that text is valid.
static void ValidateText(const char *pTag)
{
	if (!UTF8StringIsValid(pTag, NULL))
	{
		if (!assertIsDevelopmentMode())
			ErrorDetailsf("%s", pTag);
		devassertmsgf(0, "XML Error - invalid UTF-8.");
	}
}

// Close an open tag if necessary.
static void CloseTagIfNecessary(const XmlWriter *pWriter)
{
	if (pWriter->pInternal->bInStartTag)
	{
		estrConcatChar(&pWriter->pInternal->pBuffer, '>');
		pWriter->pInternal->bInStartTag = false;
	}
}

// Create a new XML writer.
void xmlWriterCreate(XmlWriter *pWriter)
{
	PERFINFO_AUTO_START_FUNC();

	pWriter->pInternal = malloc(sizeof(struct XmlWriterInternal));
	pWriter->pInternal->pBuffer = NULL;
	estrCreate(&pWriter->pInternal->pBuffer);
	pWriter->pInternal->ppTagStack = NULL;
	eaCreate(&pWriter->pInternal->ppTagStack);
	pWriter->pInternal->bInStartTag = false;

	ValidateWriter(pWriter);

	PERFINFO_AUTO_STOP_FUNC();
}

// Destroy an XML writer.
void xmlWriterDestroy(XmlWriter *pWriter)
{
	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	estrDestroy(&pWriter->pInternal->pBuffer);

	PERFINFO_AUTO_STOP_FUNC();
}

// Get a pointer to the EString buffer associated with a writer.
char *xmlWriterGet(XmlWriter *pWriter)
{
	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	CloseTagIfNecessary(pWriter);

	PERFINFO_AUTO_STOP_FUNC();

	return pWriter->pInternal->pBuffer;
}

// Empty the output buffer.
void xmlWriterClear(XmlWriter *pWriter)
{
	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	estrClear(&pWriter->pInternal->pBuffer);

	PERFINFO_AUTO_STOP_FUNC();
}

// Start a new element.
void xmlWriterStartElement(XmlWriter *pWriter, const char *pName)
{
	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	ValidateName(pName);

	// Close any open tag.
	CloseTagIfNecessary(pWriter);

	// Open new tag.
	estrConcatChar(&pWriter->pInternal->pBuffer, '<');
	estrAppend2(&pWriter->pInternal->pBuffer, pName);

	// Push tag to stack.
	eaPush(&pWriter->pInternal->ppTagStack, estrDup(pName));
	pWriter->pInternal->bInStartTag = true;

	PERFINFO_AUTO_STOP_FUNC();
}

// Add attributes an element.  This must be called immediately after xmlWriterStartElement(), but it can be called repeatedly.
void xmlWriterAddAttribute(XmlWriter *pWriter, const char *pName, const char *pValue)
{
	char *escapedValue = NULL;

	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	ValidateName(pName);
	ValidateText(pValue);
	devassert(eaSize(&pWriter->pInternal->ppTagStack));
	devassert(pWriter->pInternal->bInStartTag);

	// Write attribute.
	estrStackCreate(&escapedValue);
	EscapeForXmlAttributes(&escapedValue, pValue);
	estrConcatf(&pWriter->pInternal->pBuffer, " %s='%s'", pName, escapedValue);
	estrDestroy(&escapedValue);

	PERFINFO_AUTO_STOP_FUNC();
}

// Add attributes to an element, value formatted.
void xmlWriterAddAttributef(XmlWriter *pWriter, const char *pName, const char *pValueFormat, ...)
{
	char *pValue = NULL;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pValue);
	estrGetVarArgs(&pValue, pValueFormat);
	xmlWriterAddAttribute(pWriter, pName, pValue);
	estrDestroy(&pValue);

	PERFINFO_AUTO_STOP_FUNC();
}

// Convenience function for calling xmlWriterAddAttribute() with multiple attributes.
void xmlWriterAddAttributes(XmlWriter *pWriter, ...)
{
	va_list args;
	const char *name;

	PERFINFO_AUTO_START_FUNC();

	// Add each attribute.
	va_start(args, pWriter);
	for (name = va_arg(args, const char *); name; name = va_arg(args, const char *))
	{
		const char *value = va_arg(args, const char *);
		xmlWriterAddAttribute(pWriter, name, value);
	}
	va_end(args);

	PERFINFO_AUTO_STOP_FUNC();
}

// Add a text chunk to an XML element.
void xmlWriterCharacters(XmlWriter *pWriter, const char *pText)
{
	char *escapedText = NULL;

	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	ValidateText(pText);
	devassert(eaSize(&pWriter->pInternal->ppTagStack));

	// Close any open tag.
	CloseTagIfNecessary(pWriter);

	// Write escaped text.
	estrStackCreate(&escapedText);
	EscapeForXmlCharacters(&escapedText, pText);
	estrAppend(&pWriter->pInternal->pBuffer, &escapedText);
	estrDestroy(&escapedText);

	PERFINFO_AUTO_STOP_FUNC();
}

// Add a text chunk to an XML element, formatted.
void xmlWriterCharactersf(XmlWriter *pWriter, const char *pFormat, ...)
{
	char *pText = NULL;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pFormat);
	xmlWriterCharacters(pWriter, pText);
	estrDestroy(&pText);

	PERFINFO_AUTO_STOP_FUNC();
}

// End an XML element.
void xmlWriterEndElement(XmlWriter *pWriter)
{
	char *estrTag;
	PERFINFO_AUTO_START_FUNC();

	ValidateWriter(pWriter);
	devassert(eaSize(&pWriter->pInternal->ppTagStack));

	// Close element.
	if (pWriter->pInternal->bInStartTag)
		estrAppend2(&pWriter->pInternal->pBuffer, "/>");
	else
	{
		char *last = eaGetLast(&pWriter->pInternal->ppTagStack);
		estrAppend2(&pWriter->pInternal->pBuffer, "</");
		estrAppend(&pWriter->pInternal->pBuffer, &last);
		estrConcatChar(&pWriter->pInternal->pBuffer, '>');
	}

	// Pop tag stack and free the tag string.
	estrTag = eaPop(&pWriter->pInternal->ppTagStack);
	estrDestroy(&estrTag);

	pWriter->pInternal->bInStartTag = false;

	PERFINFO_AUTO_STOP_FUNC();
}

// Gets the depth level for the XML document
U32 xmlWriterGetDepthLevel(XmlWriter *pWriter)
{
	return eaSize(&pWriter->pInternal->ppTagStack);
}

void xmlWriterGetTagStack(char **estr, XmlWriter *pWriter)
{
	EARRAY_CONST_FOREACH_BEGIN(pWriter->pInternal->ppTagStack, i, s);
	{
		if (i == 0)
			estrPrintf(estr, "%s", pWriter->pInternal->ppTagStack[i]);
		else
			estrPrintf(estr, ",%s", pWriter->pInternal->ppTagStack[i]);
	}
	EARRAY_FOREACH_END;
}