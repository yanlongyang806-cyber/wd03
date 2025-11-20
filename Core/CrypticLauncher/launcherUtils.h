#pragma once
#include "windefinclude.h"
#include "CrypticLauncher.h"

typedef struct ShardInfo_Basic ShardInfo_Basic;

char *shardRootFolder(ShardInfo_Basic *shard);
int shardPrefSet(ShardInfo_Basic *shard);

LPCWSTR FindStringResource(HINSTANCE hinst, UINT uId);
LPCWSTR FindStringResourceEx(HINSTANCE hinst, UINT uId, UINT langId);
char *AllocStringFromResource(UINT uId);
char *AllocStringFromResourceEx(HINSTANCE hinst, UINT uId, UINT langId);

const char *cgettext(const char *str);
#define _(s) cgettext((s))
void setLauncherLocale(int locid);
void UTF8ToACP(const char *str, char *out, int len);
void ACPToUTF8(const char *str, char *out, int len);

void humanBytes(S64 bytes, F32 *num, char **units, U32 *prec);

void postCommandString(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, const char *str_value);
void postCommandInt(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, U32 int_value);
void postCommandPtr(XLOCKFREE_HANDLE queue, enumCrypticLauncherCommandType type, void *ptr_value);

void SetThreadName(DWORD dwThreadID, LPCSTR szThreadName);