/***************************************************************************



***************************************************************************/
#include "structNet.h"
#include <stddef.h>   // for offsetof

#include <math.h> // fabs

#include "net/net.h"
#include "structinternals.h"
#include "timing.h"
#include "estring.h"
#include "StringCache.h"

#include "sock.h"
#include "netprivate.h" //so that error_occurred can be checked in a packet without function call overhead

#include "tokenStore.h"
#include "textparser_h_ast.h"
#include "tokenstore_inline.h"
#include "textparserUtils_inline.h"
#include "StashTable.h"
#include "StringUtil.h"

#include "structinternals_h_ast.h"

#define FAST_SEND_SAFE 1


static char *invalidName="IncompatibleDataType"; // Used when creating dynamic TPIs

int intFromTokenType(StructTokenType v);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

/* sdDifferenceDetector functions
*  Returns:
*      1 - values are different.
*      2 - values are not different.
*/
typedef int (*sdDifferenceDetector)(void*, void*);

static int diff(ParseTable *tpi, void *a, void *b);

static int sdBadDifferenceDetector(void* useless1, void* useless2)
{
	assert(0);
	return 0;
}

static U32 parserTimingFlags = 2;
AUTO_CMD_INT(parserTimingFlags, parserTimingFlags);

#define PARSER_START_BIT_COUNT(pak, name, flags)					\
			PERFINFO_RUN(											\
				if(parserTimingFlags){								\
					flags = parserTimingFlags;						\
					if(flags & 1){PERFINFO_AUTO_START(name, 1);}	\
					if(flags & 2){START_BIT_COUNT(pak, name);}		\
				}													\
			)
			
#define PARSER_START_BIT_COUNT_STATIC(pak, piStatic, name, flags)					\
			PERFINFO_RUN(															\
				if(parserTimingFlags){												\
					flags = parserTimingFlags;										\
					if(flags & 1){PERFINFO_AUTO_START_STATIC(name, piStatic, 1);}	\
					if(flags & 2){START_BIT_COUNT_STATIC(pak, piStatic, name);}		\
				}																	\
			)

#define PARSER_STOP_BIT_COUNT(pak, flags)											\
			PERFINFO_RUN(															\
				if(flags){															\
					if(flags & 2){STOP_BIT_COUNT(pak);}								\
					if(flags & 1){PERFINFO_AUTO_STOP();}							\
				}																	\
			)

typedef struct
{
	const char	*name;
	int			total;
	int			sent;
	int			bytes;
	U8			is_struct;
	U8			is_root;
} StructSendLog;

static StructSendLog **struct_logs;
int do_struct_send_logging;
StashTable parse_struct_names;

void *parserPopulateStructName(ParseTable* tpi,char *rootname)
{
	int				i;
	StructSendLog	*log = calloc(sizeof(StructSendLog),1);
	char	name[MAX_PATH];

	if (!rootname[0])
	{
		struct_logs = 0;
		eaPush(&struct_logs,log);
		log->name = strdup("untracked (non ent)");
		log = calloc(sizeof(StructSendLog),1);
	}
	if (stashAddPointer(parse_struct_names,tpi,log,0))
	{
		sprintf(name,"%s/%s",rootname,tpi->name);
		log->name = strdup(name);
		log->is_struct = 1;
		if (!rootname[0])
			log->is_root = 1;
		eaPush(&struct_logs,log);

		FORALL_PARSETABLE(tpi, i)
		{
			StructSendLog	*elem;
			char			elem_name[MAX_PATH];
			StructTypeField type = TOK_GET_TYPE(tpi[i].type);
			ParseTable		*pSubtable = (ParseTable*)tpi[i].subtable;

			if ((type == TOK_IGNORE || type == TOK_START || type == TOK_END) || (tpi[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED)))
				continue;

			elem = calloc(sizeof(StructSendLog),1);
			if ((type == TOK_STRUCT_X || type == TOK_POLYMORPH_X) && pSubtable && tpi != pSubtable)
			{
				StructSendLog	*child=parserPopulateStructName(pSubtable,name);
				stashAddPointer(parse_struct_names,&tpi[i],child,0);
			}
			else if (stashAddPointer(parse_struct_names,&tpi[i],elem,0))
			{
				sprintf(elem_name,"%s/%s",name,tpi[i].name);
				elem->name = strdup(elem_name);
				eaPush(&struct_logs,elem);
			}
		}
	}
	return log;
}

int bytesCmp(const StructSendLog *a, const StructSendLog *b)
{
	return a->bytes - b->bytes;
}

void tpsBytesInternal(void)
{
	int				i,count,bytes=0;
	StructSendLog	*log,*logs,totals = {0};

	count = eaSize(&struct_logs);
	logs = malloc(count * sizeof(logs[0]));
	for(i=0;i<count;i++)
		logs[i] = *struct_logs[i];
	qsort(logs,count,sizeof(logs[0]),bytesCmp);
	for(i=0;i<count;i++)
	{
		log = &logs[i];
		if (!log->is_struct)
		{
			totals.bytes += log->bytes;
			totals.sent += log->sent;
			totals.total += log->total;
		}
	}
	for(i=0;i<count;i++)
	{
		log = &logs[i];
		if (log->sent || log->total)
		{
			const char	*s = log->name;

			if (strnicmp(log->name,"/entity/",8)==0)
				s+=8;
			if (strlen(s) > 50)
				s += strlen(s) - 50;
			if (!log->is_struct)
			{
				printf("%-50.50s %4.1f     %4.1f\n",s,100.f * log->bytes/totals.bytes,100.f * (totals.bytes - bytes) / totals.bytes);
				bytes += log->bytes;
			}
		}
	}
	free(logs);
	printf("bytes: %d\n",totals.bytes);
}

int logCmp(const StructSendLog *a, const StructSendLog *b)
{
	return (a->total-a->sent) - (b->total-b->sent);
}

