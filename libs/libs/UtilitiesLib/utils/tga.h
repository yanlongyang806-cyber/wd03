#ifndef _TGA_H
#define _TGA_H


char *tgaLoad(FILE *file,int *wp,int *hp);
char *tgaLoadFromFname(const char *fname, int *wp, int *hp);
char *tgaLoadFromMemory(char *memory,int size,int *wp,int *hp);
U8* tgaCreateScratchData( int width, int height );

// regardless of nchannels, data* is interpreted as 32bpp
bool tgaSave(const char *filename,char *data,int width,int height,int nchannels);

// data* is interpreted based on nchannels
void tgaSaveRGB(const char *filename,char *data,int width,int height,int nchannels);

char *tgaSaveToMem(char *data,int width,int height,int nchannels, int *datalen);

void tgaClear( U8* tga, int tgaWidth, int tgaHeight );
void tgaCopyTgaAlphaMap( U8* tga, int tgaWidth, int tgaHeight, U8* tgaAlphaMap );
void tgaCopyTga( U8* dest, int destWidth, int destHeigh, int x, int y,
                 U8* src, int srcWidth, int srcHeight );
void tgaCopyTgaTransposed( U8* dest, int destWidth, int destHeigh, int x, int y,
                           U8* src, int srcWidth, int srcHeight );
void tgaScale( U8* dest, int destWidth, int destHeight,
               U8* src, int srcWidth, int srcHeight,
               bool isFiltered );

void tgaFillRect( U8* tga, int tgaWidth, int tgaHeight, int x, int y, int dx, int dy, int color );

void tgaAlphaPad( U8* tga, int tgaWidth, int tgaHeight, int x, int y, int dx, int dy,
                  int numTimes );
void tgaMultAlphaNoise( U8* tga, int tgaWidth, int tgaHeight, int x, int y, int dx, int dy );

#endif
