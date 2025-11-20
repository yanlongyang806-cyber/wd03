// Write streaming XML to an EString.

#ifndef CRYPTIC_XMLWRITER_H
#define CRYPTIC_XMLWRITER_H

struct XmlWriterInternal;

// XML writer object
typedef struct XmlWriter
{
	struct XmlWriterInternal *pInternal;
} XmlWriter;

// Create a new XML writer.
void xmlWriterCreate(XmlWriter *pWriter);

// Destroy an XML writer.
void xmlWriterDestroy(XmlWriter *pWriter);

// Get a pointer to the EString buffer associated with a writer.
char *xmlWriterGet(XmlWriter *pWriter);

// Empty the output buffer.
void xmlWriterClear(XmlWriter *pWriter);

// Start a new element. Must have a matching xmlWriterEndElement call.
void xmlWriterStartElement(XmlWriter *pWriter, const char *pName);

// Add attributes an element.  This must be called immediately after xmlWriterStartElement(), but it can be called repeatedly.
void xmlWriterAddAttribute(XmlWriter *pWriter, const char *pName, const char *pValue);

// Add attributes to an element, value formatted.
void xmlWriterAddAttributef(XmlWriter *pWriter, const char *pName, const char *pValueFormat, ...);

// Convenience function for calling xmlWriterAddAttribute() with multiple attributes.
void xmlWriterAddAttributes(XmlWriter *pWriter, ...);

// Add a text chunk to an XML element.
void xmlWriterCharacters(XmlWriter *pWriter, const char *pText);

// Add a text chunk to an XML element, formatted.
void xmlWriterCharactersf(XmlWriter *pWriter, const char *pFormat, ...);

// End an XML element. Must have a matching previous xmlWriterStartElement call.
void xmlWriterEndElement(XmlWriter *pWriter);

// Gets the depth level for the XML document
U32 xmlWriterGetDepthLevel(XmlWriter *pWriter);

// Get the tag stack for debugging
void xmlWriterGetTagStack(char **estr, XmlWriter *pWriter);

#endif  // CRYPTIC_XMLWRITER_H
