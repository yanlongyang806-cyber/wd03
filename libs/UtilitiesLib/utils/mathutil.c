#include "mathutil.h"
#include "timing.h"
#include "ContinuousBuilderSupport.h"
#include "crypticerror.h"
#include "GlobalTypes.h"
#include "Quat.h"
#include "memlog.h"
#include "bounds.h" // ideally, any function that needed this would not be in here

#if _XBOX
#include "windefinclude.h"
#include <xboxmath.h>
#endif

#if !SPU
#include <time.h> //for rand seed
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

U32 oldControlState;

#if !_PS3
U32 fp_default_exception_mask = _MCW_EM & ~(0); // Default to hiding all exceptions, add in from the list below to enable:
S32 fp_exceptions_disabled;
S32 fp_exceptions_forced;
// Left not throwing exceptions: _EM_OVERFLOW|_EM_ZERODIVIDE|_EM_INVALID|_EM_INEXACT|_EM_DENORMAL|_EM_UNDERFLOW

// Enables floating point exceptions.  Valid parameters are: Inexact, Underflow, Overflow, ZeroDivide, Invalid, Denormal or All
AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);
void enableFPException(const char *exception)
{
	U32 enable = 0;
	if (stricmp(exception, "Inexact")==0)
		enable = _EM_INEXACT;					/*   inexact (precision) */
	else if (stricmp(exception, "Underflow")==0)
		enable = _EM_UNDERFLOW;					/*   precision underflow */
	else if (stricmp(exception, "Overflow")==0)
		enable = _EM_OVERFLOW;					/*   precision overflow */
	else if (stricmp(exception, "ZeroDivide")==0 || stricmp(exception, "DivZero")==0)
		enable = _EM_ZERODIVIDE;				/*   zero divide */
	else if (stricmp(exception, "Invalid")==0)
		enable = _EM_INVALID;					/*   invalid operation including stack overflow */
	else if (stricmp(exception, "Denormal")==0)
		enable = _EM_DENORMAL;					/* denormal exception mask */
	else if (stricmp(exception, "All")==0)
		enable = _MCW_EM;
	else
		Errorf("Invalid string passed to %s: %s", __FUNCTION__, exception);
	fp_default_exception_mask &= ~enable;
	SET_FP_CONTROL_WORD_DEFAULT;
}
// Enables floating point exceptions.  Valid parameters are: Inexact, Underflow, Overflow, ZeroDivide, Invalid, Denormal or All
AUTO_COMMAND ACMD_NAME(enableFPException) ACMD_CATEGORY(Debug);
void enableFPExceptionAtRunTime(const char *exception)
{
	enableFPException(exception);
}

U32 getFPExceptionMask(void){
	return fp_default_exception_mask;
}

void setFPExceptionMask(U32 mask){
	fp_default_exception_mask = mask & _MCW_EM;
	SET_FP_CONTROL_WORD_DEFAULT;
}

static void logExceptions(int fpuExceptionFlags)
{
	if (0 && fpuExceptionFlags)  // Disabled per email from drichardson Tue, 11 Sep 2012 20:24:41 -0700
		memlog_printf(NULL, "Warning: some exceptions masked during FP_NO_EXCEPTIONS_BEGIN .. FP_NO_EXCEPTIONS_END bracket, and being cleared %s,%s,%s,%s,%s,%s", 
			fpuExceptionFlags & _EM_INEXACT ? "Inexact" : "",
			fpuExceptionFlags & _EM_UNDERFLOW ? "Underflow" : "",
			fpuExceptionFlags & _EM_OVERFLOW ? "Overflow" : "",
			fpuExceptionFlags & _EM_ZERODIVIDE ? "Divide by Zero" : "",
			fpuExceptionFlags & _EM_INVALID ? "Invalid Operation" : "",
			fpuExceptionFlags & _EM_DENORMAL ? "Denormal" : ""
			);
}

void clearFPExceptionMaskForThisThread(FPExceptionMask* oldMaskOut){
	int exceptionsTriggered;
	exceptionsTriggered = _clearfp();
	logExceptions(exceptionsTriggered);
	_controlfp_s(oldMaskOut, 0, 0);
	_controlfp_s(NULL, ~0, _MCW_EM);
}