void tpsStructsInternal(void)
{
	int				i,count,total=0;
	StructSendLog	*log,*logs,totals = {0};

	count = eaSize(&struct_logs);
	logs = malloc(count * sizeof(logs[0]));
	for(i=0;i<count;i++)
	{
		logs[i] = *struct_logs[i];
		log = &logs[i];
		if (log->is_struct)
		{
			totals.bytes += log->bytes;
			totals.sent += log->sent;
			totals.total += log->total;
		}
	}
	qsort(logs,count,sizeof(logs[0]),logCmp);

	for(i=0;i<count;i++)
	{
		log = &logs[i];
		if (log->sent || log->total)
		{
			const char	*s = log->name;

			if (strnicmp(log->name,"/entity/",8)==0)
				s+=8;
			if (strlen(s) > 50)
				s += strlen(s) - 50;
			if (log->is_struct)
			{
				printf("%-50.50s %7d/%7d  %4.1f  %4.1f\n",s,log->sent,log->total,100.f * log->total / totals.total,100.f * (totals.total - total) / totals.total);
				total += log->total;
			}
		}
	}
	free(logs);
	printf("struct numsent/total: %d/%d\n",totals.sent,totals.total);
}

static void structLog(ParseTable* tpi,int sent,int bytes)
{
	StructSendLog *log;

	if (!stashFindPointer(parse_struct_names,tpi,&log))
		log = struct_logs[0];
	if (bytes < 0 && !log->is_root)
		return;
	if (log)
	{
		log->total++;
		log->sent+=sent;
		log->bytes+=bytes;
	}
}


/* void ParserSend()
*
*  Given the structure definition and two versions of the structure, this function
*  packs up values in the new version that are different from the old version into
*  the given packet.
*
*  Parameters:
*      forcePackAllFields:
*          Send full value of all fields.  Bypass diff detection and bypass delta
*          delta generation.  This should be used when a structure is being sent
*          for the first time.
*
*		allowDiffs:
*			Whether or not to allow diffs, if set to false, only absolute values
*			will be sent.
*/

#ifdef _FULLDEBUG
int verifyDirtyBits = true; // should be true after PowerTree dirty bit is used properly
#else
int verifyDirtyBits = false;
#endif

AUTO_CMD_INT(verifyDirtyBits, verifyDirtyBits) ACMD_CMDLINE;

