#include <file.h>
#include <tiff.h>
#include "error.h"
#include "StashTable.h"
#include "ScratchStack.h"
#include "endian.h"
#include "mathutil.h"
#include "HashFunctions.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define SIZE_OF_TIFF_HEADER 8
#define SIZE_OF_TIFF_IDF_HEADER 2
#define SIZE_OF_TIFF_IDF_ENTRY 12
#define SIZE_OF_TIFF_IDF_FOOTER 4

#define NUM_OF_TIFF_IFD_ENTRIES 15

#define TIFF_BYTE		1
#define TIFF_ASCII		2
#define TIFF_SHORT		3
#define TIFF_LONG		4
#define TIFF_RATIONAL	5

#define TIFF_BYTE_SIZE		1
#define TIFF_ASCII_SIZE		1
#define TIFF_SHORT_SIZE		2
#define TIFF_LONG_SIZE		4
#define TIFF_RATIONAL_SIZE	8

#define TIFF_SAVED_BY_NAME "CrypticStudios"
#define TIFF_SAVED_BY_NAME_SIZE 14

//12bit codes are the max for Tiff files
//12bits can save up to 4096 entries
//The first 258 are reserved so 4096 - 258 = 
#define LZW_MAX_TABLE_ENTRIES 3838 
#define LZW_CLEAR_CODE 256
#define LZW_EOI_CODE 257 
#define LZW_CODES_START 258 
#define LZW_MIN_BIT_WRITE_SIZE 9
#define LZW_MAX_BIT_WRITE_SIZE 12

#define TIFF_IMAGE_WIDTH				0x0100
#define TIFF_IMAGE_HEIGHT				0x0101
#define TIFF_IMAGE_BITS_PER_SAMPLE		0x0102
#define TIFF_IMAGE_COMPRESSION			0x0103
#define TIFF_IMAGE_PHOTOMETRICS			0x0106
#define TIFF_IMAGE_STRIP_OFFSETS		0x0111
#define TIFF_IMAGE_SAMPLES_PER_PIXEL	0x0115
#define TIFF_IMAGE_ROWS_PER_STRIP		0x0116
#define TIFF_IMAGE_STRIP_BYTE_COUNTS	0x0117
#define TIFF_IMAGE_X_RESOLUTION			0x011A
#define TIFF_IMAGE_Y_RESOLUTION			0x011B
#define TIFF_IMAGE_RESOLUTION_UNIT		0x0128
#define TIFF_IMAGE_SOFTWARE				0x0131
#define TIFF_IMAGE_PREDICTOR			0x013D
#define TIFF_IMAGE_SAMPLE_FORMAT		0x0153

#define TIFF_COMPRESSION_UNCOMPRESSED	1
#define TIFF_COMPRESSION_LZW			5
#define TIFF_COMPRESSION_JPEG			6

#define TIFF_PREDICTOR_NONE				1
#define TIFF_PREDICTOR_HORIZ_DIFF		2
#define TIFF_PREDICTOR_FLOAT_DIFF		3

// the spec says 3 is float, but Photoshop writes out 0 for float and no IFD at all for uint
#define TIFF_FORMAT_UINT				1
#define TIFF_FORMAT_FLOAT				0
#define TIFF_FORMAT_FLOAT2				3

#define MAX_ROWS_PER_STRIP				32 // for saving only

typedef struct
{
	U32 byte_pos;
	U32 bit_pos;
	U32 max_size;
	U8 *data;
} lzwBitStream;

typedef struct
{
	U8 *string;
	U32 string_size;
} lzwTableEntry;

void tiffWriteHeader(FILE *file, U32 first_ifd_offset, U32 header_location)
{
	char *byte_order = "II";
	U16 tiff_version = 42;

	fseek(file, header_location, SEEK_SET);
	fwrite(byte_order, 2, 1, file);
	fwrite(&tiff_version, 2, 1, file);
	fwrite(&first_ifd_offset, 4, 1, file);	
}

