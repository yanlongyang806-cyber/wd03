#pragma once
GCC_SYSTEM

typedef struct Player Player;

AUTO_STRUCT;
typedef struct StoredCredentials
{
	char *user; AST(NAME(User) ESTRING)
	char *token; AST(NAME(Token) ESTRING)
	char *secret; AST(NAME(Secret) ESTRING)
} StoredCredentials;

void StoredCredentialsUserKey(char **out, const char *service);
void StoredCredentialsTokenKey(char **out, const char *service);
void StoredCredentialsSecretKey(char **out, const char *service);
void StoredCredentialsFromPlayer(Player *pPlayer, StoredCredentials *pCreds, const char *service);