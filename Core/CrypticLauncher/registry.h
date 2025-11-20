#pragma once

#define CRYPTIC_REG_KEY "HKEY_CURRENT_USER\\Software\\Cryptic\\%s"

// Read a string from the product-specific registry key
int readRegStr(const char *prodName, const char *name, char *outbuffer, int outlen, char **history);

// Write a string to the product-specific registry key
int writeRegStr(const char *prodName, const char *name, const char *str);

// Read a number from the product-specific registry key
int readRegInt(const char *prodName, const char *name, unsigned int *out, char **history);

// Write a number to the product-specific registry key
int writeRegInt(const char *prodName, const char *name, unsigned int val);