void tiffWriteIFD(FILE *file, const TiffIFD *imageFileDir, U32 imageFileDirStartPos)
{
	U32 i, idf_count = ((imageFileDir->isFloat ? 0 : -1) + NUM_OF_TIFF_IFD_ENTRIES);
	char *saved_by_name = TIFF_SAVED_BY_NAME;
	U16 code;
	U16 type;
	U32 count;
	U16 data_short;
	U32 data_long;
	U16 fill_data = 0;
	U32 tiff_idf_end =	imageFileDirStartPos +
						SIZE_OF_TIFF_IDF_HEADER + 
						idf_count * SIZE_OF_TIFF_IDF_ENTRY + 
						SIZE_OF_TIFF_IDF_FOOTER;

	assert(imageFileDir->bytesPerChannel);
	assert(imageFileDir->numChannels);

	if (imageFileDir->bytesPerChannel < 2)
		assert(!imageFileDir->isFloat);


	/////////////////////////////
	//Fixed length Data
	/////////////////////////////

	//Number of Entries
	code = idf_count;
	fwrite(&code, 2, 1, file);	
	
	//Image Width
	code = TIFF_IMAGE_WIDTH;
	type = TIFF_LONG;
	count = 1;
	data_long = imageFileDir->width;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);	

	//Image Height
	code = TIFF_IMAGE_HEIGHT;
	type = TIFF_LONG;
	count = 1;
	data_long = imageFileDir->height;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);	

	//Bits Per Sample
	code = TIFF_IMAGE_BITS_PER_SAMPLE;
	type = TIFF_SHORT;
	count = imageFileDir->numChannels;
	data_long = tiff_idf_end; // data is at end of IDF block
	tiff_idf_end += TIFF_SHORT_SIZE*imageFileDir->numChannels;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);	

	//Compression
	code = TIFF_IMAGE_COMPRESSION;
	type = TIFF_SHORT;
	count = 1;
	data_short = imageFileDir->lzwCompressed ? TIFF_COMPRESSION_LZW : TIFF_COMPRESSION_UNCOMPRESSED;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_short,	2, 1, file);	
	fwrite(&fill_data,	2, 1, file);		

	//Photometric Interpretation
	code = TIFF_IMAGE_PHOTOMETRICS;
	type = TIFF_SHORT;
	count = 1;
	data_short = 2;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_short,	2, 1, file);	
	fwrite(&fill_data,	2, 1, file);		

	//Strip Offsets
	code = TIFF_IMAGE_STRIP_OFFSETS;
	type = TIFF_LONG;
	count = imageFileDir->strip_count;
	data_long = tiff_idf_end;
	tiff_idf_end += (TIFF_LONG_SIZE * imageFileDir->strip_count);
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);

	//Samples Per Pixel
	code = TIFF_IMAGE_SAMPLES_PER_PIXEL;
	type = TIFF_SHORT;
	count = 1;
	data_short = imageFileDir->numChannels;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_short,	2, 1, file);
	fwrite(&fill_data,	2, 1, file);

	//Rows Per Strip
	code = TIFF_IMAGE_ROWS_PER_STRIP;
	type = TIFF_LONG;
	count = 1;
	data_long = imageFileDir->rows_per_strip;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);

	//Strip Byte Counts
	code = TIFF_IMAGE_STRIP_BYTE_COUNTS;
	type = TIFF_LONG;
	count = imageFileDir->strip_count;
	data_long = tiff_idf_end;
	tiff_idf_end += (TIFF_LONG_SIZE * imageFileDir->strip_count);
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);

	//XResolution
	code = TIFF_IMAGE_X_RESOLUTION;
	type = TIFF_RATIONAL;
	count = 1;
	data_long = tiff_idf_end;
	tiff_idf_end += TIFF_RATIONAL_SIZE;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);
	
	//YResolution
	code = TIFF_IMAGE_Y_RESOLUTION;
	type = TIFF_RATIONAL;
	count = 1;
	data_long = tiff_idf_end;
	tiff_idf_end += TIFF_RATIONAL_SIZE;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);
	
	//Resolution Unit
	code = TIFF_IMAGE_RESOLUTION_UNIT;
	type = TIFF_SHORT;
	count = 1;
	data_short = 2;		//2 = Inches
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_short,	2, 1, file);
	fwrite(&fill_data,	2, 1, file);

	//Software
	code = TIFF_IMAGE_SOFTWARE;
	type = TIFF_ASCII;
	count = TIFF_SAVED_BY_NAME_SIZE;
	data_long = tiff_idf_end;
	tiff_idf_end += (TIFF_ASCII_SIZE * TIFF_SAVED_BY_NAME_SIZE);
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_long,	4, 1, file);	

	//Predictor
	code = TIFF_IMAGE_PREDICTOR;
	type = TIFF_SHORT;
	count = 1;
	if (imageFileDir->horizontalDifferencingType == TIFF_DIFF_FLOAT)
		data_short = TIFF_PREDICTOR_FLOAT_DIFF;
	else if (imageFileDir->horizontalDifferencingType == TIFF_DIFF_HORIZONTAL)
		data_short = TIFF_PREDICTOR_HORIZ_DIFF;
	else
		data_short = TIFF_PREDICTOR_NONE;
	fwrite(&code,		2, 1, file);	
	fwrite(&type,		2, 1, file);	
	fwrite(&count,		4, 1, file);	
	fwrite(&data_short,	2, 1, file);
	fwrite(&fill_data,	2, 1, file);

	if (imageFileDir->isFloat)
	{
		//SampleFormat
		code = TIFF_IMAGE_SAMPLE_FORMAT;
		type = TIFF_SHORT;
		count = 1;
		data_short = TIFF_FORMAT_FLOAT;
		fwrite(&code,		2, 1, file);	
		fwrite(&type,		2, 1, file);	
		fwrite(&count,		4, 1, file);	
		fwrite(&data_short,	2, 1, file);
		fwrite(&fill_data,	2, 1, file);
	}

	//Footer
	data_long = 0;
	fwrite(&data_long,	4, 1, file);	

	/////////////////////////////
	//Values larger than 4 bytes
	/////////////////////////////

	//Bits Per Sample (TIFF_IMAGE_BITS_PER_SAMPLE)
	data_short = imageFileDir->bytesPerChannel << 3;
	for (i = 0; i < imageFileDir->numChannels; ++i)
		fwrite(&data_short,	2, 1, file);

	//Strip Offsets
	for(i=0; i < imageFileDir->strip_count; i++)
		fwrite(&imageFileDir->strip_offsets[i], 4, 1, file);

	//Strip Byte Counts
	for(i=0; i < imageFileDir->strip_count; i++)
		fwrite(&imageFileDir->strip_byte_counts[i], 4, 1, file);

	//XResolution
	data_long = 72;
	fwrite(&data_long,	4, 1, file);	
	data_long = 1;
	fwrite(&data_long,	4, 1, file);	
	
	//YResolution
	data_long = 72;
	fwrite(&data_long,	4, 1, file);	
	data_long = 1;
	fwrite(&data_long,	4, 1, file);	

	//Software
	fwrite(saved_by_name, TIFF_SAVED_BY_NAME_SIZE, 1, file);	
}

