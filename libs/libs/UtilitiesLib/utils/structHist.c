#include "StashTable.h"
#include "structHist.h"

#include "file.h"
#include "StringCache.h"
#include "timing.h"
#include <string.h>
#include "tokenstore.h"
#include "rgb_hsv.h"
#include "mathutil.h"

typedef struct StructHistImp {
	F32 *vals;
	U32 lastTimestamp;
	F32 timeAccum;
	ParseTable *tpi;
	U32 remove:1; // flagged for removal from the SHC
	const char *folder; // Folder to dump logs into
} StructHistImp;

typedef struct StructHistCollectionImp {
	StashTable ghtStructHists;
	StashTable ghtRemovedStructHists;
	U32 timestamp; // Used in dumpProcessor callback
	char logDir[CRYPTIC_MAX_PATH];
} StructHistCollectionImp;

static int dateSetGlobally = false;
static char *currentDate=NULL;

StructHist createStructHist(ParseTable *tpi, char *folder)
{
	StructHist sh = calloc(sizeof(StructHistImp), 1);
	sh->tpi = tpi;
	sh->folder = allocAddFilename(folder);
	return sh;
}

void shFreeSampleData(ParseTable* fieldDefs, void *** vals);

void destroyStructHist(StructHist sh)
{
	shFreeSampleData(sh->tpi, (void***)&sh->vals);
	free(sh);
}

void shFreeSampleData(ParseTable* fieldDefs, void *** vals) 
{
	int fieldIndex;

	if (!vals || !*vals)
		return;
	// Iterate through all fields defined in ParseTable, and if they're a struct, recurse and free
	FORALL_PARSETABLE(fieldDefs, fieldIndex)
	{
		ParseTable* fd = &fieldDefs[fieldIndex];
		int storage = TokenStoreGetStorageType(fd->type);
		void** fieldAddr;

		fieldAddr = (*vals) + fieldIndex;
		if(fd->type & TOK_REDUNDANTNAME)
			continue;

		if (storage != TOK_STORAGE_DIRECT_SINGLE && storage != TOK_STORAGE_INDIRECT_SINGLE)
		{
			Errorf("shFreeSampleData is not compatible with array fields");
			continue;
		}
		
		switch (TOK_GET_TYPE(fd->type))
		{
		case TOK_STRUCT_X:
			shFreeSampleData((ParseTable*)fd->subtable, (void***)fieldAddr);
			break;
		case TOK_STRING_X:
		case TOK_FILENAME_X:
			{
				char** s=(char**)fieldAddr;
				SAFE_FREE(*s);
			}
			break;
		}
	}
	SAFE_FREE(*vals);
}



