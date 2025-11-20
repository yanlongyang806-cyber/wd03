#pragma once
GCC_SYSTEM

void gclStoredCredentialsStore(const char *service, const char *user, const char *token, const char *secret);
void gclStoredCredentialsGet(const char *service, char **user, char **token, char **secret);