U32 tiffWriteStrip(FILE *file, const TiffIFD *imageFileDir, const void *data, U32 num_rows)
{
	U32 data_size = num_rows * imageFileDir->width * imageFileDir->bytesPerChannel * imageFileDir->numChannels;

	assert(imageFileDir->bytesPerChannel && imageFileDir->numChannels);

	if(imageFileDir->lzwCompressed)
	{
		U32 compressed_size;
		U8 *compressed_data = lzwCompress(data, data_size, &compressed_size);
		
		fwrite(compressed_data, 1, compressed_size, file);	

		free(compressed_data);
		return compressed_size;
	}

	fwrite(data, 1, data_size, file);
	return data_size;
}

bool tiffReadHeader(FILE *file, U32 *first_ifd_offset)
{
	char byte_order[2];
	U16 tiff_version;

	fseek(file, 0, SEEK_SET);

	fread(byte_order, 2, 1, file);
	if(byte_order[0] != 'I' || byte_order[1] != 'I')
	{
		ErrorFilenamef(file->nameptr, "ERROR - Tiff Read Header: Invalid byte order");
		return false;
	}
	fread(&tiff_version, 2, 1, file);
	if(tiff_version != 42)
	{
		ErrorFilenamef(file->nameptr, "ERROR - Tiff Read Header: Invalid version number");
		return false;
	}

	fread(first_ifd_offset, 4, 1, file);	
	return true;
}

U32 tiffTypeSize(U16 type)
{
	switch(type)
	{
	xcase TIFF_BYTE:
		return TIFF_BYTE_SIZE;
	xcase TIFF_ASCII:
		return TIFF_ASCII_SIZE;
	xcase TIFF_SHORT:
		return TIFF_SHORT_SIZE;
	xcase TIFF_LONG:
		return TIFF_LONG_SIZE;
	xcase TIFF_RATIONAL:
		return TIFF_RATIONAL_SIZE;
	}
	return TIFF_SHORT_SIZE;
}

void tiffReadIFDEntry(FILE *file, U16 *code, U16 *type, U32 *count, U16 *short_data, U32 *long_data)
{
	U16 garb;
	U32 data_size;
	fread(code, 2, 1, file);	
	fread(type, 2, 1, file);
	fread(count, 4, 1, file);
	data_size = tiffTypeSize(*type);
	if(data_size*(*count) <= 2)
	{
		fread(short_data, 2, 1, file);	
		fread(&garb, 2, 1, file);	
	}
	else
	{
		fread(long_data, 4, 1, file);	
	}
}

