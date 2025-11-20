#include "mathutil.h"
#include "simplexNoise.h"

#if !SPU
#include "error.h"
#include "rand.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););
#endif

// Adapted from http://staffwww.itn.liu.se/~stegu/simplexnoise/simplexnoise.pdf


#define dot2(g, x, y) (g[0] * x + g[1] * y)
#define dot3(g, x, y, z) (g[0] * x + g[1] * y + g[2] * z)
#define dot4(g, x, y, z, w) (g[0] * x + g[1] * y + g[2] * z + g[3] * w)

static Vec3 grad3[] =
{
	{1,1,0},
	{-1,1,0},
	{1,-1,0},
	{-1,-1,0},

	{1,0,1},
	{-1,0,1},
	{1,0,-1},
	{-1,0,-1},

	{0,1,1},
	{0,-1,1},
	{0,1,-1},
	{0,-1,-1}
};

#if !SPU
static Vec4 grad4[]= {{0,1,1,1}, {0,1,1,-1}, {0,1,-1,1}, {0,1,-1,-1},
	{0,-1,1,1}, {0,-1,1,-1}, {0,-1,-1,1}, {0,-1,-1,-1},
	{1,0,1,1}, {1,0,1,-1}, {1,0,-1,1}, {1,0,-1,-1},
	{-1,0,1,1}, {-1,0,1,-1}, {-1,0,-1,1}, {-1,0,-1,-1},
	{1,1,0,1}, {1,1,0,-1}, {1,-1,0,1}, {1,-1,0,-1},
	{-1,1,0,1}, {-1,1,0,-1}, {-1,-1,0,1}, {-1,-1,0,-1},
	{1,1,1,0}, {1,1,-1,0}, {1,-1,1,0}, {1,-1,-1,0},
	{-1,1,1,0}, {-1,1,-1,0}, {-1,-1,1,0}, {-1,-1,-1,0}};


