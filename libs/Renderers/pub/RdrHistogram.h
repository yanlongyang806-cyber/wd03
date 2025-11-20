
// Enables prototype code and data to profile various draw list statistics with histograms.
// Currently, the code buckets object depths and sizes in the z prepass (and so the visual pass).
// To enable, you must also manually uncomment fields in RdrDrawListPassStats.
// See RdrDrawListStats and visualization code GfxDebug.c on the show_draw_list_histograms switch.
#define RDR_ENABLE_DRAWLIST_HISTOGRAMS 0

typedef F32 (*HistogramBucketFn)(F32 scale, F32 baseValue, F32 y);
typedef F32 (*HistogramBucketSplitFn)(F32 scale, F32 baseValue, int bucketN);
__forceinline F32 histoGetLogBucket(F32 scale, F32 baseValue, F32 y)
{
	return (F32)log(y / scale) / log(baseValue);
}

__forceinline F32 histoGetLogBucketSplit(F32 scale, F32 baseValue, int bucketN)
{
	return (F32)scale * pow(baseValue, (F32)bucketN);
}

__forceinline F32 histoGetLinearBucket(F32 scale, F32 baseValue, F32 y)
{
	return (y - baseValue) / scale;
}

__forceinline F32 histoGetLinearBucketSplit(F32 scale, F32 baseValue, int bucketN)
{
	return scale * bucketN + baseValue;
}

typedef struct HistogramConfig
{
	F32 scale, initial;
	HistogramBucketFn getBucketFn;
	HistogramBucketSplitFn getBucketsSplitFn;
} HistogramConfig;

__forceinline void histogramAccum(int * buckets, int buckets_size, F32 data, const HistogramConfig * config)
{
	int bucketNum = config->getBucketFn(config->scale, config->initial, data);
	if (bucketNum >= buckets_size)
		bucketNum = buckets_size - 1;
	else
		if (bucketNum < 0)
			bucketNum = 0;
	++buckets[bucketNum];
}