bool tiffReadIFD_dbg(FILE *file, TiffIFD *imageFileDir, U32 imageFileDirStartPos MEM_DBG_PARMS)
{
	U32 i, j;
	U16 num_entries;
	U32 cur_pos;

	imageFileDir->height = 0;
	imageFileDir->width = 0;
	imageFileDir->bytesPerChannel = 0;
	imageFileDir->numChannels = 0;
	imageFileDir->isFloat = false;
	imageFileDir->lzwCompressed = false;
	imageFileDir->horizontalDifferencingType = TIFF_DIFF_NONE;
	imageFileDir->strip_count = 0;
	imageFileDir->rows_per_strip = 0;
	imageFileDir->strip_offsets = NULL;
	imageFileDir->strip_byte_counts = NULL;

	fseek(file, imageFileDirStartPos, SEEK_SET);
	fread(&num_entries, 2, 1, file);	

	for(i=0; i < num_entries; i++)
	{
		U16 code=0;
		U16 type=0;
		U32 count=0;
		U16 short_data=0;
		U32 long_data=0;

		tiffReadIFDEntry(file, &code, &type, &count, &short_data, &long_data);
		
		switch(code)
		{
		xcase TIFF_IMAGE_WIDTH:
			imageFileDir->width = (type == TIFF_SHORT) ? short_data : long_data;

		xcase TIFF_IMAGE_HEIGHT:
			imageFileDir->height = (type == TIFF_SHORT) ? short_data : long_data;

		xcase TIFF_IMAGE_BITS_PER_SAMPLE:
			cur_pos = ftell(file);
			fseek(file, long_data, SEEK_SET);
			for(j=0; j < 3; j++)
			{
				fread(&short_data, 2, 1, file);
				imageFileDir->bytesPerChannel = short_data >> 3;
			}
			fseek(file, cur_pos, SEEK_SET);

		xcase TIFF_IMAGE_COMPRESSION:
			if(short_data == TIFF_COMPRESSION_UNCOMPRESSED)
			{
				imageFileDir->lzwCompressed = false;
			}
			else if(short_data == TIFF_COMPRESSION_LZW)
			{
				imageFileDir->lzwCompressed = true;
			}
			else
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Bad compression format, only uncompressed and LZW compression are supported");
				return false;
			}

		xcase TIFF_IMAGE_PHOTOMETRICS:
			if(short_data != 2)	
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Invalid Photometric Interpretation");
				return false;
			}

		xcase TIFF_IMAGE_STRIP_OFFSETS:
			if(imageFileDir->strip_offsets != NULL)
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Multiple offset entries");
				return false;
			}
			if(type != TIFF_LONG)
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Invalid strip offsets type");
				return false;
			}
			imageFileDir->strip_count = count;
			imageFileDir->strip_offsets = scalloc(count, sizeof(U32));

			if(count == 1)
			{
				imageFileDir->strip_offsets[0] = long_data;
				break;
			}

			cur_pos = ftell(file);
			fseek(file, long_data, SEEK_SET);
			for(j=0; j < count; j++)
			{
				fread(&long_data, 4, 1, file);			
				imageFileDir->strip_offsets[j] = long_data;
			}
			fseek(file, cur_pos, SEEK_SET);

		xcase TIFF_IMAGE_SAMPLES_PER_PIXEL:
			imageFileDir->numChannels = short_data;

		xcase TIFF_IMAGE_ROWS_PER_STRIP:
			imageFileDir->rows_per_strip = (type == TIFF_SHORT) ? short_data : long_data;

		xcase TIFF_IMAGE_STRIP_BYTE_COUNTS:
			if(imageFileDir->strip_byte_counts != NULL)
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Multiple byte count entries");
				return false;
			}
			if(type != TIFF_LONG)
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Invalid strip byte counts type");
				return false;
			}
			imageFileDir->strip_count = count;
			imageFileDir->strip_byte_counts = scalloc(count, sizeof(U32));

			if(count == 1)
			{
				imageFileDir->strip_byte_counts[0] = long_data;
				break;
			}

			cur_pos = ftell(file);
			fseek(file, long_data, SEEK_SET);
			for(j=0; j < count; j++)
			{
				fread(&long_data, 4, 1, file);			
				imageFileDir->strip_byte_counts[j] = long_data;
			}
			fseek(file, cur_pos, SEEK_SET);

		xcase TIFF_IMAGE_PREDICTOR:
			if (short_data == TIFF_PREDICTOR_NONE)
			{
				imageFileDir->horizontalDifferencingType = TIFF_DIFF_NONE;
			}
			else if (short_data == TIFF_PREDICTOR_HORIZ_DIFF)
			{
				imageFileDir->horizontalDifferencingType = TIFF_DIFF_HORIZONTAL;
			}
			else if (short_data == TIFF_PREDICTOR_FLOAT_DIFF)
			{
				imageFileDir->horizontalDifferencingType = TIFF_DIFF_FLOAT;
			}
			else
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Bad predictor type, horizontal differencing is the only supported type");
				return false;
			}

		xcase TIFF_IMAGE_SAMPLE_FORMAT:
			if (short_data == TIFF_FORMAT_UINT)
			{
				imageFileDir->isFloat = false;
			}
			else if (short_data == TIFF_FORMAT_FLOAT || short_data == TIFF_FORMAT_FLOAT2)
			{
				imageFileDir->isFloat = true;
			}
			else
			{
				ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Bad sample format, float and uint are the only supported formats");
				return false;
			}
		}
	}

	if (!imageFileDir->height || !imageFileDir->width || !imageFileDir->numChannels || !imageFileDir->bytesPerChannel || 
		!imageFileDir->strip_count || !imageFileDir->rows_per_strip || !imageFileDir->strip_byte_counts || !imageFileDir->strip_offsets)
	{
		ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Missing required IFD Data");
		return false;
	}

	if (imageFileDir->isFloat && imageFileDir->bytesPerChannel < 2)
	{
		ErrorFilenamef(file->nameptr, "ERROR - Tiff IFD: Incompatable sample format and bit depth");
		return false;
	}

	return true;
}

bool tiffReadStrip_dbg(FILE *file, const TiffIFD *imageFileDir, U8 *strip, U32 num_lines, U32 strip_idx MEM_DBG_PARMS)
{
	U32 byte_count = imageFileDir->strip_byte_counts[strip_idx];
	U32 offset = imageFileDir->strip_offsets[strip_idx];
	U32 out_size = num_lines * imageFileDir->width * imageFileDir->bytesPerChannel * imageFileDir->numChannels;
	U8 *compressed_data;

	fseek(file, offset, SEEK_SET);

	if(!imageFileDir->lzwCompressed)
	{
		if(byte_count != out_size)
			return false;
		fread(strip, 1, out_size, file);
		return true;
	}

	compressed_data = scalloc(byte_count, sizeof(U8));
	fread(compressed_data, 1, byte_count, file);

	if(!lzwUncompress(compressed_data, strip, byte_count, out_size))
	{
		free(compressed_data);
		return false;
	}
	free(compressed_data);
	return true;
}

