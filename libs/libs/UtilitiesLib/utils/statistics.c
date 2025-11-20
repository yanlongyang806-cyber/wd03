#include "BlockEarray.h"
#include "earray.h"
#include "EString.h"
#include "statistics.h"
#include "qsortG.h"
#include "error.h"
#include "mathutil.h"

// Calculate general statistics for a F32 block earray, and add them to a log line.
void statisticsLogStatsF32_dbg(char **estrLogLine, F32 *stat, const char *prefix, const char *caller_fname, int line)
{
	int n = beaSize(&stat);
	F32 sum = 0;
	F32 min_value;
	F32 max_value;
	int i;
	int q1_index, q3_index, med_index;

	// If no data was collected, don't log anything.
	if (!n)
		return;

	// Sort data for numerical stability and quartile computation.
	qsort(stat, n, sizeof(F32), cmpF32);

	// Compute size
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_n %d", prefix, n);

	// Compute sum, min, and max by scanning.
	min_value = stat[0];
	max_value = stat[0];
	for (i = 0; i != n; ++i)
	{
		sum += stat[i];
		min_value = MIN(min_value, stat[i]);
		max_value = MAX(max_value, stat[i]);
	}
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_sum %f", prefix, sum);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_min %f", prefix, min_value);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_max %f", prefix, max_value);

	// Compute quartiles.
	q1_index = n/4;
	q3_index = n - q1_index - 1;
	med_index = n/2;
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q1 %f", prefix, stat[q1_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_med %f", prefix, stat[med_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q3 %f", prefix, stat[q3_index]);
}

// Calculate general statistics for a U64 block earray, and add them to a log line.
// Duplicate of statisticsLogStatsF32() except for different type
void statisticsLogStatsU64_dbg(char **estrLogLine, U64 *stat, const char *prefix, const char *caller_fname, int line)
{
	int n = beaSize(&stat);
	U64 sum = 0;
	U64 min_value;
	U64 max_value;
	int i;
	int q1_index, q3_index, med_index;

	// If no data was collected, don't log anything.
	if (!n)
		return;

	// Sort data for numerical stability and quartile computation.
	qsort(stat, n, sizeof(U64), cmpU64);

	// Compute size
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_n %d", prefix, n);

	// Compute sum, min, and max by scanning.
	min_value = stat[0];
	max_value = stat[0];
	for (i = 0; i != n; ++i)
	{
		sum += stat[i];
		min_value = MIN(min_value, stat[i]);
		max_value = MAX(max_value, stat[i]);
	}
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_sum %"FORM_LL"u", prefix, sum);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_min %"FORM_LL"u", prefix, min_value);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_max %"FORM_LL"u", prefix, max_value);

	// Compute quartiles.
	q1_index = n/4;
	q3_index = n - q1_index - 1;
	med_index = n/2;
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q1 %"FORM_LL"u", prefix, stat[q1_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_med %"FORM_LL"u", prefix, stat[med_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q3 %"FORM_LL"u", prefix, stat[q3_index]);
}

// Calculate general statistics for a U32 block earray, and add them to a log line.
// Duplicate of statisticsLogStatsF32() except for different type
void statisticsLogStatsU32_dbg(char **estrLogLine, U32 *stat, const char *prefix, const char *caller_fname, int line)
{
	int n = beaSize(&stat);
	U32 sum = 0;
	U32 min_value;
	U32 max_value;
	int i;
	int q1_index, q3_index, med_index;

	// If no data was collected, don't log anything.
	if (!n)
		return;

	// Sort data for numerical stability and quartile computation.
	qsort(stat, n, sizeof(U32), cmpU32);

	// Compute size
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_n %d", prefix, n);

	// Compute sum, min, and max by scanning.
	min_value = stat[0];
	max_value = stat[0];
	for (i = 0; i != n; ++i)
	{
		sum += stat[i];
		min_value = MIN(min_value, stat[i]);
		max_value = MAX(max_value, stat[i]);
	}
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_sum %lu", prefix, sum);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_min %lu", prefix, min_value);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_max %lu", prefix, max_value);

	// Compute quartiles.
	q1_index = n/4;
	q3_index = n - q1_index - 1;
	med_index = n/2;
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q1 %lu", prefix, stat[q1_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_med %lu", prefix, stat[med_index]);
	estrConcatf_dbg(estrLogLine, caller_fname, line, " %s_q3 %lu", prefix, stat[q3_index]);
}

static U32 statisticsCountInHalfOpenIntervalF32(const F32 *data, F32 minimum, F32 maximum)
{
	int i;
	U32 sum = 0;

	for (i = 0; i != eafSize(&data); ++i)
		if (data[i] >= minimum && data[i] < maximum)
			++sum;

	return sum;
}

