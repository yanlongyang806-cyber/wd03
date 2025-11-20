#define DEF3(a, b) a ## b
#define DEF2(a, b) DEF3(a, b)
#define DEF(a) DEF2(a, DIM)

#define VECTYPE DEF(Vec)
#define MATTYPE DEF(Mat)

#if _PS3
    #if DIM>4
    #define DEFINLINE static 
    #else
    #define DEFINLINE __forceinline static 
    #endif
#else
    #define DEFINLINE __forceinline 
#endif

typedef float DEF(Vec)[DIM];
typedef DEF(Vec) DEF(Mat)[DIM];


//////////////////////////////////////////////////////////////////////////


DEFINLINE void DEF(zeroVec) (VECTYPE v)
{
	memset(v, 0, sizeof(float) * DIM);
}

// r = a + b
DEFINLINE void DEF(addVec) (const VECTYPE a, const VECTYPE b, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] = a[i] + b[i];
}

// a += b
DEFINLINE void DEF(addToVec) (const VECTYPE v, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] += v[i];
}

// r = a - b
DEFINLINE void DEF(subVec) (const VECTYPE a, const VECTYPE b, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] = a[i] - b[i];
}

// a -= b
DEFINLINE void DEF(subFromVec) (const VECTYPE v, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] -= v[i];
}

// r = v * scale
DEFINLINE void DEF(scaleVec) (const VECTYPE v, float scale, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] = v[i] * scale;
}

// r = v1 * v2
DEFINLINE void DEF(mulVecVec) (const VECTYPE v1, const VECTYPE v2, VECTYPE r)
{
	int i;
	for (i = 0; i < DIM; i++)
		r[i] = v1[i] * v2[i];
}

// v *= scale
DEFINLINE void DEF(scaleByVec) (VECTYPE v, float scale)
{
	int i;
	for (i = 0; i < DIM; i++)
		v[i] *= scale;
}

DEFINLINE void DEF(copyVec) (const VECTYPE v, VECTYPE r)
{
	memcpy(r, v, sizeof(float) * DIM);
}


//////////////////////////////////////////////////////////////////////////


DEFINLINE void DEF(zeroMat) (MATTYPE m)
{
	memset(m, 0, sizeof(float) * DIM * DIM);
}

DEFINLINE void DEF(identityMat) (MATTYPE m)
{
	int i;
	DEF(zeroMat)(m);
	for (i = 0; i < DIM; i++)
		m[i][i] = 1.f;
}

// r = a + b
DEFINLINE void DEF(addMat) (const MATTYPE a, const MATTYPE b, MATTYPE r)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			r[i][j] = a[i][j] + b[i][j];
}

// a += b
DEFINLINE void DEF(addToMat) (const MATTYPE m, MATTYPE r)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			r[i][j] += m[i][j];
}

// r = a - b
DEFINLINE void DEF(subMat) (const MATTYPE a, const MATTYPE b, MATTYPE r)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			r[i][j] = a[i][j] - b[i][j];
}

// a -= b
DEFINLINE void DEF(subFromMat) (const MATTYPE m, MATTYPE r)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			r[i][j] -= m[i][j];
}

// r = m * scale
DEFINLINE void DEF(scaleMat) (const MATTYPE m, MATTYPE r, float scale)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			r[i][j] = m[i][j] * scale;
}

// m *= scale
DEFINLINE void DEF(scaleByMat) (MATTYPE m, float scale)
{
	int i, j;
	for (i = 0; i < DIM; i++)
		for (j = 0; j < DIM; j++)
			m[i][j] *= scale;
}

DEFINLINE void DEF(copyMat) (const MATTYPE m, MATTYPE r)
{
	memcpy(r, m, sizeof(float) * DIM * DIM);
}

DEFINLINE void DEF(mulVecMat) (const VECTYPE v, const MATTYPE m, VECTYPE r)
{
	int i, j;
	for (j = 0; j < DIM; j++)
	{
		r[j] = m[0][j] * v[0];
		for (i = 1; i < DIM; i++)
			r[j] += m[i][j] * v[i];
	}
}