static U32 lzwHashFunc(const lzwTableEntry *key, int hashSeed)
{
	return MurmurHash2(key->string, key->string_size, 0x8769876);
}

static int lzwCompFunc(const lzwTableEntry *key1, const lzwTableEntry *key2)
{
	if(key1->string_size != key2->string_size)
		return 1;//not equal
	return memcmp(key1->string, key2->string, key1->string_size)!=0;
}

//Do not want Hash to deallocate for me
void lzwHashKeyDstr(lzwTableEntry *key)
{
	key=NULL;
}
void lzwHashValDstr(void *val)
{
	val=NULL;
}

static void lzwClearTable(lzwTableEntry *table, U16 *size)
{
	int i;
	for(i=0; i < (*size); i++)
	{
		free(table[i].string);
	}
	*size = 0;
}

static bool lzwIsInTable(lzwTableEntry *table, U16 size, U8 *string, U16 string_size, U16 *string_code)
{
/*	int i, j;
	if(string_size == 1)
	{
		(*string_code) = string[0];
		return true;
	}
	for(i=size-1; i >= 0; i--)
	{
		if(table[i].string_size == string_size)
		{
			bool found_match = true;
			for(j=0; j < string_size; j++)
			{
				if(table[i].string[j] != string[j])
				{
					found_match = false;
					break;
				}
			}
			if(found_match)	
			{
				(*string_code) = i + LZW_CODES_START;
				return true;
			}
		}
	}*/
	return false;
}

static void lzwAddEntry(lzwTableEntry *table, U16 *size, U8 *string, U16 string_size)
{
	int i;
	assert((*size) < LZW_MAX_TABLE_ENTRIES);
	table[(*size)].string = malloc(string_size);
	for(i=0; i < string_size; i++)
	{
		table[(*size)].string[i] = string[i];
	}
	table[(*size)].string_size = string_size;
	(*size)++;
}


static void lzwAddTableEntry(lzwTableEntry *table, U16 *size, U8 *string, U16 string_size)
{
	int i;
	assert((*size) < LZW_MAX_TABLE_ENTRIES);
	table[(*size)].string = malloc(string_size);
	for(i=0; i < string_size; i++)
	{
		table[(*size)].string[i] = string[i];
	}
	table[(*size)].string_size = string_size;
	(*size)++;
}

static void lzwBitStreamWriteInit(lzwBitStream *bs, U32 size)
{
	bs->data = calloc(1, size);
	bs->byte_pos = 0;
	bs->bit_pos = 0;
	bs->max_size = size;
}

static void lzwBitStreamReadInit(lzwBitStream *bs, U8 *data, U32 size)
{
	bs->data = data;
	bs->byte_pos = 0;
	bs->bit_pos = 0;
	bs->max_size = size;
}

static void lzwWriteBits(lzwBitStream *bs, U16 in_val, U16 bit_write_size /*must be larger than a byte*/)
{	
	int out_bits;
	int bits_left_to_write = bit_write_size;
	int val = in_val;
	int write_val;
	//If we are close to running out of space
	if(bs->byte_pos + 4 >= bs->max_size)
	{
		//Allocate and Clear Data
		bs->data = realloc(bs->data, bs->max_size + 1024);
		memset(bs->data + bs->max_size, 0, 1024);
		bs->max_size += 1024;
	}

	//Write Bits
	out_bits = (8-bs->bit_pos);
	while(bits_left_to_write >= out_bits)
	{
		bits_left_to_write -= out_bits;
		write_val = val >> bits_left_to_write;
		val ^= (write_val << bits_left_to_write);
		
		bs->data[bs->byte_pos] |= write_val;
		
		bs->byte_pos++;
		bs->bit_pos = 0;
		out_bits = 8;
	}
	assert(bs->bit_pos == 0);
	if(bits_left_to_write)
	{
		bs->bit_pos = bits_left_to_write;
		bs->data[bs->byte_pos] = (val << (8-bits_left_to_write));
	}
}

U16 lzwReadBits(lzwBitStream *bs, U16 bit_read_size /*must be larger than a byte*/)
{
	U16 out_data=0;
	int in_bits;
	int bits_left_to_read = bit_read_size;

	in_bits = (8-bs->bit_pos);
	while(bits_left_to_read >= in_bits)
	{
		bits_left_to_read -= in_bits;
		assert(bs->byte_pos < bs->max_size);
		out_data = (out_data << in_bits) | bs->data[bs->byte_pos];
		
		bs->byte_pos++;
		bs->bit_pos = 0;
		in_bits = 8;	
	}
	assert(bs->bit_pos == 0);
	if(bits_left_to_read)
	{
		bs->bit_pos = bits_left_to_read;
		assert(bs->byte_pos < bs->max_size);
		out_data = (out_data << bits_left_to_read) | (bs->data[bs->byte_pos] >> (8-bits_left_to_read));
		bs->data[bs->byte_pos] <<= bits_left_to_read;
		bs->data[bs->byte_pos] >>= bits_left_to_read;
	}
	return out_data;
}