// 
bool ParserSend(ParseTable* tpi, Packet* pak, const void* oldVersion, const void* newVersion, enumSendDiffFlags eFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude,
				StructGenerateCustomIncludeExcludeFlagsCB *pGenerateFlagsCB)
{
	int i;
	StructTypeField iFlagsToMatch_ToRecurseWith = iOptionFlagsToMatch;
	StructTypeField iFlagsToExclude_ToRecurseWith = iOptionFlagsToExclude | TOK_NO_NETSEND;
	bool bDirtyBitVal = true;
	bool allowDiffs;
	bool bSentSomething = false;
	enumSendDiffFlags eRecurseFlags;
	int dirtyBitValid = false;
	PERFINFO_TYPE** piChild = NULL;
	U32 timingFlagsFunc = 0;
	
	PERFINFO_RUN(
		ParseTableInfo* pti = ParserGetTableInfo(tpi);
		if(oldVersion && !(eFlags & SENDDIFF_FLAG_FORCEPACKALL)){
			PERFINFO_AUTO_START("ParserSend (diff)", 1);
		}else{
			PERFINFO_AUTO_START("ParserSend (full)", 1);
		}
		PARSER_START_BIT_COUNT_STATIC(pak, &pti->piSend, pti->name, timingFlagsFunc);
		piChild = pti->piChildSend;
		if(!piChild){
			S32 count = ParserGetTableNumColumns(tpi);
			piChild = pti->piChildSend = callocStructs(PERFINFO_TYPE*, count);
		}
	);

	allowDiffs = (eFlags & SENDDIFF_FLAG_ALLOWDIFFS) && !(eFlags & SENDDIFF_FLAG_FORCEPACKALL) && oldVersion;

	//eRecurseFlags is eFlags, but with SENDDIFF_FLAG_ALLOWDIFFS set to the newly calculated value of allowDiffs
	eRecurseFlags = (eFlags & ~SENDDIFF_FLAG_ALLOWDIFFS) | (allowDiffs ? SENDDIFF_FLAG_ALLOWDIFFS : 0);

	if (pGenerateFlagsCB)
		pGenerateFlagsCB(tpi, newVersion, &iOptionFlagsToMatch, &iOptionFlagsToExclude, &iFlagsToMatch_ToRecurseWith, &iFlagsToExclude_ToRecurseWith);

	{
		U32 flags = 0;
		PARSER_START_BIT_COUNT(pak, "allowDiffs", flags);
		pktSendBits(pak, 1, allowDiffs);
		PARSER_STOP_BIT_COUNT(pak, flags);
	}

	if (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING) 
	{
		dirtyBitValid = ParserTableHasDirtyBitAndGetIt_Inline(tpi, newVersion, &bDirtyBitVal);
		if(dirtyBitValid && !bDirtyBitVal && !verifyDirtyBits)
		{
			//we were trying to do a diff, our new version had a dirty bit, but our new version's dirty bit was not set... abort
			{
				U32 flags = 0;
				PARSER_START_BIT_COUNT(pak, "notDirty", flags);
				pktSendBits(pak, 1, 0);
				PARSER_STOP_BIT_COUNT(pak, flags);
			}
			PARSER_STOP_BIT_COUNT(pak, timingFlagsFunc);
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (newVersion)
	FORALL_PARSETABLE(tpi, i)
	{
		StructTypeField type = TOK_GET_TYPE(tpi[i].type);
		int send = false;
		U32 childTimingFlags = 0;

		if ((type == TOK_IGNORE || type == TOK_START || type == TOK_END) || (tpi[i].type & (TOK_REDUNDANTNAME | TOK_UNOWNED)))
			continue;
		if (!FlagsMatchAll(tpi[i].type,iFlagsToMatch_ToRecurseWith) || !FlagsMatchNone(tpi[i].type,iFlagsToExclude_ToRecurseWith))
			continue;

		if (eFlags & SENDDIFF_FLAG_COMPAREBEFORESENDING)
		{
			int iCurIndex;
			int diffRetVal;

			if (FieldsMightDiffer(tpi,i,oldVersion,newVersion))
			{
				iCurIndex = pktGetWriteIndex(pak);

				{
					U32 flags = 0;
					PARSER_START_BIT_COUNT(pak, "hasMore + fieldIndex", flags);
					pktSendBits(pak, 1, 1);
					pktSendBitsPack(pak, 1, i);
					PARSER_STOP_BIT_COUNT(pak, flags);
				}

				if(piChild)
				{
					PARSER_START_BIT_COUNT_STATIC(	pak,
													&piChild[i],
													tpi[i].name,
													childTimingFlags);
				}

				diffRetVal = senddiff_autogen(pak, tpi, i, oldVersion, newVersion, 0, eRecurseFlags, iFlagsToMatch_ToRecurseWith, iFlagsToExclude_ToRecurseWith, pGenerateFlagsCB);
				
				if (diffRetVal == 0)
					pktSetWriteIndex(pak, iCurIndex);
				else
				{
					bSentSomething = true;

					if(verifyDirtyBits && dirtyBitValid && !bDirtyBitVal)
					{
						ParseTableInfo* pti = ParserGetTableInfo(tpi);
						Errorf("Dirty bit for %s was not set, but field %s changed", pti->name, tpi[i].name);
					}
				}
				if (do_struct_send_logging)
					structLog(&tpi[i],diffRetVal,pktGetWriteIndex(pak) - iCurIndex);

				if(piChild)
				{
					PARSER_STOP_BIT_COUNT(pak, childTimingFlags);
				}
			}
			else if (do_struct_send_logging)
				structLog(&tpi[i],0,0);
			
		}
		else
		{

			send = (eFlags & SENDDIFF_FLAG_FORCEPACKALL) || (compare_autogen(tpi, i, oldVersion, newVersion, 0,COMPAREFLAG_NULLISDEFAULT, iOptionFlagsToMatch, iOptionFlagsToExclude));

			if (send)
			{
				int sent;
				
				if(piChild)
				{
					PARSER_START_BIT_COUNT_STATIC(	pak,
													&piChild[i],
													tpi[i].name,
													childTimingFlags);
				}

				{
					U32 flags = 0;
					PARSER_START_BIT_COUNT(pak, "hasMore + fieldIndex", flags);

					// Send a bit saying there is a field diff to follow.
					pktSendBits(pak, 1, 1);

					// Send the index of this field.
					// We're assuming that that we'll have the exact same definition
					// during unpack.
					pktSendBitsPack(pak, 1, i);
					
					PARSER_STOP_BIT_COUNT(pak, flags);
				}
				
				sent = senddiff_autogen(pak, tpi, i, oldVersion, newVersion, 0, eRecurseFlags, iFlagsToMatch_ToRecurseWith, iFlagsToExclude_ToRecurseWith, pGenerateFlagsCB);

				if(piChild)
				{
					PARSER_STOP_BIT_COUNT(pak, childTimingFlags);
				}

				bSentSomething |= sent;
				if(verifyDirtyBits && sent && dirtyBitValid && !bDirtyBitVal)
				{
					ParseTableInfo* pti = ParserGetTableInfo(tpi);
					Errorf("Dirty bit for %s was not set, but field %s changed", pti->name, tpi[i].name);
				}
			}
		}
	}

	{
		U32 flags = 0;
		PARSER_START_BIT_COUNT(pak, "done", flags);
		// Say there there are no more diffs.
		pktSendBits(pak, 1, 0);
		PARSER_STOP_BIT_COUNT(pak, flags);
	}

	if (do_struct_send_logging && newVersion)
		structLog(tpi,bSentSomething,-1);

	PARSER_STOP_BIT_COUNT(pak, timingFlagsFunc);
	PERFINFO_AUTO_STOP();

	return bSentSomething;
}

void ParserSendEmptyDiff(ParseTable* sd, Packet* pak)
{
	ParserSend(sd, pak, NULL, NULL, 0, 0, 0, NULL);
}



/*
README README README
if you change ParserRecv so that the format of packets will be different, you MUST make the same
change to ParserRecv2tpis*/

bool ParserRecv(ParseTable *fieldDefs, Packet *pak, void *data, enumRecvDiffFlags eFlags)
{
	int hasMoreData;
	int numFields = ParserGetTableNumColumns(fieldDefs);
	int allowDiffs;
	int absValues;
	PERFINFO_TYPE** piChild = NULL;
	U32 timingFlagsFunc = 0;
		
	if (pktIsNotTrustworthy(pak) && !(eFlags & RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE))
	{
		Errorf("Untrustworthy packet being received (struct type %s) without untrustworthy flag set on link: %s", 
			ParserGetTableName(fieldDefs), pktLink(pak) ? linkDebugName(pktLink(pak)) : "(No link)");
		eFlags |= RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE;
	}

	PERFINFO_RUN(
		ParseTableInfo* pti = ParserGetTableInfo(fieldDefs);
		PERFINFO_AUTO_START_FUNC();
		PARSER_START_BIT_COUNT_STATIC(pak, &pti->piRecv, pti->name, timingFlagsFunc);
		piChild = pti->piChildRecv;
		if(!piChild){
			S32 count = ParserGetTableNumColumns(fieldDefs);
			piChild = pti->piChildRecv = callocStructs(PERFINFO_TYPE*, count);
		}
	);
		
	{
		U32 flags = 0;
		PARSER_START_BIT_COUNT(pak, "ParserRecv.allowDiffs", flags);
			allowDiffs = pktGetBits(pak,1);
		PARSER_STOP_BIT_COUNT(pak, flags);
	}
	
	absValues = !allowDiffs;
	eFlags &= ~RECVDIFF_FLAG_ABS_VALUES;


	while(1)
	{
		int fieldIndex;
		U32 timingFlagsChild = 0;
		
		{
			U32 flags = 0;
			PARSER_START_BIT_COUNT(pak, "ParserRecv.hasMoreData", flags);
				hasMoreData = pktGetBits(pak, 1);
			PARSER_STOP_BIT_COUNT(pak, flags);
		}
		
		if(!hasMoreData)
		{
			break;
		}
		
		{
			U32 flags = 0;
			PARSER_START_BIT_COUNT(pak, "ParserRecv.fieldIndex", flags);
				fieldIndex = pktGetBitsPack(pak, 1);
			PARSER_STOP_BIT_COUNT(pak, flags);
		}
		
		if (fieldIndex < 0 || fieldIndex >=numFields ) 
		{
			PERFINFO_RUN(
				PARSER_STOP_BIT_COUNT(pak, timingFlagsFunc);
				PERFINFO_AUTO_STOP();
			);
			RECV_FAIL("Received bad data in packed struct from server (code probably out of sync)");
		}
		if (fieldDefs[fieldIndex].name == invalidName) // need to ignore this field
			data = NULL;
			
		if(piChild)
		{
			PARSER_START_BIT_COUNT_STATIC(	pak,
											&piChild[fieldIndex],
											fieldDefs[fieldIndex].name,
											timingFlagsChild);
		}
										
		if (!recvdiff_autogen(pak, fieldDefs, fieldIndex, data, 0, eFlags | (absValues ? RECVDIFF_FLAG_ABS_VALUES : 0)))
			return 0;
		
		if(piChild)
		{
			PARSER_STOP_BIT_COUNT(pak, timingFlagsChild);
		}
	}
	
	RECV_CHECK_PAK(pak);

	PERFINFO_RUN(
		PARSER_STOP_BIT_COUNT(pak, timingFlagsFunc);
		PERFINFO_AUTO_STOP();
	);

	return 1;
}



void sdFreeParseInfo(ParseTable* fieldDefs)
{
	int i;
	if (!fieldDefs)
		return;

	FORALL_PARSETABLE(fieldDefs, i)
	{
		ParseTable* fd = &fieldDefs[i];
		if (TOK_HAS_SUBTABLE(fd->type))
			sdFreeParseInfo((ParseTable*)fd->subtable);
	}
	ParserClearTableInfo(fieldDefs);
	free(fieldDefs);
}


bool sdFieldsCompatible(ParseTable *field1, ParseTable *field2)
{
	if (stricmp(field1->name, field2->name)==0) 
	{
		StructTypeField ttt1 = TOK_GET_TYPE(field1->type);
		StructTypeField ttt2 = TOK_GET_TYPE(field2->type);

		if (TOK_GET_PRECISION(field1->type) != TOK_GET_PRECISION(field2->type)) // same network packing
			return false;
		if ((TOK_FIXED_ARRAY & field1->type) != (TOK_FIXED_ARRAY & field2->type)) // same array type, but don't care about indirection
			return false;
		if ((TOK_EARRAY & field1->type) != (TOK_EARRAY & field2->type))	// ''
			return false;
		if ((TOK_FIXED_ARRAY & field1->type) && field1->param != field2->param) // size of fixed array the same
			return false;
		if (ttt1==ttt2)
			return true;
		if ((ttt1==TOK_INT_X || ttt1==TOK_U8_X || ttt1==TOK_BOOL_X  || ttt1==TOK_INT16_X) &&
			(ttt2==TOK_INT_X || ttt2==TOK_U8_X || ttt2==TOK_BOOL_X  || ttt2==TOK_INT16_X))
		{
			// All integer types are compatible, but we must use the *local* type when unpacking
			return true;
		}
	}
	return false;
}

void sdPackParseInfo(ParseTable* fieldDefs, Packet *pak)
{
	int i, totalFields;

	totalFields = ParserGetTableNumColumns(fieldDefs);
	pktSendBitsPack(pak, 1, totalFields);

	// Iterate through all fields defined in StructDef
	for(i = 0; i < totalFields; i++)
	{
		U32 low, high;
		ParseTable* fd = &fieldDefs[i];

		pktSendString(pak, fd->name);
		
		// arg, really need pktSendBits64..
		low = (U64)fd->type & 0xffffffff;
		high = (U64)fd->type >> 32;
		pktSendBits(pak, 32, low);
		pktSendBits(pak, 32, high);

		pktSendBits(pak, 32, (U32)fd->param); // may be cutting from 64 to 32, but pointers are ignored on other side
		if (TOK_HAS_SUBTABLE(fd->type))
			sdPackParseInfo((ParseTable*)fd->subtable, pak);
	}
	assert(i==totalFields);
}

ParseTable *sdUnpackParseInfo(ParseTable *supported, Packet *pak)
{
	int i, j, totalFields;
	ParseTable *ret;

	totalFields = pktGetBitsPack(pak, 1);
	ret = calloc(sizeof(ParseTable), totalFields+1);
	//ParseTableClearCachedInfo(ret);

	// Iterate through all fields defined in StructDef
	for(i = 0; i<totalFields; i++)
	{
		U32 low, high;
		ParseTable* fd = &ret[i];
		ParseTable* match = NULL;
		char *tempString = pktGetStringTemp(pak);

		fd->name = allocAddString(tempString);
		low = pktGetBits(pak, 32);
		high = pktGetBits(pak, 32);
		fd->type = (U64)high << 32 | (U64)low;
		fd->param = pktGetBits(pak,32);

		// Try to find local match
		if (supported) {
			for (j=0; supported[j].name; j++) {
				if (sdFieldsCompatible(&supported[j], fd)) {
					match = &supported[j];
					break;
				}
			}
		}

		if (TOK_HAS_SUBTABLE(fd->type))
			fd->subtable = sdUnpackParseInfo(match? match->subtable: NULL, pak);

		// copy compatibility info if we matched
		if (match) {
			fd->type = match->type; // Use local type beacuse it could be different, but still compatible
			fd->param = match->param;
			fd->storeoffset = match->storeoffset;
			fd->format = match->format;
		} else {			
			fd->name = invalidName;
		}
	}
	assert(i==totalFields);

	return ret;
}



void ParserSendStructSafe(ParseTable *pTPI, Packet *pPack, void *pStruct)
{

#if FAST_SEND_SAFE
	if (LinkParseTableAlreadySent(pktLink(pPack), pTPI))
	{
		pktSendBits(pPack, 32, 0);
		ParserSendStruct(pTPI, pPack, pStruct);
	}
	else
#endif
	{
		const char *pTableName = ParserGetTableName(pTPI);
		U32 iCRC = ParseTableCRC(pTPI, NULL, 0);
		pktSendBits(pPack, 32, iCRC);
		ParseTableSend(pPack, pTPI, pTableName ? pTableName : "unknown", PARSETABLESENDFLAG_MAINTAIN_BITFIELDS);
		ParserSendStruct(pTPI, pPack, pStruct);
#if FAST_SEND_SAFE
		LinkSetParseTableSent(pktLink(pPack), pTPI);
#endif
	}
}

bool ParserRecvStructSafe(ParseTable *pTPI, Packet *pPack, void *pStruct)
{
	ParseTable **ppParseTables = NULL;
	U32 iRemoteCRC = pktGetBits(pPack, 32);
	int iSize;
	char *pName;

	bool bUntrustworthy = pktIsNotTrustworthy(pPack);

	//means that the remote side thinks they've already sent us this TPI, so we should have saved it somewhere
	if (iRemoteCRC == 0)
	{
		LinkGetReceivedTableResult eResult = LinkGetReceivedParseTable(pktLink(pPack), pTPI, &ppParseTables);

		if (eResult == TABLE_NOT_YET_RECEIVED)
		{
			StructDeInitVoid(pTPI, pStruct);

			if (bUntrustworthy)
			{
				ErrorOrAlert("SAFERECV_FAIL", "Link %s thinks it has already sent us its TPI for %s, we disagree",
					linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
			}	
			else
			{
				AssertOrAlert("SAFERECV_FAIL", "Link %s thinks it has already sent us its TPI for %s, we disagree",
					linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
			}
			return false;
		}
		else if (eResult == LOCAL_TABLE_IDENTICAL)
		{
			if (!ParserRecv(pTPI, pPack, pStruct, 0))
			{
				StructDeInitVoid(pTPI, pStruct);

				if (bUntrustworthy)
				{
					ErrorOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with identical TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
					return false;
				}
				else
				{
					AssertOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with identical TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
					return false;
				}
			}
		}
		else //TABLE_RECEIVED_AND_DIFFERENT
		{
			void *pOtherStruct = StructCreateVoid(ppParseTables[0]);
			char *pResultString = NULL;
			enumCopy2TpiResult eCopyResult;

			if (!ParserRecv(ppParseTables[0], pPack, pOtherStruct, 0))
			{
				StructDestroyVoid(ppParseTables[0], pOtherStruct);
				StructDeInitVoid(pTPI, pStruct);

				if (bUntrustworthy)
				{
					ErrorOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with identical TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
					return false;
				}
				else
				{
					AssertOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with identical TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
				}

				return false;


			}
			StructDeInitVoid(pTPI, pStruct);

			eCopyResult = StructCopyFields2tpis(ppParseTables[0], pOtherStruct, pTPI, pStruct, 0, 0, &pResultString);

			if (eCopyResult == COPY2TPIRESULT_FAILED_FIELDS)
			{
				Errorf("ParserReceiveSafe error during struct copying: %s", pResultString);
			}

			StructDestroyVoid(ppParseTables[0], pOtherStruct);
			estrDestroy(&pResultString);
		}
	}
	else
	{
		U32 iLocalCRC = ParseTableCRC(pTPI, NULL, 0);


		if (!ParseTableRecv(pPack, &ppParseTables, &iSize, &pName, PARSETABLESENDFLAG_MAINTAIN_BITFIELDS))
		{
			StructDeInitVoid(pTPI, pStruct);

			if (bUntrustworthy)
			{
				ErrorOrAlert("TABLE_RECV_FAILURE", "Link %s got ParseTableRecv failure during ParserReceiveSafe while trying to receive parse_%s. This will cause all future sending of this object type to fail, this could be catastrophic",
					linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
			}
			else
			{
				AssertOrAlert("TABLE_RECV_FAILURE", "Link %s got ParseTableRecv failure during ParserReceiveSafe while trying to receive parse_%s. This will cause all future sending of this object type to fail, this could be catastrophic",
					linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
			}
			return false;
		}

		if (iLocalCRC == iRemoteCRC)
		{
			ParseTableFree(&ppParseTables);

			if (!ParserRecv(pTPI, pPack, pStruct, 0))
			{
				StructDeInitVoid(pTPI, pStruct);

				if (bUntrustworthy)
				{
					ErrorOrAlert("SAFERECV_FAIL", "Link %s had identical tpi for %s in safereceive, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
				}
				else
				{
					AssertOrAlert("SAFERECV_FAIL", "Link %s had identical tpi for %s in safereceive, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
				}

				return false;

			}
			
			
			LinkSetReceivedParseTable(pktLink(pPack), pTPI, true, NULL);
		}
		else
		{
			void *pOtherStruct = StructCreateVoid(ppParseTables[0]);
			char *pResultString = NULL;
			enumCopy2TpiResult eResult;


			if (!ParserRecv(ppParseTables[0], pPack, pOtherStruct, 0))
			{
				StructDestroyVoid(ppParseTables[0], pOtherStruct);
				StructDeInitVoid(pTPI, pStruct);

				if (bUntrustworthy)
				{
					ErrorOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with different TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
				}
				else
				{
					AssertOrAlert("SAFERECV_FAIL", "Link %s got a safe Recv packet for TPI %s, thought it could receive with different TPI, but ParserRecv failed",
						linkDebugName(pktLink(pPack)), ParserGetTableName(pTPI));
				}

				return false;





			}
			StructDeInitVoid(pTPI, pStruct);

			eResult = StructCopyFields2tpis(ppParseTables[0], pOtherStruct, pTPI, pStruct, 0, 0, &pResultString);

			if (eResult == COPY2TPIRESULT_FAILED_FIELDS)
			{
				Errorf("ParserReceiveSafe error during struct copying: %s", pResultString);
			}

			StructDestroyVoid(ppParseTables[0], pOtherStruct);
			estrDestroy(&pResultString);
			LinkSetReceivedParseTable(pktLink(pPack), pTPI, false, &ppParseTables);
		}
	}

	return true;
}

/*
enumCopy2TpiResult ParserRecv2tpis(Packet *pak, ParseTable *pSrcTable, ParseTable *pTargetTable, void *data)
{
	void *pSrcStruct = StructCreate(pSrcTable);
	char *pErrorString = NULL;
	enumCopy2TpiResult eResult;
	
	ParserRecv(pSrcTable, pak, pSrcStruct, 0);

	eResult = StructCopyFields2tpis(pSrcTable, pSrcStruct, pTargetTable, data, 0, 0, &pErrorString);

	StructDestroyVoid(pSrcTable, pSrcStruct);
	estrDestroy(&pErrorString);

	return eResult;
}
*/


bool TypeIsSentAsString(U32 eType)
{
	switch (eType)
	{
	case TOK_STRING_X:
	case TOK_REFERENCE_X:
		return true;
	}

	return false;
}

bool TypeCanBeReceivedAsString(U32 eType)
{
	switch (eType)
	{
	case TOK_U8_X:			
	case TOK_INT16_X:	
	case TOK_INT_X:			
	case TOK_INT64_X:		
	case TOK_F32_X:			
	case TOK_STRING_X:		
	case TOK_CURRENTFILE_X:	
	case TOK_TIMESTAMP_X:	
	case TOK_LINENUM_X:	
	case TOK_BOOL_X:		
	case TOK_BOOLFLAG_X:		
	case TOK_QUATPYR_X:		
	case TOK_MATPYR_X:		
	case TOK_FILENAME_X:		
	case TOK_REFERENCE_X:	
	case TOK_BIT:			
	case TOK_MULTIVAL_X:
		return true;
	}
	return false;
}


Recv2TpiCachedInfo *FindOrCreateRecv2TpiCachedInfo(ParseTable *pSrcTable, ParseTable *pDestTable)
{
	int i;
	ParseTableInfo *pInfo = ParserGetTableInfo(pDestTable);
	Recv2TpiCachedInfo *pCache;
	int iSrcColumn;
	int iDestColumn;
	U32 iSrcCRC;
	U32 iDestCRC;

	//unlikely the size of the earray will be larger than 1 for now, so just doing a linear search
	for (i=0; i < eaSize(&pInfo->ppRecv2TpiCachedInfos); i++)
	{
		if (pInfo->ppRecv2TpiCachedInfos[i]->pSrcTable == pSrcTable)
		{
			return pInfo->ppRecv2TpiCachedInfos[i];
		}
	}

	pCache = calloc(sizeof(Recv2TpiCachedInfo), 1);
	pCache->pSrcTable = pSrcTable;
	pCache->pColumns = calloc(sizeof(Recv2TpiColumnInfo), ParserGetTableNumColumns(pSrcTable));

	FORALL_PARSETABLE(pSrcTable, iSrcColumn)
	{

		U32 iSrcType = TOK_GET_TYPE(pSrcTable[iSrcColumn].type);
		U32 iDestType;

		if (iSrcType == TOK_IGNORE || iSrcType == TOK_START || iSrcType == TOK_END
			||pSrcTable[iSrcColumn].type & TOK_REDUNDANTNAME
			||pSrcTable[iSrcColumn].type & TOK_PARSETABLE_INFO)
		{
			continue; //leave eType as DONTCARE
		}

		iDestColumn = FindMatchingColumnInOtherTpi(pSrcTable, iSrcColumn, pDestTable);

		if (iDestColumn < 0)
		{
			AssertOrAlert("UNKNOWN_RECV2TPI_FIELD", "Unknown field %s while setting up to recv2tpis from tpi %s", pSrcTable[iSrcColumn].name, ParserGetTableName(pDestTable));
			continue;
		}

		pCache->pColumns[iSrcColumn].iTargetColumn = iDestColumn;

		iDestType = TOK_GET_TYPE(pDestTable[iDestColumn].type);
		
		//polys are not supported at all, because they're complicated and because they're not allowed in persisted data, which
		//is currently all that this system is used for
		if (iSrcType == TOK_POLYMORPH_X)
		{
			AssertOrAlert("RECV2TPI_POLY", "While trying to recv2tpi %s, src column %s was a poly. That is unsupported",
				ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name);
			continue;
		}
		if (iDestType == TOK_POLYMORPH_X)
		{
			AssertOrAlert("RECV2TPI_POLY", "While trying to recv2tpi %s, dest column %s was a poly. That is unsupported",
				ParserGetTableName(pDestTable), pDestTable[iDestColumn].name);
			continue;
		}

		if ((iSrcType == TOK_STRUCT_X) ^ (iDestType == TOK_STRUCT_X))
		{
			AssertOrAlert("RECV2TPI_STRUCT", "While trying to recv2tpi %s, src column %s is a struct and dest column isn't, or vice versa. This is unsupported",
				ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name);
			continue;
		}

		//array types must match exactly
		if ((TokenStoreStorageTypeIsFixedArray(pSrcTable[iSrcColumn].type) ^ TokenStoreStorageTypeIsFixedArray(pDestTable[iDestColumn].type)
			|| TokenStoreStorageTypeIsEArray(pSrcTable[iSrcColumn].type) ^ TokenStoreStorageTypeIsEArray(pDestTable[iDestColumn].type)))
		{
			AssertOrAlert("RECV2TPI_ARRAY", "While trying to recv2tpi %s, src column %s is an array of a type dest column isn't, or vice versa. This is unsupported",
				ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name);
			continue;
		}

		//if it's a struct or array of structs, the CRC definitely won't match, and recursion will occur
		if (iSrcType == TOK_STRUCT_X)
		{
			pCache->pColumns[iSrcColumn].eType = RECV2TPITYPE_STRUCT;
			continue;
		}


		iSrcCRC = GetCRCFromParseInfoColumn(pSrcTable, iSrcColumn, TPICRCFLAG_IGNORE_NAME);
		iDestCRC = GetCRCFromParseInfoColumn(pDestTable, iDestColumn, TPICRCFLAG_IGNORE_NAME);

		if (iSrcCRC == iDestCRC)
		{
			pCache->pColumns[iSrcColumn].eType = RECV2TPITYPE_NORMALRECV;
			continue;
		}

		//fixed size arrays must be the same size
		if (TokenStoreStorageTypeIsFixedArray(pSrcTable[iSrcColumn].type))
		{
			if (pSrcTable[iSrcColumn].param != pDestTable[iDestColumn].param)
			{
				AssertOrAlert("RECV2TPI_NONMATCHING_ARRAY", "While trying to recv2tpi %s, src column %s and dest column have different fixed array sizes",
					ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name);
				continue;
			}
		}


		if (TypeIsSentAsString(iSrcType) && TypeCanBeReceivedAsString(iDestType))
		{
			pCache->pColumns[iSrcColumn].eType = RECV2TPITYPE_STRINGRECV;
			continue;
		}

		if (iSrcType == TOK_U8_X && iDestType == TOK_BIT)
		{
			pCache->pColumns[iSrcColumn].eType = RECV2TPITYPE_U8TOBIT;
			continue;
		}

		//special case for ints... because staticdefines can be extended at runtime, and we send ints as binary anyhow,
		//accept any two ints which are identical except that one has a staticdefine which is a prefix of another
		if (TypeIsInt(iSrcType) && iSrcType == iDestType && pSrcTable[iSrcColumn].subtable && pDestTable[iDestColumn].subtable && 
			OneStaticDefineIsPrefixOfOther(pSrcTable[iSrcColumn].subtable, pDestTable[iDestColumn].subtable)
			&& GetCRCFromParseInfoColumn(pSrcTable, iSrcColumn, TPICRCFLAG_IGNORE_NAME | TPICRCFLAG_IGNORE_SUBTABLE) == GetCRCFromParseInfoColumn(pDestTable, iDestColumn, TPICRCFLAG_IGNORE_NAME | TPICRCFLAG_IGNORE_SUBTABLE))
		{
			pCache->pColumns[iSrcColumn].eType = RECV2TPITYPE_NORMALRECV;
			continue;
		}

		//in the case where they are both ints and have subtables and the subtables don't match, give a more verbose error message
		if (TypeIsInt(iSrcType) && iSrcType == iDestType && pSrcTable[iSrcColumn].subtable && pDestTable[iDestColumn].subtable)
		{
			AssertOrAlert("RECV2TPI_NONMATCHING_ENUMS", "While trying to recv2tpi %s, the StaticDefines for column %s don't match. This is most likely because dynamically loaded enums are not being loaded properly, either on the gameserver during makeSchemas, or on the destination server",
				ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name);
		}
		else
		{
			AssertOrAlert("RECV2TPI_UNKNOWN_TYPE", "While trying to recv2tpi %s, src column %s and dest column don't match, and can't be stringified. Src type is %s. Dest type is %s",
				ParserGetTableName(pDestTable), pSrcTable[iSrcColumn].name, StaticDefineIntRevLookup(StructTokenTypeEnum, iSrcType),
				StaticDefineIntRevLookup(StructTokenTypeEnum, iDestType));
		}

	}

	eaPush(&pInfo->ppRecv2TpiCachedInfos, pCache);
	


	return pCache;


}




int ParserRecv2tpis(Packet *pak, ParseTable *pSrcTable, ParseTable *pTargetTable, void *data)
{

	Recv2TpiCachedInfo *pCache = FindOrCreateRecv2TpiCachedInfo(pSrcTable, pTargetTable);
	int numSrcFields = ParserGetTableNumColumns(pSrcTable);

	//allowdiffs
	assertmsg(pktGetBits(pak,1) == 0, "ParserRecv2tpis expects allowDiffs to be false");

	while (1)
	{
		int fieldIndex;
		int hasMoreData = pktGetBits(pak, 1);

		if (!hasMoreData)
		{
			break;
		}

		fieldIndex = pktGetBitsPack(pak, 1);

		if (!(fieldIndex >= 0 && fieldIndex < numSrcFields))
		{
			AssertOrAlert("BAD_RECV2TPI_INDEX", "Received bad index while doing recv2tpis on %s, fieldIndex = %d, numSrcFields = %d", 
				ParserGetTableName(pTargetTable), fieldIndex, numSrcFields);
			return 0;
		}

		switch(pCache->pColumns[fieldIndex].eType)
		{
		case RECV2TPITYPE_DONTCARE:
		case RECV2TPITYPE_UNKNOWN:
			//we're ignoring this data, but make sure to read it out of the packet
			if (!recvdiff_autogen(pak, pSrcTable, fieldIndex, NULL, 0, 0))
			{
				return 0;
			}
			break;			

		default: //the two columns are different, but you can just receive a string and then call a string-token-setting
			if (!recvdiff2tpis_autogen(pak, pSrcTable, pTargetTable, fieldIndex, 0, pCache, data))
			{
				return 0;
			}
		}
	}

	return 1;
}

char *DEFAULT_LATELINK_GetRecvFailCommentString(void)
{
	return "";
}

#define CHECKEDNAMEVALUEPAIRSEND_MAX_FIELD_NAME_LEN 64
#define CHECKEDNAMEVALUEPAIRSEND_MAX_EXTRA_FIELDS 16
#define CHECKEDNAMEVALUEPAIRSEND_MAX_STRING_FIELD_LEN 1032

#define CHECKEDNAMEVALUEPAIRSEND_MAX_FIELD_LEN 1032

typedef enum CheckedNameValuePairSendType
{
	SENDTYPE_INT32 = 0,
	SENDTYPE_STRING = 1,
} CheckedNameValuePairSendType;

int AssertTPIIsOKForCheckedNameValuePairSendReceive(ParseTable *pTPI)
{
	int i;
	int iFieldCount = 0;

	FORALL_PARSETABLE(pTPI, i)
	{
		StructTypeField type = TOK_GET_TYPE(pTPI[i].type);
		U32 storage = TokenStoreGetStorageType(pTPI[i].type);
				
		if ((type == TOK_IGNORE || type == TOK_START || type == TOK_END) || (pTPI[i].type & (TOK_REDUNDANTNAME)))
		{
			continue;
		}

		iFieldCount++;

		assertmsgf(pTPI[i].name[0] && strlen(pTPI[i].name) < CHECKEDNAMEVALUEPAIRSEND_MAX_FIELD_NAME_LEN,
			"Parse table %s can not be sent/received with ParserSendStructAsCheckedNameValuePairs, field %i(%s) has an invalid name",
				ParserGetTableName(pTPI), i, pTPI[i].name);
		
		assertmsgf(type == TOK_INT_X || type == TOK_STRING_X, "Parse table %s can not be sent/received with ParserSendStructAsCheckedNameValuePairs, field %d(%s) is not either a single 32-bit integer or embedded string",
			ParserGetTableName(pTPI), i, pTPI[i].name);
		
		assertmsgf(storage == TOK_STORAGE_DIRECT_SINGLE, "Parse table %s can not be sent/received with ParserSendStructAsCheckedNameValuePairs, field (%d)%s is not a direct embedded field",
			ParserGetTableName(pTPI), i, pTPI[i].name);

		if (type == TOK_STRING_X)
		{
			assertmsgf(pTPI[i].param < CHECKEDNAMEVALUEPAIRSEND_MAX_STRING_FIELD_LEN, "Parse table %s can not be sent/received with ParserSendStructAsCheckedNameValuePairs, field %d(%s) is oversized",
				ParserGetTableName(pTPI), i, pTPI[i].name);
		}

	}

	return iFieldCount;
}

void ParserSendStringForNameValuePairs(Packet *pPak, const char *pStr)
{
	int iLen;
	if (!pStr)
	{
		pStr = "";
	}

	iLen = (int)(strlen(pStr) + 1);
	pktSendBits(pPak, 32, iLen );
	pktSendBytes(pPak, iLen, (void*)pStr);
	
}

void ParserSendIntForNameValuePairs(Packet *pPak, U32 iVal)
{
	pktSendBits(pPak, 32, 4);
	pktSendBytes(pPak, 4, &iVal);
}

void ParserSendStructAsCheckedNameValuePairs(Packet *pPak, ParseTable *pTPI, void *pStruct)
{
	int i;

	AssertTPIIsOKForCheckedNameValuePairSendReceive(pTPI);

	FORALL_PARSETABLE(pTPI, i)
	{
		StructTypeField type = TOK_GET_TYPE(pTPI[i].type);
		U32 iVal;
	
		if ((type == TOK_IGNORE || type == TOK_START || type == TOK_END) || (pTPI[i].type & (TOK_REDUNDANTNAME)))
		{
			continue;
		}

		if (type == TOK_INT_X)
		{
			iVal = TokenStoreGetInt_inline(pTPI, &pTPI[i], i, pStruct, 0, NULL);
			if (iVal)
			{
				ParserSendStringForNameValuePairs(pPak, pTPI[i].name);
				pktSendBitsPack(pPak, 32, SENDTYPE_INT32);
				ParserSendIntForNameValuePairs(pPak, iVal);
			}
		}
		else
		{
			const char *pStr = TokenStoreGetString_inline(pTPI, &pTPI[i], i, pStruct, 0, NULL);
			if (pStr && pStr[0])
			{
				ParserSendStringForNameValuePairs(pPak, pTPI[i].name);
				pktSendBitsPack(pPak, 32, SENDTYPE_STRING);
				ParserSendStringForNameValuePairs(pPak, pStr);
			}
		}
	}

	ParserSendStringForNameValuePairs(pPak, "");
}

int ParserFindColumnFromFieldName(ParseTable *pTPI, char *pColumnName)
{
	int iRetVal = -1;
	int i;

	FORALL_PARSETABLE(pTPI, i)
	{
	
		if (stricmp_safe(pColumnName, pTPI[i].name) == 0)
		{
			if (pTPI[i].type & TOK_REDUNDANTNAME)
			{
				iRetVal = ParseInfoFindAliasedField(pTPI, i);
			}
			else
			{
				iRetVal = i;
			}
			break;
		}
	}

	return iRetVal;
}

bool ParserReadStringForNameValuePairs(Packet *pPak, char *pStr, int iBufferSize)
{
	int iLen = pktGetBits(pPak, 32);
	if (iLen > iBufferSize)
	{
		return false;
	}

	if (iLen == 0)
	{
		return false;
	}


	pktGetBytes(pPak, iLen, pStr);

	if (pStr[iLen - 1] != 0)
	{
		return false;
	}

	if ((int)strlen(pStr) != iLen - 1)
	{
		return false;
	}
	
	return true;
}

bool ParserReadUnknownColumnForNameValuePairs(Packet *pPak)
{
	int iLen = pktGetBits(pPak, 32);
	if (iLen > CHECKEDNAMEVALUEPAIRSEND_MAX_FIELD_LEN)
	{
		return false;
	}

	pktGetBytes(pPak, iLen, NULL);
	return true;
}

bool ParserReadIntForNameValuePairs(Packet *pPak, U32 *pOutInt)
{
	int iLen = pktGetBits(pPak, 32);
	if (iLen != 4)
	{
		return false;
	}
	pktGetBytes(pPak, 4, pOutInt);
	return true;
}

bool ParserReceiveStructAsCheckedNameValuePairs(Packet *pPak, ParseTable *pTPI, void *pStruct)
{
	int iMaxFieldCount = AssertTPIIsOKForCheckedNameValuePairSendReceive(pTPI) + CHECKEDNAMEVALUEPAIRSEND_MAX_EXTRA_FIELDS;
	int iFieldsReceived = 0;

	while (1)
	{
		char tempFieldName[CHECKEDNAMEVALUEPAIRSEND_MAX_FIELD_NAME_LEN];
		CheckedNameValuePairSendType eSendType;
		int iFoundColumn;
		StructTypeField eFieldType;

		if (!ParserReadStringForNameValuePairs(pPak, SAFESTR(tempFieldName)))
		{
			return false;
		}

		if (!tempFieldName[0])
		{
			return true;
		}

		if (iFieldsReceived >= iMaxFieldCount)
		{
			return false;
		}

		iFieldsReceived++;

		iFoundColumn = ParserFindColumnFromFieldName(pTPI, tempFieldName);
		eSendType = pktGetBitsPack(pPak, 32);

		if (iFoundColumn == -1)
		{
			if (!ParserReadUnknownColumnForNameValuePairs(pPak))
			{
				return false;
			}
			continue;
		}

		eFieldType = TOK_GET_TYPE(pTPI[iFoundColumn].type);

		switch (eFieldType)
		{
		xcase TOK_INT_X:
			if (eSendType == SENDTYPE_INT32)
			{
				U32 iVal;
				if (!ParserReadIntForNameValuePairs(pPak, &iVal))
				{
					return false;
				}

				TokenStoreSetInt_inline(pTPI, &pTPI[iFoundColumn], iFoundColumn, pStruct, 0, iVal, NULL, NULL);
			}
			else
			{
				if (!ParserReadUnknownColumnForNameValuePairs(pPak))
				{
					return false;
				}
			}

		xcase TOK_STRING_X:
			if (eSendType == SENDTYPE_STRING)
			{
				char tempStr[CHECKEDNAMEVALUEPAIRSEND_MAX_STRING_FIELD_LEN];
				if (!ParserReadStringForNameValuePairs(pPak, SAFESTR(tempStr)))
				{
					return false;
				}

				if (strlen(tempStr) >= (size_t)(pTPI[iFoundColumn].param))
				{
					//string didn't fit, don't truncate, just do nothing
				}
				else
				{
					TokenStoreSetString(pTPI, iFoundColumn, pStruct, 0, tempStr, NULL, NULL, NULL, NULL);
				}
			}
			else
			{
				if (!ParserReadUnknownColumnForNameValuePairs(pPak))
				{
					return false;
				}
			}

		xdefault:
			ParserReadUnknownColumnForNameValuePairs(pPak);
		}
	}
}
		
		