//////////////////////////////////////////////////////////////////////////


// compute the inner product of two vectors
DEFINLINE float DEF(dotVec) (const VECTYPE a, const VECTYPE b)
{
	int i;
	float res = a[0] * b[0];
	for (i = 1; i < DIM; i++)
		res += a[i] * b[i];
	return res;
}

// compute the outer product of two vectors
DEFINLINE void DEF(outerProductVec) (const VECTYPE a, const VECTYPE b, MATTYPE r)
{
	int x,y;

	for (y = 0; y < DIM; y++)
	{
		for (x = 0; x < DIM; x++)
			r[x][y] = a[y] * b[x];
	}
}

DEFINLINE void DEF(normalVec) (VECTYPE v)
{
	float mag2 = DEF(dotVec)(v, v);
	float mag = fsqrt(mag2);
	if (mag > 0)
		DEF(scaleVec)(v, 1.f/mag, v);
}


//////////////////////////////////////////////////////////////////////////

#define MAXTRIX_SINGULAR_EPSILON (1.0e-8f)

DEFINLINE int DEF(invertMat) (const MATTYPE m, MATTYPE r)
{
	float max, t;
	double det, pivot, oneoverpivot;
	int i, j, k;
	MATTYPE A;
	DEF(copyMat)(m, A);


	/*---------- forward elimination ----------*/

	DEF(identityMat)(r);

	det = 1.0f;
	for (i = 0; i < DIM; i++)
	{
		/* eliminate in column i, below diag */
		max = -1.f;
		j = -1;
		for (k = i; k < DIM; k++)
		{
			/* find pivot for column i */
			t = A[i][k];
			if (fabs(t) > max)
			{
				max = fabs(t);
				j = k;
			}
		}

		if (max <= MAXTRIX_SINGULAR_EPSILON)
			return 0;         /* if max pivot is very close to zero, it's a singular matrix, so PUNT */

		if (j != i)
		{
			/* swap rows i and j */
			for (k = i; k < DIM; k++)
			{
				t = A[k][i];
				A[k][i] = A[k][j];
				A[k][j] = t;
			}
			for (k = 0; k < DIM; k++)
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
		for (k = i + 1; k < DIM; k++)           /* only do elems to right of pivot */
			A[k][i] *= oneoverpivot;
		for (k = 0; k < DIM; k++)
			r[k][i] *= oneoverpivot;

		/* we know that A(i, i) will be set to 1, so don't bother to do it */

		for (j = i + 1; j < DIM; j++)
		{
			/* eliminate in rows below i */
			t = A[i][j];                /* we're gonna zero this guy */
			for (k = i + 1; k < DIM; k++)       /* subtract scaled row i from row j */
				A[k][j] -= A[k][i] * t;   /* (ignore k<=i, we know they're 0) */
			for (k = 0; k < DIM; k++)
				r[k][j] -= r[k][i] * t;   /* (ignore k<=i, we know they're 0) */
		}
	}

	if (fabs(det) < MAXTRIX_SINGULAR_EPSILON)
		return 0;         /* if determinant is very close to zero, it's a singular matrix, so PUNT */

	/*---------- backward elimination ----------*/

	for (i = DIM - 1; i > 0; i--)
	{
		/* eliminate in column i, above diag */
		for (j = 0; j < i; j++)
		{
			/* eliminate in rows above i */
			t = A[i][j];                /* we're gonna zero this guy */
			for (k = 0; k < DIM; k++)         /* subtract scaled row i from row j */
				r[k][j] -= r[k][i] * t;   /* (ignore k<=i, we know they're 0) */
		}
	}

	return 1;
}


//////////////////////////////////////////////////////////////////////////


#undef DEF3
#undef DEF2
#undef DEF

#undef VECTYPE
#undef MATTYPE

#undef DIM

#undef DEFINLINE 