static U32 lzwBitStreamFlush(lzwBitStream *bs)
{
	//If no bits have been written then nothing to do
	if(bs->bit_pos == 0)
		return bs->byte_pos;

	//Shift so that data is correctly aligned and exit
	bs->data[bs->byte_pos] <<= (8-bs->bit_pos);
	bs->byte_pos++;
	bs->bit_pos=0;
	return bs->byte_pos;
}

U8 *lzwCompress(const U8 *data, U32 size, U32 *size_out)
{
	U32 i;
	lzwBitStream bs;
	lzwTableEntry values[LZW_MAX_TABLE_ENTRIES];
	U16 bit_write_size = LZW_MIN_BIT_WRITE_SIZE;
	U16 table_index = LZW_CODES_START;
	U16 string_code=0;
	void *hashValue=NULL;
	StashTable string_table;
	lzwTableEntry cur_val;
	U8 *data_copy = malloc(size);

	memcpy(data_copy, data, size);

	cur_val.string = data_copy;
	cur_val.string_size = 1;
	string_code = data_copy[0];

	lzwBitStreamWriteInit(&bs, size);
	lzwWriteBits(&bs, LZW_CLEAR_CODE, bit_write_size);
	string_table = stashTableCreateExternalFunctions(4096, StashDefault, lzwHashFunc, lzwCompFunc);

	for(i=0; i < size; i++)
	{
		cur_val.string_size++;
		assert(cur_val.string_size-1 < size);
		if(stashFindPointer(string_table, &cur_val, &hashValue))
		{
			string_code = (U16)hashValue;
		}
		else
		{
			lzwWriteBits(&bs, string_code, bit_write_size);

			values[table_index-LZW_CODES_START].string = cur_val.string;
			values[table_index-LZW_CODES_START].string_size = cur_val.string_size;
			hashValue = (void*)(intptr_t)table_index;
			stashAddPointer(string_table, values + (table_index-LZW_CODES_START), hashValue, false);
			table_index++;

			//If table is full
			if(table_index-LZW_CODES_START >= LZW_MAX_TABLE_ENTRIES-2)
			{
				lzwWriteBits(&bs, LZW_CLEAR_CODE, bit_write_size);
				bit_write_size = LZW_MIN_BIT_WRITE_SIZE;
				stashTableClearEx(string_table, lzwHashKeyDstr, lzwHashValDstr);
				table_index = LZW_CODES_START;
			}
			//If we need to start writing codes larger
			else if(table_index >= (1<<bit_write_size))
			{
				bit_write_size++;
			}

			cur_val.string += (cur_val.string_size-1);
			cur_val.string_size = 1;
			string_code = cur_val.string[0];
		}
	}
	lzwWriteBits(&bs, string_code, bit_write_size);
	lzwWriteBits(&bs, LZW_EOI_CODE, bit_write_size);
	stashTableDestroyEx(string_table, lzwHashKeyDstr, lzwHashValDstr);

	*size_out = lzwBitStreamFlush(&bs);

	free(data_copy);

	return bs.data;
}

void lzwWriteStringFromCode(U8 *ptr, U32 *offset, U32 cap, lzwTableEntry *table, U16 table_size, U32 code, bool first_char_only)
{
	U32 i;
	if(code < LZW_CODES_START)
	{
		if((*offset) >= cap)
			return;
		ptr[(*offset)] = code;
		(*offset)++;
	}
	else
	{
		code -= LZW_CODES_START;
		assert(code < table_size);
		for(i=0; i < table[code].string_size; i++)
		{
			if((*offset) >= cap)
				return;
			ptr[(*offset)] = table[code].string[i];
			(*offset)++;
			if(first_char_only)
				return;
		}
	}			
}

