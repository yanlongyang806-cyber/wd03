#include "uuid.h"
#include "rand.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unknown););

UUID_t *uuidGenerateV4(void)
{
	UUID_t *uuid = calloc(1, sizeof(UUID_t));
	uuid->version = 4;
	uuid->data.words[0] = randomU32();
	uuid->data.words[1] = randomU32();
	uuid->data.words[2] = randomU32();
	uuid->data.words[3] = randomU32();

	// Setup check bits
	uuid->data.bytes[8] |= 0x80;
	uuid->data.bytes[8] &= ~0x40;
	uuid->data.bytes[6] &= 0x0F;
	uuid->data.bytes[6] |= 0x40;
	
	return uuid;
}

char *uuidString(UUID_t *uuid, char *out, size_t out_size)
{
	sprintf_s(SAFESTR2(out), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", 
		uuid->data.bytes[ 0], uuid->data.bytes[ 1], uuid->data.bytes[ 2], uuid->data.bytes[ 3], 
		uuid->data.bytes[ 4], uuid->data.bytes[ 5], uuid->data.bytes[ 6], uuid->data.bytes[ 7],
		uuid->data.bytes[ 8], uuid->data.bytes[ 9], uuid->data.bytes[10], uuid->data.bytes[11],
		uuid->data.bytes[12], uuid->data.bytes[13], uuid->data.bytes[14], uuid->data.bytes[15]);
	return out;
}

char *uuidStringShort(UUID_t *uuid, char *out, size_t out_size)
{
	sprintf_s(SAFESTR2(out), "%02x%02x%02x%02x", 
		uuid->data.bytes[ 0], uuid->data.bytes[ 1], uuid->data.bytes[ 2], uuid->data.bytes[ 3]);
	return out;
}