void shSampleInternal(F32 **vals, void *elem, F32 dt, ParseTable *fieldDefs)
{
	int i;
	if (!*vals) {
		*vals = calloc(ParserGetTableNumColumns(fieldDefs), sizeof(F32));
	}

	// Iterate through all fields defined in StructDef
	FORALL_PARSETABLE(fieldDefs, i)
	{
		F32* valptr;
		F32 valadd = 0;
		ParseTable* fd = &fieldDefs[i];
		int format = TOK_GET_FORMAT_OPTIONS(fd->format);
		int storage = TokenStoreGetStorageType(fd->type);

		if(fd->type & TOK_REDUNDANTNAME)
			continue;
		
		if (format == TOK_FORMAT_DATESS2000 ||
			format == TOK_FORMAT_FRIENDLYDATE ||
			format == TOK_FORMAT_FRIENDLYSS2000 ||
			format == TOK_FORMAT_IP)
			continue;

		if (storage != TOK_STORAGE_DIRECT_SINGLE && storage != TOK_STORAGE_INDIRECT_SINGLE)
		{
			Errorf("shSampleInternal is not compatible with array fields");
			continue;
		}

		// Pointer to where the running total is stored
		valptr = &(*vals)[i];

		switch (TOK_GET_TYPE(fd->type))
		{
		case TOK_INT_X:
			valadd = (F32)(elem?TokenStoreGetInt(fieldDefs, i, elem, 0, NULL):0);
			break;
		case TOK_INT64_X:
			valadd = (F32)(elem?TokenStoreGetInt64(fieldDefs, i, elem, 0, NULL):0);
			break;
		case TOK_INT16_X:
			valadd = (F32)(elem?TokenStoreGetInt16(fieldDefs, i, elem, 0, NULL):0);
			break;
		case TOK_BOOL_X:
		case TOK_U8_X:
			valadd = (F32)(elem?TokenStoreGetU8(fieldDefs, i, elem, 0, NULL):0);
			break;
		case TOK_F32_X:
			valadd = elem?TokenStoreGetF32(fieldDefs, i, elem, 0, NULL):0;
			if (format == TOK_FORMAT_PERCENT) {
				valadd*=100;
			}
			break;

		case TOK_STRUCT_X:
			shSampleInternal((F32**)valptr, elem?TokenStoreGetPointer(fieldDefs, i, elem, 0, NULL): 0, dt, (ParseTable*)fd->subtable);
			break;
		case TOK_STRING_X:
			{
				char* newstr = elem? TokenStoreGetPointer(fieldDefs, i, elem, 0, NULL): 0;
                char** s=(char**)valptr;
				if (*s && (!(newstr) || strcmp(*s, newstr)!=0)) {
					// It's changed
					free(*s);
					*s=NULL;
				}
				if (!*s) {
					// String not allocated or it's new
					*s = strdup(newstr);
				}
			}
			break;
		case TOK_IGNORE:
		case TOK_START:
		case TOK_END:
			// Ignored
			break;

		default:
			assert(0);
		}
		if (valadd) {
			*valptr += valadd*dt;
		}
	}

}

void shSample(StructHist sh, void *elem, U32 timestamp)
{
	F32 dt;
	if (sh->lastTimestamp == 0) {
		// Initial values, just zero out the structure/allocate it
		dt = 0.;
	} else {
		dt = timerSeconds(timestamp - sh->lastTimestamp);
	}
	shSampleInternal(&sh->vals, elem, dt, sh->tpi);
	sh->lastTimestamp = timestamp;
	sh->timeAccum += dt;
}

void shResetInternal(F32 **vals, ParseTable *fieldDefs)
{
	int i;
	if (!*vals) {
		*vals = calloc(ParserGetTableNumColumns(fieldDefs), sizeof(F32));
	}

	// Iterate through all fields defined in StructDef
	FORALL_PARSETABLE(fieldDefs, i)
	{
		F32* valptr;
		ParseTable* fd = &fieldDefs[i];
		int format = TOK_GET_FORMAT_OPTIONS(fd->format);
		int storage = TokenStoreGetStorageType(fd->type);

		if(fd->type & TOK_REDUNDANTNAME)
			continue;
		
		if (format == TOK_FORMAT_DATESS2000 ||
			format == TOK_FORMAT_FRIENDLYDATE ||
			format == TOK_FORMAT_FRIENDLYSS2000 ||
			format == TOK_FORMAT_IP)
			continue;

		if (storage != TOK_STORAGE_DIRECT_SINGLE && storage != TOK_STORAGE_INDIRECT_SINGLE)
		{
			Errorf("shSampleInternal is not compatible with array fields");
			continue;
		}

		// Pointer to where the running total is stored
		valptr = &(*vals)[i];

		switch (TOK_GET_TYPE(fd->type))
		{
		case TOK_INT_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_U8_X:
		case TOK_BOOL_X:
		case TOK_F32_X:
			*valptr = 0.0;
			break;

		case TOK_STRUCT_X:
			shResetInternal((F32**)valptr, (ParseTable*)fd->subtable);
			break;
		case TOK_STRING_X:
			// No point in resetting strings, we'll just re-alloc them!
			break;
		case TOK_IGNORE:
		case TOK_START:
		case TOK_END:
			// Ignored
			break;

		default:
			assert(0);
		}
	}
}