// A lookup table to traverse the simplex around a given point in 4D.
// Details can be found where this table is used, in the 4D noise method.
static Vec4 simplex[] = {
	{0,1,2,3},{0,1,3,2},{0,0,0,0},{0,2,3,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,2,3,0},
	{0,2,1,3},{0,0,0,0},{0,3,1,2},{0,3,2,1},{0,0,0,0},{0,0,0,0},{0,0,0,0},{1,3,2,0},
	{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
	{1,2,0,3},{0,0,0,0},{1,3,0,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,3,0,1},{2,3,1,0},
	{1,0,2,3},{1,0,3,2},{0,0,0,0},{0,0,0,0},{0,0,0,0},{2,0,3,1},{0,0,0,0},{2,1,3,0},
	{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0},
	{2,0,1,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,0,1,2},{3,0,2,1},{0,0,0,0},{3,1,2,0},
	{2,1,0,3},{0,0,0,0},{0,0,0,0},{0,0,0,0},{3,1,0,2},{0,0,0,0},{3,2,0,1},{3,2,1,0}
};

void simplexNoise3DTable_x4Init(SimplexNoise3DTable_x4 table)
{
	int i;
	
	for(i=0;i<256;i++)
	{
		table[i] = randomU32Seeded(NULL, RandType_Mersenne);
	}
}

SimplexNoiseTable* simplexNoiseTableCreate(U32 uiTableSize)
{
	SimplexNoiseTable* pTable = malloc(sizeof(SimplexNoiseTable));
	U32 i;
	pTable->uiTableSize = pow2(uiTableSize);
	if (pTable->uiTableSize != uiTableSize)
		Errorf("simplexNoiseTableCreate requires the parameter uiTableSize to be a power of 2, which is not true of %d!", uiTableSize);
	pTable->uiMask = pTable->uiTableSize - 1;
	pTable->puiTable = malloc(sizeof(U8) * pTable->uiTableSize);
	for (i=0; i<pTable->uiTableSize; ++i)
	{
		pTable->puiTable[i] = (U8)(randomU32Seeded(NULL, RandType_Mersenne) & 0xFF);
	}
	return pTable;
}

void simplexNoiseTableDestroy(SimplexNoiseTable* pTable)
{
	free(pTable->puiTable);
	free(pTable);
}

#define GetRand(a) (pTable->puiTable[a & pTable->uiMask])


// 2D simplex noise
F32 simplexNoise2D(SimplexNoiseTable* pTable, F32 xin, F32 yin)
{
	F32 n0, n1, n2; // Noise contributions from the three corners
	// Skew the input space to determine which simplex cell we're in
	F32 F2 = 0.5*(fsqrt(3.0)-1.0);
	F32 s = (xin+yin)*F2; // Hairy factor for 2D
	int i = qtrunc(xin+s);
	int j = qtrunc(yin+s);
	F32 G2 = (3.0-fsqrt(3.0))/6.0;
	F32 t = (i+j)*G2;
	F32 X0 = i-t; // Unskew the cell origin back to (x,y) space
	F32 Y0 = j-t;
	F32 x0 = xin-X0; // The x,y distances from the cell origin
	F32 y0 = yin-Y0;
	F32 x1, y1, x2, y2, t0, t1, t2;
	int ii, jj, gi0, gi1, gi2;

	// For the 2D case, the simplex shape is an equilateral triangle.
	// Determine which simplex we are in.
	int i1, j1; // Offsets for second (middle) corner of simplex in (i,j) coords
	if(x0>y0)
	{
		i1=1;
		j1=0;
	} // lower triangle, XY order: (0,0)->(1,0)->(1,1)
	else
	{
		i1=0;
		j1=1;
	} // upper triangle, YX order: (0,0)->(0,1)->(1,1)

	// A step of (1,0) in (i,j) means a step of (1-c,-c) in (x,y), and
	// a step of (0,1) in (i,j) means a step of (-c,1-c) in (x,y), where
	// c = (3-sqrt(3))/6
	x1 = x0 - i1 + G2; // Offsets for middle corner in (x,y) unskewed coords
	y1 = y0 - j1 + G2;
	x2 = x0 - 1.0 + 2.0 * G2; // Offsets for last corner in (x,y) unskewed coords
	y2 = y0 - 1.0 + 2.0 * G2;
	// Work out the hashed gradient indices of the three simplex corners
	ii = i & pTable->uiMask;
	jj = j & pTable->uiMask;
	gi0 = GetRand(ii+GetRand(jj)) % 12;
	gi1 = GetRand(ii+i1+GetRand(jj+j1)) % 12;
	gi2 = GetRand(ii+1+GetRand(jj+1)) % 12;
	// Calculate the contribution from the three corners
	t0 = 0.5 - x0*x0-y0*y0;
	if(t0<0)
		n0 = 0.0;
	else
	{
		t0 *= t0;
		n0 = t0 * t0 * dot2(grad3[gi0], x0, y0); // (x,y) of grad3 used for 2D gradient
	}
	t1 = 0.5 - x1*x1-y1*y1;
	if(t1<0)
		n1 = 0.0;
	else
	{
		t1 *= t1;
		n1 = t1 * t1 * dot2(grad3[gi1], x1, y1);
	}
	t2 = 0.5 - x2*x2-y2*y2;
	if(t2<0)
		n2 = 0.0;
	else
	{
		t2 *= t2;
		n2 = t2 * t2 * dot2(grad3[gi2], x2, y2);
	}
	// Add contributions from each corner to get the final noise value.
	// The result is scaled to return values in the interval [-1,1].
	return 70.0 * (n0 + n1 + n2);
}


// 3D simplex noise
F32 simplexNoise3D(SimplexNoiseTable* pTable, F32 xin, F32 yin, F32 zin)
{
	F32 n0, n1, n2, n3; // Noise contributions from the four corners
	// Skew the input space to determine which simplex cell we're in
	F32 F3 = 1.0/3.0;
	F32 s = (xin+yin+zin)*F3; // Very nice and simple skew factor for 3D
	int i = qtrunc(xin+s);
	int j = qtrunc(yin+s);
	int k = qtrunc(zin+s);
	F32 G3 = 1.0/6.0; // Very nice and simple unskew factor, too
	F32 t = (i+j+k)*G3;
	F32 X0 = i-t; // Unskew the cell origin back to (x,y,z) space
	F32 Y0 = j-t;
	F32 Z0 = k-t;
	F32 x0 = xin-X0; // The x,y,z distances from the cell origin
	F32 y0 = yin-Y0;
	F32 z0 = zin-Z0;
	// For the 3D case, the simplex shape is a slightly irregular tetrahedron.
	// Determine which simplex we are in.
	int i1, j1, k1; // Offsets for second corner of simplex in (i,j,k) coords
	int i2, j2, k2; // Offsets for third corner of simplex in (i,j,k) coords

	if(x0>=y0)
	{
		if(y0>=z0)
		{
			i1=1; j1=0; k1=0; i2=1; j2=1; k2=0;
		} // X Y Z order
		else if(x0>=z0)
		{
			i1=1; j1=0; k1=0; i2=1; j2=0; k2=1;
		} // X Z Y order
		else
		{
			i1=0; j1=0; k1=1; i2=1; j2=0; k2=1;
		} // Z X Y order
	}
	else
	{ // x0<y0
		if(y0<z0)
		{
			i1=0; j1=0; k1=1; i2=0; j2=1; k2=1;
		} // Z Y X order
		else if(x0<z0)
		{
			i1=0; j1=1; k1=0; i2=0; j2=1; k2=1;
		} // Y Z X order
		else
		{
			i1=0; j1=1; k1=0; i2=1; j2=1; k2=0;
		} // Y X Z order
	}

	{

		// A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
		// a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
		// a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
		// c = 1/6.
		F32 x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
		F32 y1 = y0 - j1 + G3;
		F32 z1 = z0 - k1 + G3;
		F32 x2 = x0 - i2 + 2.0*G3; // Offsets for third corner in (x,y,z) coords
		F32 y2 = y0 - j2 + 2.0*G3;
		F32 z2 = z0 - k2 + 2.0*G3;
		F32 x3 = x0 - 1.0 + 3.0*G3; // Offsets for last corner in (x,y,z) coords
		F32 y3 = y0 - 1.0 + 3.0*G3;
		F32 z3 = z0 - 1.0 + 3.0*G3;
		// Work out the hashed gradient indices of the four simplex corners
		int ii = i & pTable->uiMask;
		int jj = j & pTable->uiMask;
		int kk = k & pTable->uiMask;
		int gi0 = GetRand(ii+GetRand(jj+GetRand(kk))) % 12;
		int gi1 = GetRand(ii+i1+GetRand(jj+j1+GetRand(kk+k1))) % 12;
		int gi2 = GetRand(ii+i2+GetRand(jj+j2+GetRand(kk+k2))) % 12;
		int gi3 = GetRand(ii+1+GetRand(jj+1+GetRand(kk+1))) % 12;
		// Calculate the contribution from the four corners
		F32 t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
		F32 t1, t2, t3;
		if(t0<0) n0 = 0.0;
		else {
			t0 *= t0;
			n0 = t0 * t0 * dot3(grad3[gi0], x0, y0, z0);
		}
		t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
		if(t1<0) n1 = 0.0;
		else {
			t1 *= t1;
			n1 = t1 * t1 * dot3(grad3[gi1], x1, y1, z1);
		}
		t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
		if(t2<0) n2 = 0.0;
		else {
			t2 *= t2;
			n2 = t2 * t2 * dot3(grad3[gi2], x2, y2, z2);
		}
		t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
		if(t3<0) n3 = 0.0;
		else {
			t3 *= t3;
			n3 = t3 * t3 * dot3(grad3[gi3], x3, y3, z3);
		}
		// Add contributions from each corner to get the final noise value.
		// The result is scaled to stay just inside [-1,1]
		return 32.0*(n0 + n1 + n2 + n3);
	}
}


// 4D simplex noise
F32 simplexNoise4D(SimplexNoiseTable* pTable, F32 x, F32 y, F32 z, F32 w)
{
	// The skewing and unskewing factors are hairy again for the 4D case
	F32 F4 = (fsqrt(5.0)-1.0)/4.0;
	F32 G4 = (5.0-fsqrt(5.0))/20.0;
	F32 n0, n1, n2, n3, n4; // Noise contributions from the five corners
	// Skew the (x,y,z,w) space to determine which cell of 24 simplices we're in
	F32 s = (x + y + z + w) * F4; // Factor for 4D skewing
	int i = qtrunc(x + s);
	int j = qtrunc(y + s);
	int k = qtrunc(z + s);
	int l = qtrunc(w + s);
	F32 t = (i + j + k + l) * G4; // Factor for 4D unskewing
	F32 X0 = i - t; // Unskew the cell origin back to (x,y,z,w) space
	F32 Y0 = j - t;
	F32 Z0 = k - t;
	F32 W0 = l - t;
	F32 x0 = x - X0; // The x,y,z,w distances from the cell origin
	F32 y0 = y - Y0;
	F32 z0 = z - Z0;
	F32 w0 = w - W0;
	// For the 4D case, the simplex is a 4D shape I won't even try to describe.
	// To find out which of the 24 possible simplices we're in, we need to
	// determine the magnitude ordering of x0, y0, z0 and w0.
	// The method below is a good way of finding the ordering of x,y,z,w and
	// then find the correct traversal order for the simplex we’re in.
	// First, six pair-wise comparisons are performed between each possible pair
	// of the four coordinates, and the results are used to add up binary bits
	// for an integer index.
	int c1 = (x0 > y0) ? 32 : 0;
	int c2 = (x0 > z0) ? 16 : 0;
	int c3 = (y0 > z0) ? 8 : 0;
	int c4 = (x0 > w0) ? 4 : 0;
	int c5 = (y0 > w0) ? 2 : 0;
	int c6 = (z0 > w0) ? 1 : 0;
	int c = c1 + c2 + c3 + c4 + c5 + c6;
	int i1, j1, k1, l1; // The integer offsets for the second simplex corner
	int i2, j2, k2, l2; // The integer offsets for the third simplex corner
	int i3, j3, k3, l3; // The integer offsets for the fourth simplex corner
	// simplex[c] is a 4-vector with the numbers 0, 1, 2 and 3 in some order.
	// Many values of c will never occur, since e.g. x>y>z>w makes x<z, y<w and x<w
	// impossible. Only the 24 indices which have non-zero entries make any sense.
	// We use a thresholding to set the coordinates in turn from the largest magnitude.
	// The number 3 in the "simplex" array is at the position of the largest coordinate.
	i1 = simplex[c][0]>=3 ? 1 : 0;
	j1 = simplex[c][1]>=3 ? 1 : 0;
	k1 = simplex[c][2]>=3 ? 1 : 0;
	l1 = simplex[c][3]>=3 ? 1 : 0;
	// The number 2 in the "simplex" array is at the second largest coordinate.
	i2 = simplex[c][0]>=2 ? 1 : 0;
	j2 = simplex[c][1]>=2 ? 1 : 0;
	k2 = simplex[c][2]>=2 ? 1 : 0;
	l2 = simplex[c][3]>=2 ? 1 : 0;
	// The number 1 in the "simplex" array is at the second smallest coordinate.
	i3 = simplex[c][0]>=1 ? 1 : 0;
	j3 = simplex[c][1]>=1 ? 1 : 0;
	k3 = simplex[c][2]>=1 ? 1 : 0;
	l3 = simplex[c][3]>=1 ? 1 : 0;
	// The fifth corner has all coordinate offsets = 1, so no need to look that up.

	{

		F32 x1 = x0 - i1 + G4; // Offsets for second corner in (x,y,z,w) coords
		F32 y1 = y0 - j1 + G4;
		F32 z1 = z0 - k1 + G4;
		F32 w1 = w0 - l1 + G4;
		F32 x2 = x0 - i2 + 2.0*G4; // Offsets for third corner in (x,y,z,w) coords
		F32 y2 = y0 - j2 + 2.0*G4;
		F32 z2 = z0 - k2 + 2.0*G4;
		F32 w2 = w0 - l2 + 2.0*G4;
		F32 x3 = x0 - i3 + 3.0*G4; // Offsets for fourth corner in (x,y,z,w) coords
		F32 y3 = y0 - j3 + 3.0*G4;
		F32 z3 = z0 - k3 + 3.0*G4;
		F32 w3 = w0 - l3 + 3.0*G4;
		F32 x4 = x0 - 1.0 + 4.0*G4; // Offsets for last corner in (x,y,z,w) coords
		F32 y4 = y0 - 1.0 + 4.0*G4;
		F32 z4 = z0 - 1.0 + 4.0*G4;
		F32 w4 = w0 - 1.0 + 4.0*G4;
		// Work out the hashed gradient indices of the five simplex corners
		int ii = i & pTable->uiMask;
		int jj = j & pTable->uiMask;
		int kk = k & pTable->uiMask;
		int ll = l & pTable->uiMask;
		int gi0 = GetRand(ii+GetRand(jj+GetRand(kk+GetRand(ll)))) % 32;
		int gi1 = GetRand(ii+i1+GetRand(jj+j1+GetRand(kk+k1+GetRand(ll+l1)))) % 32;
		int gi2 = GetRand(ii+i2+GetRand(jj+j2+GetRand(kk+k2+GetRand(ll+l2)))) % 32;
		int gi3 = GetRand(ii+i3+GetRand(jj+j3+GetRand(kk+k3+GetRand(ll+l3)))) % 32;
		int gi4 = GetRand(ii+1+GetRand(jj+1+GetRand(kk+1+GetRand(ll+1)))) % 32;
		// Calculate the contribution from the five corners
		F32 t0 = 0.6 - x0*x0 - y0*y0 - z0*z0 - w0*w0;
		F32 t1, t2, t3, t4;
		if(t0<0) n0 = 0.0;
		else {
			t0 *= t0;
			n0 = t0 * t0 * dot4(grad4[gi0], x0, y0, z0, w0);
		}
		t1 = 0.6 - x1*x1 - y1*y1 - z1*z1 - w1*w1;
		if(t1<0) n1 = 0.0;
		else {
			t1 *= t1;
			n1 = t1 * t1 * dot4(grad4[gi1], x1, y1, z1, w1);
		}
		t2 = 0.6 - x2*x2 - y2*y2 - z2*z2 - w2*w2;
		if(t2<0) n2 = 0.0;
		else {
			t2 *= t2;
			n2 = t2 * t2 * dot4(grad4[gi2], x2, y2, z2, w2);
		}
		t3 = 0.6 - x3*x3 - y3*y3 - z3*z3 - w3*w3;
		if(t3<0) n3 = 0.0;
		else {
			t3 *= t3;
			n3 = t3 * t3 * dot4(grad4[gi3], x3, y3, z3, w3);
		}
		t4 = 0.6 - x4*x4 - y4*y4 - z4*z4 - w4*w4;
		if(t4<0) n4 = 0.0;
		else {
			t4 *= t4;
			n4 = t4 * t4 * dot4(grad4[gi4], x4, y4, z4, w4);
		}
		// Sum up and scale the result to cover the range [-1,1]
		return 27.0 * (n0 + n1 + n2 + n3 + n4);
	}
}
#endif  // !SPU

#define GetTable(a) table[(a) & 0xff]
// 3D simplex noise : This does 4 at once and returns result in a Vec4
void simplexNoise3D_x4(const SimplexNoise3DTable_x4 table, F32 xin, F32 yin, F32 zin, Vec4 dest)
{
	F32 n00, n01, n02, n03; // Noise contributions from the four corners for Vec[0]
	F32 n10, n11, n12, n13; // Noise contributions from the four corners for Vec[1]
	F32 n20, n21, n22, n23; // Noise contributions from the four corners for Vec[2]
	F32 n30, n31, n32, n33; // Noise contributions from the four corners for Vec[3]
	// Skew the input space to determine which simplex cell we're in
	F32 F3 = 1.0/3.0;
	F32 s = (xin+yin+zin)*F3; // Very nice and simple skew factor for 3D
	int i = qtrunc(xin+s);
	int j = qtrunc(yin+s);
	int k = qtrunc(zin+s);
	F32 G3 = 1.0/6.0; // Very nice and simple unskew factor, too
	F32 t = (i+j+k)*G3;
	F32 X0 = i-t; // Unskew the cell origin back to (x,y,z) space
	F32 Y0 = j-t;
	F32 Z0 = k-t;
	F32 x0 = xin-X0; // The x,y,z distances from the cell origin
	F32 y0 = yin-Y0;
	F32 z0 = zin-Z0;
	// For the 3D case, the simplex shape is a slightly irregular tetrahedron.
	// Determine which simplex we are in.
	int i1, j1, k1; // Offsets for second corner of simplex in (i,j,k) coords
	int i2, j2, k2; // Offsets for third corner of simplex in (i,j,k) coords

	if(x0>=y0)
	{
		if(y0>=z0)
		{
			i1=1; j1=0; k1=0; i2=1; j2=1; k2=0;
		} // X Y Z order
		else if(x0>=z0)
		{
			i1=1; j1=0; k1=0; i2=1; j2=0; k2=1;
		} // X Z Y order
		else
		{
			i1=0; j1=0; k1=1; i2=1; j2=0; k2=1;
		} // Z X Y order
	}
	else
	{ // x0<y0
		if(y0<z0)
		{
			i1=0; j1=0; k1=1; i2=0; j2=1; k2=1;
		} // Z Y X order
		else if(x0<z0)
		{
			i1=0; j1=1; k1=0; i2=0; j2=1; k2=1;
		} // Y Z X order
		else
		{
			i1=0; j1=1; k1=0; i2=1; j2=1; k2=0;
		} // Y X Z order
	}

	{
		// A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
		// a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), and
		// a step of (0,0,1) in (i,j,k) means a step of (-c,-c,1-c) in (x,y,z), where
		// c = 1/6.
		F32 x1 = x0 - i1 + G3; // Offsets for second corner in (x,y,z) coords
		F32 y1 = y0 - j1 + G3;
		F32 z1 = z0 - k1 + G3;
		F32 x2 = x0 - i2 + 2.0*G3; // Offsets for third corner in (x,y,z) coords
		F32 y2 = y0 - j2 + 2.0*G3;
		F32 z2 = z0 - k2 + 2.0*G3;
		F32 x3 = x0 - 1.0 + 3.0*G3; // Offsets for last corner in (x,y,z) coords
		F32 y3 = y0 - 1.0 + 3.0*G3;
		F32 z3 = z0 - 1.0 + 3.0*G3;
		
		// get 4 corners of 32 bit random numbers.  These will get used to lookup into table of gradients
		// actually 32 bit random seed is 4 - 8 bit numbers packed
		int r0 = GetTable(i+GetTable(j+GetTable(k)));
		int r1 = GetTable(i+i1+GetTable(j+j1+GetTable(k+k1)));
		int r2 = GetTable(i+i2+GetTable(j+j2+GetTable(k+k2)));
		int r3 = GetTable(i+1+GetTable(j+1+GetTable(k+1)));

		// Calculate the contribution from the four corners
		F32 t0 = 0.6 - x0*x0 - y0*y0 - z0*z0;
		F32 t1, t2, t3;
		if(t0<0) 
		{
			n00 = 0.0;
			n10 = 0.0;
			n20 = 0.0;
			n30 = 0.0;
		}
		else {
			t0 = t0*t0*t0*t0;
			n00 = t0 * dot3(grad3[ (r0      &0xff)%12 ], x0, y0, z0);
			n10 = t0 * dot3(grad3[ ((r0>>8) &0xff)%12 ], x0, y0, z0);
			n20 = t0 * dot3(grad3[ ((r0>>16)&0xff)%12 ], x0, y0, z0);
			n30 = t0 * dot3(grad3[ ((r0>>24)&0xff)%12 ], x0, y0, z0);
		}
		t1 = 0.6 - x1*x1 - y1*y1 - z1*z1;
		if(t1<0)
		{
			n01 = 0.0;
			n11 = 0.0;
			n21 = 0.0;
			n31 = 0.0;
		}
		else {
			t1 = t1*t1*t1*t1;
			n01 = t1 * dot3(grad3[ (r1      &0xff)%12 ], x1, y1, z1);
			n11 = t1 * dot3(grad3[ ((r1>>8) &0xff)%12 ], x1, y1, z1);
			n21 = t1 * dot3(grad3[ ((r1>>16)&0xff)%12 ], x1, y1, z1);
			n31 = t1 * dot3(grad3[ ((r1>>24)&0xff)%12 ], x1, y1, z1);
		}

		t2 = 0.6 - x2*x2 - y2*y2 - z2*z2;
		if(t2<0)
		{
			n02 = 0.0;
			n12 = 0.0;
			n22 = 0.0;
			n32 = 0.0;
		}
		else {
			t2 = t2*t2*t2*t2;
			n02 = t2 * dot3(grad3[ (r2      &0xff)%12 ], x2, y2, z2);
			n12 = t2 * dot3(grad3[ ((r2>>8) &0xff)%12 ], x2, y2, z2);
			n22 = t2 * dot3(grad3[ ((r2>>16)&0xff)%12 ], x2, y2, z2);
			n32 = t2 * dot3(grad3[ ((r2>>24)&0xff)%12 ], x2, y2, z2);
		}
		t3 = 0.6 - x3*x3 - y3*y3 - z3*z3;
		if(t3<0)
		{
			n03 = 0.0;
			n13 = 0.0;
			n23 = 0.0;
			n33 = 0.0;
		}
		else {
			t3 = t3*t3*t3*t3;
			n03 = t3 * dot3(grad3[ (r3      &0xff)%12 ], x3, y3, z3);
			n13 = t3 * dot3(grad3[ ((r3>>8) &0xff)%12 ], x3, y3, z3);
			n23 = t3 * dot3(grad3[ ((r3>>16)&0xff)%12 ], x3, y3, z3);
			n33 = t3 * dot3(grad3[ ((r3>>24)&0xff)%12 ], x3, y3, z3);
		}

		// Add contributions from each corner to get the final noise value.
		// The result is scaled to stay just inside [-1,1]
		dest[0] = 32.0*(n00 + n01 + n02 + n03);
		dest[1] = 32.0*(n10 + n11 + n12 + n13);
		dest[2] = 32.0*(n20 + n21 + n22 + n23);
		dest[3] = 32.0*(n30 + n31 + n32 + n33);
	}
}
