#if !PLATFORM_CONSOLE
#include "nvtt.h"

#pragma comment(lib, "../../3rdparty/bin/nvtt.lib")

#include "wlSaveDXT.h"
#include "fpmacros.h"
#include "mathutil.h"

class DXTOutputHandler : public nvtt::OutputHandler
{
public:
	U8 *data, *ptr;
	int size;
#if !_WIN64
	DXTOutputHandler(U8 *out_data, int out_size)
	{
		data = out_data;
		ptr = data;
		size = out_size;
	}
	
	/// Indicate the start of a new compressed image that's part of the final texture.
	void beginImage(int size, int width, int height, int depth, int face, int miplevel)
	{
	}
	
	/// Output data. Compressed data is output as soon as it's generated to minimize memory allocations.
	bool writeData(const void *indata, int insize)
	{
		memcpy(ptr, indata, insize);
		ptr += insize;
		return true;
	}
#endif
};

int nvdxtCompress(U8 *dataIn, U8 *dataOut, int width, int height, RdrTexFormat fmt, int quality, int max_extent)
{
#if !_WIN64
	nvtt::Compressor compressor;


	// input options
	nvtt::InputOptions inputOptions;
	inputOptions.reset();
	inputOptions.setTextureLayout(nvtt::TextureType_2D, width, height);
	inputOptions.setFormat(nvtt::InputFormat_BGRA_8UB);
	inputOptions.setMipmapData(dataIn, width, height);
	inputOptions.setMipmapGeneration(false);
	inputOptions.setRoundMode(nvtt::RoundMode_None);

	if (max_extent)
		inputOptions.setMaxExtents(max_extent);


	// compression options
	nvtt::CompressionOptions compressionOptions;
	compressionOptions.reset();
	if (fmt == RTEX_DXT1)
		compressionOptions.setFormat(nvtt::Format_DXT1);
	else
		compressionOptions.setFormat(nvtt::Format_DXT5);

	switch (quality)
	{
	case 1:
		compressionOptions.setQuality(nvtt::Quality_Fastest);
		break;
	case 3:
		compressionOptions.setQuality(nvtt::Quality_Production);
		break;
	case 4:
		compressionOptions.setQuality(nvtt::Quality_Highest);
		break;
	default:
		compressionOptions.setQuality(nvtt::Quality_Normal);
	}


	// output options
	DXTOutputHandler handler(dataOut, width*height);
	nvtt::OutputOptions outputOptions;
	outputOptions.reset();
	outputOptions.setOutputHandler(&handler);
	outputOptions.setOutputHeader(false);


	// process
	int total_bytes = compressor.estimateSize(inputOptions, compressionOptions);
	compressor.enableCudaAcceleration(false);
	
	FP_NO_EXCEPTIONS_BEGIN;
	if (!compressor.process(inputOptions, compressionOptions, outputOptions))
	{
		memset(dataOut, 0, total_bytes);
	}
	FP_NO_EXCEPTIONS_END;

	return total_bytes;
#else
	return 0;
#endif
}
#endif