void setFPExceptionMaskForThisThread(FPExceptionMask mask){
	int exceptionsTriggered;
	exceptionsTriggered = _clearfp();
	logExceptions(exceptionsTriggered);
	_controlfp_s(NULL, mask, _MCW_EM);
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Debug);
void forceFPExceptions(S32 enabled)
{
	fp_exceptions_forced = !!enabled;
	if(enabled){
		enableFPException("DivZero");
		enableFPException("Overflow");
		enableFPException("Invalid");
	}
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void disableFPExceptions(S32 disabled){
	fp_exceptions_disabled = !!disabled;
	if(fp_exceptions_disabled){
		fp_default_exception_mask = _MCW_EM;
		SET_FP_CONTROL_WORD_DEFAULT;
	}
}

AUTO_RUN_LATE;
void enableProgrammerFPExceptions(void){
	if(	fp_exceptions_forced
		||
		!fp_exceptions_disabled &&
		!g_isContinuousBuilder &&
		(GetAppGlobalType() == GLOBALTYPE_CLIENT || GetAppGlobalType() == GLOBALTYPE_GAMESERVER || GetAppGlobalType() == GLOBALTYPE_APPSERVER) &&
		UserIsInGroup("Software") &&
		!UserIsInGroup("All"))
	{
		enableFPException("DivZero");
		enableFPException("Overflow");
		enableFPException("Invalid");
	}
}

#endif

U32 rule30Float_c = 0xf244f343;

// Matrix memory layout
// XX XY XZ
// YX YY YZ
// ZX ZY ZZ
// PX PY PZ

#define EPSILON 0.00001f
#define NEARZERO(x) (fabs(x) < EPSILON)

const Mat4	zeromat		= {{ 0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}};
const Mat4	unitmat		= {{ 1.0,0,0},{0, 1.0,0},{0,0, 1.0},{0,0,0}};
const Mat44	unitmat44	= {{ 1.0,0,0,0},{0, 1.0,0,0},{0,0, 1.0,0},{0,0,0,1}};
const Mat4	lrmat		= {{-1.0,0,0},{0, 1.0,0},{0,0,-1.0},{0.0,0.0,0.0}};
const Vec3	zerovec3	= { 0,0,0 };
const Vec3	onevec3		= { 1,1,1 };
const Vec3	unitvec3	= { 1,1,1 };
const Vec4	sidevec		= { 1,0,0,0 };
const Vec4	upvec		= { 0,1,0,0 };
const Vec4	forwardvec  = { 0,0,1,0 };
const Vec4	zerovec4	= { 0,0,0,0 };
const Vec4	onevec4		= { 1,1,1,1 };
const Vec4	unitvec4	= { 1,1,1,1 };

F32 floatseed = 1.0;
long holdrand = 1L;

F32 sintable[TRIG_TABLE_ENTRIES];
F32 costable[TRIG_TABLE_ENTRIES];

#if !SPU
/* Function getRandom()
 *	Gets a random number.
 *
 * Returns:
 *	A random integer.
 *
 */
static int getRandom()
{
	static int seeded = 0;
	
	if(seeded== 0)
	{
		srand(_time32(NULL));
		seeded = 1;
	}
	
	return rand();
}

/* Function getCappedRandom()
 *	Gets a random number that is smaller than the specified maximum.
 *
 * Parameters:
 *	max - a maximum cap for the returned random integer
 *
 * Returns:
 *	A random integer.
 *
 */
//interesting bug, if you change int maxq to int max, Intellisense goes crazy. 
int getCappedRandom(int maxq)
{
	if(0 == maxq)
	{
		return 0;
	}
	return (int)(getRandom() % maxq);
}

#include "wininclude.h" // for Sleep()
U32 verySlowRandomNumber()
{
	int		i;
	U32		num=0;

	for(i=0;i<8;i++)
	{
		S64		x;
		U32		t;

		Sleep(1);
		GET_CPU_TICKS_64(x);
		t = x;
		num ^= (t & 255) << (4*i);
	}
	return num;
}
static U32 rule30_c = 0xf244f343;

void rule30Seed(U32 c)
{
	rule30_c = c;
}

U32 rule30Rand()
{
      U32 l,r;
      l = r = rule30_c;
      l = _lrotr(l, 1);/* bit i of l equals bit just left of bit i in c */
      r = _lrotl(r, 1);/* bit i of r euqals bit just right of bit i in c */
      rule30_c |= r;
      rule30_c ^= l;           /* c = l xor (c or r), aka rule 30, named by wolfram */
      return rule30_c;
}


char *safe_ftoa_s(F32 f,char *buf, size_t buf_size)
{
	char	*s;

	if (!_finite(f))
		f = 0;
	sprintf_s(SAFESTR2(buf), "%f", f);
	for(s=(buf + strlen(buf) -1);*s == '0';s--)
		;
	s[1] = 0;
	if (s[0] == '.')
		*s = 0;
	if (buf[0] == 0)
		buf[0] = '0';
	return buf;
}

int randInt(int max)
{
#define A			(48271)
#define RAND_10K	(399268537)		/* value of seed at ten-thousandth iteration */
#define M			(2147483647)	/* 2**31-1 */
static U32	seed_less_one;
U32			hprod,lprod,result,seed = seed_less_one + 1;

	if (max <= 0) return 0;
    /* first do two 16x16 multiplies */
    hprod = ((seed>>16)&0xFFFF) * A;
    lprod = (seed&0xFFFF)*A;

    /* combine the products (suitably shifted) to form 48-bit product,
    *  with bottom 32 bits in lprod and top 16 bits in hprod.
    */
    hprod += ((lprod>>16) & 0xFFFF);
    lprod = (lprod&0xFFFF)|((hprod&0xFFFF)<<16);
    hprod >>= 16;

    /* now subtract the top 17 bits from the bottom 31 bits to implement
    *  a deferred "end-around carry".
    */
    hprod = hprod + hprod + ((lprod>>31)&1);
    lprod += hprod;

    /* final "and" gives modulo(2^31-1) */
    seed = lprod & 0x7FFFFFFF;
    result = (seed_less_one = seed - 1);

	return result % max;
#undef A
#undef RAND_10K
#undef M
}

#endif

void initQuickTrig() 
{	
	int i;
	F32 rad;
	for( i = 0 ; i < TRIG_TABLE_ENTRIES ; i++)
	{
		rad = (F32)(TWOPI * ((F32)i / TRIG_TABLE_ENTRIES));
		sincosf(rad, &(sintable[i]), &(costable[i]));
	}
}



void camLookAt(const Vec3 lookdir, Mat4 mat)
{
	Vec3 dv;

	dv[2] = 0;
	getVec3YP(lookdir,&dv[1],&dv[0]);
	createMat3YPR(mat,dv);
}

void mat3FromUpVector(const Vec3 upVec, Mat3 mat)
{
	// We need an orientation matrix that has a certain up vector,
	// but we just don't care about the X or Z vector (as long as it's orthonormal)
	copyVec3( upVec, mat[1] );
	zeroVec3( mat[0] );

	// This is just to insure that the x-axis vector is not colinear with the y-axis vector
	if ( mat[1][0] == 1.0f )
		mat[0][2] = 1.0f;
	else
		mat[0][0] = 1.0f;

	// Since a lot of this is zeros, we can optimize, but for now just make it work
	crossVec3( mat[0], mat[1], mat[2] );
	normalVec3(mat[2]);
	crossVec3( mat[1], mat[2], mat[0] ); 
}

void mat3FromFwdVector(const Vec3 fwdVec, Mat3 mat)
{
	if(vec3IsZero(fwdVec)) {
		copyMat3(unitmat, mat);
		return;
	}
	copyVec3(fwdVec, mat[2]);
	normalVec3(mat[2]);
	copyVec3(upvec, mat[1]);
	crossVec3(mat[1], mat[2], mat[0]);
	normalVec3(mat[0]);
	crossVec3(mat[2], mat[0], mat[1]);
	normalVec3(mat[1]);
}

float distance3( const Vec3 a, const Vec3 b )
{
	return (F32)sqrt(SQR(a[0] - b[0]) + SQR(a[1] - b[1]) + SQR(a[2] - b[2]));
}

float distance3SquaredXZ( const Vec3 a, const Vec3 b )
{
	return (F32)SQR(a[0] - b[0]) + SQR(a[2] - b[2]);
}

void getScale(const Mat3 mat, Vec3 scale )
{
	scale[0] = lengthVec3(mat[0]);
	scale[1] = lengthVec3(mat[1]);
	scale[2] = lengthVec3(mat[2]);
}

void extractScale(Mat3 mat,Vec3 scale)
{
	scale[0] = normalVec3(mat[0]);
	scale[1] = normalVec3(mat[1]);
	scale[2] = normalVec3(mat[2]);
}

// same as the above, but doesn't copy
void normalMat3(Mat3 mat)
{
	normalVec3(mat[0]);
	normalVec3(mat[1]);
	normalVec3(mat[2]);
}

F32 near_same_vec3_tol_squared = 0.001f;
void setNearSameVec3Tolerance(F32 tol)
{
	near_same_vec3_tol_squared = tol*tol;
}

bool nearSameMat3Tol(const Mat3 a, const Mat3 b, F32 tolerence)
{
	Vec3 pyr_a, pyr_b;
	getMat3YPR(a, pyr_a);
	getMat3YPR(b, pyr_b);
	return nearSameVec3Tol(pyr_a, pyr_b, tolerence);
}

bool nearSameMat4Tol(const Mat4 a, const Mat4 b, F32 rotation_tolerence, F32 position_tolerence)
{
	return nearSameVec3Tol(a[3], b[3], position_tolerence) && nearSameMat3Tol(a, b, rotation_tolerence);
}

int nearSameDVec2(const DVec2 a,const DVec2 b)
{
	int		i;

	for(i=0;i<2;i++)
		if (!nearSameDouble(a[i], b[i]))
			return 0;
	return 1;
}

void copyMat3(const Mat3 a,Mat3 b)
{
	/*copyVec3(a[0],b[0]);
	copyVec3(a[1],b[1]);
	copyVec3(a[2],b[2]);*/
	memcpy(b,a,sizeof(Mat3));
}

void transposeMat3(Mat3 uv)
{
F32 tmp;
	
	tmp = uv[0][1];
	uv[0][1] = uv[1][0];
	uv[1][0] = tmp;

	tmp = uv[0][2];
	uv[0][2] = uv[2][0];
	uv[2][0] = tmp;

	tmp = uv[1][2];
	uv[1][2] = uv[2][1];
	uv[2][1] = tmp;
}

F32 mat3Determinant(const Mat3 a)
{
	return	a[0][0] * ( a[1][1] * a[2][2] - a[1][2] * a[2][1] ) -
			a[0][1] * ( a[1][0] * a[2][2] - a[1][2] * a[2][0] ) +
			a[0][2] * ( a[1][0] * a[2][1] - a[1][1] * a[2][0] );
}

F32 mat4Determinant(const Mat4 a)
{
	// expand using bottom row, which is (0,0,0,1)
	// so this simplifies to the upper 3x3 matrix determinant
	return mat3Determinant(a);
}

F32 mat44Determinant(const Mat44 a)
{
	Mat3 minor;
	F32 det;

	copyVec3(&a[1][1], minor[0]);
	copyVec3(&a[2][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det = a[0][0] * mat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[2][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det -= a[1][0] * mat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[1][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det += a[2][0] * mat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[1][1], minor[1]);
	copyVec3(&a[2][1], minor[2]);
	det -= a[3][0] * mat3Determinant(minor);

	return det;
}

F64 dmat3Determinant(const DMat3 a)
{
	return	a[0][0] * ( a[1][1] * a[2][2] - a[1][2] * a[2][1] ) -
			a[0][1] * ( a[1][0] * a[2][2] - a[1][2] * a[2][0] ) +
			a[0][2] * ( a[1][0] * a[2][1] - a[1][1] * a[2][0] );
}

F64 dmat4Determinant(const DMat4 a)
{
	// expand using bottom row, which is (0,0,0,1)
	// so this simplifies to the upper 3x3 matrix determinant
	return dmat3Determinant(a);
}

F64 dmat44Determinant(const DMat44 a)
{
	DMat3 minor;
	F64 det;

	copyVec3(&a[1][1], minor[0]);
	copyVec3(&a[2][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det = a[0][0] * dmat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[2][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det -= a[1][0] * dmat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[1][1], minor[1]);
	copyVec3(&a[3][1], minor[2]);
	det += a[2][0] * dmat3Determinant(minor);

	copyVec3(&a[0][1], minor[0]);
	copyVec3(&a[1][1], minor[1]);
	copyVec3(&a[2][1], minor[2]);
	det -= a[3][0] * dmat3Determinant(minor);

	return det;
}

int invertMat3Copy(const Mat3 a, Mat3 b)  
{
	F32 fDetInv;

	fDetInv	= mat3Determinant(a);

	if (fabs(fDetInv) < SQR(FLT_EPSILON))
		return 0;
	fDetInv = 1.f / fDetInv;

	b[0][0] =  ( a[1][1] * a[2][2] - a[1][2] * a[2][1] );
	b[0][1] =  ( a[0][1] * a[2][2] - a[0][2] * a[2][1] );
	b[0][2] =  ( a[0][1] * a[1][2] - a[0][2] * a[1][1] );

	b[0][0] *= fDetInv;
	b[0][1] *= -fDetInv;
	b[0][2] *= fDetInv;

	b[1][0] = -fDetInv * ( a[1][0] * a[2][2] - a[1][2] * a[2][0] );
	b[1][1] =  fDetInv * ( a[0][0] * a[2][2] - a[0][2] * a[2][0] );
	b[1][2] = -fDetInv * ( a[0][0] * a[1][2] - a[0][2] * a[1][0] );

	b[2][0] =  fDetInv * ( a[1][0] * a[2][1] - a[1][1] * a[2][0] );
	b[2][1] = -fDetInv * ( a[0][0] * a[2][1] - a[0][1] * a[2][0] );
	b[2][2] =  fDetInv * ( a[0][0] * a[1][1] - a[0][1] * a[1][0] );
  
  return 1;
}

S32 invertMat4Copy(const Mat4 mat,Mat4 inv)
{
	Vec3 dv;

	if(!invertMat3Copy(mat,inv)){
		return 0;
	}
	negateVec3(mat[3],dv);
	mulVecMat3(dv,inv,inv[3]);
	return 1;
}

// fast invert (assuming no shear)
void invertMat4ScaledCopy(const Mat4 mat,Mat4 inv)
{
	Vec3 dv, scale;

	copyMat4(mat, inv);
	getScale(inv, scale);
	recipVec3(scale);
	scaleMat3Vec3(inv, scale);
	transposeMat3(inv);
	scaleMat3Vec3(inv, scale);
	negateVec3(inv[3],dv);
	mulVecMat3(dv,inv,inv[3]);
}

int invertMat44Copy(const Mat44 m, Mat44 r)
{
	float max, t, det, pivot, oneoverpivot;
	int i, j=0, k;
	Mat44 A;
	copyMat44(m, A);


	/*---------- forward elimination ----------*/

	identityMat44(r);

	det = 1.0f;
	for (i = 0; i < 4; i++)
	{
		/* eliminate in column i, below diag */
		max = -1.f;
		for (k = i; k < 4; k++)
		{
			/* find pivot for column i */
			t = A[i][k];
			if (fabs(t) > max)
			{
				max = fabs(t);
				j = k;
			}
		}

		if (max <= 0.f)
			return 0;         /* if no nonzero pivot, PUNT */

		if (j != i)
		{
			/* swap rows i and j */
			for (k = i; k < 4; k++)
			{
				t = A[k][i];
				A[k][i] = A[k][j];
				A[k][j] = t;
			}
			for (k = 0; k < 4; k++)
			{
				t = r[k][i];
				r[k][i] = r[k][j];
				r[k][j] = t;
			}
			det = -det;
		}
		pivot = A[i][i];
		oneoverpivot = 1.f / pivot;
		det *= pivot;
		for (k = i + 1; k < 4; k++)           /* only do elems to right of pivot */
			A[k][i] *= oneoverpivot;
		for (k = 0; k < 4; k++)
			r[k][i] *= oneoverpivot;

		/* we know that A(i, i) will be set to 1, so don't bother to do it */

		for (j = i + 1; j < 4; j++)
		{
			/* eliminate in rows below i */
			t = A[i][j];                /* we're gonna zero this guy */
			for (k = i + 1; k < 4; k++)       /* subtract scaled row i from row j */
				A[k][j] -= A[k][i] * t;   /* (ignore k<=i, we know they're 0) */
			for (k = 0; k < 4; k++)
				r[k][j] -= r[k][i] * t;   /* (ignore k<=i, we know they're 0) */
		}
	}

	/*---------- backward elimination ----------*/

	for (i = 4 - 1; i > 0; i--)
	{
		/* eliminate in column i, above diag */
		for (j = 0; j < i; j++)
		{
			/* eliminate in rows above i */
			t = A[i][j];                /* we're gonna zero this guy */
			for (k = 0; k < 4; k++)         /* subtract scaled row i from row j */
				r[k][j] -= r[k][i] * t;   /* (ignore k<=i, we know they're 0) */
		}
	}

	if (det < 1e-8 && det > -1e-8)
		return 0;

	return 1;
}


void transposeMat3Copy(const Mat3 uv,Mat3 uv2)
{
	uv2[0][0] = uv[0][0];
	uv2[0][1] = uv[1][0];
	uv2[0][2] = uv[2][0];

	uv2[1][0] = uv[0][1];
	uv2[1][1] = uv[1][1];
	uv2[1][2] = uv[2][1];

	uv2[2][0] = uv[0][2];
	uv2[2][1] = uv[1][2];
	uv2[2][2] = uv[2][2];
}

void transposeMat4Copy(const Mat4 mat,Mat4 inv)
{
Vec3	dv;

	transposeMat3Copy(mat,inv);
	negateVec3(mat[3],dv);
	mulVecMat3(dv,inv,inv[3]);
}

void transposeMat44(Mat44 mat)
{
	F32 tempf;
#define SWAP(f1, f2) tempf = f1; f1 = f2; f2 = tempf;
	SWAP(mat[0][1], mat[1][0]);
	SWAP(mat[0][2], mat[2][0]);
	SWAP(mat[0][3], mat[3][0]);
	SWAP(mat[1][2], mat[2][1]);
	SWAP(mat[1][3], mat[3][1]);
	SWAP(mat[2][3], mat[3][2]);
#undef SWAP
}

void transposeMat44Copy(const Mat44 mIn, Mat44 mOut)
{
	mOut[0][0] = mIn[0][0];
	mOut[0][1] = mIn[1][0];
	mOut[0][2] = mIn[2][0];
	mOut[0][3] = mIn[3][0];

	mOut[1][0] = mIn[0][1];
	mOut[1][1] = mIn[1][1];
	mOut[1][2] = mIn[2][1];
	mOut[1][3] = mIn[3][1];

	mOut[2][0] = mIn[0][2];
	mOut[2][1] = mIn[1][2];
	mOut[2][2] = mIn[2][2];
	mOut[2][3] = mIn[3][2];

	mOut[3][0] = mIn[0][3];
	mOut[3][1] = mIn[1][3];
	mOut[3][2] = mIn[2][3];
	mOut[3][3] = mIn[3][3];
}


void scaleMat3(const Mat3 a,Mat3 b,F32 sfactor)
{
F32		t0, t1, t2;
int		i;

    for (i=0;i < 3;++i) 
	{
		t0 = a[i][0]*sfactor;
		t1 = a[i][1]*sfactor;
		t2 = a[i][2]*sfactor;
		b[i][0] = t0;
		b[i][1] = t1;
		b[i][2] = t2;
    }
}

void scaleMat44(const Mat44 a,Mat44 b,F32 sfactor)
{
	F32		t0, t1, t2, t3;
	int		i;

	for (i=0;i < 4;++i) 
	{
		t0 = a[i][0]*sfactor;
		t1 = a[i][1]*sfactor;
		t2 = a[i][2]*sfactor;
		t3 = a[i][3]*sfactor;
		b[i][0] = t0;
		b[i][1] = t1;
		b[i][2] = t2;
		b[i][3] = t3;
	}
}

//scale a Mat3 into another Mat3
void scaleMat3Vec3Xfer(const Mat3 a, const Vec3 sfactor, Mat3 b)
{
//int		i;

	copyMat3( a, b );
	scaleMat3Vec3(b,sfactor); 
	
    /*for (i=0;i < 3;++i) 
	{
		b[i][0] = a[i][0]*sfactor[0];
		b[i][1] = a[i][1]*sfactor[1];
		b[i][2] = a[i][2]*sfactor[2];
    }*/
}


void scaleMat3Vec3(Mat3 a, const Vec3 sfactor)
{
	scaleVec3(a[0], sfactor[0], a[0]);
	scaleVec3(a[1], sfactor[1], a[1]);
	scaleVec3(a[2], sfactor[2], a[2]);
}

void rotateMat3(const F32 *rpy, Mat3 uvs)
{
	if(rpy[1])
		yawMat3(rpy[1],uvs);

	if(rpy[0])
		pitchMat3(rpy[0],uvs);

	if(rpy[2])
		rollMat3(rpy[2],uvs);
}


void yawMat3World(F32 angle, Mat3 uv )
{
	F32 ut,sint,cost;
	int i;

	sincosf(angle, &sint, &cost);

	for(i=0;i<3;i++)
	{
		ut	 = uv[i][0]*cost - uv[i][2]*sint;
		uv[i][2] = uv[i][2]*cost + uv[i][0]*sint;
		uv[i][0] = ut; 
	}
}

void pitchMat3World(F32 angle, Mat3 uv)
{
F32 ut,sint,cost;
int i; 

	sincosf(angle, &sint, &cost);
	for(i=0;i<3;i++)
	{ 
		ut	 = uv[i][1]*cost - uv[i][2]*sint;
		uv[i][2] = uv[i][2]*cost + uv[i][1]*sint;
		uv[i][1] = ut; 
	}
} 


void rollMat3World(F32 angle, Mat3 uv)
{
F32 ut,sint,cost;
int i;

	sincosf(angle, &sint, &cost);
	for(i=0;i<3;i++)
	{ 
		ut	 = uv[i][0]*cost - uv[i][1]*sint;
		uv[i][1] = uv[i][1]*cost + uv[i][0]*sint;
		uv[i][0] = ut; 
	}
} 


void yawMat3(F32 angle, Mat3 uv)
{
F32 ut,sint,cost;
int i; 

	sincosf(angle, &sint, &cost);
	for(i=0;i<3;i++)
	{ 
		ut	 = uv[0][i]*cost - uv[2][i]*sint;
		uv[2][i] = uv[2][i]*cost + uv[0][i]*sint;
		uv[0][i] = ut; 
	}
}


void pitchMat3(F32 angle, Mat3 uv)
{
F32 ut,sint,cost;
int i; 

	sincosf(angle, &sint, &cost);
	for(i=0;i<3;i++)
	{ 
		ut	 = uv[1][i]*cost - uv[2][i]*sint;
		uv[2][i] = uv[2][i]*cost + uv[1][i]*sint;
		uv[1][i] = ut; 
	}
} 


void rollMat3(F32 angle, Mat3 uv)
{
F32 ut,sint,cost;
int i;

	sincosf(angle, &sint, &cost);
	for(i=0;i<3;i++) { 
		ut	 = uv[0][i]*cost - uv[1][i]*sint;
		uv[1][i] = uv[1][i]*cost + uv[0][i]*sint;
		uv[0][i] = ut; 
	}
} 


// Multiply a vector times a transposed matrix. Normally this implies the input
// matrix contains only rotatations, so the transpose is the same as the inverse.
void mulVecMat3Transpose( const Vec3 uvec, const Mat3 uvs,Vec3 bodvec)
{
#ifdef _FULLDEBUG
	assert(uvec!=bodvec);
#endif
	bodvec[0] = uvec[0]*uvs[0][0] + uvec[1]*uvs[0][1] + uvec[2]*uvs[0][2];
	bodvec[1] = uvec[0]*uvs[1][0] + uvec[1]*uvs[1][1] + uvec[2]*uvs[1][2];
	bodvec[2] = uvec[0]*uvs[2][0] + uvec[1]*uvs[2][1] + uvec[2]*uvs[2][2];
}


// Multiply a vector times a transposed matrix
void mulVecMat4Transpose(const Vec3 vin, const Mat4 m, Vec3 vout)
{
Vec3	dv;
#ifdef _FULLDEBUG
	assert(vin!=vout);
#endif

	subVec3(vin,m[3],dv);
	mulVecMat3Transpose(dv,m,vout);
}

#if 0
// some sse intrinsic experiments

__m128	t,t2;

	t2.m128_f32[0] = 1;
	t2.m128_f32[1] = 1;
	t2.m128_f32[2] = 1;
	t2.m128_f32[3] = 1;
	t2 = _mm_add_ps(t2,t2);

	t.m128_f32[0] = mag2;
	t2 = _mm_rsqrt_ss(t);

#endif

void mulMat4(const Mat4 a,const Mat4 b,Mat4 c)
{
#ifdef _FULLDEBUG
	assert(a!=c && b!=c);
#endif
	// This is the fastest way to do it in C for x86
	(c)[0][0] = (b)[0][0] * (a)[0][0] + (b)[0][1] * (a)[1][0] + (b)[0][2] * (a)[2][0];
	(c)[0][1] = (b)[0][0] * (a)[0][1] + (b)[0][1] * (a)[1][1] + (b)[0][2] * (a)[2][1];
	(c)[0][2] = (b)[0][0] * (a)[0][2] + (b)[0][1] * (a)[1][2] + (b)[0][2] * (a)[2][2];
	(c)[1][0] = (b)[1][0] * (a)[0][0] + (b)[1][1] * (a)[1][0] + (b)[1][2] * (a)[2][0];
	(c)[1][1] = (b)[1][0] * (a)[0][1] + (b)[1][1] * (a)[1][1] + (b)[1][2] * (a)[2][1];
	(c)[1][2] = (b)[1][0] * (a)[0][2] + (b)[1][1] * (a)[1][2] + (b)[1][2] * (a)[2][2];
	(c)[2][0] = (b)[2][0] * (a)[0][0] + (b)[2][1] * (a)[1][0] + (b)[2][2] * (a)[2][0];
	(c)[2][1] = (b)[2][0] * (a)[0][1] + (b)[2][1] * (a)[1][1] + (b)[2][2] * (a)[2][1];
	(c)[2][2] = (b)[2][0] * (a)[0][2] + (b)[2][1] * (a)[1][2] + (b)[2][2] * (a)[2][2];
	(c)[3][0] = (b)[3][0] * (a)[0][0] + (b)[3][1] * (a)[1][0] + (b)[3][2] * (a)[2][0] + (a)[3][0];
	(c)[3][1] = (b)[3][0] * (a)[0][1] + (b)[3][1] * (a)[1][1] + (b)[3][2] * (a)[2][1] + (a)[3][1];
	(c)[3][2] = (b)[3][0] * (a)[0][2] + (b)[3][1] * (a)[1][2] + (b)[3][2] * (a)[2][2] + (a)[3][2];
}

void mulMat3( const Mat3 a,  const Mat3 b, Mat3 c)
{
#ifdef _FULLDEBUG
	assert(a!=c && b!=c);
#endif
	(c)[0][0] = (b)[0][0] * (a)[0][0] + (b)[0][1] * (a)[1][0] + (b)[0][2] * (a)[2][0];
	(c)[0][1] = (b)[0][0] * (a)[0][1] + (b)[0][1] * (a)[1][1] + (b)[0][2] * (a)[2][1];
	(c)[0][2] = (b)[0][0] * (a)[0][2] + (b)[0][1] * (a)[1][2] + (b)[0][2] * (a)[2][2];
	(c)[1][0] = (b)[1][0] * (a)[0][0] + (b)[1][1] * (a)[1][0] + (b)[1][2] * (a)[2][0];
	(c)[1][1] = (b)[1][0] * (a)[0][1] + (b)[1][1] * (a)[1][1] + (b)[1][2] * (a)[2][1];
	(c)[1][2] = (b)[1][0] * (a)[0][2] + (b)[1][1] * (a)[1][2] + (b)[1][2] * (a)[2][2];
	(c)[2][0] = (b)[2][0] * (a)[0][0] + (b)[2][1] * (a)[1][0] + (b)[2][2] * (a)[2][0];
	(c)[2][1] = (b)[2][0] * (a)[0][1] + (b)[2][1] * (a)[1][1] + (b)[2][2] * (a)[2][1];
	(c)[2][2] = (b)[2][0] * (a)[0][2] + (b)[2][1] * (a)[1][2] + (b)[2][2] * (a)[2][2];
}

F32 normalVec3XZ(Vec3 v)
{
F32		mag2,mag,invmag;

	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[2]);
	mag = fsqrt(mag2);

	if (mag > FLT_EPSILON)
	{
		invmag = 1.f/mag;
		scaleVec3XZ(v,invmag,v);
	}

	return mag;
}

double normalDVec3(DVec3 v)
{
	F64		mag2,mag,invmag;

	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[1]) + SQR(v[2]);
	mag = sqrt(mag2);

	if (mag > FLT_EPSILON)
	{
		invmag = 1.0/mag;
		scaleVec3(v,invmag,v);
	}

	return mag;
}

F32 normalVec2(Vec2 v)
{
	F32		mag2,mag,invmag;

	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[1]);
	mag = fsqrt(mag2);

	if (mag > FLT_EPSILON)
	{
		invmag = 1.f/mag;
		scaleVec2(v,invmag,v);
	}

	return mag;
}


double normalDVec2(DVec2 v)
{
	double		mag2,mag,invmag;

	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[1]);
	mag = sqrt(mag2);

	if (mag > FLT_EPSILON)
	{
		invmag = 1.0/mag;
		scaleVec2(v,invmag,v);
	}

	return(mag);
}

F32 normalVec4(Vec4 v)
{
	F32		mag2,mag,invmag;

	// Is there some way to let the compiler know to generate an atomic inverse
	// square root if the cpu supports it? (ie; sse, 3dnow, r10k, etc.)
	mag2 = SQR(v[0]) + SQR(v[1]) + SQR(v[2]) + SQR(v[3]);
	mag = fsqrt(mag2);

	if (mag > FLT_EPSILON)
	{
		invmag = 1.f/mag;
		scaleVec4(v,invmag,v);
	}

	return mag;
}

// Gets yaw with respect to positive z.
void getVec3YP(const Vec3 dvec,F32 *yawp,F32 *pitp)
{
	if (yawp)
		*yawp = (F32)fatan2(dvec[0],dvec[2]);
	if (pitp)
	{
		F32	dist = fsqrt(SQR(dvec[0]) + SQR(dvec[2]));
		*pitp = (F32)fatan2(dvec[1],dist);
	}
}

// Gets yaw with respect to negative z.
void getVec3YvecOut(const Vec3 dvec,F32 *yawp,F32 *pitp)
{
F32		dist;
	
	*yawp = (F32)fatan2(-dvec[0], -dvec[2]);
	dist = fsqrt(SQR(dvec[0]) + SQR(dvec[2]));
	*pitp = (F32)fatan2(dvec[1],dist);
}

void getVec3PY(const Vec3 dvec,F32 *pitp,F32 *yawp)
{
	if (pitp)
		*pitp = (F32)fatan2(dvec[1],dvec[2]);
	if (yawp)
	{
		F32 dist = fsqrt(SQR(dvec[1]) + SQR(dvec[2]));
		*yawp = (F32)fatan2(dvec[0],dist);
	}
}

F32 interpAngle(  F32 scale_a_to_b, F32 a, F32 b )
{
	return addAngle( a, ( subAngle( b, a ) * scale_a_to_b ) );
}

void interpPYR( F32 scale_a_to_b, const Vec3 a, const Vec3 b, Vec3 result )
{
	result[0] = interpAngle( scale_a_to_b, a[0], b[0] );
	result[1] = interpAngle( scale_a_to_b, a[1], b[1] );
	result[2] = interpAngle( scale_a_to_b, a[2], b[2] );
}

void interpPY( F32 scale_a_to_b, const Vec2 a, const Vec2 b, Vec2 result )
{
	result[0] = interpAngle( scale_a_to_b, a[0], b[0] );
	result[1] = interpAngle( scale_a_to_b, a[1], b[1] );
}

F32 fixAngle(F32 a)
{
	while(a > RAD(180.0))
		a -= RAD(360.0);
	while(a <= RAD(-180.0))
		a += RAD(360.0);
	return(a);
}

F32 subAngle(F32 a, F32 b)
{
	return(fixAngle(a-b));
}

F32 addAngle(F32 a, F32 b)
{
	return(fixAngle(a+b));
}

F32 fixAngleDeg(F32 a)
{
	while(a > 180.0)
		a -= 360.0;
	while(a <= -180.0)
		a += 360.0;
	return(a);
}

F32 subAngleDeg(F32 a, F32 b)
{
	return(fixAngleDeg(a-b));
}

F32 addAngleDeg(F32 a, F32 b)
{
	return(fixAngleDeg(a+b));
}

void createScaleMat(Mat3 mat, const Vec3 scale)
{
	memset(mat, 0, sizeof(Mat3));
	mat[0][0] = scale[0];
	mat[1][1] = scale[1];
	mat[2][2] = scale[2];
}

// seems to create a simple scale/translate matrix that transforms all the points in an AABB such that they fit within specified limits
// This of course will also be an AABB
void createScaleTranslateFitMat(Mat44 output, const Vec3 vMin, const Vec3 vMax, 
								F32 xMinOut, F32 xMaxOut, 
								F32 yMinOut, F32 yMaxOut, 
								F32 zMinOut, F32 zMaxOut)
{
	F32 xSrcSize = vMax[0] - vMin[0];
	F32 xSrcMid = 0.5f * (vMax[0] + vMin[0]);
	F32 xDstSize = xMaxOut - xMinOut;
	F32 xDstMid = 0.5f * (xMaxOut + xMinOut);

	F32 ySrcSize = vMax[1] - vMin[1];
	F32 ySrcMid = 0.5f * (vMax[1] + vMin[1]);
	F32 yDstSize = yMaxOut - yMinOut;
	F32 yDstMid = 0.5f * (yMaxOut + yMinOut);

	F32 zSrcSize = vMax[2] - vMin[2];
	F32 zSrcMid = 0.5f * (vMax[2] + vMin[2]);
	F32 zDstSize = zMaxOut - zMinOut;
	F32 zDstMid = 0.5f * (zMaxOut + zMinOut);

	output[0][0] = xDstSize / AVOID_DIV_0(xSrcSize);
	output[1][0] = 0;
	output[2][0] = 0;
	output[3][0] = xDstMid - xSrcMid * output[0][0];

	output[0][1] = 0;
	output[1][1] = yDstSize / AVOID_DIV_0(ySrcSize);
	output[2][1] = 0;
	output[3][1] = yDstMid - ySrcMid * output[1][1];

	output[0][2] = 0;
	output[1][2] = 0;
	output[2][2] = zDstSize / AVOID_DIV_0(zSrcSize);
	output[3][2] = zDstMid - zSrcMid * output[2][2];

	output[0][3] = 0;
	output[1][3] = 0;
	output[2][3] = 0;
	output[3][3] = 1;
}

// void createMat3PYR(Mat3 mat,const F32 *pyr)
// {
// 	F32 cosP,sinP,cosY,sinY,cosR,sinR,temp;
// 
// 	// Matricies in the game are transposed, so the pitch, yaw, and roll matrices
// 	// are also transposed. An easy way to do this is just to change the sign of sin
// 	sincosf(pyr[0], &sinP, &cosP);
// 	sincosf(pyr[1], &sinY, &cosY);
// 	sincosf(pyr[2], &sinR, &cosR);
// 
// 
// 	mat[0][0] =	 cosY * cosR;
// 	mat[1][0] =	 cosY * sinR;
// 	mat[2][0] =	 sinY;
// 
// 	temp = 	 	 sinP * sinY;
// 	mat[0][1] = -temp * cosR - cosP * sinR;
// 	mat[1][1] =  cosP * cosR - temp * sinR;
// 	mat[2][1] =  sinP * cosY;
// 
// 	temp = 	 	 cosP * -sinY;
// 	mat[0][2] =  temp * cosR + sinP * sinR;
// 	mat[1][2] =  temp * sinR - sinP * cosR;
// 	mat[2][2] =  cosP * cosY;
// }





// Note that the parameters are still pyr not ryp
// void createMat3RYP(Mat3 mat,const F32 *pyr)
// {
// 	F32 cosP,sinP,cosY,sinY,cosR,sinR,temp;
// 
// 	sincosf(pyr[0], &sinP, &cosP);
// 	sincosf(pyr[1], &sinY, &cosY);
// 	sincosf(pyr[2], &sinR, &cosR);
// 
// 	temp = 	 	 cosR * sinY;
// 	mat[0][0] =	 cosR * cosY;
// 	mat[1][0] =	 sinR * cosP - temp * sinP;
// 	mat[2][0] =	 sinR * sinP + temp * cosP;
// 	
// 	temp = 	 	-sinR * sinY;
// 	mat[0][1] =  -sinR * cosY;
// 	mat[1][1] =  cosR * cosP - temp * sinP;
// 	mat[2][1] = cosR * sinP + temp * cosP;
// 
// 	mat[0][2] = -sinY;
// 	mat[1][2] = cosY * -sinP;
// 	mat[2][2] = cosY * cosP;
// }

// Note that the parameters are still pyr not ypr
void createMat3YPR(Mat3 mat,const Vec3 pyr)
{
	F32 temp;

#if _XBOX
	XMVECTOR vangle;
	XMVECTOR vs, vc;
	vangle.x = pyr[0];
	vangle.y = pyr[1];
	vangle.z = pyr[2];
	XMVectorSinCos(&vs, &vc, vangle);
#define sinP vs.x
#define cosP vc.x
#define sinY vs.y
#define cosY vc.y
#define sinR vs.z
#define cosR vc.z
#else
	F32 cosP,sinP,cosY,sinY,cosR,sinR;
	sincosf(pyr[0], &sinP, &cosP);
	sincosf(pyr[1], &sinY, &cosY);
	sincosf(pyr[2], &sinR, &cosR);
#endif

	temp = 	 	 sinY * sinP;
	mat[0][0] =  cosY * cosR + temp * sinR;
	mat[1][0] =  cosY * sinR - temp * cosR;
	mat[2][0] =  sinY * cosP;

	mat[0][1] =	-cosP * sinR;
	mat[1][1] =	 cosP * cosR;
	mat[2][1] =	 sinP;

	temp = 	 	-cosY * sinP;
	mat[0][2] = -sinY * cosR - temp * sinR;
	mat[1][2] = -sinY * sinR + temp * cosR;
	mat[2][2] =  cosY * cosP;

	if (!FINITEVEC3(mat[0]) || !FINITEVEC3(mat[1]) || !FINITEVEC3(mat[2]))
		copyMat3(unitmat, mat);
#if _XBOX
#undef sinP
#undef cosP
#undef sinY
#undef cosY
#undef sinR
#undef cosR
#endif
}

void createMat3DegYPR(Mat3 mat, const Vec3 degPYR)
{
	Vec3 radPYR;
	copyVec3(degPYR, radPYR);
	RADVEC3(radPYR);
	createMat3YPR(mat, radPYR);
}

// Note that the parameters are still py not yp
void createMat3YP(Mat3 mat,const Vec2 py)
{
	F32 temp;
	F32 cosP,sinP,cosY,sinY;

	sincosf(py[0], &sinP, &cosP);
	sincosf(py[1], &sinY, &cosY);

	temp = 	 	 sinY * sinP;
	mat[0][0] =  cosY;
	mat[1][0] =  -temp;
	mat[2][0] =  sinY * cosP;

	mat[0][1] =	 0;
	mat[1][1] =	 cosP;
	mat[2][1] =	 sinP;

	temp = 	 	-cosY * sinP;
	mat[0][2] = -sinY;
	mat[1][2] =  temp;
	mat[2][2] =  cosY * cosP;

	if (!FINITEVEC3(mat[0]) || !FINITEVEC3(mat[1]) || !FINITEVEC3(mat[2]))
		copyMat3(unitmat, mat);
}

// Note that the parameters are still pyr not ryp
void createMat3_0_YPR(Vec3 mat0,const Vec3 pyr)
{
	F32 cosP,sinP,cosY,sinY,cosR,sinR;

	sincosf(pyr[0], &sinP, &cosP);
	sincosf(pyr[1], &sinY, &cosY);
	sincosf(pyr[2], &sinR, &cosR);

	mat0[0] =  cosY * cosR + sinY * sinP * sinR;
	mat0[1] = -cosP * sinR;
	mat0[2] = -sinY * cosR + cosY * sinP * sinR;
}

// Note that the parameters are still pyr not ryp
void createMat3_1_YPR(Vec3 mat1,const Vec3 pyr)
{
	F32 cosP,sinP,cosY,sinY,cosR,sinR;

	sincosf(pyr[0], &sinP, &cosP);
	sincosf(pyr[1], &sinY, &cosY);
	sincosf(pyr[2], &sinR, &cosR);

	mat1[0] =  cosY * sinR - sinY * sinP * cosR;
	mat1[1] =  cosP * cosR;
	mat1[2] = -sinY * sinR - cosY * sinP * cosR;
}

// Note that the parameters are still pyr not ryp
void createMat3_2_YP(Vec3 mat2, const Vec2 py)
{
	F32 cosP,sinP,cosY,sinY;

	sincosf(py[0], &sinP, &cosP);
	sincosf(py[1], &sinY, &cosY);

	mat2[0] =  sinY * cosP;
	mat2[1] =  sinP;
	mat2[2] = cosY * cosP;
}

// void getMat3PYR(const Mat3 mat,F32 *pyr)
// {
// 	F32 R,P,Y;
// 	F32 cosY,cosP;
// 
// 	if (NEARZERO(1.0 - fabs(mat[0][2])))	// Special case: Y = +- 90, R = 0
// 	{
// 		P = (F32)fatan2(mat[2][1],mat[1][1]);
// 		Y = (F32)(mat[0][2] > 0 ? -RAD(90.0) : RAD(90.0));
// 		R = 0;
// 	}
// 	else
// 	{
// 		P = (F32)fatan2(-mat[1][2],mat[2][2]);
// 		cosP = (F32)fcos(P);
// 		if (cosP == 0.0) {
// 			if (P > 0.0) {
// 				Y = (F32)fatan2(-mat[0][2], -mat[1][2]);
// 				R = (F32)fatan2(-mat[2][0], -mat[2][1]);
// 			} else {
// 				Y = (F32)fatan2(-mat[0][2], mat[1][2]);
// 				R = (F32)fatan2( mat[2][0], mat[2][1]);
// 			}
// 		} else {
// 			cosY = mat[2][2] / cosP;
// 			Y = (F32)fatan2(-mat[0][2],cosY);
// 			R = (F32)fatan2(-mat[0][1] / cosY, mat[0][0] / cosY);
// 		}
// 	}
// 	pyr[0] = P;
// 	pyr[1] = Y;
// 	pyr[2] = R;
// }
// 

void getMat3YPR(const Mat3 mat,F32 *pyr)
{
	F64 R,P,Y;
	F64 cosP,cosR;

	if ( fabsf( 1.0f - fabsf( mat[2][1] ) ) < 0.0001f )
	{
		Y = atan2(-mat[0][2],mat[0][0]);
		P = mat[2][1] > 0 ? RAD(90.0) : -RAD(90.0);
		R = 0;
	}
	else
	{
		R = atan2(-mat[0][1],mat[1][1]);
		cosR = cos(R);
		if (fabs(cosR) < 0.0001f) {
			if (R > 0.0) {
				P = atan2( mat[2][1],-mat[0][1]);
				Y = atan2(-mat[1][2], mat[1][0]);
			} else {
				P = atan2( mat[2][1], mat[0][1]);
				Y = atan2( mat[1][2],-mat[1][0]);
			}
		} else {
			cosP = mat[1][1] / cosR;
			P = atan2( mat[2][1],cosP);
			Y = atan2( mat[2][0] / cosP,mat[2][2] / cosP);
		}
	}
	if (P < -16.f)
		P = 0;
	if (Y < -16.f)
		Y = 0;
	if (R < -16.f)
		R = 0;
	pyr[0] = (F32)P;
	pyr[1] = (F32)Y;
	pyr[2] = (F32)R;

	if (!FINITEVEC3(pyr))
		zeroVec3(pyr);
}


void sphericalCoordsToVec3(Vec3 vOut, F32 theta, F32 phi, F32 radius)
{
	F32 fSinPhi, fCosPhi, fSinTheta, fCosTheta;
	sincosf(phi, &fSinPhi, &fCosPhi);
	sincosf(theta, &fSinTheta, &fCosTheta);
	vOut[2] = radius * fCosTheta * fSinPhi;
	vOut[0] = radius * fSinTheta * fSinPhi;
	vOut[1] = radius * fCosPhi;
}


void posInterp(F32 t, const Vec3 T0, const Vec3 T1, Vec3 T)
{
	F32 vec[3];

	subVec3(T1, T0, vec);
	scaleVec3(vec, t, vec);
	addVec3(T0, vec, T);
}

bool planeIntersect(const Vec3 start,const Vec3 end,const Mat4 pmat,Vec3 cpos)
{
Vec3	tpA,tpB,dv;
F32		v1x,v1z,d,dist,da,tpx,tpz;

	//subVec3(start,pmat[3],dv);
	mulVecMat4Transpose(start,pmat,tpA);

	//subVec3(end,pmat[3],dv);
	mulVecMat4Transpose(end,pmat,tpB);

	// Are points on opposite sides of the plane
	if((tpA[1] > 0.0) && (tpB[1] > 0.0))
		return false;
	else if((tpA[1] < 0.0) && (tpB[1] < 0.0))
		return false;

	v1x = tpB[0]-tpA[0];
	v1z = tpB[2]-tpA[2];
	d = fsqrt(SQR(v1x) + SQR(v1z));
	if(d > 0.01) /* pA != pB */
	{
		/** Find the distance from tpA to the collision position - similar triangles **/
		dist = fabsf(tpA[1]) + fabsf(tpB[1]);
		if (dist == 0.0F) 
			da = (F32)((fabsf(tpA[1])) * 1000.0);
		else
			da = (F32)((fabsf(tpA[1])) / dist);
		/* v1 does not need to be normalized because it gets multiplied back by d in da */
		/***** Find the collision position on the plane *****/
		tpx = tpA[0] + v1x*da;
		tpz = tpA[2] + v1z*da;
	} else /* pA = pB */ {
		tpx = tpA[0];
		tpz = tpA[2];
	}
	dv[0] = tpx;
	dv[1] = 0;
	dv[2] = tpz;
	mulVecMat4(dv,pmat,cpos);
	return true;
}

F32	pointPlaneDist(const Vec3 v,const Vec3 n,const Vec3 pt)
{
	return dotVec3(n, pt)-dotVec3(n, v);
}

#define TRI_EPSILON 0.0001f

bool triangleLineIntersect(const Vec3 orig, const Vec3 end, const Vec3 vert0, const Vec3 vert1, const Vec3 vert2, Vec3 collision_point)
{
	Vec3 edge1, edge2, tvec, pvec, qvec, dir;
	F32 det, inv_det, t, u, v;

	subVec3(end, orig, dir);

	/* find vectors for two edges sharing vert0 */
	subVec3(vert1, vert0, edge1);
	subVec3(vert2, vert0, edge2);

	/* begin calculating determinant - also used to calculate U parameter */
	crossVec3(dir, edge2, pvec);

	/* if determinant is near zero, ray lies in plane of triangle */
	det = dotVec3(edge1, pvec);

	if (det > -TRI_EPSILON && det < TRI_EPSILON)
		return false;
	inv_det = 1.0 / det;

	/* calculate distance from vert0 to ray origin */
	subVec3(orig, vert0, tvec);

	/* calculate U parameter and test bounds */
	u = dotVec3(tvec, pvec) * inv_det;
	if (u < 0.0 || u > 1.0)
		return false;

	/* prepare to test V parameter */
	crossVec3(tvec, edge1, qvec);

	/* calculate V parameter and test bounds */
	v = dotVec3(dir, qvec) * inv_det;
	if (v < 0.0 || u + v > 1.0)
		return false;

	/* calculate t, ray intersects triangle */
	t = dotVec3(edge2, qvec) * inv_det;

	// CD: I'm assuming these are barycentric coords
	scaleVec3(vert0, t, collision_point);
	scaleAddVec3(vert1, u, collision_point, collision_point);
	scaleAddVec3(vert2, v, collision_point, collision_point);
	return true;
}

#define TRI_DP_EPSILON 0.01f

bool triangleLineIntersect2(const Vec3 line_p0, const Vec3 line_p1, const Vec3 tri_verts[3], Vec3 intersection)
{
	Vec3 	dv0p0;
	Vec3 	dp1p0;
	int		side_count = 0, unsure_count = 0;
	int		i;
	F32		r, d2, dp;
	Vec3	tri_normal;

	subVec3(tri_verts[0], line_p0, dv0p0);
	subVec3(line_p1, line_p0, dp1p0);

	makePlaneNormal(tri_verts[0], tri_verts[1], tri_verts[2], tri_normal);
	dp = dotVec3(tri_normal, dp1p0);
	if (!dp)
		return false; // TODO - perpendicular surfaces, should still check intersection with edges (line-line intersection)
	r = dotVec3(tri_normal, dv0p0) / dp;

	scaleVec3(dp1p0, r, intersection);
	addVec3(intersection, line_p0, intersection);

	d2 = distance3Squared(line_p0, line_p1);
	if (distance3Squared(line_p0, intersection) > d2 || distance3Squared(line_p1, intersection) > d2)
		return false;

	// Check if the point is in the triangle.
	for (i = 0; i < 3; ++i)
	{
		Vec3 temp;
		Vec3 pnormal;
		if (nearSameVec3(intersection, tri_verts[i]))
		{
			copyVec3(tri_verts[i], intersection);
			return true;
		}
		subVec3(tri_verts[(i+1)%3], tri_verts[i], temp);
		crossVec3(temp, tri_normal, pnormal);
		subVec3(intersection, tri_verts[i], temp);
		dp = dotVec3(pnormal, temp);
		if (dp > TRI_DP_EPSILON)
			side_count++;
		else if (dp > -TRI_DP_EPSILON)
			unsure_count++;
	}

	return !side_count || (side_count + unsure_count) == 3;
}


AUTO_COMMAND;
void testTriangleLineIntersect(void)
{
	Vec3 vert0 = {0, 0, 0};
	Vec3 vert1 = {0, 0, 4};
	Vec3 vert2 = {4, 0, 4};
	Vec3 start = {1, 1, 3};
	Vec3 end = {1, -1, 3};
	Vec3 intersect;

	assert(triangleLineIntersect(start, end, vert0, vert1, vert2, intersect));
	assert(intersect[0] == 1 && intersect[1] == 0 && intersect[2] == 3);
}


void closestPointOnTriangle( const Vec3 v0,const Vec3 v1,const Vec3 v2, const Vec3 vPos, Vec3 vOut )
{
	Vec3 vEdge0,vEdge1,vToTri;
	F32 a,b,c,d,e;
	F32 det,s,t;
	subVec3(v1,v0,vEdge0);
	subVec3(v2,v0,vEdge1);
	subVec3(v0,vPos,vToTri);

	a = dotVec3(vEdge0,vEdge0);
	b = dotVec3(vEdge0,vEdge1);
	c = dotVec3(vEdge1,vEdge1);
	d = dotVec3(vEdge0,vToTri);
	e = dotVec3(vEdge1,vToTri);

    det = a*c - b*b;
    s = b*e - c*d;
    t = b*d - a*e;

    if ( s + t < det )
    {
        if ( s < 0.f )
        {
            if ( t < 0.f )
            {
                if ( d < 0.f )
                {
                    s = CLAMPF( -d/a, 0.f, 1.f );
                    t = 0.f;
                }
                else
                {
                    s = 0.f;
                    t = CLAMPF( -e/c, 0.f, 1.f );
                }
            }
            else
            {
                s = 0.f;
                t = CLAMPF( -e/c, 0.f, 1.f );
            }
        }
        else if ( t < 0.f )
        {
            s = CLAMPF( -d/a, 0.f, 1.f );
            t = 0.f;
        }
        else
        {
            F32 invDet = 1.f / det;
            s *= invDet;
            t *= invDet;
        }
    }
    else
    {
        if ( s < 0.f )
        {
            F32 tmp0 = b+d;
            F32 tmp1 = c+e;
            if ( tmp1 > tmp0 )
            {
                F32 numer = tmp1 - tmp0;
                F32 denom = a-2*b+c;
                s = CLAMPF( numer/denom, 0.f, 1.f );
                t = 1-s;
            }
            else
            {
                t = CLAMPF( -e/c, 0.f, 1.f );
                s = 0.f;
            }
        }
        else if ( t < 0.f )
        {
            if ( a+d > b+e )
            {
                F32 numer = c+e-b-d;
                F32 denom = a-2*b+c;
                s = CLAMPF( numer/denom, 0.f, 1.f );
                t = 1-s;
            }
            else
            {
                s = CLAMPF( -e/c, 0.f, 1.f );
                t = 0.f;
            }
        }
        else
        {
            F32 numer = c+e-b-d;
            F32 denom = a-2*b+c;
            s = CLAMPF( numer/denom, 0.f, 1.f );
            t = 1.f - s;
        }
    }

	scaleVec3(vEdge0,s,vOut);
	scaleAddVec3(vEdge1,t,vOut,vOut);
	addVec3(v0,vOut,vOut);
}

#if 0

int log2_old(int val)
{
int		i;

	for(i=0;i<32;i++)
	{
		if ((1 << i) >= val)
			return i;
	}
	return -1;
}

int log2(int val) // gives ceiling, not floor
{
	int i;
	unsigned int r,v=val;
	const unsigned int b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
	const unsigned int S[] = {1, 2, 4, 8, 16};

	for (r = 0, i = 4; i >= 0; i--)
	{
	  if (v & b[i])
	  {
		v >>= S[i];
		r |= S[i];
	  } 
	}
	if (val & (val-1))
		return r+1;
	return r;
}

int pow2(int val)
{
	return 1 << log2(val);
}

#endif

int log2_floor(int val)
{
	int i;
	unsigned int r,v=val;
	const unsigned int b[] = {0x2, 0xC, 0xF0, 0xFF00, 0xFFFF0000};
	const unsigned int S[] = {1, 2, 4, 8, 16};

	for (r = 0, i = 4; i >= 0; i--)
	{
		if (v & b[i])
		{
			v >>= S[i];
			r |= S[i];
		} 
	}
	return r;
}

int randUIntGivenSeed(int * rand)
{
	return(((*rand = *rand * 214013L + 2531011L) >> 16) & 0x7fff);
}

int randIntGivenSeed(int * rand)
{
	return(((*rand = *rand * 214013L + 2531011L) >> 16) & 0xffff);
}

//mw a work in progress
F32 quick_fsqrt( F32 x )
{
	// the other version of this function didnt seem to work
	// at all after about 9, so i added one that does work,
	// but with a 5% margin of error, so the function can
	// be used while the other is being worked on
	// -bh
	U32 answer;
	answer = (((*(U32*)(&x)) - 0x3F800000) >> 1) + 0x3F800000;
	return *(F32*)(&(answer));


	// this is the original
	//return (2.0 + (0.25 * (x - 4) - (0.015625 * (x - 4) * (x - 4)) ));
}



//TO DO describe this thing
//this is really a math function.
F32	circleDelta( F32 org, F32 dest )
{
#define	RAD_NORM( a ) while( a < 0 ) a += ( PI * 2.f ); while( a > ( PI * 2.f ) ) a -= ( PI * 2.f );

	F32	delta;

	RAD_NORM( org );
	RAD_NORM( dest );

	if( org < dest )
	{
		delta = dest - org;

		if( delta <= PI )
			return delta;

		return (F32)(-( ( PI * 2.f ) - dest ) + org);
	}
	
	delta = org - dest;

	if( delta <= PI )
		return -delta;
	
	return (F32)(( ( PI * 2.f ) - org ) + dest);
}


//this is really a math function.
void circleDeltaVec3(const Vec3 org, const Vec3 dest, Vec3 delta)
{
	int	i;

	for( i = 0; i < 3; i++ )
		delta[i] = circleDelta( org[i], dest[i] );
}


int finiteVec3(const Vec3 y)
{
	return ( (FINITE((y)[0])) && (FINITE((y)[1])) && (FINITE((y)[2])) );
}

int finiteVec4(const Vec4 y)
{
	return ( (FINITE((y)[0])) && (FINITE((y)[1])) && (FINITE((y)[2])) && (FINITE((y)[3])) );
}

//Looks like it'll work for "squished" spheres too.  If you just assume rx = ry = rz = the radius, 
//it works for a normal sphere =).  I use http://astronomy.swin.edu.au/~pbourke/geometry/ for all my 
//intersection math and things like that, it's a good site =).
//(DefTracker *tracker,CollInfo *coll)
	/*sphere_radius = tracker->radius;
	line_len = coll->line_len;
	sphere_mid = tracker->mid;
	line_start = coll->start;
	line_dir   = coll->dir;*/
int sphereLineCollision( F32 sphere_radius, const Vec3 sphere_mid, const Vec3 line_start, const Vec3 line_end )
{
	F32		d,rad,t,line_len;
	Vec3	dv,dv_vecIn,dv_ln,line_pos,line_dir;
	
	PERFINFO_AUTO_START_FUNC_L2();

	subVec3( line_start, line_end, line_dir );
	line_len = normalVec3( line_dir );

	// First do a quick sphere-sphere test. Since a lot of the lines are short this discards a bunch
	rad = sphere_radius + line_len;
	subVec3(sphere_mid, line_start, dv_vecIn);
	t = line_len + rad;
	if (lengthVec3Squared(dv_vecIn) > t*t)
	{
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	// Get dot product of point and line seg
	// Is the sphere off either end of the line?
	d = dotVec3(dv_vecIn,line_dir);
	if (d < -rad || d > line_len + rad)
	{
		PERFINFO_AUTO_STOP_L2();
		return 0;
	}

	// Get point on line closest to given point
	scaleVec3(line_dir,d,dv_ln);
	addVec3(dv_ln,line_start,line_pos);

	// How far apart are they?
	subVec3(line_pos,sphere_mid,dv);
	d = lengthVec3Squared(dv);

	PERFINFO_AUTO_STOP_L2();

	return d < rad*rad;
}

bool sphereLineCollisionWithHitPoint(const Vec3 start, const Vec3 end, const Vec3 mid, F32 radius, Vec3 hit)
{
	F32 a, b, c, t1, t2;
	F32 bb4ac;
	Vec3 diff, diff2;

	PERFINFO_AUTO_START_FUNC_L2();

	subVec3(end, start, diff);
	subVec3(start, mid, diff2);

	a = lengthVec3Squared(diff);

	b = 2 * dotVec3(diff, diff2);

	c = lengthVec3Squared(mid) + lengthVec3Squared(start);
	c -= 2 * dotVec3(mid, start);
	c -= radius * radius;

	bb4ac = b * b - 4 * a * c;

	if (ABS(a) < EPSILON || bb4ac < 0)
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	t1 = (-b + sqrt(bb4ac)) / (2 * a);
	t2 = (-b - sqrt(bb4ac)) / (2 * a);

	if (t1 < t2)
		scaleAddVec3(diff, t1, start, hit);
	else
		scaleAddVec3(diff, t2, start, hit);

	PERFINFO_AUTO_STOP_L2();
	return true;
}

// This does NOT test against the ends of the cylinder
bool cylinderLineCollisionWithHitPoint(const Vec3 linePos, const Vec3 lineDir, F32 lineLen, const Vec3 cylPos, const Vec3 cylDir, F32 cylLen, F32 radius, Vec3 hit)
{
	F32 a, b, c, bb4ac, t1, t2, t = 0;
	Vec3 vTemp, alinePos, alineDir;
	Vec3 diff, diff2;
	Mat3 cylDirMat, cylDirMatInv;

	PERFINFO_AUTO_START_FUNC_L2();

	orientMat3(cylDirMat,cylDir);
	if(!invertMat3(cylDirMat,cylDirMatInv)){
		devassertmsgf(0, "Invalid input matrix");
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	mulVecMat3(lineDir,cylDirMatInv,alineDir);
	normalVec3(alineDir);
	
	subVec3(linePos,cylPos,vTemp);
	mulVecMat3(vTemp,cylDirMatInv,alinePos); // alinePos is now relative to a cylPos being at the origin, which makes the rest of this easier
	
	scaleVec3(alineDir,lineLen,diff);
	diff[2] = 0;
	a = lengthVec3Squared(diff);

	copyVec3(alinePos,diff2);
	diff2[2] = 0;
	b = 2 * dotVec3(diff,diff2);

	c = alinePos[0] * alinePos[0] + alinePos[1] * alinePos[1] - radius * radius;
	
	bb4ac = b * b - 4 * a * c;

	if (ABS(a) < EPSILON || bb4ac < 0)
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	t1 = (-b + sqrt(bb4ac)) / (2 * a);
	t2 = (-b - sqrt(bb4ac)) / (2 * a);

	if (t1 > 0 && t1 < 1)
	{
		if (t2 > 0 && t2 < 1)
			t = t1 < t2 ? t1 : t2;
		else
			t = t1;
	}
	else if (t2 > 0 && t2 < 1)
	{
		t = t2;
	}

	if(t!=0)
	{
		t *= lineLen;
		scaleAddVec3(lineDir,t,linePos,hit);
	}

	PERFINFO_AUTO_STOP_L2();
	return (t!=0);
}


/* Function graphInterp
*	Give a set of x, y pairs and an x, return the appropriate y value using
*	linear interpolation beteween points.  The give x, y pairs must be
*	sorted in decreasing value on x.
*	
*	First the function determines which two points are appropriate for
*	for use for interpolation using a linear search.  It compares the given
*	x value with all graph point x values.  Note that this function does no 
*	bounds checking on the given x value.  It is assumed that the graph points 
*	define the range of all possible x values.
*
*/

float graphInterp(float x, const Vec3* interpPoints){
	int interpPointCursor = 0;
	float interpStrength;
	Vec3 interpResult;

	// Decide which interpolation points to use
	//	When the loop is done, we've found interp points, interpPointCursor - 1 and interpPointCursor,
	//	to use as proper interpolation points.
	//  Due to the way the interpPoints are assumed to be sorted, interpPointCursor - 1 should always
	//	index the x, y pair with the greater x value.
	while(x < interpPoints[interpPointCursor][0])
		interpPointCursor++;

	interpStrength = (x - interpPoints[interpPointCursor][0])/
		(interpPoints[interpPointCursor - 1][0] - interpPoints[interpPointCursor][0]);

	posInterp(interpStrength, interpPoints[interpPointCursor], interpPoints[interpPointCursor - 1], interpResult);
	return interpResult[1];
}


bool sphereOrientBoxCollision(const Vec3 point, F32 radius, const Vec3 min, const Vec3 max, const Mat4 mat, const Mat4 inv_mat)
{
	Mat4 mat_inverse;
	Vec3 mid_bounds;
	bool ret;

	PERFINFO_AUTO_START_FUNC_L2();

	if (!inv_mat)
	{
		invertMat4Copy(mat, mat_inverse);
		inv_mat = mat_inverse;
	}

	// transform sphere into box's space and check
	mulVecMat4(point, inv_mat, mid_bounds);
	ret = boxSphereCollision(min, max, mid_bounds, radius);

	PERFINFO_AUTO_STOP_L2();

	return ret;
}

// CD: probably not optimal, but it is accurate and simple
// RMARR: not optimal, not accurate.  Returns false positives and requires matrix inverts.
bool orientBoxBoxCollisionOld(const Vec3 min1, const Vec3 max1, const Mat4 mat1, const Mat4 inv_mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2, const Mat4 inv_mat2)
{
	Mat4 mat1_inverse, mat2_inverse, mat;
	Vec3 min_bounds, max_bounds;
	PERFINFO_AUTO_START_FUNC();

	if (!inv_mat2)
	{
		invertMat4Copy(mat2, mat2_inverse);
		inv_mat2 = mat2_inverse;
	}

	// transform box1 into box2's local space
	mulMat4Inline(inv_mat2, mat1, mat);
	mulBoundsAA(min1, max1, mat, min_bounds, max_bounds);
	if (!boxBoxCollision(min_bounds, max_bounds, min2, max2))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	if (!inv_mat1)
	{
		invertMat4Copy(mat1, mat1_inverse);
		inv_mat1 = mat1_inverse;
	}

	// transform box2 into box1's local space
	mulMat4Inline(inv_mat1, mat2, mat);
	mulBoundsAA(min2, max2, mat, min_bounds, max_bounds);
	if (!boxBoxCollision(min_bounds, max_bounds, min1, max1))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}


#define boxExtentsOverlap(min1, max1, min2, max2) (!(min1 > max2 || min2 > max1))


void projectBox(const Vec3 vAxis, const OrientedBoundingBox* box, float* pfMin, float* pfMax)
{
	float fPos = dotVec3(box->vPos, vAxis);
	Vec3 vProj;
	float fProj;

	vProj[0] = fabsf(dotVec3(vAxis, box->m[0])) * box->vHalfSize[0];
	vProj[1] = fabsf(dotVec3(vAxis, box->m[1])) * box->vHalfSize[1];
	vProj[2] = fabsf(dotVec3(vAxis, box->m[2])) * box->vHalfSize[2];

	fProj = vProj[0] + vProj[1] + vProj[2];

	(*pfMin) = fPos - fProj;
	(*pfMax) = fPos + fProj;
}

bool checkAxis(const Vec3 vAxis, const OrientedBoundingBox* b1, const OrientedBoundingBox* b2)
{
	F32 fMin1, fMax1, fMin2, fMax2;
	projectBox(vAxis, b1, &fMin1, &fMax1);
	projectBox(vAxis, b2, &fMin2, &fMax2);
	return boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2);
}

// This should be generally faster than orientBoxBoxCollision, but is not yet thoroughly tested (pretty well exercised now - [RMARR - 10/3/13])
bool orientBoxBoxCollisionFast(const OrientedBoundingBox* box1, const OrientedBoundingBox* box2)
{
	int i, j;
	for (i=0; i<3; ++i)
	{
		if (!checkAxis(box1->m[i], box1, box2))
			return false;
	}
	for (i=0; i<3; ++i)
	{
		if (!checkAxis(box2->m[i], box1, box2))
			return false;
	}
	for (i=0; i<3; ++i)
	{
		for (j=0; j<3; ++j)
		{
			Vec3 vAxis;
			crossVec3(box1->m[i], box2->m[j], vAxis);
			if (!checkAxis(vAxis, box1, box2))
				return false;
		}
	}
	return true;
}


// For testing equivalence of broken algorithm to accurate one.  :-|
#if 0
bool orientBoxBoxCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1, const Mat4 inv_mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2, const Mat4 inv_mat2)
{
	OrientedBoundingBox b1,b2;
	Vec3 vTemp;
	bool bOld,bNew;

 	bOld = orientBoxBoxCollisionOld(min1, max1, mat1, inv_mat1, min2, max2, mat2, inv_mat2);

	copyMat3(mat1,b1.m);
	lerpVec3(min1,0.5f,max1,vTemp);
	mulVecMat4(vTemp,mat1,b1.vPos);
	subVec3(max1,min1,b1.vHalfSize);
	scaleVec3(b1.vHalfSize,0.5f,b1.vHalfSize);

	copyMat3(mat2,b2.m);
	lerpVec3(min2,0.5f,max2,vTemp);
	mulVecMat4(vTemp,mat2,b2.vPos);
	subVec3(max2,min2,b2.vHalfSize);
	scaleVec3(b2.vHalfSize,0.5f,b2.vHalfSize);

	bNew = orientBoxBoxCollisionFast(&b1,&b2);


	assert(bOld == bNew);
	return bOld;
}
#endif

bool orientBoxBoxCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2)
{
	OrientedBoundingBox b1,b2;
	Vec3 vTemp;

	copyMat3(mat1,b1.m);
	lerpVec3(min1,0.5f,max1,vTemp);
	mulVecMat4(vTemp,mat1,b1.vPos);
	subVec3(max1,min1,b1.vHalfSize);
	scaleVec3(b1.vHalfSize,0.5f,b1.vHalfSize);

	copyMat3(mat2,b2.m);
	lerpVec3(min2,0.5f,max2,vTemp);
	mulVecMat4(vTemp,mat2,b2.vPos);
	subVec3(max2,min2,b2.vHalfSize);
	scaleVec3(b2.vHalfSize,0.5f,b2.vHalfSize);

	return orientBoxBoxCollisionFast(&b1,&b2);
}

__forceinline void projectBox2(const Vec3 vAxis, const Vec3 vPos, const Vec2 vHalfSize, float* pfMin, float* pfMax)
{
	float fPos = dotVec3(vPos, vAxis);
	Vec3 vProj;
	float fProj;

	vProj[0] = fabsf(vAxis[0]) * vHalfSize[0];
	vProj[1] = fabsf(vAxis[1]) * vHalfSize[1];

	fProj = vProj[0] + vProj[1];

	(*pfMin) = fPos - fProj;
	(*pfMax) = fPos + fProj;
}

// This code is a literal reduction of orientBoxBoxCollision.  It's substantially leaner, but I have no idea if it's optimal.  It makes the assumption
// that we are in the space of the Rect.  I had to hand-roll things because the optimizer was unable to take advantage of the fact that certain
// vectors were known to be all 1s and 0s.
bool orientBoxRectCollision(const Vec3 min1, const Vec3 max1, const Mat4 mat1,
						   const Vec3 min2, const Vec3 max2)
{
	OrientedBoundingBox b1,b2;
	Vec3 vTemp;

	copyMat3(mat1,b1.m);
	lerpVec3(min1,0.5f,max1,vTemp);
	mulVecMat4(vTemp,mat1,b1.vPos);
	subVec3(max1,min1,b1.vHalfSize);
	scaleVec3(b1.vHalfSize,0.5f,b1.vHalfSize);

	lerpVec3(min2,0.5f,max2,b2.vPos);
	subVec3(max2,min2,b2.vHalfSize);
	scaleVec3(b2.vHalfSize,0.5f,b2.vHalfSize);

	{	
		int i;
		for (i=0; i<3; ++i)
		{
			F32 fMin1, fMax1, fMin2, fMax2;
			projectBox(b1.m[i], &b1, &fMin1, &fMax1);
			projectBox2(b1.m[i], b2.vPos, b2.vHalfSize, &fMin2, &fMax2);
			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;
		}
		{
			Vec3 vProj;
			float fProj;
			F32 fMin1, fMax1, fMin2, fMax2;

			{
				vProj[0] = fabsf(b1.m[0][0]) * b1.vHalfSize[0];
				vProj[1] = fabsf(b1.m[1][0]) * b1.vHalfSize[1];
				vProj[2] = fabsf(b1.m[2][0]) * b1.vHalfSize[2];

				fProj = vProj[0] + vProj[1] + vProj[2];

				fMin1 = b1.vPos[0] - fProj;
				fMax1 = b1.vPos[0] + fProj;

				fMin2 = b2.vPos[0] - b2.vHalfSize[0];
				fMax2 = b2.vPos[0] + b2.vHalfSize[0];
			}

			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;

			{
				vProj[0] = fabsf(b1.m[0][1]) * b1.vHalfSize[0];
				vProj[1] = fabsf(b1.m[1][1]) * b1.vHalfSize[1];
				vProj[2] = fabsf(b1.m[2][1]) * b1.vHalfSize[2];

				fProj = vProj[0] + vProj[1] + vProj[2];

				fMin1 = b1.vPos[1] - fProj;
				fMax1 = b1.vPos[1] + fProj;

				fMin2 = b2.vPos[1] - b2.vHalfSize[1];
				fMax2 = b2.vPos[1] + b2.vHalfSize[1];
			}

			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;

			{
				vProj[0] = fabsf(b1.m[0][2]) * b1.vHalfSize[0];
				vProj[1] = fabsf(b1.m[1][2]) * b1.vHalfSize[1];
				vProj[2] = fabsf(b1.m[2][2]) * b1.vHalfSize[2];

				fProj = vProj[0] + vProj[1] + vProj[2];

				fMin1 = b1.vPos[2] - fProj;
				fMax1 = b1.vPos[2] + fProj;

				fMin2 = b2.vPos[2]; //b2.vHalfSize[2] is 0
				fMax2 = b2.vPos[2];
			}

			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;
		}
		for (i=0; i<3; ++i)
		{
			F32 fMin1, fMax1, fMin2, fMax2;
			Vec3 vAxis;

			// hand-rolled cross-products cause the optimizer is too dumb.
			vAxis[0] = 0.0f;
			vAxis[1] = b1.m[i][2];
			vAxis[2] = -b1.m[i][1];

			projectBox(vAxis, &b1, &fMin1, &fMax1);
			projectBox2(vAxis, b2.vPos, b2.vHalfSize, &fMin2, &fMax2);
			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;

			vAxis[0] = -b1.m[i][2];
			vAxis[1] = 0.0f;
			vAxis[2] = b1.m[i][0];
			projectBox(vAxis, &b1, &fMin1, &fMax1);
			projectBox2(vAxis, b2.vPos, b2.vHalfSize, &fMin2, &fMax2);
			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;

			vAxis[0] = b1.m[i][1];
			vAxis[1] = -b1.m[i][0];
			vAxis[2] = 0.0f;
			projectBox(vAxis, &b1, &fMin1, &fMax1);
			projectBox2(vAxis, b2.vPos, b2.vHalfSize, &fMin2, &fMax2);
			if (!boxExtentsOverlap(fMin1, fMax1, fMin2, fMax2))
				return false;
		}
		return true;
	}
}

bool orientBoxBoxContained(const Vec3 min1, const Vec3 max1, const Mat4 mat1, const Mat4 inv_mat1,
						   const Vec3 min2, const Vec3 max2, const Mat4 mat2, const Mat4 inv_mat2)
{
	Mat4 mat1_inverse, mat;
	Vec3 min_bounds, max_bounds;

	if (!inv_mat1)
	{
		invertMat4Copy(mat1, mat1_inverse);
		inv_mat1 = mat1_inverse;
	}

	// transform box2 into box1's local space
	mulMat4Inline(inv_mat1, mat2, mat);
	mulBoundsAA(min2, max2, mat, min_bounds, max_bounds);
	return pointBoxCollision(min_bounds, min1, max1) && pointBoxCollision(max_bounds, min1, max1);
}


bool lineBoxCollision( const Vec3 start, const Vec3 end, const Vec3 min, const Vec3 max, Vec3 intersect )
{
	int i, found[3] = {0};
	bool inside = true, whichplane = 0;
	float dist;
	Vec3 dir, MaxT;

	PERFINFO_AUTO_START_FUNC_L2();

	MaxT[0]=MaxT[1]=MaxT[2]=-1.0f;
	dist = distance3( start, end );
	
	if(dist == 0.f){
		bool ret = pointBoxCollision(start, min, max);
		if (ret) 
			copyVec3(start, intersect);
		PERFINFO_AUTO_STOP_L2();
		return ret;
	}

	setVec3( dir, (end[0]-start[0])/dist, (end[1]-start[1])/dist, (end[2]-start[2])/dist );

	// Find candidate planes.
	for(i=0;i<3;i++)
	{
		if(start[i] < min[i])
		{
			intersect[i] = min[i];
			inside = false;

			// Calculate T distances to candidate planes
			if(dir[i] != 0.f)
			{
				MaxT[i] = (min[i] - start[i]) / dir[i];
				found[i] = true;
			}
		}
		else if(start[i] > max[i])
		{
			intersect[i] = max[i];
			inside = false;

			// Calculate T distances to candidate planes
			if(dir[i] != 0.f)
			{
				MaxT[i] = (max[i] - start[i]) / dir[i];
				found[i] = true;
			}
		}
	}

	// Ray origin inside bounding box
	if(inside)
	{
		copyVec3(start, intersect);
		PERFINFO_AUTO_STOP_L2();
		return true;
	}

	// Get largest of the maxT's for final choice of intersection
	if(MaxT[1] > MaxT[whichplane])	whichplane = 1;
	if(MaxT[2] > MaxT[whichplane])	whichplane = 2;

	if(!found[whichplane])
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	// Check final candidate actually inside box
	for(i=0;i<3;i++)
	{
		if(i!=whichplane)
		{
			intersect[i] = start[i] + MaxT[whichplane] * dir[i];
			if(intersect[i] < min[i] || intersect[i] > max[i])
			{
				PERFINFO_AUTO_STOP_L2();
				return false;
			}
		}
	}

	PERFINFO_AUTO_STOP_L2();
	return true;	// ray hits box
}

bool lineOrientedBoxCollision( const Vec3 start, const Vec3 end, const Mat4 local_to_world_mat, const Mat4 world_to_local_mat, const Vec3 local_min, const Vec3 local_max, Vec3 intersect)
{
	// Rotate line into box space
	bool bResult;
	Vec3 local_intersect;
	Vec3 local_start, local_end;
	mulVecMat4(start, world_to_local_mat, local_start);
	mulVecMat4(end, world_to_local_mat, local_end);

	bResult = lineBoxCollision(local_start, local_end, local_min, local_max, local_intersect);

	if (bResult && intersect)
		mulVecMat4(local_intersect, local_to_world_mat, intersect);

	return bResult;
}

__forceinline static bool intersection(float fDst1, float fDst2, const Vec3 start, const Vec3 end, const Vec3 diff, Vec3 hit)
{
	F32 scale;
	if (fDst1 * fDst2 >= 0)
		return false;
	if (fDst1 == fDst2)
		return false;
	scale = -fDst1 / (fDst2 - fDst1);
	scaleAddVec3(diff, scale, start, hit);
	return true;
}

__forceinline static bool inBox1(Vec3 intersect, const Vec3 min, const Vec3 max)
{
	return intersect[2] > min[2] && intersect[2] < max[2] && intersect[1] > min[1] && intersect[1] < max[1];
}

__forceinline static bool inBox2(Vec3 intersect, const Vec3 min, const Vec3 max)
{
	return intersect[2] > min[2] && intersect[2] < max[2] && intersect[0] > min[0] && intersect[0] < max[0];
}

__forceinline static bool inBox3(Vec3 intersect, const Vec3 min, const Vec3 max)
{
	return intersect[0] > min[0] && intersect[0] < max[0] && intersect[1] > min[1] && intersect[1] < max[1];
}

// CD: I'm 100% sure this is not the optimal way to do this, so if it becomes a performance problem it should be replaced
bool lineBoxCollisionHollow(const Vec3 start, const Vec3 end, const Vec3 min, const Vec3 max, Vec3 intersect, VolumeFaces face_bits)
{
	Vec3 start_min, end_min;
	Vec3 start_max, end_max;
	bool has_hit = false;
	F32 intersect_dist_sqrd = 0; // just to make /ANALYZE happy
	Vec3 hit, diff;
	int i;

	PERFINFO_AUTO_START_FUNC_L2();

	if (!face_bits)
		face_bits = VOLFACE_ALL;

	subVec3(start, min, start_min);
	subVec3(end, min, end_min);
	subVec3(start, max, start_max);
	subVec3(end, max, end_max);

	// find a separating axis between the endpoints and the box
	for (i = 0; i < 3; ++i)
	{
		if (end_min[i] < 0 && start_min[i] < 0)
		{
			PERFINFO_AUTO_STOP_L2();
			return false;
		}
		if (end_max[i] > 0 && start_max[i] > 0)
		{
			PERFINFO_AUTO_STOP_L2();
			return false;
		}
	}

	// check if the line is completely in the box
	if (start_min[0] > 0 && start_max[0] < 0 && start_min[1] > 0 && start_max[1] < 0 && start_min[2] > 0 && start_max[2] < 0 &&
		end_min[0] > 0 && end_max[0] < 0 && end_min[1] > 0 && end_max[1] < 0 && end_min[2] > 0 && end_max[2] < 0) 
	{
		PERFINFO_AUTO_STOP_L2();
		return false;
	}

	subVec3(end, start, diff);

	// intersect each face
	if ((face_bits & VOLFACE_NEGX) && intersection(start_min[0], end_min[0], start, end, diff, hit) && inBox1(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	if ((face_bits & VOLFACE_POSX) && intersection(start_max[0], end_max[0], start, end, diff, hit) && inBox1(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	if ((face_bits & VOLFACE_NEGY) && intersection(start_min[1], end_min[1], start, end, diff, hit) && inBox2(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	if ((face_bits & VOLFACE_POSY) && intersection(start_max[1], end_max[1], start, end, diff, hit) && inBox2(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	if ((face_bits & VOLFACE_NEGZ) && intersection(start_min[2], end_min[2], start, end, diff, hit) && inBox3(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	if ((face_bits & VOLFACE_POSZ) && intersection(start_max[2], end_max[2], start, end, diff, hit) && inBox3(hit, min, max))
	{
		F32 dist_sqrd = distance3Squared(start, hit);
		if (!has_hit || dist_sqrd < intersect_dist_sqrd)
		{
			has_hit = true;
			copyVec3(hit, intersect);
			intersect_dist_sqrd = dist_sqrd;
		}
	}

	PERFINFO_AUTO_STOP_L2();
	return has_hit;
}

bool boxSphereCollision(const Vec3 min1, const Vec3 max1, const Vec3 mid2, F32 radius2)
{
	Vec3 mid1, min2, max2;
	F32 radius1;

	radius1 = distance3(min1, max1) * 0.5f;
	centerVec3(min1, max1, mid1);
	if (!sphereSphereCollision(mid1, radius1, mid2, radius2))
		return false;

	addVec3same(mid2, -radius2, min2);
	addVec3same(mid2, radius2, max2);
	return boxBoxCollision(min1, max1, min2, max2);
}

void calcTransformVectors(const Vec3 pos[3], const Vec2 uv[3], Vec3 utransform, Vec3 vtransform)
{
	Vec3 v01, v02;
	Vec2 uv01, uv02;
	Vec2 inv0, inv1;

	utransform[0] = utransform[1] = utransform[2] = 0;
	vtransform[0] = vtransform[1] = vtransform[2] = 0;

	subVec3(pos[1], pos[0], v01);
	subVec3(pos[2], pos[0], v02);
	subVec2(uv[1], uv[0], uv01);
	subVec2(uv[2], uv[0], uv02);

	// |d1| = |u1 v1| * |Tu|
	// |d2|   |u2 v2|   |Tv|
	if (!invertMat2(uv01[0], uv02[0], uv01[1], uv02[1], inv0, inv1))
		return;

	// solve for Tu and Tv for each dx,dy,dz
	mulVecMat2_special(inv0, inv1, v01[0], v02[0], &utransform[0], &vtransform[0]);
	mulVecMat2_special(inv0, inv1, v01[1], v02[1], &utransform[1], &vtransform[1]);
	mulVecMat2_special(inv0, inv1, v01[2], v02[2], &utransform[2], &vtransform[2]);
}

void calcTransformVectorsAccurate(const Vec3 pos[3], const Vec2 uv[3], Vec3 utransform, Vec3 vtransform)
{
	DVec3 v01, v02;
	DVec2 uv01, uv02;
	DVec2 inv0, inv1;
	double t0, t1;

	utransform[0] = utransform[1] = utransform[2] = 0;
	vtransform[0] = vtransform[1] = vtransform[2] = 0;

	subVec3(pos[1], pos[0], v01);
	subVec3(pos[2], pos[0], v02);
	subVec2(uv[1], uv[0], uv01);
	subVec2(uv[2], uv[0], uv02);

	// |d1| = |u1 v1| * |Tu|
	// |d2|   |u2 v2|   |Tv|
	if (!invertDMat2(uv01[0], uv02[0], uv01[1], uv02[1], inv0, inv1))
		return;

	// solve for Tu and Tv for each dx,dy,dz
	mulDVecMat2_special(inv0, inv1, v01[0], v02[0], &t0, &t1);
	utransform[0] = (F32)t0; vtransform[0] = (F32)t1;
	mulDVecMat2_special(inv0, inv1, v01[1], v02[1], &t0, &t1);
	utransform[1] = (F32)t0; vtransform[1] = (F32)t1;
	mulDVecMat2_special(inv0, inv1, v01[2], v02[2], &t0, &t1);
	utransform[2] = (F32)t0; vtransform[2] = (F32)t1;
}

#define NEAR_ONE 0.999999f

// This function sets the matrix's z axis to the given direction vector.
// Note that for camera matrices the view direction is the negative z axis,
// so you will need to pass in a negative direction vector.
void orientMat3(Mat3 mat, const Vec3 dir)
{
	Vec3 dirnorm;
	copyVec3(dir, dirnorm);
	normalVec3(dirnorm);

	if (dirnorm[1] >= NEAR_ONE)
	{
		setVec3(mat[0], 1.f, 0.f, 0.f);
		setVec3(mat[1], 0.f, 0.f,-1.f);
		setVec3(mat[2], 0.f, 1.f, 0.f);
	}
	else if (dirnorm[1] <= -NEAR_ONE)
	{
		setVec3(mat[0], 1.f, 0.f, 0.f);
		setVec3(mat[1], 0.f, 0.f, 1.f);
		setVec3(mat[2], 0.f,-1.f, 0.f);
	}
	else
	{
		Vec3 up,xuv,yuv;
		setVec3(up, 0.f,1.f,0.f);
		crossVec3(up,dirnorm,xuv);
		normalVec3(xuv);
		crossVec3(dirnorm,xuv,yuv);
		normalVec3(yuv);
		copyVec3(xuv, mat[0]);
		copyVec3(yuv, mat[1]);
		copyVec3(dirnorm, mat[2]);
	}
}

// Generates an orthonormal mat3 with a Y vector equal to the normal,
// but Z vector pointed toward the forward vector

// assumes norm and forward are normalized, and that norm != forward
void orientMat3ToNormalAndForward(Mat3 mat, const Vec3 norm, const Vec3 forward)
{
	/*
	if (norm[1] >= NEAR_ONE)
		setVec3(mat[1], 0.f, 1.0f, 0.0f);
	else if (norm[1] <= -NEAR_ONE)
		setVec3(mat[1], 0.f, -1.0f, 0.0f);
	else
	*/
		copyVec3(norm, mat[1]);
	crossVec3(mat[1], forward, mat[0]);
	normalVec3(mat[0]);
	crossVec3(mat[0], mat[1], mat[2]);
	normalVec3(mat[2]);
}

// Generate PYR from a forward vector (and assuming 0,1,0 is up)
// void orientPYR(Vec3 pyr, const Vec3 dir)
// {
// 	// ST: Please make faster soon
// 	Mat3 mat;
// 	orientMat3(mat, dir);
// 	getMat3PYR(mat, pyr);
// }

// Generate PYR from a forward vector (and assuming 0,1,0 is up)
void orientYPR(Vec3 pyr, const Vec3 dir)
{
	// ST: Please make faster soon
	Mat3 mat;
	orientMat3(mat, dir);
	getMat3YPR(mat, pyr);
}

void orientMat3Yvec(Mat3 mat, const Vec3 dir, const Vec3 yvec)
{
	Vec3 up,xuv,yuv,dirnorm;

	copyVec3(dir, dirnorm);
	normalVec3(dirnorm);

	copyVec3(yvec, up);
	normalVec3(up);

	crossVec3(up,dirnorm,xuv);
	normalVec3(xuv);
	crossVec3(dirnorm,xuv,yuv);
	normalVec3(yuv);
	copyVec3(xuv, mat[0]);
	copyVec3(yuv, mat[1]);
	copyVec3(dirnorm, mat[2]);
}

F32 distanceToBoxSquared(const Vec3 boxmin, const Vec3 boxmax, const Vec3 point)
{
	float dist, d;

	if (point[0] < boxmin[0])
		d = point[0] - boxmin[0];
	else if (point[0] > boxmax[0] )
		d = point[0] - boxmax[0];
	else
		d = 0;
	dist = SQR(d);

	if (point[1] < boxmin[1])
		d = point[1] - boxmin[1];
	else if (point[1] > boxmax[1] )
		d = point[1] - boxmax[1];
	else
		d = 0;
	dist += SQR(d);

	if (point[2] < boxmin[2])
		d = point[2] - boxmin[2];
	else if (point[2] > boxmax[2] )
		d = point[2] - boxmax[2];
	else
		d = 0;
	dist += SQR(d);

	return dist;
}

// half->float and float->half conversions courtesy of OpenEXR
F16 F32toF16(F32 f)
{
	//
	// Our floating point number, f, is represented by the bit
	// pattern in integer i.  Disassemble that bit pattern into
	// the sign, s, the exponent, e, and the significand, m.
	// Shift s into the position where it will go in in the
	// resulting half number.
	// Adjust e, accounting for the different exponent bias
	// of float and half (127 versus 15).
	//
	int i = *((int *)&f);

	register int s =  (i >> 16) & 0x00008000;
	register int e = ((i >> 23) & 0x000000ff) - (127 - 15);
	register int m =   i        & 0x007fffff;

	if (f == 0)
		return 0;

	//
	// Now reassemble s, e and m into a half:
	//

	if (e <= 0)
	{
		if (e < -10)
		{
			//
			// E is less than -10.  The absolute value of f is
			// less than HALF_MIN (f may be a small normalized
			// float, a denormalized float or a zero).
			//
			// We convert f to a half zero.
			//

			return 0;
		}

		//
		// E is between -10 and 0.  F is a normalized float,
		// whose magnitude is less than HALF_NRM_MIN.
		//
		// We convert f to a denormalized half.
		// 

		m = (m | 0x00800000) >> (1 - e);

		//
		// Round to nearest, round "0.5" up.
		//
		// Rounding may cause the significand to overflow and make
		// our number normalized.  Because of the way a half's bits
		// are laid out, we don't have to treat this case separately;
		// the code below will handle it correctly.
		// 

		if (m &  0x00001000)
			m += 0x00002000;

		//
		// Assemble the half from s, e (zero) and m.
		//

		return s | (m >> 13);
	}
	else if (e == 0xff - (127 - 15))
	{
		if (m == 0)
		{
			//
			// F is an infinity; convert f to a half
			// infinity with the same sign as f.
			//

			return s | 0x7c00;
		}
		else
		{
			//
			// F is a NAN; we produce a half NAN that preserves
			// the sign bit and the 10 leftmost bits of the
			// significand of f, with one exception: If the 10
			// leftmost bits are all zero, the NAN would turn 
			// into an infinity, so we have to set at least one
			// bit in the significand.
			//

			m >>= 13;
			return s | 0x7c00 | m | (m == 0);
		}
	}
	else
	{
		//
		// E is greater than zero.  F is a normalized float.
		// We try to convert f to a normalized half.
		//

		//
		// Round to nearest, round "0.5" up
		//

		if (m &  0x00001000)
		{
			m += 0x00002000;

			if (m & 0x00800000)
			{
				m =  0;		// overflow in significand,
				e += 1;		// adjust exponent
			}
		}

		//
		// Handle exponent overflow
		//

		if (e > 30)
		{
			return s | 0x7c00;	// if this returns, the half becomes an
		}   			// infinity with the same sign as f.

		//
		// Assemble the half from s, e and m.
		//

		return s | (e << 10) | (m >> 13);
	}
}

F32 F16toF32(F16 y)
{
	int i;
	int s = (y >> 15) & 0x00000001;
	int e = (y >> 10) & 0x0000001f;
	int m =  y        & 0x000003ff;

	if (e == 0)
	{
		if (m == 0)
		{
			//
			// Plus or minus zero
			//

			i = s << 31;
			return *((F32 *)&i);
		}
		else
		{
			//
			// Denormalized number -- renormalize it
			//

			while (!(m & 0x00000400))
			{
				m <<= 1;
				e -=  1;
			}

			e += 1;
			m &= ~0x00000400;
		}
	}
	else if (e == 31)
	{
		if (m == 0)
		{
			//
			// Positive or negative infinity
			//

			i = (s << 31) | 0x7f800000;
			return *((F32 *)&i);
		}
		else
		{
			//
			// Nan -- preserve sign and significand bits
			//

			i = (s << 31) | 0x7f800000 | (m << 13);
			return *((F32 *)&i);
		}
	}

	//
	// Normalized number
	//

	e = e + (127 - 15);
	m = m << 13;

	//
	// Assemble s, e and m.
	//

	i = (s << 31) | (e << 23) | m;
	return *((F32 *)&i);
}

void F32toF16Vector(F16 *dst, F32 *src, int count)
{
	int i;
	for (i=0; i<count; i++) {
		dst[i] = F32toF16(src[i]);
	}
}

const F16 U8toF16[256] =
{
	0x0000,
	0x1C04,
	0x2004,
	0x2206,
	0x2404,
	0x2505,
	0x2606,
	0x2707,
	0x2804,
	0x2885,
	0x2905,
	0x2986,
	0x2A06,
	0x2A87,
	0x2B07,
	0x2B88,
	0x2C04,
	0x2C44,
	0x2C85,
	0x2CC5,
	0x2D05,
	0x2D45,
	0x2D86,
	0x2DC6,
	0x2E06,
	0x2E46,
	0x2E87,
	0x2EC7,
	0x2F07,
	0x2F47,
	0x2F88,
	0x2FC8,
	0x3004,
	0x3024,
	0x3044,
	0x3064,
	0x3085,
	0x30A5,
	0x30C5,
	0x30E5,
	0x3105,
	0x3125,
	0x3145,
	0x3165,
	0x3186,
	0x31A6,
	0x31C6,
	0x31E6,
	0x3206,
	0x3226,
	0x3246,
	0x3266,
	0x3287,
	0x32A7,
	0x32C7,
	0x32E7,
	0x3307,
	0x3327,
	0x3347,
	0x3367,
	0x3388,
	0x33A8,
	0x33C8,
	0x33E8,
	0x3404,
	0x3414,
	0x3424,
	0x3434,
	0x3444,
	0x3454,
	0x3464,
	0x3474,
	0x3485,
	0x3495,
	0x34A5,
	0x34B5,
	0x34C5,
	0x34D5,
	0x34E5,
	0x34F5,
	0x3505,
	0x3515,
	0x3525,
	0x3535,
	0x3545,
	0x3555,
	0x3565,
	0x3575,
	0x3586,
	0x3596,
	0x35A6,
	0x35B6,
	0x35C6,
	0x35D6,
	0x35E6,
	0x35F6,
	0x3606,
	0x3616,
	0x3626,
	0x3636,
	0x3646,
	0x3656,
	0x3666,
	0x3676,
	0x3687,
	0x3697,
	0x36A7,
	0x36B7,
	0x36C7,
	0x36D7,
	0x36E7,
	0x36F7,
	0x3707,
	0x3717,
	0x3727,
	0x3737,
	0x3747,
	0x3757,
	0x3767,
	0x3777,
	0x3788,
	0x3798,
	0x37A8,
	0x37B8,
	0x37C8,
	0x37D8,
	0x37E8,
	0x37F8,
	0x3804,
	0x380C,
	0x3814,
	0x381C,
	0x3824,
	0x382C,
	0x3834,
	0x383C,
	0x3844,
	0x384C,
	0x3854,
	0x385C,
	0x3864,
	0x386C,
	0x3874,
	0x387C,
	0x3885,
	0x388D,
	0x3895,
	0x389D,
	0x38A5,
	0x38AD,
	0x38B5,
	0x38BD,
	0x38C5,
	0x38CD,
	0x38D5,
	0x38DD,
	0x38E5,
	0x38ED,
	0x38F5,
	0x38FD,
	0x3905,
	0x390D,
	0x3915,
	0x391D,
	0x3925,
	0x392D,
	0x3935,
	0x393D,
	0x3945,
	0x394D,
	0x3955,
	0x395D,
	0x3965,
	0x396D,
	0x3975,
	0x397D,
	0x3986,
	0x398E,
	0x3996,
	0x399E,
	0x39A6,
	0x39AE,
	0x39B6,
	0x39BE,
	0x39C6,
	0x39CE,
	0x39D6,
	0x39DE,
	0x39E6,
	0x39EE,
	0x39F6,
	0x39FE,
	0x3A06,
	0x3A0E,
	0x3A16,
	0x3A1E,
	0x3A26,
	0x3A2E,
	0x3A36,
	0x3A3E,
	0x3A46,
	0x3A4E,
	0x3A56,
	0x3A5E,
	0x3A66,
	0x3A6E,
	0x3A76,
	0x3A7E,
	0x3A87,
	0x3A8F,
	0x3A97,
	0x3A9F,
	0x3AA7,
	0x3AAF,
	0x3AB7,
	0x3ABF,
	0x3AC7,
	0x3ACF,
	0x3AD7,
	0x3ADF,
	0x3AE7,
	0x3AEF,
	0x3AF7,
	0x3AFF,
	0x3B07,
	0x3B0F,
	0x3B17,
	0x3B1F,
	0x3B27,
	0x3B2F,
	0x3B37,
	0x3B3F,
	0x3B47,
	0x3B4F,
	0x3B57,
	0x3B5F,
	0x3B67,
	0x3B6F,
	0x3B77,
	0x3B7F,
	0x3B88,
	0x3B90,
	0x3B98,
	0x3BA0,
	0x3BA8,
	0x3BB0,
	0x3BB8,
	0x3BC0,
	0x3BC8,
	0x3BD0,
	0x3BD8,
	0x3BE0,
	0x3BE8,
	0x3BF0,
	0x3BF8,
	0x3C00,
};


static void tangentBasisOrig(Mat3 basis, const Vec3 pv0, const Vec3 pv1, const Vec3 pv2, const Vec2 t0, const Vec2 t1, const Vec2 t2, const Vec3 n)
{
	bool do_twist = false; // CD turning this off because it causes bad tangent spaces (completely incorrect)
	Vec3	cp;
	Vec3	e0 = { 0, t1[0] - t0[0], t1[1] - t0[1] };
	Vec3	e1 = { 0, t2[0] - t0[0], t2[1] - t0[1] };
	Vec3	dv,v0,v1,v2;
	bool valid = true;

	// twist vertices to match vertex normal
	if (do_twist) {
		// JE: This appears to sometimes rotate the tangent basis by over 45 degrees, which can be bad,
		// but seems to greatly reduce artifacts on round smooth helmets, which can be good,
		// so I'm leaving this in.
		subVec3(pv2,pv0,dv);
		crossVec3(dv,n,cp);
		addVec3(cp,pv0,v1);

		subVec3(pv1,pv0,dv);
		crossVec3(n,dv,cp);
		addVec3(cp,pv0,v2);
	}

	copyVec3(pv0,v0);

	if (!do_twist) {
		dv[0]=0;
		copyVec3(pv1,v1);
		copyVec3(pv2,v2);
	}

	// ok. now do the rest of it
	e0[0] = v1[0] - v0[0];
	e1[0] = v2[0] - v0[0];
	crossVec3(e0,e1,cp);

	if ( fabs(cp[0]) > EPSILON)
	{
		basis[0][0] = -cp[1] / cp[0];        
		basis[1][0] = -cp[2] / cp[0];
	}
	else
	{
		valid = false;
	}

	e0[0] = v1[1] - v0[1];
	e1[0] = v2[1] - v0[1];

	crossVec3(e0,e1,cp);
	if ( fabs(cp[0]) > EPSILON)
	{
		basis[0][1] = -cp[1] / cp[0];        
		basis[1][1] = -cp[2] / cp[0];
	}
	else
	{
		valid = false;
	}

	e0[0] = v1[2] - v0[2];
	e1[0] = v2[2] - v0[2];

	crossVec3(e0,e1,cp);
	if ( fabs(cp[0]) > EPSILON)
	{
		basis[0][2] = -cp[1] / cp[0];        
		basis[1][2] = -cp[2] / cp[0];
	}
	else
	{
		valid = false;
	}

	if (valid)
	{
		// tangent...
		normalVec3(basis[0]);

		// binormal...
		normalVec3(basis[1]);

		// normal...
		// compute the cross product TxB
		crossVec3(basis[0],basis[1],basis[2]);
		normalVec3(basis[2]);
	}
	else
	{
		orientMat3(basis,n);
	}

	// Gram-Schmidt orthogonalization process for B
	// compute the cross product B=NxT to obtain 
	// an orthogonal basis
	crossVec3(basis[2],basis[0],basis[1]);

	if (dotVec3(basis[2],n) < 0.f)
		negateVec3(basis[2],basis[2]);

}

static void orthogonalizeNv(Vec3 tangent, Vec3 binormal, Vec3 normal)
{
	// Copied from MeshMender
	Vec3 tmpTan;
	Vec3 tmpNorm;
	Vec3 tmpBin;
	Vec3 newT, newB;
	Vec3 tv;
	float lenTan, lenBin;

	copyVec3(tangent, tmpTan);
	copyVec3(normal, tmpNorm);
	copyVec3(binormal, tmpBin);

	//newT = tmpTan -  (D3DXVec3Dot(&tmpNorm , &tmpTan)  * tmpNorm );
	scaleVec3(tmpNorm, dotVec3(tmpNorm, tmpTan), tv);
	subVec3(tmpTan, tv, newT);

	//D3DXVECTOR3 newB = tmpBin - (D3DXVec3Dot(&tmpNorm , &tmpBin) * tmpNorm)
	//	- (D3DXVec3Dot(&newT,&tmpBin)*newT);
	scaleVec3(tmpNorm, dotVec3(tmpNorm, tmpBin), tv);
	subVec3(tmpBin, tv, newB);
	scaleVec3(newT, dotVec3(newT, tmpBin), tv);
	subVec3(newB, tv, newB);

	//D3DXVec3Normalize(&(theVerts[i].tangent), &newT);
	//D3DXVec3Normalize(&(theVerts[i].binormal), &newB);		
	normalVec3(newT);copyVec3(newT, tangent);
	normalVec3(newB);copyVec3(newB, binormal);

	//this is where we can do a final check for zero length vectors
	//and set them to something appropriate
	lenTan = lengthVec3(tangent);
	lenBin = lengthVec3(binormal);

	if( (lenTan <= 0.001f) || (lenBin <= 0.001f)  ) //should be approx 1.0f
	{	
		//the tangent space is ill defined at this vertex
		//so we can generate a valid one based on the normal vector,
		//which I'm assuming is valid!

		if(lenTan > 0.5f)
		{
			//the tangent is valid, so we can just use that
			//to calculate the binormal
			crossVec3(normal, tangent, binormal);
			//D3DXVec3Cross(&(theVerts[i].binormal), &(theVerts[i].normal), &(theVerts[i].tangent) );

		}
		else if(lenBin > 0.5)
		{
			//the binormal is good and we can use it to calculate
			//the tangent
			//D3DXVec3Cross(&(theVerts[i].tangent), &(theVerts[i].binormal), &(theVerts[i].normal) );
			crossVec3(binormal, normal, tangent);
		}
		else
		{
			//both vectors are invalid, so we should create something
			//that is at least valid if not correct
			Vec3 xAxis = { 1.0f , 0.0f , 0.0f};
			Vec3 yAxis = { 0.0f , 1.0f , 0.0f};
			//I'm checking two possible axis, because the normal could be one of them,
			//and we want to chose a different one to start making our valid basis.
			//I can find out which is further away from it by checking the dot product
			Vec3 *startAxis;

			if( dotVec3(xAxis, normal)  <  dotVec3(yAxis, normal) )
			{
				//the xAxis is more different than the yAxis when compared to the normal
				startAxis = &xAxis;
			}
			else
			{
				//the yAxis is more different than the xAxis when compared to the normal
				startAxis = &yAxis;
			}

			//D3DXVec3Cross(&(theVerts[i].tangent), &(theVerts[i].normal), &startAxis );
			//D3DXVec3Cross(&(theVerts[i].binormal), &(theVerts[i].normal), &(theVerts[i].tangent) );
			crossVec3(normal, *startAxis, tangent);
			crossVec3(normal, tangent, binormal);
		}
	}
	else
	{
		//one final sanity check, make sure that they tangent and binormal are different enough
		if( dotVec3(binormal, tangent)  > 0.999f )
		{
			//then they are too similar lets make them more different
			//D3DXVec3Cross(&(theVerts[i].binormal), &(theVerts[i].normal), &(theVerts[i].tangent) );
			crossVec3(normal, tangent, binormal);
		}
	}
	normalVec3(tangent);
	normalVec3(binormal);
	normalVec3(normal);
}

static void tangentBasisNv(Mat3 basis,Vec3 v0pos,Vec3 v1pos,Vec3 v2pos,Vec2 v0st,Vec2 v1st,Vec2 v2st,Vec3 normal)
{
	// Copied from MeshMender
	Vec3 P;
	Vec3 Q;
	float s1, t1, t2, s2, tmp;
	Vec3 binormal;
	Vec3 tangent;

	subVec3(v1pos, v0pos, P);
	subVec3(v2pos, v0pos, Q);
	s1 = v1st[0] - v0st[0];
	t1 = v1st[1] - v0st[1];
	s2 = v2st[0] - v0st[0];
	t2 = v2st[1] - v0st[1];

	tmp = 0.0f;
	if(fabsf(s1*t2 - s2*t1) <= 0.0001f)
	{
		tmp = 1.0f;
	}
	else
	{
		tmp = 1.0f/(s1*t2 - s2*t1 );
	}

	tangent[0] = (t2*P[0] - t1*Q[0]);
	tangent[1] = (t2*P[1] - t1*Q[1]);
	tangent[2] = (t2*P[2] - t1*Q[2]);

	scaleVec3(tangent, tmp, tangent);

	binormal[0] = (s1*Q[0] - s2*P[0]);
	binormal[1] = (s1*Q[1] - s2*P[1]);
	binormal[2] = (s1*Q[2] - s2*P[2]);

	scaleVec3(binormal, tmp, binormal);

	// Only do this here if it was *not* MeshMendered
	orthogonalizeNv(tangent, binormal, normal);

	copyVec3(tangent, basis[0]);
	copyVec3(binormal, basis[1]);
	normalVec3(basis[0]);
	normalVec3(basis[1]);
}


void tangentBasis(Mat3 basis, const Vec3 pv0, const Vec3 pv1, const Vec3 pv2, const Vec2 t0, const Vec2 t1, const Vec2 t2, const Vec3 n)
{
	PERFINFO_AUTO_START_FUNC();
	//	if (use old tangent basis)
	tangentBasisOrig(basis, pv0, pv1, pv2, t0, t1, t2, n);
	//	else
	//		tangentBasisNv(basis, pv0, pv1, pv2, t0, t1, t2, n); // Warning this changes the model's normals!
	PERFINFO_AUTO_STOP();
}

bool calcBarycentricCoordsXZProjected(const Vec3 v0_in, const Vec3 v1_in, const Vec3 v2_in, const Vec3 pos_in, Vec3 barycentric_coords)
{
	Vec3 v0, v1, v2, pos;
	Vec3 p0, p1, p2, plane_normal, orig_plane_normal;
	F32 a0, a1, a2;
	F32 total;

	setVec3(v0, v0_in[0], 0, v0_in[2]);
	setVec3(v1, v1_in[0], 0, v1_in[2]);
	setVec3(v2, v2_in[0], 0, v2_in[2]);
	setVec3(pos, pos_in[0], 0, pos_in[2]);

	subVec3(v1, v0, p0);
	subVec3(v2, v0, p1);
	crossVec3(p0, p1, orig_plane_normal);
	total = 0.5f * ABS(orig_plane_normal[1]);
	if (!total)
		return false;

	subVec3(v0, pos, p0);
	subVec3(v1, pos, p1);
	subVec3(v2, pos, p2);

	crossVec3(p1, p2, plane_normal);
	if (plane_normal[1] * orig_plane_normal[1] < 0)
		return false;
	a0 = 0.5f * ABS(plane_normal[1]);

	crossVec3(p2, p0, plane_normal);
	if (plane_normal[1] * orig_plane_normal[1] < 0)
		return false;
	a1 = 0.5f * ABS(plane_normal[1]);

	crossVec3(p0, p1, plane_normal);
	if (plane_normal[1] * orig_plane_normal[1] < 0)
		return false;
	a2 = 0.5f * ABS(plane_normal[1]);

	total = 1.f / total;
	barycentric_coords[0] = a0 * total;
	barycentric_coords[1] = a1 * total;
	barycentric_coords[2] = a2 * total;

	total = barycentric_coords[0] + barycentric_coords[1] + barycentric_coords[2];
	total = 1.f / total;
	scaleVec3(barycentric_coords, total, barycentric_coords);

	return true;
}

void bezierGetPoint(const Vec2 controlPoints[4], F32 t, Vec2 point)
{
	point[0]=(controlPoints[0][0]+t*(-controlPoints[0][0]*3+t*(3*controlPoints[0][0]- 
		controlPoints[0][0]*t)))+t*(3*controlPoints[1][0]+t*(-6*controlPoints[1][0]+ 
		controlPoints[1][0]*3*t))+t*t*(controlPoints[2][0]*3-controlPoints[2][0]*3*t)+ 
		controlPoints[3][0]*t*t*t; 
	point[1]=(controlPoints[0][1]+t*(-controlPoints[0][1]*3+t*(3*controlPoints[0][1]- 
		controlPoints[0][1]*t)))+t*(3*controlPoints[1][1]+t*(-6*controlPoints[1][1]+ 
		controlPoints[1][1]*3*t))+t*t*(controlPoints[2][1]*3-controlPoints[2][1]*3*t)+ 
		controlPoints[3][1]*t*t*t; 
}

void bezierGetPoint3D(const Vec3 controlPoints[4], F32 t, Vec3 point)
{
	point[0]=(controlPoints[0][0]+t*(-controlPoints[0][0]*3+t*(3*controlPoints[0][0]- 
		controlPoints[0][0]*t)))+t*(3*controlPoints[1][0]+t*(-6*controlPoints[1][0]+ 
		controlPoints[1][0]*3*t))+t*t*(controlPoints[2][0]*3-controlPoints[2][0]*3*t)+ 
		controlPoints[3][0]*t*t*t; 
	point[1]=(controlPoints[0][1]+t*(-controlPoints[0][1]*3+t*(3*controlPoints[0][1]- 
		controlPoints[0][1]*t)))+t*(3*controlPoints[1][1]+t*(-6*controlPoints[1][1]+ 
		controlPoints[1][1]*3*t))+t*t*(controlPoints[2][1]*3-controlPoints[2][1]*3*t)+ 
		controlPoints[3][1]*t*t*t; 
	point[2]=(controlPoints[0][2]+t*(-controlPoints[0][2]*3+t*(3*controlPoints[0][2]- 
		controlPoints[0][2]*t)))+t*(3*controlPoints[1][2]+t*(-6*controlPoints[1][2]+ 
		controlPoints[1][2]*3*t))+t*t*(controlPoints[2][2]*3-controlPoints[2][2]*3*t)+ 
		controlPoints[3][2]*t*t*t; 
}

void bezierGetTangent3D(const Vec3 controlPoints[4], F32 t, Vec3 point)
{
	F32 t_sq = t*t;
	point[0] = controlPoints[0][0]*(-3*t_sq+6*t-3) + controlPoints[1][0]*(9*t_sq-12*t+3) + 
		controlPoints[2][0]*(-9*t_sq+6*t) + controlPoints[3][0]*(3*t_sq);
	point[1] = controlPoints[0][1]*(-3*t_sq+6*t-3) + controlPoints[1][1]*(9*t_sq-12*t+3) + 
		controlPoints[2][1]*(-9*t_sq+6*t) + controlPoints[3][1]*(3*t_sq);
	point[2] = controlPoints[0][2]*(-3*t_sq+6*t-3) + controlPoints[1][2]*(9*t_sq-12*t+3) + 
		controlPoints[2][2]*(-9*t_sq+6*t) + controlPoints[3][2]*(3*t_sq);
}

void bezierGet2ndDeriv3D(const Vec3 controlPoints[4], F32 t, Vec3 point)
{
	point[0] = controlPoints[0][0]*(-6*t+6) + controlPoints[1][0]*(18*t-12) + 
		controlPoints[2][0]*(-18*t+6) + controlPoints[3][0]*(6*t);
	point[1] = controlPoints[0][1]*(-6*t+6) + controlPoints[1][1]*(18*t-12) + 
		controlPoints[2][1]*(-18*t+6) + controlPoints[3][1]*(6*t);
	point[2] = controlPoints[0][2]*(-6*t+6) + controlPoints[1][2]*(18*t-12) + 
		controlPoints[2][2]*(-18*t+6) + controlPoints[3][2]*(6*t);
}

void bezierGetPoint3D_fast(const Vec3 controlPoints[4], F32 t, Vec3 point, Vec3 tangent, Vec3 deriv2)
{
#if 0
	_sseVec3 cP[4];
	_sseVec3 t1, t2, t3;
	_sseVec3 c1 = { -3.f, -3.f, -3.f };
	_sseVec3 c2 = { -6.f, -6.f, -6.f };
	_sseVec3 c3 = { 3.f, 3.f, 3.f };
	_sseVec3 c4 = { 1.f, 1.f, 1.f };
	_sseVec3 c5 = { 6.f, 6.f, 6.f };
	_sseVec3 c6 = { 9.f, 9.f, 9.f };
	_sseVec3 c7 = { -12.f, -12.f, -12.f };
	_sseVec3 c8 = { 18.f, 18.f, 18.f };
	_sseVec3 i1 = { t, t, t };
	_sseVec3 i2;
	_sseVec3 i3;

	copyVec3(controlPoints[0], cP[0].v);
	copyVec3(controlPoints[1], cP[1].v);
	copyVec3(controlPoints[2], cP[2].v);
	copyVec3(controlPoints[3], cP[3].v);

	//	cP[0]*(1+t*(-3+t*(3-t)))+
	//		cP[1]*t*(3+t*(-6+3*t)) +
	//		t*t*cP[2]*(3-3*t) +
	//		t*t*t*cP[3]

	sse_mul3(&i1, &i1, &i2);						// i2 = t*t
	sse_mul3(&c3, &i1, &i3);						// i3 = 3*t

	sse_add3(&c2, &i3, &t1);						// t1 =-6+3*t
	sse_mul3(&t1, &i1, &t2);						// t2 = t*(-6+3*t)
	sse_add3(&t2, &c3, &t1);						// t1 = 3+t*(-6+3*t)
	sse_mul3(&t1, &i1, &t2);						// t2 = t*(3+t*(-6+3*t))
	sse_mul3(&t2, &cP[1], &t3);						// t3 = cP[1]*t*(3+t*(-6+3*t)) [A]

	sse_sub3(&c3, &i1, &t1);						// t1 = 3-t
	sse_mul3(&t1, &i1, &t2);						// t2 = t*(3-t)
	sse_sub3(&t2, &c3, &t1);						// t1 = -3+t*(3-t)
	sse_mul3(&t1, &i1, &t2);						// t2 = t*(-3+t*(3-t))
	sse_add3(&t2, &c4, &t1);						// t1 = 1+t*(-3+t*(3-t))
	sse_mul3(&t1, &cP[0], &t2);						// t2 = cP[0]*(1+t*(-3+t*(3-t))) [B]

	sse_add3(&t2, &t3, &t1);						// t1 = [A] + [B]

	sse_sub3(&c3, &i3, &t3);						// t3 = 3-3*t
	sse_mul3(&t3, &i2, &t2);						// t2 = t*t*(3-3*t)
	sse_mul3(&t2, &cP[2], &t3);						// t3 = cP[2]*(t*t*(3-3*t)) [C]

	sse_add3(&t1, &t3, &t2);						// t2 = [A] + [B] + [C]

	sse_mul3(&i2, &cP[3], &t1);						// t1 = cP[3]*t*t
	sse_mul3(&t1, &i1, &t3);						// t3 = cP[3]*t*t*t [D]

	sse_add3(&t2, &t3, &t1);						// t1 = [A] + [B] + [C] + [D]

	copyVec3(t1.v, point);

	// cP[0]*(-3*t*t+6*t-3) + cP[1]*((9*t-12)*t+3) + 
	//	cP[2]*(-9*t+6)*t + cP[3]*(3*t*t)
	sse_mul3(&c5, &i1, &t1);						// t1 = 6*t
	sse_mul3(&i2, &c1, &t2);						// t2 = -3*t*t
	sse_add3(&t1, &t2, &t3);						// t3 = -3*t*t+6*t
	sse_sub3(&t3, &c3, &t1);						// t1 = -3*t*t+6*t-3
	sse_mul3(&t1, &cP[0], &t2);						// t2 = cP[0]*(-3*t*t+6*t-3) [A]

	sse_mul3(&i1, &c6, &t1);						// t1 = 9*t
	sse_add3(&t1, &c7, &t3);						// t3 = 9*t-12
	sse_mul3(&t3, &i1, &t1);						// t1 = (9*t-12)*t
	sse_add3(&t1, &c3, &t3);						// t3 = (9*t-12)*t+3
	sse_mul3(&t3, &cP[1], &t1);						// t1 = cP[1]*((9*t-12)*t+3) [B]

	sse_add3(&t2, &t1, &t3);						// t3 = [A] + [B]

	sse_mul3(&c6, &i1, &t1);						// t1 = 9*t
	sse_sub3(&c5, &t1, &t2);						// t2 = -9*t+6
	sse_mul3(&t2, &i1, &t1);						// t1 = (-9*t+6)*t
	sse_mul3(&t1, &cP[2], &t2);						// t2 = cP[2]*(-9*t+6)*t [C]

	sse_add3(&t3, &t2, &t1);						// t1 = [A] + [B] + [C]

	sse_mul3(&c3, &i2, &t2);						// t2 = 3*t*t
	sse_mul3(&t2, &cP[3], &t3);						// t3 = cP[3]*3*t*t [D]

	sse_add3(&t1, &t3, &t2);						// t2 = [A] + [B] + [C] + [D]

	copyVec3(t2.v, tangent);

	// cP[0]*(-6*t+6) + cP[1]*(18*t-12) + cP[2]*(-18*t+6) + cP[3]*6*t

	sse_mul3(&c5, &i1, &t1);						// t1 = 6*t
	sse_sub3(&c5, &t1, &t2);						// t2 = -6*t+6
	sse_mul3(&t2, &i1, &t1);						// t1 = cP[0]*(-6*t+6) [A]

	sse_mul3(&c8, &i1, &t2);						// t2 = 18*t
	sse_add3(&t2, &c7, &t3);						// t3 = 18*t-12
	sse_mul3(&t3, &cP[1], &t2);						// t2 = cP[1]*(18*t-12) [B]

	sse_add3(&t1, &t2, &t3);						// t3 = [A] + [B]
	
	sse_mul3(&c8, &i1, &t2);						// t2 = 18*t
	sse_sub3(&c5, &t2, &t1);						// t1 = -18*t+6
	sse_mul3(&t1, &cP[2], &t2);						// t2 = cP[2]*(-18*t+6) [C]

	sse_add3(&t3, &t2, &t1);						// t1 = [A] + [B] + [C]

	sse_mul3(&c5, &i1, &t2);						// t2 = 6*t
	sse_mul3(&t2, &cP[3], &t3);						// t3 = cP[3]*6*t [D]

	sse_add3(&t3, &t1, &t2);						// t2 = [A] + [B] + [C] + [D]

	copyVec3(t2.v, deriv2);
#else
	bezierGetPoint3D(controlPoints, t, point);
	bezierGetTangent3D(controlPoints, t, tangent);
	bezierGet2ndDeriv3D(controlPoints, t, deriv2);
#endif
}

static int getClipCode2D(const Vec2 v, const Vec2 clip_min, const Vec2 clip_max)
{
	return	((v[0] < clip_min[0])) |
		((v[0] > clip_max[0]) << 1) |
		((v[1] < clip_min[1]) << 2) |
		((v[1] > clip_max[1]) << 3);
}

__forceinline static void clip2D(const Vec2 p0, const Vec2 p1, Vec2 p, int clip_bit, const Vec2 clip_min, const Vec2 clip_max)
{
	int idx = clip_bit >= 2;
	const F32 *mm = (clip_bit & 1) ? clip_max : clip_min;
	Vec2 pd;
	F32 t;

	subVec2(p1, p0, pd);

	t = (mm[idx] - p0[idx]) / pd[idx];

	p[idx] = mm[idx];
	p[!idx] = p0[!idx] + t * pd[!idx];
}

static F32 findClippedArea2DInternal(const Vec2 p0, const Vec2 p1, const Vec2 p2, int clip_bit, const Vec2 clip_min, const Vec2 clip_max)
{
	int co, c_and, co1, clip_mask;
	Vec2 tmp1, tmp2;
	Vec2 q[3];
	F32 total_area = 0;
	int cc0, cc1, cc2;

	cc0 = getClipCode2D(p0, clip_min, clip_max);
	cc1 = getClipCode2D(p1, clip_min, clip_max);
	cc2 = getClipCode2D(p2, clip_min, clip_max);

	co = cc0 | cc1 | cc2;
	if (co == 0)
	{
		// find area of triangle
		subVec2(p1, p0, tmp1);
		subVec2(p2, p0, tmp2);
		total_area = 0.5f * crossVec2(tmp1, tmp2);
		total_area = ABS(total_area);
	}
	else
	{
		c_and = cc0 & cc1 & cc2;

		// the triangle is completely outside
		if (c_and != 0)
			return 0;

		// find the next direction to clip
		while (clip_bit < 4 && (co & (1 << clip_bit)) == 0)
			clip_bit++;

		// this test can be true only in case of rounding errors
		if (clip_bit == 4)
			return 0;

		clip_mask = 1 << clip_bit;
		co1 = (cc0 ^ cc1 ^ cc2) & clip_mask;

		if (co1)
		{ 
			// one point outside
			if (cc0 & clip_mask)
			{
				copyVec2(p0, q[0]);
				copyVec2(p1, q[1]);
				copyVec2(p2, q[2]);
			}
			else if (cc1 & clip_mask)
			{
				copyVec2(p1, q[0]);
				copyVec2(p2, q[1]);
				copyVec2(p0, q[2]);
			}
			else
			{
				copyVec2(p2, q[0]);
				copyVec2(p0, q[1]);
				copyVec2(p1, q[2]);
			}

			clip2D(q[0], q[1], tmp1, clip_bit, clip_min, clip_max);
			clip2D(q[0], q[2], tmp2, clip_bit, clip_min, clip_max);

			total_area += findClippedArea2DInternal(tmp1, q[1], q[2], clip_bit+1, clip_min, clip_max);
			total_area += findClippedArea2DInternal(tmp2, tmp1, q[2], clip_bit+1, clip_min, clip_max);
		}
		else
		{
			// two points outside
			if ((cc0 & clip_mask)==0)
			{
				copyVec2(p0, q[0]);
				copyVec2(p1, q[1]);
				copyVec2(p2, q[2]);
			}
			else if ((cc1 & clip_mask)==0)
			{
				copyVec2(p1, q[0]);
				copyVec2(p2, q[1]);
				copyVec2(p0, q[2]);
			} 
			else
			{
				copyVec2(p2, q[0]);
				copyVec2(p0, q[1]);
				copyVec2(p1, q[2]);
			}

			clip2D(q[0], q[1], tmp1, clip_bit, clip_min, clip_max);
			clip2D(q[0], q[2], tmp2, clip_bit, clip_min, clip_max);

			total_area += findClippedArea2DInternal(q[0], tmp1, tmp2, clip_bit+1, clip_min, clip_max);
		}
	}

	return total_area;
}

F32 findClippedArea2D(const Vec2 p0, const Vec2 p1, const Vec2 p2, const Vec2 clip_min, const Vec2 clip_max)
{
	return findClippedArea2DInternal(p0, p1, p2, 0, clip_min, clip_max);
}


// vFacing is assumed to be a unit vector
// vDir is assumed to not be normalized and fDirLength is its magnitude
// if fAllowedDeviationInRadians <= 0, then front facing is considered an angle of 90 degrees or less
bool isFacingDirectionEx( Vec3 vFacing, Vec3 vDir, F32 fDirLength, F32 fAllowedDeviationInRadians )
{
	if ( fAllowedDeviationInRadians > 0 )
	{
		F32 fDot;

		if ( fDirLength <= 0 ) return false;

		fDot = dotVec3( vFacing, vDir ) / fDirLength;

		return ( acosf( CLAMP( fDot, -1.0f, 1.0f ) ) < fAllowedDeviationInRadians );
	}

	return ( dotVec3( vFacing, vDir ) > 0 );
}

//vectors need not be normalized
//checks to see if vDir is generally pointing towards vFacing
bool isFacingDirection( Vec3 vFacing, Vec3 vDir )
{
	return isFacingDirectionEx( vFacing, vDir, -1, -1 );
}

// like pointLinePos, except more efficient
// project a point(pt) onto a line(lnA,lnB)
void pointProjectOnLine( const Vec3 lnA, const Vec3 lnB, const Vec3 pt, Vec3 out )
{
	Vec3 ds, vo;
	F32 t;

	subVec3( lnB, lnA, ds );
	subVec3( pt, lnA, vo );

	t = dotVec3( ds, vo );

	if ( t <= 0.0f )
	{
		copyVec3( lnA, out );
	}
	else
	{
		F32 d = dotVec3( ds, ds );

		if ( t >= d )
		{
			copyVec3( lnB, out );
		}
		else
		{
			t /= d;
			scaleByVec3( ds, t );
			addVec3( ds, lnA, out );
		}
	}
}

void projectVecOntoPlane(	const Vec3 vecToProject,
							const Vec3 vecUnitPlaneNormal,
							Vec3 vecProjOut)
{
	F32 scale = -dotVec3(vecToProject, vecUnitPlaneNormal);
	
	scaleAddVec3(vecUnitPlaneNormal, scale, vecToProject, vecProjOut);
}

void projectYOntoPlane(	const F32 y,
						const Vec3 vecUnitPlaneNormal,
						Vec3 vecProjOut)
{
	F32 scale = -y * vecUnitPlaneNormal[1];
	
	setVec3(vecProjOut,
			vecUnitPlaneNormal[0] * scale,
			y + vecUnitPlaneNormal[1] * scale,
			vecUnitPlaneNormal[2] * scale);
}

void unitDirVec3ToMat3(const Vec3 vecUnitDir, Mat3 mat3Out)
{
	// Set the forward vector
	copyVec3(vecUnitDir, mat3Out[2]);

	// Set the right vector
	crossVec3(upvec, vecUnitDir, mat3Out[0]);
	if (normalVec3(mat3Out[0]) == 0.f)
	{
		mat3Out[0][0] = 1.f;
		mat3Out[0][1] = 0.f;
		mat3Out[0][2] = 0.f;
	}

	// Set the up vector
	crossVec3(mat3Out[2], mat3Out[0], mat3Out[1]);
}

void rotateUnitVecTowardsUnitVec(	const Vec3 vecUnitSource,
									const Vec3 vecUnitTarget,
									const F32 scale,
									Vec3 vecOut)
{
	Mat3	mat;
	Vec3	vecYawWorld;
	F32		angleBetween;

	angleBetween = dotVec3(vecUnitSource, vecUnitTarget);
	
	copyVec3(vecUnitSource, mat[2]);

	if(angleBetween >= 1.f){
		copyVec3(vecUnitTarget, vecOut);
		return;
	}
	else if(angleBetween <= -1.f){
		angleBetween = PI;
		
		if(fabs(vecUnitSource[1]) < 0.99f){
			crossVec3(vecUnitSource, unitmat[1], mat[1]);
		}else{
			crossVec3(vecUnitSource, unitmat[0], mat[1]);
		}
	}else{
		angleBetween = acosf(angleBetween);

		crossVec3(mat[2], vecUnitTarget, mat[1]);
	}

	normalVec3(mat[1]);

	crossVec3(mat[1], mat[2], mat[0]);

	angleBetween *= scale;
	sincosf(angleBetween, vecYawWorld + 0, vecYawWorld + 2);
	
	mulVecXZMat3(vecYawWorld, mat, vecOut);
}

//helper function for rotateVecAboutAxisEx
__forceinline static void getParallelAxisVec3( Vec3 vA, Vec3 vAxis, Vec3 vOut )
{
	scaleVec3( vAxis, dotVec3( vA, vAxis ), vOut );
}
//helper function for rotateVecAboutAxisEx
__forceinline static void getPerpAxisVec3( Vec3 vA, Vec3 vAxis, Vec3 vOut )
{
	getParallelAxisVec3( vA, vAxis, vOut );
	subVec3( vA, vOut, vOut );
}

//rotate a vector around an axis vector by specified c=cos(angle) s=sin(angle)
void rotateVecAboutAxisEx( F32 c, F32 s, const Vec3 vToRotAbout, const Vec3 vToRot, Vec3 vResult )
{
	Vec3 vR, vRA, vPerp, vCross, vPara;
	
	copyVec3( vToRot, vR );
	copyVec3( vToRotAbout, vRA );
	getPerpAxisVec3( vR, vRA, vPerp );
	crossVec3( vRA, vR, vCross );
	getParallelAxisVec3( vR, vRA, vPara );
	scaleByVec3( vPerp, c );
	scaleByVec3( vCross, s );
	addVec3( vPerp, vCross, vResult );
	addVec3( vResult, vPara, vResult );
}

//rotate a vector around an axis vector by specified angle
void rotateVecAboutAxis( F32 fAngle, const Vec3 vToRotAbout, const Vec3 vToRot, Vec3 vResult )
{
	F32 c, s;
	sincosf( fAngle, &s, &c );
	rotateVecAboutAxisEx( c, s, vToRotAbout, vToRot, vResult );
}

F32 getAngleBetweenNormalizedVec3(const Vec3 v1, const Vec3 v2)
{
	F32 fAngle = dotVec3(v1, v2);
	return acosf(CLAMP(fAngle, -1.f, 1.f));
}

F32 getAngleBetweenVec3(const Vec3 v1, const Vec3 v2)
{
	F32 fLengths;
	F32 fDot;
	F32 fAngle;

	fLengths = lengthVec3(v1) * lengthVec3(v2);
	if (fLengths == 0.f)
		return 0.f;

	fDot = dotVec3(v1, v2);
	fAngle = fDot / fLengths;
	return acosf(CLAMP(fAngle, -1.f, 1.f));
}

void interpMat3(F32 t, const Mat3 a, const Mat3 b, Mat3 r)
{
	Quat qa, qb, qr;
	mat3ToQuat(a, qa);
	mat3ToQuat(b, qb);
	quatInterp(t, qa, qb, qr);
	quatToMat(qr, r);
}

void interpMat4(F32 t, const Mat4 a, const Mat4 b, Mat4 r) 
{
	interpMat3(t, a, b, r);
	interpVec3(t, a[3], b[3], r[3]);
}

/*
#ifdef _XBOX
__forceinline static void Vec3fromPacked(Vec3 vec, Vec3_Packed packed) {

	static const U32 SignExtend10[]={0x00000000, 0xFFFFFC00};
	static const U32 SignExtend11[]={0x00000000, 0xFFFFF800};
	U32 x = packed & 0x7FF; 
	U32 y = (packed>>11) & 0x7FF; 
	U32 z = (packed>>22) & 0x3FF; 

	vec[0] = ((S32)(x | SignExtend11[x >> 10]))/1023.f;
	vec[1] = ((S32)(y | SignExtend11[y >> 10]))/1023.f;
	vec[2] = ((S32)(z | SignExtend10[z >> 9]))/511.f;
}
#else
__forceinline static void Vec3fromPacked(Vec3 vec, Vec3_Packed packed) {

	static const U32 SignExtend[]={0x00000000, 0xFFFF0000};
	U32 x = packed & 0xFFFF; 
	U32 y = (packed>>16LL) & 0xFFFF; 
	U32 z = (packed>>32LL) & 0xFFFF; 

	vec[0] = ((S32)(x | SignExtend[x >> 15]))/32767.f;
	vec[1] = ((S32)(y | SignExtend[y >> 15]))/32767.f;
	vec[2] = ((S32)(z | SignExtend[z >> 15]))/32767.f;
}
#endif

void testVec3toPacked(void)
{
	// Validity test
	Vec3 input;
	for (input[0]=-1.1f; input[0]<=1.1f; input[0]+=0.1f) {
		for (input[1]=-1.1f; input[1]<=1.1f; input[1]+=0.1f) {
			for (input[2]=-1.1f; input[2]<=1.1f; input[2]+=0.1f) {
				Vec3_Packed packed = Vec3toPacked(input);
				Vec3 unpacked;
				Vec3fromPacked(unpacked, packed);
				assert(nearSameVec3Tol(input, unpacked, 0.11 * 3));
			}
		}
	}
	// Speed test
	{
		int i, j;
		Vec3_Packed result=0;
		Vec3 *test_data = calloc(sizeof(Vec3), 100000);
		int timer;
		for (i=0; i<100000; i++) 
			setVec3same(test_data[i], i / 50000.f - 1.f);
		timer = timerAlloc();
		for (i=0; i<100; i++) {
			for (j=0; j<100000; j++) {
				result ^= Vec3toPacked(test_data[j]) + i;
			}
		}
		printf("testVec3toPacked: %1.3f %d\n", timerElapsed(timer), result);
		printf("");
		free(test_data);
		timerFree(timer);
	}
}
*/

#if !_XBOX && !_PS3 && !_M_X64
bool x87_stack_empty(void)
{
	int i;
	U32 z[8];
	__asm {
		fldz
		fldz
		fldz
		fldz
		fldz
		fldz
		fldz
		fldz
		fstp dword ptr [z+0x00]
		fstp dword ptr [z+0x04]
		fstp dword ptr [z+0x08]
		fstp dword ptr [z+0x0c]
		fstp dword ptr [z+0x10]
		fstp dword ptr [z+0x14]
		fstp dword ptr [z+0x18]
		fstp dword ptr [z+0x1c]
	}

	// Verify bit patterns. 0 = 0.0
	for (i = 0; i < 8; ++i) {
		if (z[i] != 0)
			return false;
	}
	return true;
}

bool is_fp_default_control_word(void)
{
	U32 current_state;
	_controlfp_s(&current_state, 0, 0);
	return (current_state & (_MCW_RC | _MCW_EM)) ==  (_RC_NEAR | fp_default_exception_mask);
}
#endif

#if 0
void floatfunc(float f);

void sincosspeedtest(void)
{
	int timer = timerAlloc();
	int loops;
	int numloops = 1000000;
	float angle;
	float s=0, c=0;
	int i;
#if _XBOX
	XMVECTOR vangle={0};
	XMVECTOR vs={0}, vc={0};
#endif

	for (i=0; i<3; i++)
	{
		Vec3 pyr = {0,0.2,0.3};
		Mat3 m;
		copyMat3(unitmat, m);
		timerStart(timer);
		for (angle=0, loops=0; loops < numloops; loops++, angle+=0.01)
		{
			pyr[0] = angle;
			createMat3YPR(m, pyr);
			floatfunc(m[0][0]);
		}
		printf("%1.3fM createMat3YPR/sec\n", loops / timerElapsed(timer) / 1000000.f);

#if _XBOX
		timerStart(timer);
		for (vangle.x=0, loops=0; loops < numloops; loops++, vangle.x+=0.01)
		{
			XMVectorSinCos(&vs, &vc, vangle);
			floatfunc(vs.x);
			floatfunc(vc.x);
		}
		printf("%1.3fM sincosfXMV/sec\n", loops / timerElapsed(timer) / 1000000.f);
#endif

		timerStart(timer);
		for (angle=0, loops=0; loops < numloops; loops++, angle+=0.01)
		{
			sincosf(angle, &s, &c);
			floatfunc(s);
			floatfunc(c);
		}
		printf("%1.3fM sincosf/sec\n", loops / timerElapsed(timer) / 1000000.f);

		timerStart(timer);
#if _XBOX && 0
		for (vangle.x=0, loops=0; loops < numloops; loops++, vangle.x+=0.01)
		{
			vs = XMVectorSin(vangle);
			floatfunc(vs.x);
			floatfunc(c);
			//g_sincos_result += s + c;
		}
#else
		for (angle=0, loops=0; loops < numloops; loops++, angle+=0.01)
		{
			s = sin(angle);
			floatfunc(s);
			floatfunc(c);
		}
#endif
		printf("%1.3fM sin/sec\n", loops / timerElapsed(timer) / 1000000.f);

		timerStart(timer);
		for (angle=0, loops=0; loops < numloops; loops++, angle+=0.01)
		{
			c = cos(angle);
			floatfunc(s);
			floatfunc(c);
		}
		printf("%1.3fM cos/sec\n", loops / timerElapsed(timer) / 1000000.f);
	}

	timerFree(timer);
}



#endif

#define REVERSIBLE_HASH_STEPS 5

//seeds all most be even so that XORing them doesn't change bottom bit
U32 iSeeds[REVERSIBLE_HASH_STEPS * 2] = 
{ 
	(27879 << 16) + 57836, (11141 << 16) + 63564,
	(8440 << 16) + 52496, (28909 << 16) + 11384,
	(43259 << 16) + 17124, (46059 << 16) + 45352,
	(1413 << 16) + 64190, (17890 << 16) + 63562,
	(52152 << 16) + 24916, (957 << 16) + 24320,
};




U32 rotateLeftBitCount(U32 iIn)
{
	int iCount = countBitsFast(iIn);
	U32 iP1 = iIn << iCount;
	U32 iP2 = iIn >> (32 - iCount);
	return iP1 | iP2;
}

U32 rotateRightBitCount(U32 iIn)
{
	int iCount = countBitsFast(iIn);
	U32 iP1 = iIn >> iCount;
	U32 iP2 = iIn << (32 - iCount);
	return iP1 | iP2;
}


//NOT cryptographically secure in any meaningful way. Just useful for obfuscating "1 2 3 4 5"
U32 reversibleHash(U32 iIn, bool bDirection)
{
	int i;

	if (bDirection)
	{
		for (i=0; i < REVERSIBLE_HASH_STEPS; i++)
		{
			iIn = rotateLeftBitCount(iIn);
			iIn ^= iSeeds[i * 2 + (iIn & 1)];
		}
	}
	else
	{
		for (i=REVERSIBLE_HASH_STEPS - 1; i >= 0; i--)
		{
			iIn ^= iSeeds[i * 2 + (iIn & 1)];
			iIn = rotateRightBitCount(iIn);
		}
	}

	return iIn;
}


bool isSphereInsideCone(Vec3 vConeApex, Vec3 vConeDir, F32 fConeLength, F32 fTanConeAngle, Vec3 vSphereCenter, F32 fSphereRadius)
{
	Vec3 vConeAxis, vSphereDir;
	Vec3 vConeEnd;
	F32 t;

	scaleAddVec3(vConeDir, fConeLength, vConeApex, vConeEnd);
	subVec3(vConeEnd, vConeApex, vConeAxis);
	subVec3(vSphereCenter, vConeApex, vSphereDir);

	t = dotVec3(vConeAxis, vSphereDir);

	if (t > 0.0f)
	{
		F32 d = fConeLength * fConeLength;

		if (t < d)
		{
			Vec3 vProjPoint, vDir;
			F32 fProjDist, fConeRadius;

			t /= d;
			scaleAddVec3(vConeAxis, t, vConeApex, vProjPoint);
		
			subVec3(vSphereCenter, vProjPoint, vDir);
			fProjDist = fConeLength * t;
			fConeRadius = fProjDist * fTanConeAngle;

			if (fProjDist + fSphereRadius <= fConeLength &&
				lengthVec3(vDir) + fSphereRadius <= fConeRadius)
			{
				return true;
			}
		}
	}
	return false;
}

#if SAFE_TRANSCENDENTAL_FUNCTIONS

#undef sqrt
#undef acos
#undef asin
#undef log
#undef logf
#undef log10

double safe_sqrt(double x)
{
	assert(x >= 0.0 && x <= DBL_MAX);
	return sqrt(x);
}

double safe_acos(double x)
{
	assert(x >= -1.0 && x <= 1.0);
	return acos(x);
}

double safe_asin(double x)
{
	assert(x >= -1.0 && x <= 1.0);
	return asin(x);
}

double safe_log(_In_ double x)
{
	assert(x > 0.0 && x <= DBL_MAX);
	return log(x);
}

float safe_logf(_In_ float x)
{
	assert(x > 0.0 && x <= FLT_MAX);
	return (float)log(x);
}

double safe_log10(_In_ double x)
{
	assert(x > 0.0 && x <= DBL_MAX);
	return log10(x);
}

#endif
