#include"rt_xfastparticles.h"

#include"rand.h"
#include"rgb_hsv.h"
#include"rt_xstate.h"
#if !PLATFORM_CONSOLE && !_M_X64
#define USE_SSE
#endif
#ifdef USE_SSE
#include <xmmintrin.h>
#include "ssemath.h"
#else
typedef struct sseVec4
{
	Vec4 v;
} sseVec4;
typedef struct sseVec3
{
	Vec3 v;
} sseVec3;
#endif

extern RdrSurfaceStateDX *current_state;
typedef enum FastParticleMode
{
	FAPA_NORMAL,
	FAPA_STREAK,
	FAPA_LINKSCALE,
} FastParticleMode;

// faster when not forceinlined
static float calcLerpParam(float fTimeA, float fTimeB, float fTime)
{
	float denom = fTimeB - fTimeA;
	if( denom == 0 )
		denom = 1;
	return (fTime - fTimeA) / (denom);
}

static __forceinline void findIndex(Vec4 newTime, IVec4 result, const Mat44 time, const Vec4 fScaledTime, int index)
{
	if (time[1][index] >= fScaledTime[index])
	{
		result[index] = 1;
		newTime[index] = calcLerpParam(time[0][index], time[1][index], fScaledTime[index]);
	}
	else if (time[2][index] >= fScaledTime[index])
	{
		result[index] = 2;
		newTime[index] = calcLerpParam(time[1][index], time[2][index], fScaledTime[index]);
	}
	else if (time[3][index] >= fScaledTime[index])
	{
		result[index] = 3;
		newTime[index] = calcLerpParam(time[2][index], time[3][index], fScaledTime[index]);
	}
	else
	{
		result[index] = 4;
		newTime[index] = 1.0f;
	}
}

static __forceinline void fpHSVToRGB(const Vec3 vHSV, Vec3 vRGB, F32 fHueShift)
{
	Vec3 vAdjustedHSV;
	vAdjustedHSV[0] = vHSV[0] + fHueShift;
	vAdjustedHSV[1] = vHSV[1];
	vAdjustedHSV[2] = vHSV[2];

	// hsvToRgb uses 0-360 instead of 0-1, and does not support
	// numbers outside that range, unlike in the vertex shader.
	if (vAdjustedHSV[0] < 0)
		vAdjustedHSV[0] += 1;
	if (vAdjustedHSV[0] > 1)
		vAdjustedHSV[0] -= 1;
	vAdjustedHSV[0] *= 360;
	hsvToRgb(vAdjustedHSV, vRGB);
}

static float calcLerpParamWithDelta(float fTimeA, float invdenom, float fTime)
{
	return (fTime - fTimeA) * (invdenom);
}
static __forceinline void findIndexWithDelta(Vec4 newTime, IVec4 result, const Mat44 time, const Mat34 time_delta, const Vec4 fScaledTime, int index)
{
	if (time[1][index] >= fScaledTime[index])
	{
		result[index] = 1;
		newTime[index] = calcLerpParamWithDelta(time[0][index], time_delta[0][index], fScaledTime[index]);
	}
	else if (time[2][index] >= fScaledTime[index])
	{
		result[index] = 2;
		newTime[index] = calcLerpParamWithDelta(time[1][index], time_delta[1][index], fScaledTime[index]);
	}
	else if (time[3][index] >= fScaledTime[index])
	{
		result[index] = 3;
		newTime[index] = calcLerpParamWithDelta(time[2][index], time_delta[2][index], fScaledTime[index]);
	}
	else
	{
		result[index] = 4;
		newTime[index] = 1.0f;
	}
}


#ifdef USE_SSE
// Not actually faster than the normal findIndex() :(
static __forceinline void findIndexSSE(Vec4 newTime, IVec4 result, const __m128 time, const __m128 sseScaledTime, int index)
{
	__m128 mytime = _mm_set1_ps(sseScaledTime.m128_f32[index]);
	__m128 r = _mm_cmple_ps(mytime, time);
	int v = 3 + r.m128_i32[1] + r.m128_i32[2] + r.m128_i32[3];
	ANALYSIS_ASSUME(v>=0 && v<=3);
	result[index] = v+1;
	if (v==3)
	{
		newTime[index] = 1.0f;
	} else {
		newTime[index] = (sseScaledTime.m128_f32[index] - time.m128_f32[v]) / (time.m128_f32[v+1] - time.m128_f32[v]);
	}
}

static __m128 getJitterSSE(int iJitterIndex, const IVec4 indices)
{
	__m128 result;
	__m128 ivec;
	static const __m128 invVec = {1.f/(F32)0x7fffffffUL, 1.f/(F32)0x7fffffffUL, 1.f/(F32)0x7fffffffUL, 1.f/(F32)0x7fffffffUL};

	//return -1.f + (puiBLORN[uiSeed & BLORN_MASK] / (F32)0x7fffffffUL);
	ivec.m128_i32[0] = puiBLORN[(iJitterIndex + indices[0]) & BLORN_MASK];
	ivec.m128_i32[1] = puiBLORN[(iJitterIndex + indices[1]+4) & BLORN_MASK];
	ivec.m128_i32[2] = puiBLORN[(iJitterIndex + indices[2]+8) & BLORN_MASK];
	ivec.m128_i32[3] = puiBLORN[(iJitterIndex + indices[3]+12) & BLORN_MASK];

#if 0 // SSE2, actually slower because the optimizer can't interleave the integer math/loads with the SSE?
	_asm
	{
		movaps      xmm0,   [ivec]
		movaps      xmm1,   [invVec]
		cvtdq2ps    xmm0,	xmm0	// SIGNED int to float
		mulps       xmm0,	xmm1	// multiply
		movaps      [result],xmm0 
	}
#else

	result = _mm_cvtsi32_ss(result, ivec.m128_i32[1]);

	result = _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 3, 2, 1));
	result = _mm_cvtsi32_ss(result, ivec.m128_i32[2]);

	result = _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 3, 2, 1));
	result = _mm_cvtsi32_ss(result, ivec.m128_i32[3]);

	result = _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 3, 2, 1));
	result = _mm_cvtsi32_ss(result, ivec.m128_i32[0]);

	result = _mm_mul_ps(result, invVec);

#endif
	return result;
}

#endif


static __forceinline void findIndex4(Vec4 newTime, IVec4 result, const Mat44 time, const Vec4 fScaledTimes)
{
	findIndex(newTime, result, time, fScaledTimes, 0);
	findIndex(newTime, result, time, fScaledTimes, 1);
	findIndex(newTime, result, time, fScaledTimes, 2);
	findIndex(newTime, result, time, fScaledTimes, 3);
}


static void calcLerpColorRGB(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1, F32 fHueShift);
static void calcLerpColorHSV(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1);
static void calcLerpScale(FastParticleMode mode, Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1);
static void getJitter(Vec4 result, int iJitterIndex, const IVec4 indices);
static float frac(float value);
static void scaleAndRotate(Vec3 result, const Vec3 vec, const Vec2 scale, float rot);
static const float* getCornerPos(int iCorner);
static const float* getCornerTex(int iCorner);
static const void getQuadCornerTex(Vec2 result, int iCorner, bool b0, bool b1);
static void streak(Vec3 result, int corner, float scale_x, float scale_y, const Vec3 streakDir);

FastParticleMode fapaMode(const RdrDrawableFastParticles *particle_set)
{
	if (particle_set->streak)
	{
		return FAPA_STREAK;
	}
	else if (particle_set->link_scale)
	{
		return FAPA_LINKSCALE;
	}
	else
	{
		return FAPA_NORMAL;
	}
}

