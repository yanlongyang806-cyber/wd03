#pragma once
GCC_SYSTEM
/*
 * Author     :  Paul Kocher
 * E-mail     :  pck@netcom.com
 * Date       :  1997
 * Description:  C implementation of the Blowfish algorithm.
 */

#ifndef _BLOWFISH_H
#define _BLOWFISH_H
#define MAXKEYBYTES 56          /* 448 bits */

typedef struct {

	unsigned long P[16 + 2];
	unsigned long S[4][256];
} BLOWFISH_CTX;

void cryptBlowfishInitU32(BLOWFISH_CTX *ctx, unsigned int *key, int keyLen);
void cryptBlowfishInit(BLOWFISH_CTX *ctx, unsigned char *key, int keyLen);
void cryptBlowfishEncrypt(BLOWFISH_CTX *ctx, unsigned char *mem, int len);
void cryptBlowfishDecrypt(BLOWFISH_CTX *ctx, unsigned char *mem, int len);
int cryptBlowfishTest(BLOWFISH_CTX *ctx);       /* 0=ok, -1=bad */

#endif