void shReset(StructHist sh)
{
	sh->lastTimestamp = 0;
	sh->timeAccum = 0;
	// Need to loop through the structure and clear the F32*s and **s.
	shResetInternal(&sh->vals, sh->tpi);
}

void shDumpInternal(FILE* fout, F32 **vals, F32 timeAccum, ParseTable *fieldDefs, int headerOnly)
{
	int i;
	assert(*vals);

	// Iterate through all fields defined in StructDef
	FORALL_PARSETABLE(fieldDefs, i)
	{
		F32* valptr;
		ParseTable* fd = &fieldDefs[i];
		int format = TOK_GET_FORMAT_OPTIONS(fd->format);
		int storage = TokenStoreGetStorageType(fd->type);

		if(fd->type & TOK_REDUNDANTNAME)
			continue;
		
		if (format == TOK_FORMAT_DATESS2000 ||
			format == TOK_FORMAT_FRIENDLYDATE ||
			format == TOK_FORMAT_FRIENDLYSS2000 ||
			format == TOK_FORMAT_IP)
			continue;

		if (storage != TOK_STORAGE_DIRECT_SINGLE && storage != TOK_STORAGE_INDIRECT_SINGLE)
		{
			Errorf("shSampleInternal is not compatible with array fields");
			continue;
		}

		// Pointer to where the running total is stored
		valptr = &(*vals)[i];

		switch(TOK_GET_TYPE(fd->type))
		{
		case TOK_INT_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_U8_X:
		case TOK_BOOL_X:
		case TOK_F32_X:
			if (headerOnly) {
				fprintf(fout, "\"%s\",", fd->name);
			} else {
				F32 val = *valptr / timeAccum;
				int ipart = (int)val;
				F32 fpart = val - ipart;
				if (ABS(fpart) < 0.001 || ABS(fpart)>0.999) {
					fprintf(fout, "%1.f,", val);
				} else {
					fprintf(fout, "%f,", val);
				}
			}
			break;

		case TOK_STRUCT_X:
			shDumpInternal(fout, (F32**)valptr, timeAccum, (ParseTable*)fd->subtable, headerOnly);
			break;
		case TOK_STRING_X:
			if (headerOnly) {
				fprintf(fout, "\"%s\",", fd->name);
			} else {
				char** s=(char**)valptr;
				fprintf(fout, "\"%s\",", *s?*s:"");
			}
			break;
		case TOK_IGNORE:
		case TOK_START:
		case TOK_END:
			// Ignored
			break;

		default:
			assert(0);
		}
	}
}

void shDump(StructHist sh, const char *logdir)
{
	char fname[CRYPTIC_MAX_PATH];
	FILE *fout;
	
	sprintf(fname, "./%s/%s.csv", logdir, sh->folder);

	if (!dateSetGlobally) {
		currentDate = timeGetLocalDateString();
	}

	mkdirtree(fname);
	if (!fileExists(fname)) {
		fout = fopen(fname, "at");
		assert(fout);
		fprintf(fout, "\"Date\",");
		shDumpInternal(fout, &sh->vals, sh->timeAccum, sh->tpi, 1);
		fprintf(fout, "\n");
	} else {
		fout = fopen(fname, "at");
		if (!fout) {
			printf("Not logging, failed to open %s\n", fname);
			return;
		}
		assert(fout);
	}

	fprintf(fout, "\"%s\",", currentDate);
	shDumpInternal(fout, &sh->vals, sh->timeAccum, sh->tpi, 0);
	fprintf(fout, "\n");
	fclose(fout);
}


StructHistCollection createStructHistCollection(const char *logDir)
{
	StructHistCollection shc = calloc(sizeof(StructHistCollectionImp),1);
	shc->ghtStructHists = stashTableCreateAddress(128);
	shc->ghtRemovedStructHists = stashTableCreateInt(16);
	Strncpyt(shc->logDir, logDir);
	return shc;
}