#ifdef USE_SSE

// Note: these frac() functions only work with positive values, another couple
//  instructions could be added to deal with that (see http://www.masm32.com/board/index.php?topic=9515.0 )
__forceinline F32 fracsse(F32 f)
{
	__m128 v = {f};
	__m128 t;
	int i = _mm_cvttss_si32(v);
	t = _mm_cvtsi32_ss(v, i);
	v = _mm_sub_ss(v, t);
	return v.m128_f32[0];
}

__forceinline __m128 frac4sse2(__m128 v)
{
	__asm {
		movaps       xmm0,   [v]
		cvttps2dq    xmm1,   xmm0	// Convert to int
		cvtdq2ps     xmm1,	xmm1	// back to float
		subps       xmm0,	xmm1	// subtract from float for remainder
		movaps      [v],xmm0 
	}
	return v;
}

__forceinline __m128 frac4sse(__m128 v)
{
	__m128 t;
	int i;
	// Only has scalar truncate, so truncate each and rotate/shuffle

	i = _mm_cvttss_si32(v);
	t = _mm_cvtsi32_ss(v, i);

	t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 3, 2, 1));
	i = _mm_cvttss_si32(t);
	t = _mm_cvtsi32_ss(t, i);

	t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 3, 2, 1));
	i = _mm_cvttss_si32(t);
	t = _mm_cvtsi32_ss(t, i);

	t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 3, 2, 1));
	i = _mm_cvttss_si32(t);
	t = _mm_cvtsi32_ss(t, i);

	t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 3, 2, 1));
	v = _mm_sub_ps(v, t);
	return v;
}

// Despite variable names, also used for scale/rot, not just color
__forceinline static void findIndex4SSE(Vec4 vColorTimes, IVec4 iColorIndices, const Vec4 *color_time, __m128 *color_time_delta, __m128 sseScaledTimes, __m128 *sse_color_time)
{
	int index;
	// All 4 channels in parellel
	__m128 r1 = _mm_cmple_ps(sseScaledTimes, sse_color_time[1]);
	__m128 r2 = _mm_cmple_ps(sseScaledTimes, sse_color_time[2]);
	__m128 r3 = _mm_cmple_ps(sseScaledTimes, sse_color_time[3]);
	__m128 t;
	iColorIndices[0] = 1+(t.m128_i32[0] = 3 + r1.m128_i32[0] + r2.m128_i32[0] + r3.m128_i32[0]);
	iColorIndices[1] = 1+(t.m128_i32[1] = 3 + r1.m128_i32[1] + r2.m128_i32[1] + r3.m128_i32[1]);
	iColorIndices[2] = 1+(t.m128_i32[2] = 3 + r1.m128_i32[2] + r2.m128_i32[2] + r3.m128_i32[2]);
	iColorIndices[3] = 1+(t.m128_i32[3] = 3 + r1.m128_i32[3] + r2.m128_i32[3] + r3.m128_i32[3]);

	{
		__m128 t0, t1;
		for (index=0; index<4; index++)
		{
			if (t.m128_i32[index] == 3)
			{
				// want (time - t0) * t1 == 1
				t0.m128_f32[index] = sseScaledTimes.m128_f32[index]-1;
				t1.m128_f32[index] = 1;
			} else {
				ANALYSIS_ASSUME(t.m128_i32[index]>=0 && t.m128_i32[index]<=3);
				t0.m128_f32[index] = sse_color_time[t.m128_i32[index]].m128_f32[index];
				t1.m128_f32[index] = color_time_delta[t.m128_i32[index]].m128_f32[index];
			}
		}

		t0 = _mm_sub_ps(sseScaledTimes, t0);
		t0 = _mm_mul_ps(t0, t1);
		_mm_storeu_ps(vColorTimes, t0);
	}

#if 0 // verify
	{
		Vec4 testColorTimes;
		IVec4 testColorIndices;
		findIndex4(testColorTimes, testColorIndices, color_time, sseScaledTimes.m128_f32);

#define mynearf(a,b) ((a)-(b) >= -0.001 && (a)-(b) <= 0.001) // SSE recip is rather inaccurate, give some leeway

		for (index=0; index<4; index++)
		{
			assert(iColorIndices[index] == testColorIndices[index]);
			assert(mynearf(vColorTimes[index], testColorTimes[index]));
		}
	}
#endif // verify
}

static __forceinline void calcLerpColorRGBSSE(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, __m128 time, __m128 fJitter0, __m128 fJitter1, F32 fHueShift)
{
	Vec3 vBefore =
	{
		value[iIndices[0]-1][0] + jitter[iIndices[0]-1][0]*fJitter0.m128_f32[0],
		value[iIndices[1]-1][1] + jitter[iIndices[1]-1][1]*fJitter0.m128_f32[1],
		value[iIndices[2]-1][2] + jitter[iIndices[2]-1][2]*fJitter0.m128_f32[2],
	};

	Vec3 vAfter =
	{
		value[iIndices[0]][0] + jitter[iIndices[0]][0]*fJitter1.m128_f32[0],
		value[iIndices[1]][1] + jitter[iIndices[1]][1]*fJitter1.m128_f32[1],
		value[iIndices[2]][2] + jitter[iIndices[2]][2]*fJitter1.m128_f32[2],
	};

	fpHSVToRGB(vBefore, vBefore, fHueShift);
	fpHSVToRGB(vAfter, vAfter, fHueShift);


	result[0] = lerp(vBefore[0],
					 vAfter[0],
					 time.m128_f32[0]);
	result[1] = lerp(vBefore[1],
					 vAfter[1],
					 time.m128_f32[1]);
	result[2] = lerp(vBefore[2],
					 vAfter[2],
					 time.m128_f32[2]);
	result[3] = lerp(value[iIndices[3]-1][3] + jitter[iIndices[3]-1][3]*fJitter0.m128_f32[3],
					 value[iIndices[3]][3] + jitter[iIndices[3]][3]*fJitter1.m128_f32[3],
					 time.m128_f32[3]);
}

static __forceinline void calcLerpColorHSVSSE(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, __m128 time, __m128 fJitter0, __m128 fJitter1)
{
	result[0] = lerp(value[iIndices[0]-1][0] + jitter[iIndices[0]-1][0]*fJitter0.m128_f32[0],
		value[iIndices[0]][0] + jitter[iIndices[0]][0]*fJitter1.m128_f32[0],
		time.m128_f32[0]);
	result[1] = lerp(value[iIndices[1]-1][1] + jitter[iIndices[1]-1][1]*fJitter0.m128_f32[1],
		value[iIndices[1]][1] + jitter[iIndices[1]][1]*fJitter1.m128_f32[1],
		time.m128_f32[1]);
	result[2] = lerp(value[iIndices[2]-1][2] + jitter[iIndices[2]-1][2]*fJitter0.m128_f32[2],
		value[iIndices[2]][2] + jitter[iIndices[2]][2]*fJitter1.m128_f32[2],
		time.m128_f32[2]);
	result[3] = lerp(value[iIndices[3]-1][3] + jitter[iIndices[3]-1][3]*fJitter0.m128_f32[3],
		value[iIndices[3]][3] + jitter[iIndices[3]][3]*fJitter1.m128_f32[3],
		time.m128_f32[3]);
// 	__m128 v0 = {value[iIndices[0]-1][0], value[iIndices[1]-1][1], value[iIndices[2]-1][2], value[iIndices[3]-1][3]};
// 	__m128 v0Jitteradd = {jitter[iIndices[0]-1][0], jitter[iIndices[1]-1][1], jitter[iIndices[2]-1][2], jitter[iIndices[3]-1][3]};
// 	__m128 v1 = {value[iIndices[0]][0], value[iIndices[1]][1], value[iIndices[2]][2], value[iIndices[3]][3]};
// 	__m128 v1Jitteradd = {jitter[iIndices[0]][0], jitter[iIndices[1]][1], jitter[iIndices[2]][2], jitter[iIndices[3]][3]};
// 	__m128 ones = _mm_set1_ps(1);
// 	__m128 t;
// 	t = _mm_mul_ps(v0Jitteradd, fJitter0);
// 	v0 = _mm_add_ps(v0, t);
// 	t = _mm_mul_ps(v1Jitteradd, fJitter1);
// 	v1 = _mm_add_ps(v1, t);
// 	v1 = _mm_mul_ps(v1, time);
// 	time = _mm_sub_ps(ones, time);
// 	v0 = _mm_mul_ps(v0, time);
// 	t = _mm_add_ps(v0, v1);
// 	_mm_storeu_ps(result, t);
}