bool lzwUncompress(const U8 *data_in, U8 *data_out, U32 size_in, U32 size_out)
{
	lzwBitStream bs;
	lzwTableEntry table[LZW_MAX_TABLE_ENTRIES];
	U16 table_size=0;
	U16 bit_read_size = LZW_MIN_BIT_WRITE_SIZE;
	U16 string_code=0;
	U16 last_string_code=0;
	U32 data_pos=0;
	U8 *last_data_ptr=data_out;
	U8 *data_in_copy = malloc(size_in);

	memcpy(data_in_copy, data_in, size_in);

	lzwBitStreamReadInit(&bs, data_in_copy, size_in);

	//TODO: This fails to read EOI codes and needs to be fixed at some time.  
	//Because we have the size it still reads correctly however.
	while((string_code = lzwReadBits(&bs, bit_read_size)) != LZW_EOI_CODE)
	{
		if(data_pos >= size_out)
			break;

		if(string_code == LZW_CLEAR_CODE)
		{
			table_size = 0;
			bit_read_size = LZW_MIN_BIT_WRITE_SIZE;
			
			string_code = lzwReadBits(&bs, bit_read_size);
			if(string_code == LZW_EOI_CODE)
				break;

			last_data_ptr = data_out + data_pos;
			lzwWriteStringFromCode(data_out, &data_pos, size_out, table, table_size, string_code, false);
			last_string_code = string_code;
		}
		else
		{
			if(	string_code < LZW_CODES_START ||
				string_code - LZW_CODES_START < table_size )
			{
				table[table_size].string = last_data_ptr;
				table[table_size].string_size = (last_string_code < LZW_CODES_START) ? 2 : (table[last_string_code - LZW_CODES_START].string_size + 1);
				table_size++;
				assert(table_size <= LZW_MAX_TABLE_ENTRIES);

				if(table_size+LZW_CODES_START >= ((1<<bit_read_size)-1))
					bit_read_size++;
				assert(bit_read_size <= LZW_MAX_BIT_WRITE_SIZE);

				last_data_ptr = data_out + data_pos;				
				lzwWriteStringFromCode(data_out, &data_pos, size_out, table, table_size, string_code, false);
				last_string_code = string_code;
			}
			else
			{
				assert((string_code - LZW_CODES_START) == table_size);

				table[table_size].string = last_data_ptr;
				table[table_size].string_size = (last_string_code < LZW_CODES_START) ? 2 : (table[last_string_code - LZW_CODES_START].string_size + 1);
				table_size++;
				assert(table_size <= LZW_MAX_TABLE_ENTRIES);

				if(table_size+LZW_CODES_START >= ((1<<bit_read_size)-1))
					bit_read_size++;
				assert(bit_read_size <= LZW_MAX_BIT_WRITE_SIZE);

				last_data_ptr = data_out + data_pos;				
				lzwWriteStringFromCode(data_out, &data_pos, size_out, table, table_size, last_string_code, false);
				lzwWriteStringFromCode(data_out, &data_pos, size_out, table, table_size, last_string_code, true);			
				last_string_code = string_code;
			}
		}
	}

	free(data_in_copy);

	return true;
}

//////////////////////////////////////////////////////////////////////////

static void encodeDeltaBytes(U8 *bytes, int sample_count, int channel_count)
{
	int s, c;
	for (s = sample_count - 1; s > 0; --s)
	{
		for (c = 0; c < channel_count; ++c)
			bytes[s * channel_count + c] -= bytes[(s-1) * channel_count + c];
	}
}

static void decodeDeltaBytes(U8 *bytes, int sample_count, int channel_count)
{
	int s, c;
	for (s = 1; s < sample_count; ++s)
	{
		for (c = 0; c < channel_count; ++c)
			bytes[s * channel_count + c] += bytes[(s-1) * channel_count + c];
	}
}

static void encodeDeltaFloats(U8 *output, const U8 *input, int sample_count, int channel_count, int bytesPerChannel)
{
	int s, b, inc = sample_count * channel_count;

	assert(input != output);

	for (s = 0; s < inc; ++s)
	{
		for (b = 0; b < bytesPerChannel; ++b)
		{
			if (isBigEndian())
				output[b * inc + s] = input[bytesPerChannel * s + b];
			else
				output[(bytesPerChannel - b - 1) * inc + s] = input[bytesPerChannel * s + b];
		}
	}

	encodeDeltaBytes(output, sample_count * bytesPerChannel, channel_count);
}

static void decodeDeltaFloats(U8 *output, U8 *input, int sample_count, int channel_count, int bytesPerChannel)
{
	int s, b, inc = sample_count * channel_count;

	assert(input != output);

	decodeDeltaBytes(input, sample_count * bytesPerChannel, channel_count);

	for (s = 0; s < inc; ++s)
	{
		for (b = 0; b < bytesPerChannel; ++b)
		{
			if (isBigEndian())
				output[bytesPerChannel * s + b] = input[b * inc + s];
			else
				output[bytesPerChannel * s + b] = input[(bytesPerChannel - b - 1) * inc + s];
		}
	}
}

void *tiffLoad_dbg(FILE *file, int *width, int *height, int *bytesPerChannel, int *numChannels, bool *isFloat MEM_DBG_PARMS)
{
	U32 idf_offset=0;
	TiffIFD imageFileDir = {0};
	U8 *image_data, *image_ptr, *strip, *strip_ptr;
	U32 i, y, remaining_rows, pixel_size, row_size;

	if (!tiffReadHeader(file, &idf_offset))
		return NULL;

	if (!tiffReadIFD_dbg(file, &imageFileDir, idf_offset MEM_DBG_PARMS_CALL))
	{
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return NULL;
	}

	*width = imageFileDir.width;
	*height = imageFileDir.height;
	*bytesPerChannel = imageFileDir.bytesPerChannel;
	*numChannels = imageFileDir.numChannels;
	*isFloat = imageFileDir.isFloat;

	pixel_size = imageFileDir.bytesPerChannel * imageFileDir.numChannels;
	row_size = imageFileDir.width * pixel_size;

	image_data = image_ptr = scalloc(imageFileDir.height, row_size);
	strip = strip_ptr = ScratchAlloc(row_size * imageFileDir.rows_per_strip);

	i = 0;
	remaining_rows = 0;
	for (y = 0; y < imageFileDir.height; ++y)
	{
		if (remaining_rows == 0)
		{
			remaining_rows = imageFileDir.height-y;
			if (remaining_rows > imageFileDir.rows_per_strip)
				remaining_rows = imageFileDir.rows_per_strip;

			if (i >= (int)imageFileDir.strip_count ||
				!tiffReadStrip_dbg(file, &imageFileDir, strip, remaining_rows, i MEM_DBG_PARMS_CALL))
			{
				free(image_data);
				ScratchFree(strip);
				free(imageFileDir.strip_offsets);
				free(imageFileDir.strip_byte_counts);
				return NULL;
			}
			strip_ptr = strip;
			i++;
		}

		if (imageFileDir.horizontalDifferencingType == TIFF_DIFF_FLOAT)
		{
			decodeDeltaFloats(image_ptr, strip_ptr, imageFileDir.width, imageFileDir.numChannels, imageFileDir.bytesPerChannel);
		}
		else
		{
			memcpy(image_ptr, strip_ptr, row_size);
			if (imageFileDir.horizontalDifferencingType == TIFF_DIFF_HORIZONTAL)
				decodeDeltaBytes(image_ptr, imageFileDir.width, imageFileDir.numChannels);
		}

		remaining_rows--;
		strip_ptr += row_size;
		image_ptr += row_size;
	}

	ScratchFree(strip);
	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);

	return image_data;
}

