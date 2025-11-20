#pragma once
GCC_SYSTEM

typedef struct UUID_t
{
	U8 version;
	union
	{
		U32 words[4];
		U8 bytes[16];
	} data;
} UUID_t;

UUID_t *uuidGenerateV4(void);

char *uuidString(UUID_t *uuid, char *out, size_t out_len);
char *uuidStringShort(UUID_t *uuid, char *out, size_t out_len);