static __m128 streakSSE(int corner, float scale_x, float scale_y, __m128 streakDir)
{
	__m128 result;
	static const Vec2 cornMult[] = {
		{ -1.0f, 1.0f },
		{ -1.0f, 0.0f },
		{  1.0f, 0.0f },
		{  1.0f, 1.0f }
	};
	__m128 side_x;
	__m128 side_y;
	__m128 t, t2;

	setVec4(side_x.m128_f32, streakDir.m128_f32[1], -streakDir.m128_f32[0], 0.0f, 0.0f);
	t = _mm_mul_ps(side_x, side_x);
	t2 = _mm_shuffle_ps(t, t, _MM_SHUFFLE(1, 0, 1, 1));
	t = _mm_add_ss(t2, t);
	if (t.m128_f32[0] > 0)
	{
		t = _mm_rsqrt_ss(t);
		t = _mm_shuffle_ps(t, t, _MM_SHUFFLE(0, 0, 0, 0));
		side_x = _mm_mul_ps(side_x, t);
	}
	scaleVec3(side_x.m128_f32, 0.5f * scale_x, side_x.m128_f32);
	scaleVec3(side_x.m128_f32, cornMult[corner][0], side_x.m128_f32);

	scaleVec3(streakDir.m128_f32, scale_y, side_y.m128_f32);
	scaleVec3(side_y.m128_f32, cornMult[corner][1], side_y.m128_f32);

	addVec3(side_x.m128_f32, side_y.m128_f32, result.m128_f32);
	return result;
}


#endif

