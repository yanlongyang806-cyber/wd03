#define DEF3(a, b) a ## b
#define DEF2(a, b) DEF3(a, b)
#define DEF(a) DEF2(a, DIM)

#if DIM==4
#define MATDEF(a) DEF3(a, 44)
#else
#define MATDEF(a) DEF2(a, DIM)
#endif

#define VECTYPE DEF(Vec)
#define MATTYPE MATDEF(Mat)

#if _PS3
    #if DIM>4
    #define DEFINLINE static 
    #else
    #define DEFINLINE __forceinline static 
    #endif
#else
    #define DEFINLINE __forceinline 
#endif

typedef struct DEF(_Quadric)
{
	MATTYPE A;
	VECTYPE b;
	float c;
} DEF(Quadric);

#define QTYPE DEF(Quadric)


//////////////////////////////////////////////////////////////////////////


DEFINLINE void DEF(zeroQ)(QTYPE *q)
{
	MATDEF(zeroMat)(q->A);
	DEF(zeroVec)(q->b);
	q->c = 0;
}

DEFINLINE void DEF(initQ)(QTYPE *q, const VECTYPE e1, const VECTYPE e2, const VECTYPE p)
{
	MATTYPE outer;
	VECTYPE temp;
	float tempF;

	// A = I - outer(e1,e1) - outer(e2,e2)
	MATDEF(identityMat)(q->A);
	DEF(outerProductVec)(e1, e1, outer);
	MATDEF(subFromMat)(outer, q->A);
	DEF(outerProductVec)(e2, e2, outer);
	MATDEF(subFromMat)(outer, q->A);

	// b = (p*e1)e1 + (p*e2)e2 - p
	DEF(scaleVec)(e1, DEF(dotVec)(p, e1), q->b);
	DEF(scaleVec)(e2, DEF(dotVec)(p, e2), temp);
	DEF(addToVec)(temp, q->b);
	DEF(subFromVec)(p, q->b);

	// c = p*p - (p*e1)^2 - (p*e2)^2
	tempF = DEF(dotVec)(p, e1);
	q->c = DEF(dotVec)(p, p) - SQR(tempF);
	tempF = DEF(dotVec)(p, e2);
	q->c -= SQR(tempF);
}

DEFINLINE void DEF(addQ)(const QTYPE *q1, const QTYPE *q2, QTYPE *res)
{
	MATDEF(addMat)(q1->A, q2->A, res->A);
	DEF(addVec)(q1->b, q2->b, res->b);
	res->c = q1->c + q2->c;
}

// q1 += q2
DEFINLINE void DEF(addToQ)(const QTYPE *q, QTYPE *res)
{
	MATDEF(addToMat)(q->A, res->A);
	DEF(addToVec)(q->b, res->b);
	res->c += q->c;
}

DEFINLINE void DEF(subQ)(const QTYPE *q1, const QTYPE *q2, QTYPE *res)
{
	MATDEF(subMat)(q1->A, q2->A, res->A);
	DEF(subVec)(q1->b, q2->b, res->b);
	res->c = q1->c - q2->c;
}

// q1 -= q2
DEFINLINE void DEF(subFromQ)(const QTYPE *q, QTYPE *res)
{
	MATDEF(subFromMat)(q->A, res->A);
	DEF(subFromVec)(q->b, res->b);
	res->c -= q->c;
}

DEFINLINE void DEF(scaleQ)(const QTYPE *q, QTYPE *res, float scale)
{
	MATDEF(scaleMat)(q->A, res->A, scale);
	DEF(scaleVec)(q->b, scale, res->b);
	res->c = q->c * scale;
}

// q *= scale
DEFINLINE void DEF(scaleByQ)(QTYPE *q, float scale)
{
	MATDEF(scaleByMat)(q->A, scale);
	DEF(scaleByVec)(q->b, scale);
	q->c *= scale;
}

DEFINLINE float DEF(evaluateQ)(const QTYPE *q, const VECTYPE v)
{
	float res;
	VECTYPE temp;
	MATDEF(mulVecMat)(v, q->A, temp);
	res = DEF(dotVec)(v, temp) + 2.f * DEF(dotVec)(q->b, v) + q->c;
	return ABS(res);
}

DEFINLINE int DEF(optimizeQ)(const QTYPE *q, VECTYPE out)
{
	MATTYPE inv;

	if (!MATDEF(invertMat)(q->A, inv))
		return 0;

	MATDEF(mulVecMat)(q->b, inv, out);
	DEF(scaleByVec)(out, -1.f);

	return 1;
}

DEFINLINE void DEF(initFromTriQ)(QTYPE *q, const VECTYPE v1, const VECTYPE v2, const VECTYPE v3)
{
	VECTYPE e1;
	VECTYPE e2;
	VECTYPE temp;

	DEF(subVec)(v2, v1, e1);
	DEF(normalVec)(e1);

	DEF(subVec)(v3, v1, e2);
	DEF(scaleVec)(e1, DEF(dotVec)(e1, e2), temp);
	DEF(subVec)(e2, temp, e2);
	DEF(normalVec)(e2);

	DEF(initQ)(q, e1, e2, v1);
}


//////////////////////////////////////////////////////////////////////////


#undef DEF3
#undef DEF2
#undef DEF
#undef MATDEF

#undef VECTYPE
#undef MATTYPE
#undef QTYPE

#undef DIM

#undef DEFINLINE 
