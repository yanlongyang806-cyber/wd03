#ifndef _JPEG_H
#define _JPEG_H

C_DECLARATIONS_BEGIN

int jpegLoadMemRef(char *mem,int size,void **data_out,int *width_out,int *height_out,int *datasize_out);
int jpegLoad(char *mem,int size,void **data_out,int *width_out,int *height_out,int *datasize_out);
void jpgSave( char * name, U8 * pixbuf, int bpp, int x, int y, int quality );
void jpgSaveEx( char * name, U8 * pixbuf, int bpp, int x, int y, char *extraJpegData, int extraJpegDataLen, int quality);

C_DECLARATIONS_END

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

#endif