/// Run the particle system vertex shader on the CPU on PARTICLE-SET,
/// and store the output in OUT-VERTS.
void rxbxFastParticlesVertexShader(RdrPrimitiveTexturedVertex *out_verts, const RdrDrawableFastParticles *particle_set)
{
	PERFINFO_AUTO_START_FUNC();
	{

	FastParticleMode mode = fapaMode(particle_set);
	
	// set up constants
	const Vec4* color = &particle_set->constants[0];
	const Vec4* color_jitter = &particle_set->constants[4];
	const Vec4* color_time = &particle_set->constants[8];
	const Vec4* modulate_color = &particle_set->modulate_color;

	const Vec4* scale_rot = &particle_set->constants[12];
	const Vec4* scale_rot_jitter = &particle_set->constants[16];
	const Vec4* scale_rot_time = &particle_set->constants[20];
	const float* color_time_scale = particle_set->constants[24];
	const float* scale_rot_time_scale = particle_set->constants[25];
	const float* tex_params = particle_set->constants[26];
	const float* spin_integrals = particle_set->constants[27];
	const float* scroll = particle_set->constants[28];
	const float* anim_params = scroll;
	const float* more_params = particle_set->constants[29];
	const float* time_info = particle_set->time_info;
	const float* scale_info = particle_set->scale_info;
	const SkinningMat4* at_nodes = particle_set->bone_infos;
	
	const Vec3* viewmat;
	const Vec3* inv_viewmat;
	const float* campos;
#ifdef USE_SSE
	__m128 sse_color_time[4];
	__m128 color_time_delta[3];
	__m128 sse_scale_rot_time[4];
	__m128 scale_rot_time_delta[3];
	__m128 sse_inv_viewmat[3];
	__m128 sse_campos;
	__m128 sse_more_params0;
	__m128 *sse_at_nodes;
	int i, j;
	U32 ui;
	sseVec4 sse_color_time_scale;
	sseVec4 sse_scale_rot_time_scale;
// 	__m128 color_time_transpose[4];
// 	color_time_transpose[0] =_mm_loadu_ps(color_time[0]);
// 	color_time_transpose[1] =_mm_loadu_ps(color_time[1]);
// 	color_time_transpose[2] =_mm_loadu_ps(color_time[2]);
// 	color_time_transpose[3] =_mm_loadu_ps(color_time[3]);
// 	_MM_TRANSPOSE4_PS(color_time_transpose[0], color_time_transpose[1], color_time_transpose[2], color_time_transpose[3]);
	sse_color_time[1] = _mm_loadu_ps(color_time[1]);
	sse_color_time[2] = _mm_loadu_ps(color_time[2]);
	sse_color_time[3] = _mm_loadu_ps(color_time[3]);
	for (j=0; j<3; j++)
	{
		for (i=0; i<4; i++)
		{
			color_time_delta[j].m128_f32[i] = color_time[j+1][i] - color_time[j][i];
			// avoid divide by 0 if it's an issue:
			//if (color_time_delta[j].m128_f32[i] == 0)
			//	color_time_delta[j].m128_f32[i] = 0.0001;
		}
		color_time_delta[j] = _mm_rcp_ps(color_time_delta[j]);
	}
	sse_color_time_scale.m = _mm_loadu_ps(color_time_scale);

	sse_scale_rot_time[1] = _mm_loadu_ps(scale_rot_time[1]);
	sse_scale_rot_time[2] = _mm_loadu_ps(scale_rot_time[2]);
	sse_scale_rot_time[3] = _mm_loadu_ps(scale_rot_time[3]);
	for (j=0; j<3; j++)
	{
		for (i=0; i<4; i++)
		{
			scale_rot_time_delta[j].m128_f32[i] = scale_rot_time[j+1][i] - scale_rot_time[j][i];
			// avoid divide by 0 if it's an issue:
			//if (scale_rot_time_delta[j].m128_f32[i] == 0)
			//	scale_rot_time_delta[j].m128_f32[i] = 0.0001;
		}
		scale_rot_time_delta[j] = _mm_rcp_ps(scale_rot_time_delta[j]);
	}
	sse_scale_rot_time_scale.m = _mm_loadu_ps(scale_rot_time_scale);

	sse_more_params0 = _mm_set1_ps(more_params[0]);


	{
		// Transpose at_nodes
		sse_at_nodes = _alloca(sizeof(__m128)*4 * particle_set->num_bones);
		for (ui=0; ui<particle_set->num_bones; ui++)
		{
			__m128 *mat = &sse_at_nodes[ui*4];
			memcpy(mat, particle_set->bone_infos[ui], sizeof(Vec4) * 3);
			setVec4(mat[3].m128_f32, 0, 0, 0, 0);
			_MM_TRANSPOSE4_PS(mat[0], mat[1], mat[2], mat[3]);
		}
	}
#endif


	if (current_state->width_2d != 0)
	{
		viewmat = unitmat;
		inv_viewmat = unitmat;
		campos = zerovec4;
	}
	else
	{
		viewmat = current_state->viewmat4;
		inv_viewmat = current_state->inv_viewmat;
		campos = current_state->camera_pos_ws;
	}

#ifdef USE_SSE
	sse_inv_viewmat[0] = _mm_loadu_ps(inv_viewmat[0]);
	sse_inv_viewmat[1] = _mm_loadu_ps(inv_viewmat[1]);
	sse_inv_viewmat[2] = _mm_loadu_ps(inv_viewmat[2]);
	sse_campos = _mm_loadu_ps(campos);
#endif

	if(mode != FAPA_STREAK) 
    {
		int vert_it=0;
		unsigned int part_it;
		for (part_it=0; part_it < particle_set->particle_count; part_it++)
		{
			int corner_it;
			const RdrFastParticleVertex *vIn = &particle_set->verts[vert_it];
			RdrPrimitiveTexturedVertex vOutBase;

			int iJitterIndex = vIn->seed * 65536;
#ifdef USE_SSE
			sseVec3 vOutBasePos;
			__declspec(align(16)) union
			{
				Vec4 v[5];
				__m128 sse[5];
			} jitter;
			sseVec4 vColorTimes;
#else
			struct {
				Vec4 v[5];
			} jitter;
			struct {
				Vec4 v;
			} vColorTimes;
#endif

			float fAbsTime = time_info[0] - vIn->time; // abs time in seconds
			float fNormalTime = fAbsTime * time_info[1]; // normalized over the lifespan
			IVec4 iColorIndices;
			Vec4 vScaleTimes;
			IVec4 iScaleIndices;
			IVec4 iOnes = { 1, 1, 1, 1 };
			Vec4 vScaleRot;
			float posVS_z;
			float fNearPlaneAlpha;

			float fRot;
			Vec2 vScale;
			Vec2 effScroll;

			setVec4(iColorIndices, 5, 5, 5, 5);
			setVec4(iScaleIndices, 5, 5, 5, 5);

			{
#ifndef USE_SSE
				Vec4 fScaledTimes;

				// findIndicesColor
				fScaledTimes[0] = frac(color_time_scale[0] * fNormalTime);
				fScaledTimes[1] = frac(color_time_scale[1] * fNormalTime);
				fScaledTimes[2] = frac(color_time_scale[2] * fNormalTime);
				fScaledTimes[3] = frac(color_time_scale[3] * fNormalTime);

				// Could greatly speed up by removing divide from within findIndex (call findIndexWithDelta instead)
				findIndex4(vColorTimes.v, iColorIndices, color_time, fScaledTimes);

				// findIndicesScale
				fScaledTimes[0] = frac(scale_rot_time_scale[0] * fNormalTime);
				fScaledTimes[1] = frac(scale_rot_time_scale[1] * fNormalTime);
				fScaledTimes[2] = frac(scale_rot_time_scale[2] * fNormalTime);
				fScaledTimes[3] = frac(scale_rot_time_scale[3] * fNormalTime);

				findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 0);
				if (mode != FAPA_LINKSCALE)
				{
					findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 1);
				}
				else
				{
					iScaleIndices[1] = 1;
					vScaleTimes[1] = 0.0f;
				}
				{
					findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 2);
					findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 3);
				}
#else

				__m128 sseNormalTime;
				__m128 sseScaledTimes;
				sseNormalTime = _mm_set1_ps(fNormalTime);

				if (sse2Available)
				{
					sseScaledTimes = _mm_mul_ps(sse_color_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse2(sseScaledTimes);
				} else {
					sseScaledTimes = _mm_mul_ps(sse_color_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse(sseScaledTimes);
				}

				//findIndex4(vColorTimes, iColorIndices, color_time, sseScaledTimes.m128_f32);
				//findIndex4SSE(vColorTimes, iColorIndices, color_time, color_time_delta, sseScaledTimes, sse_color_time);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 2);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 3);


				// findIndicesScale
				if (sse2Available)
				{
					sseScaledTimes = _mm_mul_ps(sse_scale_rot_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse2(sseScaledTimes);
				} else {
					sseScaledTimes = _mm_mul_ps(sse_scale_rot_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse(sseScaledTimes);
				}

				if (mode == FAPA_NORMAL)
				{
					// Need everything
					// For some reason in the case above, findIndex4SSE is notably faster...
					//   but here, it's notably slower... -_-
					// But findIndexWithDelta is faster than both it seems.
					//findIndex4SSE(vScaleTimes, iScaleIndices, scale_rot_time, scale_rot_time_delta, sseScaledTimes, sse_scale_rot_time);
					//findIndex4(vScaleTimes, iScaleIndices, scale_rot_time, sseScaledTimes.m128_f32);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 2);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 3);

				} else
				{
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
					if (mode != FAPA_LINKSCALE)
					{
						findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
					}
					else
					{
						iScaleIndices[1] = 1;
						vScaleTimes[1] = 0.0f;
					}
					{
						findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 2);
						findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 3);
					}
				}

#endif // USE_SSE

			}

#ifndef USE_SSE
			getJitter(jitter.v[0], iJitterIndex, iColorIndices);
			getJitter(jitter.v[1], iJitterIndex+1, iColorIndices);
			getJitter(jitter.v[2], iJitterIndex+16, iScaleIndices);
			getJitter(jitter.v[3], iJitterIndex+17, iScaleIndices);
			iJitterIndex += 32;
			jitter.v[4][2] = randomF32BlornFixedSeed(iJitterIndex + (1 + 8));
			jitter.v[4][3] = randomF32BlornFixedSeed(iJitterIndex + (1 + 12));

			if (particle_set->rgb_blend)
				calcLerpColorRGB(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.v, jitter.v[0], jitter.v[1], time_info[2]);
			else
				calcLerpColorHSV(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.v, jitter.v[0], jitter.v[1]);
#else
			jitter.sse[0] = getJitterSSE(iJitterIndex, iColorIndices);
			jitter.sse[1] = getJitterSSE(iJitterIndex+1, iColorIndices);
			jitter.sse[2] = getJitterSSE(iJitterIndex+16, iScaleIndices);
			jitter.sse[3] = getJitterSSE(iJitterIndex+17, iScaleIndices);
			iJitterIndex += 32;
			jitter.v[4][2] = randomF32BlornFixedSeed(iJitterIndex + (1 + 8));
			jitter.v[4][3] = randomF32BlornFixedSeed(iJitterIndex + (1 + 12));

			if (particle_set->rgb_blend)
				calcLerpColorRGBSSE(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.m, jitter.sse[0], jitter.sse[1], time_info[2]);
			else
				calcLerpColorHSVSSE(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.m, jitter.sse[0], jitter.sse[1]);
#endif


			if (!particle_set->rgb_blend)
				fpHSVToRGB(vOutBase.color, vOutBase.color, time_info[2]);

			// Modulate color.
			mulVecVec3(vOutBase.color, *modulate_color, vOutBase.color);

			vOutBase.color[3] *= time_info[3] * vIn->alpha;

			calcLerpScale(mode, vScaleRot, scale_rot, scale_rot_jitter, iScaleIndices, vScaleTimes, jitter.v[2], jitter.v[3]);
#ifndef USE_SSE
			mulVecMat34(vIn->point, at_nodes[vIn->corner_nodeidx[1]], vOutBase.pos);
			posVS_z = (vOutBase.pos[0] * viewmat[0][2] + vOutBase.pos[1] * viewmat[1][2] + vOutBase.pos[2] * viewmat[2][2] + viewmat[3][2]);
#else
			//mulVecMat34(vIn->point, sse_at_nodes[vIn->corner_nodeidx[1]*3], vOutBase.pos);
			//vOutBasePos.m = _mm_loadu_ps(vOutBase.pos);
			{
				const __m128 *mat = &sse_at_nodes[vIn->corner_nodeidx[1]*4];
// 				vOutBasePos.v[0] = vIn->point[0]*mat[0].m128_f32[0]+vIn->point[1]*mat[0].m128_f32[1]+vIn->point[2]*mat[0].m128_f32[2] + mat[0].m128_f32[3];
// 				vOutBasePos.v[1] = vIn->point[0]*mat[1].m128_f32[0]+vIn->point[1]*mat[1].m128_f32[1]+vIn->point[2]*mat[1].m128_f32[2] + mat[1].m128_f32[3];
// 				vOutBasePos.v[2] = vIn->point[0]*mat[2].m128_f32[0]+vIn->point[1]*mat[2].m128_f32[1]+vIn->point[2]*mat[2].m128_f32[2] + mat[2].m128_f32[3];
				__m128 vin_point = _mm_loadu_ps(vIn->point);
				vOutBasePos.m = mulVecMat3SSE(vin_point, mat);
				vOutBasePos.m = _mm_add_ps(vOutBasePos.m, mat[3]);
			}
			posVS_z = (vOutBasePos.v[0] * viewmat[0][2] + vOutBasePos.v[1] * viewmat[1][2] + vOutBasePos.v[2] * viewmat[2][2] + viewmat[3][2]);
#endif


			fNearPlaneAlpha = (-posVS_z + scale_info[3]) * 0.5f;
			vOutBase.color[3] *= CLAMP(fNearPlaneAlpha, 0.0f, 1.0f);

			effScroll[0] = (scroll[0] + scroll[2] * jitter.v[4][2])  * fAbsTime;
			effScroll[1] = (scroll[1] + scroll[3] * jitter.v[4][3])  * fAbsTime;

			{
#ifdef USE_SSE
				float fSpinTime = fracsse(fNormalTime * scale_rot_time_scale[3] - scale_rot_time[iScaleIndices[3]-1][3]);
#else
				float fSpinTime = frac(fNormalTime * scale_rot_time_scale[3] - scale_rot_time[iScaleIndices[3]-1][3]);
#endif
				fRot = vScaleRot[2] + spin_integrals[iScaleIndices[3]-1] + (vScaleRot[3]*0.5f + scale_rot[iScaleIndices[3]-1][3] + scale_rot_jitter[iScaleIndices[3]-1][3] * jitter.v[2][3]) * fSpinTime;

				if (mode == FAPA_LINKSCALE)
				{
					setVec2(vScale, vScaleRot[0] * scale_info[0], vScaleRot[0] * scale_info[0]);
				}
				else
				{
					setVec2(vScale, vScaleRot[0] * scale_info[0], vScaleRot[1] * scale_info[1]);
				}

				scaleVec2(vScale, lerp(1.0f, -posVS_z * 0.01, scale_info[2]), vScale);
			}

			for (corner_it = 0; corner_it < 4; corner_it++, vert_it++)
			{
				RdrPrimitiveTexturedVertex *vOut = &out_verts[vert_it];
#ifdef USE_SSE
				sseVec3 vOutPos;
				vOutPos.m = vOutBasePos.m;
#else
				struct {
					Vec3 v;
				} vOutPos;
				copyVec3(vOutBase.pos, vOutPos.v);
#endif
				*vOut = vOutBase;

				{
					sseVec4 offset;
					scaleAndRotate(offset.v, getCornerPos(corner_it), vScale, fRot);

					// Since the vertex shader is still doing the modelview
					// transformation, the offsets must be transformed.
					{
#ifndef USE_SSE
						Vec3 transformed_offset;
						mulVecMat3(offset.v, inv_viewmat, transformed_offset);
						addVec3(vOutPos.v, transformed_offset, vOutPos.v);
#else
 						__m128 t;
						__m128 t2, t3;
						t3 = _mm_shuffle_ps(offset.m, offset.m, _MM_SHUFFLE(0, 0, 0, 0));
						t = _mm_mul_ps(t3, sse_inv_viewmat[0]);
						t3 = _mm_shuffle_ps(offset.m, offset.m, _MM_SHUFFLE(1, 1, 1, 1));
						t2 = _mm_mul_ps(t3, sse_inv_viewmat[1]);
						t = _mm_add_ps(t, t2);
						t3 = _mm_shuffle_ps(offset.m, offset.m, _MM_SHUFFLE(2, 2, 2, 2));
						t2 = _mm_mul_ps(t3, sse_inv_viewmat[2]);
						t = _mm_add_ps(t, t2);
//						t = mulVecMat3SSE(offset.m, sse_inv_viewmat); // ends up 2 instructions longer :(
 						vOutPos.m = _mm_add_ps(vOutPos.m, t);
#endif
					}
				}
				if (more_params[0])
				{
#ifndef USE_SSE
					Vec3 vNormalViewPos;
					subVec3(vOutPos.v, campos, vNormalViewPos);
					normalVec3(vNormalViewPos);
					vOutPos.v[0] -= vNormalViewPos[0] * more_params[0]; // tighten up
					vOutPos.v[1] -= vNormalViewPos[1] * more_params[0];
					vOutPos.v[2] -= vNormalViewPos[2] * more_params[0];
#else
					__m128 v;
					v = _mm_sub_ps(vOutPos.m, sse_campos);
					//normalVec3(v.m128_f32);
					{
						__m128 v2;
						v2 = _mm_mul_ps(v, v);
						//v2.m128_f32[0] = v2.m128_f32[0] + v2.m128_f32[1] + v2.m128_f32[2];
						{
							__m128 v3;
							v3 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(1, 1, 1, 1));
							v3 = _mm_add_ss(v2, v3);
							v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(2, 2, 2, 2));
							v2 = _mm_add_ss(v2, v3);
						}

						v2 = _mm_rsqrt_ss(v2);
						v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(0, 0, 0, 0));
						v = _mm_mul_ps(v, v2);
					}
					v = _mm_mul_ps(v, sse_more_params0);
					vOutPos.m = _mm_sub_ps(vOutPos.m, v);
#endif
				}

				copyVec3(vOutPos.v, vOut->pos);

				if(particle_set->animated_texture) {

					F32 fInvAnimRowCols = 1.0f / (anim_params[1] ? anim_params[1] : 1.0f);
					F32 fAnimNumFrames = anim_params[0];
					F32 fAnimFrame = floor(fNormalTime * fAnimNumFrames);

					if (tex_params[2]) {
						getQuadCornerTex(vOut->texcoord, corner_it, randomBoolBlornFixedSeed(iJitterIndex+1), randomBoolBlornFixedSeed(iJitterIndex+(1+4)));
					} else {
						copyVec2(getCornerTex(corner_it), vOut->texcoord);
					}

					vOut->texcoord[0] *= fInvAnimRowCols;
					vOut->texcoord[1] *= fInvAnimRowCols;
					vOut->texcoord[1] += floor(fAnimFrame * fInvAnimRowCols) * fInvAnimRowCols;
					vOut->texcoord[0] += frac(fAnimFrame * fInvAnimRowCols);

				} else {

					if (tex_params[2])
					{
						getQuadCornerTex(vOut->texcoord, corner_it, randomBoolBlornFixedSeed(iJitterIndex+1), randomBoolBlornFixedSeed(iJitterIndex+(1+4)));
					}
					else
					{
						copyVec2(getCornerTex(corner_it), vOut->texcoord);
					}
					addVec2(vOut->texcoord, effScroll, vOut->texcoord);

					if (tex_params[0])
						if (randomBoolBlornFixedSeed(iJitterIndex+16))
							vOut->texcoord[0] = 1 - vOut->texcoord[0];
					if (tex_params[1])
						if (randomBoolBlornFixedSeed(iJitterIndex+17))
							vOut->texcoord[1] = 1 - vOut->texcoord[1];
				}

			}
		}
	}
    else // (mode != FAPA_STREAK)
    {
		int vert_it=0;
		unsigned int part_it;
		for (part_it=0; part_it < particle_set->particle_count; part_it++)
		{
			int corner_it;
			const RdrFastParticleStreakVertex *vIn = &particle_set->streakverts[vert_it];
			RdrPrimitiveTexturedVertex vOutBase;

			int iJitterIndex = vIn->seed * 65536;
#ifdef USE_SSE
			sseVec3 vOutBasePos;
			__declspec(align(16)) union
			{
				Vec4 v[5];
				__m128 sse[5];
			} jitter;
			sseVec4 vColorTimes;
#else
			struct {
				Vec4 v[5];
			} jitter;
			struct {
				Vec4 v;
			} vColorTimes;
#endif

			float fAbsTime = time_info[0] - vIn->time; // abs time in seconds
			float fNormalTime = fAbsTime * time_info[1]; // normalized over the lifespan
			IVec4 iColorIndices;
			Vec4 vScaleTimes;
			IVec4 iScaleIndices;
			IVec4 iOnes = { 1, 1, 1, 1 };
			Vec4 vScaleRot;
			float posVS_z;
			float fNearPlaneAlpha;

			sseVec3 vVSStreakDir;
			Vec2 effScroll;

			setVec4(iColorIndices, 5, 5, 5, 5);
			setVec4(iScaleIndices, 5, 5, 5, 5);

			{
#ifndef USE_SSE
				Vec4 fScaledTimes;

				// findIndicesColor
				fScaledTimes[0] = frac(color_time_scale[0] * fNormalTime);
				fScaledTimes[1] = frac(color_time_scale[1] * fNormalTime);
				fScaledTimes[2] = frac(color_time_scale[2] * fNormalTime);
				fScaledTimes[3] = frac(color_time_scale[3] * fNormalTime);

				// Could greatly speed up by removing divide from within findIndex (call findIndexWithDelta instead)
				findIndex4(vColorTimes.v, iColorIndices, color_time, fScaledTimes);

				// findIndicesScale
				fScaledTimes[0] = frac(scale_rot_time_scale[0] * fNormalTime);
				fScaledTimes[1] = frac(scale_rot_time_scale[1] * fNormalTime);
				fScaledTimes[2] = frac(scale_rot_time_scale[2] * fNormalTime);
				fScaledTimes[3] = frac(scale_rot_time_scale[3] * fNormalTime);

				findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 0);
				if (mode != FAPA_LINKSCALE)
				{
					findIndex(vScaleTimes, iScaleIndices, scale_rot_time, fScaledTimes, 1);
				}
				else
				{
					iScaleIndices[1] = 1;
					vScaleTimes[1] = 0.0f;
				}
				{
					iScaleIndices[2] = iScaleIndices[3] = 1;
					vScaleTimes[2] = vScaleTimes[3] = 0.0f;
				}
#else

				__m128 sseNormalTime;
				__m128 sseScaledTimes;
				sseNormalTime = _mm_set1_ps(fNormalTime);

				if (sse2Available)
				{
					sseScaledTimes = _mm_mul_ps(sse_color_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse2(sseScaledTimes);
				} else {
					sseScaledTimes = _mm_mul_ps(sse_color_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse(sseScaledTimes);
				}

				//findIndex4(vColorTimes, iColorIndices, color_time, sseScaledTimes.m128_f32);
				//findIndex4SSE(vColorTimes, iColorIndices, color_time, color_time_delta, sseScaledTimes, sse_color_time);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 2);
				findIndexWithDelta(vColorTimes.v, iColorIndices, color_time, (Vec4*)&color_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 3);


				// findIndicesScale
				if (sse2Available)
				{
					sseScaledTimes = _mm_mul_ps(sse_scale_rot_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse2(sseScaledTimes);
				} else {
					sseScaledTimes = _mm_mul_ps(sse_scale_rot_time_scale.m, sseNormalTime);
					sseScaledTimes = frac4sse(sseScaledTimes);
				}

				if (mode == FAPA_NORMAL)
				{
					// Need everything
					// For some reason in the case above, findIndex4SSE is notably faster...
					//   but here, it's notably slower... -_-
					// But findIndexWithDelta is faster than both it seems.
					//findIndex4SSE(vScaleTimes, iScaleIndices, scale_rot_time, scale_rot_time_delta, sseScaledTimes, sse_scale_rot_time);
					//findIndex4(vScaleTimes, iScaleIndices, scale_rot_time, sseScaledTimes.m128_f32);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 2);
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 3);

				} else
				{
					findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 0);
					if (mode != FAPA_LINKSCALE)
					{
						findIndexWithDelta(vScaleTimes, iScaleIndices, scale_rot_time, (Vec4*)&scale_rot_time_delta[0].m128_f32[0], sseScaledTimes.m128_f32, 1);
					}
					else
					{
						iScaleIndices[1] = 1;
						vScaleTimes[1] = 0.0f;
					}
					{
						iScaleIndices[2] = iScaleIndices[3] = 1;
						vScaleTimes[2] = vScaleTimes[3] = 0.0f;
					}
				}

#endif // USE_SSE

			}

#ifndef USE_SSE
			getJitter(jitter.v[0], iJitterIndex, iColorIndices);
			getJitter(jitter.v[1], iJitterIndex+1, iColorIndices);
			getJitter(jitter.v[2], iJitterIndex+16, iScaleIndices);
			getJitter(jitter.v[3], iJitterIndex+17, iScaleIndices);
			iJitterIndex += 32;
			jitter.v[4][2] = randomF32BlornFixedSeed(iJitterIndex + (1 + 8));
			jitter.v[4][3] = randomF32BlornFixedSeed(iJitterIndex + (1 + 12));

			if (particle_set->rgb_blend)
				calcLerpColorRGB(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.v, jitter.v[0], jitter.v[1], time_info[2]);
			else
				calcLerpColorHSV(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.v, jitter.v[0], jitter.v[1]);
#else
			jitter.sse[0] = getJitterSSE(iJitterIndex, iColorIndices);
			jitter.sse[1] = getJitterSSE(iJitterIndex+1, iColorIndices);
			jitter.sse[2] = getJitterSSE(iJitterIndex+16, iScaleIndices);
			jitter.sse[3] = getJitterSSE(iJitterIndex+17, iScaleIndices);
			iJitterIndex += 32;
			jitter.v[4][2] = randomF32BlornFixedSeed(iJitterIndex + (1 + 8));
			jitter.v[4][3] = randomF32BlornFixedSeed(iJitterIndex + (1 + 12));

			if (particle_set->rgb_blend)
				calcLerpColorRGBSSE(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.m, jitter.sse[0], jitter.sse[1], time_info[2]);
			else
				calcLerpColorHSVSSE(vOutBase.color, color, color_jitter, iColorIndices, vColorTimes.m, jitter.sse[0], jitter.sse[1]);
#endif

			if (!particle_set->rgb_blend)
				fpHSVToRGB(vOutBase.color, vOutBase.color, time_info[2]);
			vOutBase.color[3] *= time_info[3] * vIn->alpha;

			// Modulate color.
			mulVecVec3(vOutBase.color, *modulate_color, vOutBase.color);

			calcLerpScale(mode, vScaleRot, scale_rot, scale_rot_jitter, iScaleIndices, vScaleTimes, jitter.v[2], jitter.v[3]);
#ifndef USE_SSE
			mulVecMat34(vIn->point, at_nodes[vIn->corner_nodeidx[1]], vOutBase.pos);
			posVS_z = (vOutBase.pos[0] * viewmat[0][2] + vOutBase.pos[1] * viewmat[1][2] + vOutBase.pos[2] * viewmat[2][2] + viewmat[3][2]);
#else
			//mulVecMat34(vIn->point, sse_at_nodes[vIn->corner_nodeidx[1]*3], vOutBase.pos);
			//vOutBasePos.m = _mm_loadu_ps(vOutBase.pos);
			{
				const __m128 *mat = &sse_at_nodes[vIn->corner_nodeidx[1]*4];
// 				vOutBasePos.v[0] = vIn->point[0]*mat[0].m128_f32[0]+vIn->point[1]*mat[0].m128_f32[1]+vIn->point[2]*mat[0].m128_f32[2] + mat[0].m128_f32[3];
// 				vOutBasePos.v[1] = vIn->point[0]*mat[1].m128_f32[0]+vIn->point[1]*mat[1].m128_f32[1]+vIn->point[2]*mat[1].m128_f32[2] + mat[1].m128_f32[3];
// 				vOutBasePos.v[2] = vIn->point[0]*mat[2].m128_f32[0]+vIn->point[1]*mat[2].m128_f32[1]+vIn->point[2]*mat[2].m128_f32[2] + mat[2].m128_f32[3];
				__m128 vin_point = _mm_loadu_ps(vIn->point);
				vOutBasePos.m = mulVecMat3SSE(vin_point, mat);
				vOutBasePos.m = _mm_add_ps(vOutBasePos.m, mat[3]);
			}
			posVS_z = (vOutBasePos.v[0] * viewmat[0][2] + vOutBasePos.v[1] * viewmat[1][2] + vOutBasePos.v[2] * viewmat[2][2] + viewmat[3][2]);
#endif


			fNearPlaneAlpha = (-posVS_z + scale_info[3]) * 0.5f;
			vOutBase.color[3] *= CLAMP(fNearPlaneAlpha, 0.0f, 1.0f);

			effScroll[0] = (scroll[0] + scroll[2] * jitter.v[4][2])  * fAbsTime;
			effScroll[1] = (scroll[1] + scroll[3] * jitter.v[4][3])  * fAbsTime;

			{
				Vec3 streakDirWS;
				mulVecW0Mat34(vIn->streak_dir, at_nodes[vIn->corner_nodeidx[1]], streakDirWS);
				mulVecMat3(streakDirWS, viewmat, vVSStreakDir.v);
			}

			for (corner_it = 0; corner_it < 4; corner_it++, vert_it++)
			{
				RdrPrimitiveTexturedVertex *vOut = &out_verts[vert_it];
#ifdef USE_SSE
				sseVec3 vOutPos;
				vOutPos.m = vOutBasePos.m;
#else
				struct {
					Vec3 v;
				} vOutPos;
				copyVec3(vOutBase.pos, vOutPos.v);
#endif
				*vOut = vOutBase;

				{
#ifndef USE_SSE
					Vec3 streakVS;
					Vec3 streakWS;
					streak(streakVS, corner_it, vScaleRot[0] * scale_info[0], vScaleRot[1] * scale_info[1], vVSStreakDir.v);
					mulVecMat3(streakVS, inv_viewmat, streakWS);
					addVec3(vOutPos.v, streakWS, vOutPos.v);
#else
					__m128 streakVS;
					__m128 streakWS;
					streakVS = streakSSE(corner_it, vScaleRot[0] * scale_info[0], vScaleRot[1] * scale_info[1], vVSStreakDir.m);
					streakWS = mulVecMat3SSE(streakVS, sse_inv_viewmat);
					vOutPos.m = _mm_add_ps(vOutPos.m, streakWS);
#endif
				}
				if (more_params[0])
				{
#ifndef USE_SSE
					Vec3 vNormalViewPos;
					subVec3(vOutPos.v, campos, vNormalViewPos);
					normalVec3(vNormalViewPos);
					vOutPos.v[0] -= vNormalViewPos[0] * more_params[0]; // tighten up
					vOutPos.v[1] -= vNormalViewPos[1] * more_params[0];
					vOutPos.v[2] -= vNormalViewPos[2] * more_params[0];
#else
					__m128 v;
					v = _mm_sub_ps(vOutPos.m, sse_campos);
					//normalVec3(v.m128_f32);
					{
						__m128 v2;
						v2 = _mm_mul_ps(v, v);
						//v2.m128_f32[0] = v2.m128_f32[0] + v2.m128_f32[1] + v2.m128_f32[2];
						{
							__m128 v3;
							v3 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(1, 1, 1, 1));
							v3 = _mm_add_ss(v2, v3);
							v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(2, 2, 2, 2));
							v2 = _mm_add_ss(v2, v3);
						}

						v2 = _mm_rsqrt_ss(v2);
						v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(0, 0, 0, 0));
						v = _mm_mul_ps(v, v2);
					}
					v = _mm_mul_ps(v, sse_more_params0);
					vOutPos.m = _mm_sub_ps(vOutPos.m, v);
#endif
				}

				copyVec3(vOutPos.v, vOut->pos);

				if(particle_set->animated_texture) {

					F32 fAnimRowCols = anim_params[1];
					F32 fAnimNumFrames = anim_params[0];
					F32 fAnimFrame = floor(fNormalTime * fAnimNumFrames);

					copyVec2(getCornerTex(corner_it), vOut->texcoord);

					if(fAnimRowCols) {
						vOut->texcoord[0] *= 1.0/fAnimRowCols;
						vOut->texcoord[1] *= 1.0/fAnimRowCols;
						vOut->texcoord[1] += floor(fAnimFrame / fAnimRowCols) / fAnimRowCols;
						vOut->texcoord[1] += frac(fAnimFrame / fAnimRowCols);
					}

				} else {

					if (tex_params[2])
					{
						getQuadCornerTex(vOut->texcoord, corner_it, randomBoolBlornFixedSeed(iJitterIndex+1), randomBoolBlornFixedSeed(iJitterIndex+(1+4)));
					}
					else
					{
						copyVec2(getCornerTex(corner_it), vOut->texcoord);
					}
					addVec2(vOut->texcoord, effScroll, vOut->texcoord);

					if (tex_params[0])
						if (randomBoolBlornFixedSeed(iJitterIndex+16))
							vOut->texcoord[0] = 1 - vOut->texcoord[0];
					if (tex_params[1])
						if (randomBoolBlornFixedSeed(iJitterIndex+17))
							vOut->texcoord[1] = 1 - vOut->texcoord[1];
				}
			}
		}
	} // (mode != FAPA_STREAK)

	}
	PERFINFO_AUTO_STOP();
}