// Get a percentile of sorted data.
// WARNING: This function does no verification that the data is, in fact, sorted.
F32 statisticsSortedPercentile(const F32 *data, int count, F32 percentile)
{
	int index;

	// Make sure there's data.
	if (!data || !count)
		return F32_infinity();

	// Get index of percentile.
	index = count*percentile;
	if (index == count)
		--index;

	// Return value at index.
	return data[index];
}

// Compute a frequency histogram.
void statisticsHistogramF32(F32 **centers, U32 **counts, const F32 *data, int bins, F32 optional_right_limit)

{
	F32 minimum;
	F32 maximum;
	int i;
	F32 bin_size;
	F32 begin;

	// Ensure there is data to work on.
	if (!eafSize(&data) || bins <= 0)
		return;

	// Get minimum and maximum.
	minimum = maximum = data[0];
	for (i = 1; i != eafSize(&data); ++i)
	{
		minimum = MIN(minimum, data[i]);
		maximum = MAX(maximum, data[i]);
	}

	// Allow the bounds to be overidden.
	if (optional_right_limit)
		maximum = optional_right_limit;

	// Allocate storage.
	eafSetSize(centers, bins);
	ea32SetSize(counts, bins);

	// Make bins.
	// TODO: If we sorted the array first, we could do this in O(n).
	bin_size = (maximum-minimum)/bins;
	begin = minimum;
	for (i = 0; i != bins; ++i)
	{
		F32 end = begin + bin_size;
		F32 bin_minimum = i == 0 ? F32_negative_infinity() : begin;
		F32 bin_maximum = !optional_right_limit && i == bins - 1 ? F32_infinity() : end;
		(*centers)[i] = (begin + end) / 2;
		(*counts)[i] = statisticsCountInHalfOpenIntervalF32(data, bin_minimum, bin_maximum);
		begin = end;
	}
}


F32 statisticsNormalDist(F32 z)
{
	if(z < -12)
		return 0.0;
	else if(z > 12)
		return 1.0;
	else if(z == 0.0)
		return 0.5;
	else
	{
		static const F32 SQRT_2PI = 2.50662827463;
		F32 z2, t, q;
		S32 i;
		bool e;
		if(z > 0.0)
			e = true;
		else
		{
			e = false;
			z = -z;
		}

		z2 = z * z;
		t = q = z * pow(2.71828, -0.5 * z2) / SQRT_2PI;

		for(i = 3; i < 200; i+=2)
		{
			F32 prev = q;
			t *= z2 / i;
			q += t;
			if(q <= prev)
				return e ? (0.5 + q) : (0.5 - q);
		}

		return e ? 1.0 : 0.0;
	}
}

F32 statisticsPNormalDist_dbg(F32 qn, const char *caller_fname, int line)
{
	static const F32 b[11] = {
		1.570796288,		0.03706987906,		-0.8364353589e-3,
		-0.2250947176e-3,	0.6841218299e-5,	0.5824238515e-5,
		-0.104527497e-5,	0.8360937017e-7,	-0.3231081277e-8,
		0.3657763036e-10,	0.6936233982e-12
	};

	if(qn <= 0.0 || qn >= 1.0)
	{
		ErrorfInternal(true, caller_fname, line, "qn <= 0 or qn >= 1 in statisticsPNormalDist_dbg: qn = %f", qn);
		return 0.0;
	}

	if(qn == 0.5)
		return 0.0;
	else
	{
		F32 w3;
		S32 i;

		F32 w1 = qn;
		if(qn > 0.5)
			w1 = 1.0 - w1;

		w3 = -log(4.0 * w1 * (1.0 - w1));
		w1 = b[0];
		for(i = 1; i < 11; i++)
			w1 += b[i] * pow(w3, i);

		if(qn > 0.5)
			return sqrt(w1 * w3);

		return -sqrt(w1 * w3);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void StatisticsTest()
{
	F32 result, expected;

	result = statisticsNormalDist(1.96);
	expected = 1 - (1 - 0.95) / 2;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsNormalDist(2.24);
	expected = 1 - (1 - 0.975) / 2;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsNormalDist(1.64);
	expected = 1 - (1 - 0.9) / 2;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsNormalDist(0.67);
	expected = 1 - (1 - 0.5) / 2;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsNormalDist(0.25);
	expected = 1 - (1 - 0.2) / 2;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsPNormalDist(1 - (1 - 0.95) / 2);
	expected = 1.96;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsPNormalDist(1 - (1 - 0.975) / 2);
	expected = 2.24;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsPNormalDist(1 - (1 - 0.9) / 2);
	expected = 1.64;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsPNormalDist(1 - (1 - 0.5) / 2);
	expected = 0.67;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);

	result = statisticsPNormalDist(1 - (1 - 0.2) / 2);
	expected = 0.25;
	devassertmsgf(fabs(expected - result) < 0.01, "Was %f [expected %f]", result, expected);
}