void destroyStructHistCollection(StructHistCollection shc)
{
	if (shc) {
		stashTableDestroyEx(shc->ghtStructHists, NULL, destroyStructHist);
		stashTableDestroyEx(shc->ghtRemovedStructHists, NULL, destroyStructHist);
		free(shc);
	}
}

void shcRemove(StructHistCollection shc, void *elem, U32 timestamp)
{
	StructHist sh=0;
	static int removed_count=0;

	if (!shc)
		return;

	if (stashAddressFindPointer(shc->ghtStructHists, elem, &sh))
	{
		shSample(sh, elem, timestamp);
		sh->remove = 1;
		// Move to removed hashTable (really just used as a list)
		removed_count++;
		stashIntAddPointer(shc->ghtRemovedStructHists, removed_count, sh, false);
		stashAddressRemovePointer(shc->ghtStructHists, elem, NULL);
	}
}

void shcUpdate(StructHistCollection *pshc, void *elem, ParseTable *tpi, U32 timestamp, char *folder)
{
	StructHistCollection shc;
	StructHist sh=0;
	if (*pshc == NULL)
		*pshc = createStructHistCollection("perflogs");

	shc = *pshc;

	if (!stashAddressFindPointer(shc->ghtStructHists, elem, &sh)) {
		sh = createStructHist(tpi, folder);
		stashAddressAddPointer(shc->ghtStructHists, elem, sh, false);
	}
	assert(!sh->remove);
	shSample(sh, elem, timestamp);
}


static int pruneProcessor(void *ht, StashElement element)
{
	StashTable ght = ht;
	StructHist sh = (StructHist)stashElementGetPointer(element);
	assert(sh->remove);
	if (sh->remove) {
		stashIntRemovePointer(ght, stashElementGetIntKey(element), NULL);
		destroyStructHist(sh);
	}
	return 1;
}

static void shcPrune(StructHistCollection shc)
{
	if (!shc)
		return;
	stashForEachElementEx(shc->ghtRemovedStructHists, pruneProcessor, shc->ghtRemovedStructHists);
}

static int dumpProcessor(void *vpshc, StashElement element)
{
	StructHistCollection shc = (StructHistCollection)vpshc;
	U32 timestamp = shc->timestamp;
	StructHist sh = (StructHist)stashElementGetPointer(element);
	if (sh->lastTimestamp) {
		if (!sh->remove) {
			shSample(sh, stashElementGetKey(element), timestamp);
		}
		shDump(sh, shc->logDir);
		shReset(sh);
		if (!sh->remove) {
			shSample(sh, stashElementGetKey(element), timestamp);
		}
	} else {
		// Hasn't ever been sampled (should only happen on a clear?)
		//assert(0);
	}
	return 1;
}

void shcDump(StructHistCollection shc, U32 timestamp)
{
	if (!shc)
		return;

	currentDate = timeGetLocalDateString();
	dateSetGlobally = true;
	shc->timestamp = timestamp;
	stashForEachElementEx(shc->ghtStructHists, dumpProcessor, (void*)shc);
	stashForEachElementEx(shc->ghtRemovedStructHists, dumpProcessor, (void*)shc);

	shcPrune(shc);
}


static int intvalue=0;
static F32 floatvalue=0;
static bool g_sh_ignore_earrays=false;
void shDoOperationSetInt(int i)
{
	intvalue = i;
}
void shDoOperationSetFloat(F32 f)
{
	floatvalue = f;
}
void shSetOperationSilentlyIgnoreEArrays(bool ignore)
{
	g_sh_ignore_earrays = true;
}