static float frac(float value)
{
	return value - floor(value);
}

static void calcLerpColorRGB(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1, float fHueShift)
{
	Vec3 vBefore =
	{
		value[iIndices[0]-1][0] + jitter[iIndices[0]-1][0]*fJitter0[0],
		value[iIndices[1]-1][1] + jitter[iIndices[1]-1][1]*fJitter0[1],
		value[iIndices[2]-1][2] + jitter[iIndices[2]-1][2]*fJitter0[2],
	};

	Vec3 vAfter =
	{
		value[iIndices[0]][0] + jitter[iIndices[0]][0]*fJitter1[0],
		value[iIndices[1]][1] + jitter[iIndices[1]][1]*fJitter1[1],
		value[iIndices[2]][2] + jitter[iIndices[2]][2]*fJitter1[2],
	};

	fpHSVToRGB(vBefore, vBefore, fHueShift);
	fpHSVToRGB(vAfter, vAfter, fHueShift);


	result[0] = lerp(vBefore[0],
					 vAfter[0],
					 time[0]);
	result[1] = lerp(vBefore[1],
					 vAfter[1],
					 time[1]);
	result[2] = lerp(vBefore[2],
					 vAfter[2],
					 time[2]);
	result[3] = lerp(value[iIndices[3]-1][3] + jitter[iIndices[3]-1][3]*fJitter0[3],
					 value[iIndices[3]][3] + jitter[iIndices[3]][3]*fJitter1[3],
					 time[3]);
}

