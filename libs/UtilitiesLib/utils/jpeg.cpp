
#include "stdtypes.h"
#include "jpeg.h"
#include "file.h"
#include "error.h"
#include "MemRef.h"

#include "..\..\3rdparty\jpgdlib\inc\jpegdecoder.h"

#if _WIN64
#pragma comment(lib, "jpgdlibX64.lib")
#pragma comment(lib, "IJGWin32X64.lib")
#elif _PS3
#elif _XBOX
#pragma comment(lib, "jpgdlibxbox.lib")	// removed
#pragma comment(lib, "IJGXBox.lib")		// removed
#else
#pragma comment(lib, "jpgdlib.lib")
#pragma comment(lib, "IJGWin32.lib")
#endif

C_DECLARATIONS_BEGIN


void* cryptic_jpeg_malloc(int size)
{
	return malloc(size);
}

void cryptic_jpeg_free(void* mem)
{
	free(mem);
}

static int jpegLoadEx(char *mem, int size, void **data_out, bool data_memref, int *width_out, int *height_out, int *datasize_out)
{
	int		success = 0;
    int lines_decoded = 0;
    uchar *Pbuf = NULL;

	Pjpeg_decoder_file_stream Pinput_stream = new jpeg_decoder_file_stream();
	Pinput_stream->set_mem_ptr(mem,size);
	Pjpeg_decoder Pd = new jpeg_decoder(Pinput_stream, 0);

	if (Pd->get_error_code() != 0)
	{
		printf("Error: Decoder failed! Error status: %i\n", Pd->get_error_code());
		delete Pd;
		delete Pinput_stream;
		return 0;
	}

	if (Pd->get_num_components() != 3)
		goto fail_exit;

	if (Pd->begin())
		goto fail_exit;

	if (data_memref)
		Pbuf = (uchar *)memrefAlloc(Pd->get_width() * 3 * Pd->get_height());
	else
		Pbuf = (uchar *)malloc(Pd->get_width() * 3 * Pd->get_height());
    if (!Pbuf)
	    goto fail_exit;

    for ( ; ; )
    {
        void *Pscan_line_ofs;
        uint scan_line_len;

        if (Pd->decode(&Pscan_line_ofs, &scan_line_len))
	        break;

        uchar *Psb = (uchar *)Pscan_line_ofs;
        uchar *Pdb = &Pbuf[lines_decoded * Pd->get_width() * 3];
        int src_bpp = Pd->get_bytes_per_pixel();

        for (int x = Pd->get_width(); x > 0; x--, Psb += src_bpp, Pdb += 3)
        {
	        Pdb[0] = Psb[2];
	        Pdb[1] = Psb[1];
	        Pdb[2] = Psb[0];
        }
        lines_decoded++;
    }

    if (Pd->get_error_code())
	    goto fail_exit;

#if 0
    printf("Lines decoded: %i\n", lines_decoded);
    printf("Input file size:  %i\n", Pinput_stream->get_size());
    printf("Input bytes actually read: %i\n", Pd->get_total_bytes_read());
#endif

    success = 1;
    *width_out			= Pd->get_width();
    *height_out			= Pd->get_height();
    *data_out			= Pbuf;
//	info->tex_format	= RTEX_BGR_U8;
    *datasize_out		= *width_out * *height_out * 3;

fail_exit:
  delete Pd;
  delete Pinput_stream;	// JS: This does not close the file handle anymore.
  return success;
}

int jpegLoad(char *mem,int size,void **data_out,int *width_out,int *height_out,int *datasize_out)
{
	return jpegLoadEx(mem, size, data_out, false, width_out, height_out, datasize_out);
}

int jpegLoadMemRef(char *mem,int size,void **data_out,int *width_out,int *height_out,int *datasize_out)
{
	return jpegLoadEx(mem, size, data_out, true, width_out, height_out, datasize_out);
}



/******************** JPEG COMPRESSION SAMPLE INTERFACE *******************/

/* This half of the example shows how to feed data into the JPEG compressor.
* We present a minimal version that does not worry about refinements such
* as error recovery (the JPEG code will just exit() if it gets an error).
*/