void shDoOperation(StructOp op, ParseTable *fieldDefs, void *dest, const void *operand)
{
	int i;
	int lhs_int, rhs_int = 0;
	S64 lhs_S64, rhs_S64 = 0;
	F32 lhs_F32 = 0.0f, rhs_F32 = 0.0f;
	bool src_constant = false;
	if (operand == OPERAND_INT)
	{
		rhs_int = intvalue;
		rhs_S64 = intvalue;
		rhs_F32 = (F32)intvalue;
		src_constant = true;
	}
	else
	if (operand == OPERAND_FLOAT)
	{
		rhs_int = (int)floatvalue;
		rhs_S64 = (S64)floatvalue;
		rhs_F32 = floatvalue;
		src_constant = true;
	}
	// Iterate through all fields defined in ParseTable, and only those fields
	FORALL_PARSETABLE(fieldDefs, i)
	{
		ParseTable* fd = &fieldDefs[i];
		int format = TOK_GET_FORMAT_OPTIONS(fd->format);
		int storage = TokenStoreGetStorageType(fd->type);
		int n, numelems = 1;
		void* src = (void*)operand;

		if(fd->type & TOK_REDUNDANTNAME)
			continue;
		
		if (format == TOK_FORMAT_DATESS2000 ||
			format == TOK_FORMAT_FRIENDLYDATE ||
			format == TOK_FORMAT_FRIENDLYSS2000 ||
			format == TOK_FORMAT_IP)
			continue;

		if (storage == TOK_STORAGE_INDIRECT_EARRAY)
		{
			if (!g_sh_ignore_earrays)
				Errorf("shDoOperation is not compatible with earray fields");
			continue;
		}
		if (storage == TOK_STORAGE_DIRECT_EARRAY) // F32 *, int *
		{
			if (src == OPERAND_INT || src == OPERAND_FLOAT)
			{
				// Doesn't matter what the size is, we just do all of them
			} else {
				// Two arrays, they must be the same size!
				int numsrcelems = TokenStoreGetNumElems(fieldDefs, i, src, NULL);
				numelems = TokenStoreGetNumElems(fieldDefs, i, dest, NULL);
				if (numelems != numsrcelems) {
					Errorf("shDoOperation called with earrays with mismatched sizes");
					MIN1(numelems, numsrcelems);
				}
			}
		}
		if (storage == TOK_STORAGE_DIRECT_FIXEDARRAY || storage == TOK_STORAGE_INDIRECT_FIXEDARRAY)
		{
			numelems = fd->param;
		}

#define VAL(get) (get(fieldDefs, i, src, n, NULL))
#define VALN(get, idx) (get(fieldDefs, i, src, idx, NULL))
#define DOIT(set, get, data_type)	\
		lhs_##data_type = get(fieldDefs, i, dest, n, NULL); \
		if (!src_constant)	\
			rhs_##data_type = VAL(get); \
		switch (op) {		\
		xcase STRUCTOP_ADD:	\
			lhs_##data_type = lhs_##data_type + rhs_##data_type; \
		xcase STRUCTOP_MIN:	\
			lhs_##data_type = MIN(lhs_##data_type, rhs_##data_type); \
		xcase STRUCTOP_MAX:	\
			lhs_##data_type = MAX(lhs_##data_type, rhs_##data_type); \
		xcase STRUCTOP_DIV:	\
			lhs_##data_type = lhs_##data_type / rhs_##data_type; \
		xcase STRUCTOP_MUL:	\
			lhs_##data_type = lhs_##data_type * rhs_##data_type; \
		xcase STRUCTOP_LERP:\
			lhs_##data_type = (data_type)( (1-floatvalue)*lhs_##data_type + (floatvalue)*rhs_##data_type ); \
		}	\
		set(fieldDefs, i, dest, n, lhs_##data_type, NULL, NULL);

		// iterate over fixed array
		for (n = 0; n < numelems; n++)
		{
			switch (TOK_GET_TYPE(fd->type))
			{
			xcase TOK_INT_X:
				DOIT(TokenStoreSetInt, TokenStoreGetInt, int);
			xcase TOK_INT64_X:
				DOIT(TokenStoreSetInt64, TokenStoreGetInt64, S64);
			xcase TOK_INT16_X:
				DOIT(TokenStoreSetInt16, TokenStoreGetInt16, int);
			xcase TOK_U8_X:
			case TOK_BOOL_X:
				DOIT(TokenStoreSetU8, TokenStoreGetU8, int);
			xcase TOK_F32_X:
				if (TOK_GET_FORMAT_OPTIONS(fd->format)==TOK_FORMAT_HSV) {
					assert(numelems >= 3);
					if (op == STRUCTOP_LERP) {
						Vec3 v0, v1, vout;
						numelems = 1;
						v0[0] = TokenStoreGetF32(fieldDefs, i, dest, 0, NULL);
						v0[1] = TokenStoreGetF32(fieldDefs, i, dest, 1, NULL);
						v0[2] = TokenStoreGetF32(fieldDefs, i, dest, 2, NULL);
						if (!src_constant)
						{
							v1[0] = VALN(TokenStoreGetF32, 0);
							v1[1] = VALN(TokenStoreGetF32, 1);
							v1[2] = VALN(TokenStoreGetF32, 2);
						}
						else
						{
							setVec3same(v1, rhs_F32);
						}
						hsvLerp(v0, v1, floatvalue, vout);
						TokenStoreSetF32(fieldDefs, i, dest, 0, vout[0], NULL, NULL);
						TokenStoreSetF32(fieldDefs, i, dest, 1, vout[1], NULL, NULL);
						TokenStoreSetF32(fieldDefs, i, dest, 2, vout[2], NULL, NULL);
					} else {
						assertmsg(0, "HSV value passed to operation other than LERP, don't know what to do!");
					}
				} else if (TOK_GET_FORMAT_OPTIONS(fd->format)==TOK_FORMAT_HSV_OFFSET) {
					assert(numelems >= 3);
					if (op == STRUCTOP_LERP) {
						Vec3 v0, v1, vout;
						numelems = 1;
						v0[0] = TokenStoreGetF32(fieldDefs, i, dest, 0, NULL);
						v0[1] = 1;
						v0[2] = 1;
						if (!src_constant)
							v1[0] = VALN(TokenStoreGetF32, 0);
						else
							v1[0] = rhs_F32;
						v1[1] = 1;
						v1[2] = 1;
						hsvLerp(v0, v1, floatvalue, vout); // interpolate hue
						v0[1] = TokenStoreGetF32(fieldDefs, i, dest, 1, NULL);
						v0[2] = TokenStoreGetF32(fieldDefs, i, dest, 2, NULL);
						v1[1] = VALN(TokenStoreGetF32, 1);
						v1[2] = VALN(TokenStoreGetF32, 2);
						vout[1] = (1-floatvalue)*v0[1] + floatvalue*v1[1];
						vout[2] = (1-floatvalue)*v0[2] + floatvalue*v1[2];
						TokenStoreSetF32(fieldDefs, i, dest, 0, vout[0], NULL, NULL);
						TokenStoreSetF32(fieldDefs, i, dest, 1, vout[1], NULL, NULL);
						TokenStoreSetF32(fieldDefs, i, dest, 2, vout[2], NULL, NULL);
					} else {
						assertmsg(0, "HSV value passed to operation other than LERP, don't know what to do!");
					}
				} else {
					DOIT(TokenStoreSetF32, TokenStoreGetF32, F32);
				}
			xcase TOK_STRUCT_X:
				shDoOperation(op, fd->subtable, TokenStoreGetPointer(fieldDefs, i, dest, n, NULL), 
					(src == OPERAND_INT || src == OPERAND_FLOAT)? src:
					TokenStoreGetPointer(fieldDefs, i, src, n, NULL));
			case TOK_STRING_X:
			case TOK_IGNORE:
			case TOK_START:
			case TOK_END:
				// Ignored
				break;
			default:
				break;
			}
		}
	}
}