static void calcLerpColorHSV(Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1)
{
	result[0] = lerp(value[iIndices[0]-1][0] + jitter[iIndices[0]-1][0]*fJitter0[0],
					 value[iIndices[0]][0] + jitter[iIndices[0]][0]*fJitter1[0],
					 time[0]);
	result[1] = lerp(value[iIndices[1]-1][1] + jitter[iIndices[1]-1][1]*fJitter0[1],
					 value[iIndices[1]][1] + jitter[iIndices[1]][1]*fJitter1[1],
					 time[1]);
	result[2] = lerp(value[iIndices[2]-1][2] + jitter[iIndices[2]-1][2]*fJitter0[2],
					 value[iIndices[2]][2] + jitter[iIndices[2]][2]*fJitter1[2],
					 time[2]);
	result[3] = lerp(value[iIndices[3]-1][3] + jitter[iIndices[3]-1][3]*fJitter0[3],
					 value[iIndices[3]][3] + jitter[iIndices[3]][3]*fJitter1[3],
					 time[3]);
}

static void calcLerpScale(FastParticleMode mode, Vec4 result, const Mat44 value, const Mat44 jitter, const IVec4 iIndices, const Vec4 time, const Vec4 fJitter0, const Vec4 fJitter1)
{
	result[0] = lerp(value[iIndices[0]-1][0] + jitter[iIndices[0]-1][0]*fJitter0[0],
					 value[iIndices[0]][0] + jitter[iIndices[0]][0]*fJitter1[0],
					 time[0]);
	if (mode == FAPA_LINKSCALE)
	{
		result[1] = 0.0f;
	}
	else
	{
		result[1] = lerp(value[iIndices[1]-1][1] + jitter[iIndices[1]-1][1]*fJitter0[1],
						 value[iIndices[1]][1] + jitter[iIndices[1]][1]*fJitter1[1],
						 time[1]);
	}
	if (mode == FAPA_STREAK)
	{
		result[2] = result[3] = 0.0f;
	}
	else
	{
		result[2] = lerp(value[iIndices[2]-1][2] + jitter[iIndices[2]-1][2]*fJitter0[2],
						 value[iIndices[2]][2] + jitter[iIndices[2]][2]*fJitter1[2],
						 time[2]);
		result[3] = lerp(value[iIndices[3]-1][3] + jitter[iIndices[3]-1][3]*fJitter0[3],
						 value[iIndices[3]][3] + jitter[iIndices[3]][3]*fJitter1[3],
						 time[3]);
	}
}

