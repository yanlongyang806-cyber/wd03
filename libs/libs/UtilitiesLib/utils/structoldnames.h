#pragma once
GCC_SYSTEM

//NOTE - this file is no longer included anywhere, but is left here for reference purpose for now in case things go wrong

#ifndef STRUCTOLDNAMES_H
#define STRUCTOLDNAMES_H

// structs and types in textparser.h
#define TokenizerTokenType		StructTokenType
#define TokenizerFormatOptions	StructFormatOptions
#define TokenizerParams			StructParams
#define TokenizerFunctionCall	StructFunctionCall
#define ParseLink				StructLink
#define TokenizerFormatField	StructFormatField

#define TokenizerParseInfo		ParseTable
#define FORALL_PARSEINFO		FORALL_PARSETABLE

// functions in textparser.h

#define ParserDumpStructAllocs	StructDumpAllocs
#define ParserFreeStruct		StructFree
#define ParserAllocString		StructAllocString
#define ParserFreeString		StructFreeString
#define ParserFreeFunctionCall	StructFreeFunctionCall
#define ParserFreeFields		StructDeInit
#define ParserDestroyStruct		StructDeInit

#define ParserCompressStruct	StructCompress
#define ParserCRCStruct			StructCRC
#define ParserCompareStruct		StructCompare
#define ParserCompareFields		StructCompare
#define ParserCopyFields		StructCopyFields
#define ParserGetStructMemoryUsage	StructGetMemoryUsage

#define ParserCompareTokens		TokenCompare

#define ParserLinkToString		StructLinkToString
#define ParserLinkFromString	StructLinkFromString

#define ParserClearCachedInfo	ParseTableClearCachedInfo
#define ParserCountFields		ParserGetTableNumColumns
#define ParserCRCFromParseInfo	ParseTableCRC

#define ParserExtractFromText	ParserReadTextEscaped
#define ParserEmbedText			ParserWriteTextEscaped

// textparserutils.h
#define ParserTokenSpecified	TokenIsSpecified
#define ParserCopyToken			TokenCopy
#define ParserAddSubStruct		TokenAddSubStruct

#define ParserOverrideStruct	StructOverride
#define ParserInterpToken		TokenInterpolate
#define ParserInterpStruct		StructInterpolate
#define ParserCalcRateForToken	TokenCalcRate
#define ParserCalcRateForStruct	StructCalcRate
#define ParserIntegrateToken	TokenIntegrate
#define ParserIntegrateStruct	StructIntegrate
#define ParserCalcCyclicValueForToken	TokenCalcCyclic
#define ParserCalcCyclicValueForStruct	StructCalcCyclic
#define ParserApplyDynOpToken	TokenApplyDynOp
#define ParserApplyDynOpStruct	StructApplyDynOp

#define ParseInfoFreeAll		ParseTableFree
#define ParseInfoWriteTextFile	ParseTableWriteTextFile
#define ParseInfoReadTextFile	ParseTableReadTextFile
#define ParseInfoSend			ParseTableSend
#define	ParseInfoRecv			ParseTableRecv

// structnet.h
#define sdPackDiff				ParserSend
#define sdPackEmptyDiff			ParserSendEmptyDiff
#define sdUnpackDiff			ParserRecv
#define sdFreePktIds			ParserFreePktIds
#define sdCopyStruct(tpi, src, dest) StructCopyFields(tpi, src, dest, 0, 0)
#define sdHasDiff(tpi, lhs, rhs) StructCompare(tpi, lhs, rhs)
#define sdFreeStruct(tpi, data) StructDeInit(tpi, data)

// structtokenizer.h
#define TextParserEscape TokenizerEscape
#define TextParserUnescape TokenizerUnescape

#endif // STRUCTOLDNAMES_H