/*
* IMAGE DATA FORMATS:
*
* The standard input image format is a rectangular array of pixels, with
* each pixel having the same number of "component" values (color channels).
* Each pixel row is an array of JSAMPLEs (which typically are unsigned chars).
* If you are working with color data, then the color values for each pixel
* must be adjacent in the row; for example, R,G,B,R,G,B,R,G,B,... for 24-bit
* RGB color.
*
* For this example, we'll assume that this data structure matches the way
* our application has stored the image in memory, so we can just pass a
* pointer to our image buffer.  In particular, let's say that the image is
* RGB color and is described by:
*/

#include "jpeglib.h"
#include "utils.h"
#include "file.h"

JSAMPLE * image_buffer;	/* Points to large array of R,G,B-order data */
int image_height;	/* Number of rows in image */
int image_width;		/* Number of columns in image */

/*
* Sample routine for JPEG compression.  We assume that the target file name
* and a compression quality factor are passed in.
*/

GLOBAL(void)
write_JPEG_file (char * filename, int quality, char *extraJpegData, int extraJpegDatalen)
{

	/* This struct contains the JPEG compression parameters and pointers to
	* working space (which is allocated as needed by the JPEG library).
	* It is possible to have several such structures, representing multiple
	* compression/decompression processes, in existence at once.  We refer
	* to any one struct (and its associated working data) as a "JPEG object".
	*/
	struct jpeg_compress_struct cinfo;
	/* This struct represents a JPEG error handler.  It is declared separately
	* because applications often want to supply a specialized error handler
	* (see the second half of this file for an example).  But here we just
	* take the easy way out and use the standard error handler, which will
	* print a message on stderr and call exit() if compression fails.
	* Note that this struct must live as long as the main JPEG parameter
	* struct, to avoid dangling-pointer problems.
	*/
	struct jpeg_error_mgr jerr;
	/* More stuff */
	FILE * outfile;		/* target file */
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	int row_stride;		/* physical row width in image buffer */

	/* Step 1: allocate and initialize JPEG compression object */

	/* We have to set up the error handler first, in case the initialization
	* step fails.  (Unlikely, but it could happen if you are out of memory.)
	* This routine fills in the contents of struct jerr, and returns jerr's
	* address which we place into the link field in cinfo.
	*/
	cinfo.err = jpeg_std_error(&jerr);
	/* Now we can initialize the JPEG compression object. */
	jpeg_create_compress(&cinfo);

	/* Step 2: specify data destination (eg, a file) */
	/* Note: steps 2 and 3 can be done in either order. */

	/* Here we use the library-supplied code to send compressed data to a
	* stdio stream.  You can also write your own code to do something else.
	* VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	* requires it in order to write binary files.
	*/
	mkdirtree(filename);

	if ((outfile = (FILE *)fopen(filename, "wb!")) == NULL) {
		Errorf("can't open %s\n", filename);
		return;
	}

	jpeg_stdio_dest(&cinfo, (FILE*)fileRealPointer(outfile));

	/* Step 3: set parameters for compression */

	/* First we supply a description of the input image.
	* Four fields of the cinfo struct must be filled in:
	*/
	cinfo.image_width = image_width; 	/* image width and height, in pixels */
	cinfo.image_height = image_height;
	cinfo.input_components = 3;		/* # of color components per pixel */
	cinfo.in_color_space = JCS_RGB; 	/* colorspace of input image */
	/* Now use the library's routine to set default compression parameters.
	* (You must set at least cinfo.in_color_space before calling this,
	* since the defaults depend on the source color space.)
	*/
	jpeg_set_defaults(&cinfo);
	/* Now you can set any non-default parameters you wish to.
	* Here we just illustrate the use of quality (quantization table) scaling:
	*/
	jpeg_set_quality(&cinfo, quality, TRUE /* limit to baseline-JPEG values */);

	/* Step 4: Start compressor */

	/* TRUE ensures that we will write a complete interchange-JPEG file.
	* Pass TRUE unless you are very sure of what you're doing.
	*/
	jpeg_start_compress(&cinfo, TRUE);


	/* Extra tags go here */
	if(extraJpegData)
	{
		jpeg_write_marker(&cinfo, JPEG_APP0+0x0d, (const JOCTET *)extraJpegData, extraJpegDatalen);
	}

	/* Step 5: while (scan lines remain to be written) */
	/*           jpeg_write_scanlines(...); */

	/* Here we use the library's state variable cinfo.next_scanline as the
	* loop counter, so that we don't have to keep track ourselves.
	* To keep things simple, we pass one scanline per call; you can pass
	* more if you wish, though.
	*/
	row_stride = image_width * 3;	/* JSAMPLEs per row in image_buffer */

	while (cinfo.next_scanline < cinfo.image_height) {
		/* jpeg_write_scanlines expects an array of pointers to scanlines.
		* Here the array is only one element long, but you could pass
		* more than one scanline at a time if that's more convenient.
		*/
		row_pointer[0] = & image_buffer[cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	/* Step 6: Finish compression */

	jpeg_finish_compress(&cinfo);
	/* After finish_compress, we can close the output file. */
	fclose(outfile);

	/* Step 7: release JPEG compression object */

	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_compress(&cinfo);

	/* And we're done! */

}


/*
* SOME FINE POINTS:
*
* In the above loop, we ignored the return value of jpeg_write_scanlines,
* which is the number of scanlines actually written.  We could get away
* with this because we were only relying on the value of cinfo.next_scanline,
* which will be incremented correctly.  If you maintain additional loop
* variables then you should be careful to increment them properly.
* Actually, for output to a stdio stream you needn't worry, because
* then jpeg_write_scanlines will write all the lines passed (or else exit
* with a fatal error).  Partial writes can only occur if you use a data
* destination module that can demand suspension of the compressor.
* (If you don't know what that's for, you don't need it.)
*
* If the compressor requires full-image buffers (for entropy-coding
* optimization or a multi-scan JPEG file), it will create temporary
* files for anything that doesn't fit within the maximum-memory setting.
* (Note that temp files are NOT needed if you use the default parameters.)
* On some systems you may need to set up a signal handler to ensure that
* temporary files are deleted if the program is interrupted.  See libjpeg.doc.
*
* Scanlines MUST be supplied in top-to-bottom order if you want your JPEG
* files to be compatible with everyone else's.  If you cannot readily read
* your data in that order, you'll need an intermediate array to hold the
* image.  See rdtarga.c or rdbmp.c for examples of handling bottom-to-top
* source data using the JPEG code's internal virtual-array mechanisms.
*/



/******************** JPEG DECOMPRESSION SAMPLE INTERFACE *******************/

/* This half of the example shows how to read data from the JPEG decompressor.
* It's a bit more refined than the above, in that we show:
*   (a) how to modify the JPEG library's standard error-reporting behavior;
*   (b) how to allocate workspace using the library's memory manager.
*
* Just to make this example a little different from the first one, we'll
* assume that we do not intend to put the whole image into an in-memory
* buffer, but to send it line-by-line someplace else.  We need a one-
* scanline-high JSAMPLE array as a work buffer, and we will let the JPEG
* memory manager allocate it for us.  This approach is actually quite useful
* because we don't need to remember to deallocate the buffer separately: it
* will go away automatically when the JPEG object is cleaned up.
*/


void jpgSaveEx( char * name, U8 * pixbuf, int bpp, int sizeOfPictureX, int sizeOfPictureY, 
			   char *extraJpegData, int extraJpegDatalen, int quality )
{
	int i;
	U8 * jpegbuf;

	if (quality <= 0)
		quality = 95; // default quality = 95

	assert(bpp > 2);

	//Convert from 4 byte to 3 byte
	jpegbuf = (U8*)calloc( sizeOfPictureY * sizeOfPictureX, 3 );
	for( i = 0 ; i < (sizeOfPictureY * sizeOfPictureX) ; i++ )
	{
		jpegbuf[i*3+0] = pixbuf[i*bpp+0];
		jpegbuf[i*3+1] = pixbuf[i*bpp+1];
		jpegbuf[i*3+2] = pixbuf[i*bpp+2];
	}

	//globals, I don't know why
	image_buffer = jpegbuf;			// Points to large array of R,G,B-order data 
	image_height = sizeOfPictureY;	// Number of rows in image 
	image_width  = sizeOfPictureX;	// Number of columns in image 

	write_JPEG_file(name, quality, extraJpegData, extraJpegDatalen); //Quality 1 - 100, 85 = 17k ish file

	free(jpegbuf);

}

void jpgSave( char * name, U8 * pixbuf, int bpp, int sizeOfPictureX, int sizeOfPictureY, int quality)
{
	jpgSaveEx(name, pixbuf, bpp, sizeOfPictureX, sizeOfPictureY, NULL, 0, quality);
}

C_DECLARATIONS_END