static void getJitter(Vec4 result, int iJitterIndex, const IVec4 indices)
{
	result[0] = randomF32BlornFixedSeed(iJitterIndex + indices[0]);
	result[1] = randomF32BlornFixedSeed(iJitterIndex + indices[1] + 4);
	result[2] = randomF32BlornFixedSeed(iJitterIndex + indices[2] + 8);
	result[3] = randomF32BlornFixedSeed(iJitterIndex + indices[3] + 12);
}

static void scaleAndRotate(Vec3 result, const Vec3 vec, const Vec2 scale, float rot)
{
#if 0
	// SSE
	// Too much work for mat on Vec2, no intrinsic dot product or sincosf, this is notably slower than the code below
	F32 c, s;
	__m128 r = {vec[0], vec[1], scale[0], scale[1]};
	__m128 sc, t;
	sincosf(-rot, &c, &s);
	sc = _mm_shuffle_ps(r, r, _MM_SHUFFLE(1, 0, 3, 2));
	r = _mm_mul_ps(r, sc);
	t = _mm_set_ps(c, -s, s, c);
	r = _mm_mul_ps(r, t);
	t = _mm_shuffle_ps(r, r, _MM_SHUFFLE(0, 3, 0, 1));
	r = _mm_add_ps(r, t);
	result[0] = r.m128_f32[0];
	result[1] = r.m128_f32[2];
	result[2] = 0;
#else
	F32 c, s, tx, ty;
	copyVec3(vec, result);
	result[0] *= scale[0];
	result[1] *= scale[1];
	tx = result[0];
	ty = result[1];
	sincosf(-rot, &s, &c);

	result[0] = tx*c - ty*s;
	result[1] = ty*c + tx*s;
#endif
}