void *tiffLoadFromFilename_dbg(const char *filename, int *width, int *height, int *bytesPerChannel, int *numChannels, bool *isFloat MEM_DBG_PARMS)
{
	void *image_data;
	FILE *f;
	
	f = fopen(filename, "rb");
	if (!f)
		return NULL;

	image_data = tiffLoad_dbg(f, width, height, bytesPerChannel, numChannels, isFloat MEM_DBG_PARMS_CALL);
	fclose(f);

	return image_data;
}

bool tiffSave(FILE *file, const void *data, U32 width, U32 height, U32 bytesPerChannel, U32 numChannels, bool isFloat, bool compress, TiffPredictorType horizontalDifferencingType)
{
	TiffIFD imageFileDir = {0};
	U32 y, j, header_location, idf_location, strip_size, row_size, pixel_size;
	U8 *strip;

	imageFileDir.width = width;
	imageFileDir.height = height;
	imageFileDir.bytesPerChannel = bytesPerChannel;
	imageFileDir.numChannels = numChannels;
	imageFileDir.isFloat = !!isFloat;
	imageFileDir.lzwCompressed = !!compress;
	imageFileDir.horizontalDifferencingType = compress ? horizontalDifferencingType : TIFF_DIFF_NONE;
	imageFileDir.rows_per_strip = MIN(height, MAX_ROWS_PER_STRIP);
	imageFileDir.strip_count = round(ceilf((F32)height / imageFileDir.rows_per_strip));

	if (imageFileDir.isFloat)
		assert(imageFileDir.bytesPerChannel > 1);

	imageFileDir.strip_offsets = calloc(imageFileDir.strip_count, sizeof(U32));
	imageFileDir.strip_byte_counts = calloc(imageFileDir.strip_count, sizeof(U32));

	pixel_size = bytesPerChannel * numChannels;
	row_size = imageFileDir.width * pixel_size;
	strip_size = imageFileDir.rows_per_strip * row_size;
	strip = malloc(strip_size);

	//Fill with 0 for now, will put correct offset in later
	header_location = ftell(file);
	tiffWriteHeader(file, 0, header_location);

	// For each strip
	for (j = 0; j < imageFileDir.strip_count; ++j)
	{
		// Save strip location
		imageFileDir.strip_offsets[j] = ftell(file);

		if (imageFileDir.horizontalDifferencingType == TIFF_DIFF_FLOAT)
		{
			for (y = 0; y < imageFileDir.rows_per_strip; ++y)
				encodeDeltaFloats(&strip[y * row_size], ((const U8 *)data) + j * strip_size + y * row_size, imageFileDir.width, imageFileDir.numChannels, imageFileDir.bytesPerChannel);
		}
		else
		{
			memcpy(strip, ((const U8 *)data) + j * strip_size, strip_size);

			if (imageFileDir.horizontalDifferencingType == TIFF_DIFF_HORIZONTAL)
			{
				for (y = 0; y < imageFileDir.rows_per_strip; ++y)
					encodeDeltaBytes(&strip[y * row_size], imageFileDir.width, numChannels);
			}
		}

		// Compress and write Data
		imageFileDir.strip_byte_counts[j] = tiffWriteStrip(file, &imageFileDir, strip, imageFileDir.rows_per_strip);
	}

	free(strip);

	//Write out the Image File Directory
	idf_location = ftell(file);
	tiffWriteIFD(file, &imageFileDir, idf_location);

	//Write the real header
	tiffWriteHeader(file, idf_location, header_location);

	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);

	return true;
}

bool tiffSaveToFilename(const char *filename, const void *data, U32 width, U32 height, U32 bytesPerChannel, U32 numChannels, bool isFloat, bool compress, TiffPredictorType horizontalDifferencingType)
{
	bool ret;
	FILE *f;

	f = fopen(filename, "wb");
	if (!f)
		return false;

	ret = tiffSave(f, data, width, height, bytesPerChannel, numChannels, isFloat, compress, horizontalDifferencingType);
	fclose(f);
	return ret;
}