static const float* getCornerPos(int iCorner)
{
	static const Vec3 corner[] = {
		{ -0.5f, -0.5f,  0.0f },
		{ -0.5f,  0.5f,  0.0f },
		{  0.5f,  0.5f,  0.0f },
		{  0.5f, -0.5f,  0.0f }
	};

	return corner[iCorner];
}

static const float* getCornerTex(int iCorner)
{
	static const Vec2 corner[] = {
		{ 0.0f, 1.0f },
		{ 0.0f, 0.0f },
		{ 1.0f, 0.0f },
		{ 1.0f, 1.0f }
	};
	return corner[iCorner];
}

static const void getQuadCornerTex(Vec2 result, int iCorner, bool b0, bool b1)
{
	static const Vec2 corner[] = {
		{ 0.0f, 0.5f },
		{ 0.0f, 0.0f },
		{ 0.5f, 0.0f },
		{ 0.5f, 0.5f }
	};

	copyVec2(corner[iCorner], result);

	if (b0)
	{
		result[0] += 0.5f;
	}
	if (b1)
	{
		result[1] += 0.5f;
	}
}

static void streak(Vec3 result, int corner, float scale_x, float scale_y, const Vec3 streakDir)
{
	static const Vec2 cornMult[] = {
		{ -1.0f, 1.0f },
		{ -1.0f, 0.0f },
		{  1.0f, 0.0f },
		{  1.0f, 1.0f }
	};
	Vec3 side_x;
	Vec3 side_y;

	setVec3(side_x, streakDir[1], -streakDir[0], 0.0f);
	normalVec3(side_x);
	scaleVec3(side_x, 0.5f * scale_x, side_x);
	scaleVec3(side_x, cornMult[corner][0], side_x);
	
	scaleVec3(streakDir, scale_y, side_y);
	scaleVec3(side_y, cornMult[corner][1], side_y);

	addVec3(side_x, side_y, result);
}
