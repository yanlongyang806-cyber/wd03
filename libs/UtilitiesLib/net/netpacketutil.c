#include "netpacketutil.h"
#include "mathutil.h"
#include "net/net.h"
#include "../../3rdparty/zlib/zlib.h"
#include <stdio.h>

#if !_PS3
#pragma warning(disable:6297)
#endif

U32 packQuatElem(F32 qelem,int numbits)
{
	assert( fabsf(qelem) <= 1.0f );
	// quat elements are non-linear in their representation, so make them linear for compression
	qelem = asinf(qelem); // range is now -pi/2 to pi/2
	qelem = (F32)((1 << numbits) * (qelem + (HALFPI)) / PI);
	if (qelem < 0)
		qelem = 0;
	if (qelem > (1 << numbits)-1)
		qelem = (F32)((1 << numbits)-1);
	return qtrunc(qelem);
}

F32 unpackQuatElem(U32 val,int numbits)
{
	F32		qelem;

	qelem = (F32)(((val * PI) / (1 << numbits)) - HALFPI);
	if (qelem < -HALFPI )
		qelem = -HALFPI;
	if (qelem > (HALFPI-0.00001f))
		qelem = HALFPI;

	if ( fabsf(qelem) < 0.00001f )
		qelem = 0.0f;

	return sinf(qelem);
}

U32 packQuatElemQuarterPi(F32 qelem,int numbits)
{
	assert( fabsf(qelem) <= 0.7072f );
	// quat elements are non-linear in their representation, so make them linear for compression
	qelem = asinf(qelem); // range is now -pi/4 to pi/4
	qelem = (F32)((1 << numbits) * (qelem + (QUARTERPI)) / HALFPI);
	if (qelem < 0)
		qelem = 0;
	if (qelem > (1 << numbits)-1)
		qelem = (F32)((1 << numbits)-1);
	return round(qelem);
}

F32 unpackQuatElemQuarterPi(U32 val,int numbits)
{
	F32		qelem;


	qelem = (F32)val;
	qelem = (F32)(((qelem * HALFPI) / (1 << numbits)) - QUARTERPI);
	if (qelem < -QUARTERPI )
		qelem = -QUARTERPI;
	if (qelem > (QUARTERPI-0.00001f))
		qelem = QUARTERPI;

	if ( fabsf(qelem) < 0.00001f )
		qelem = 0.0f;

	return sinf(qelem);
}

U32 packEuler(F32 ang,int numbits)
{
	ang = fixAngle(ang);
	ang = (F32)((1 << numbits) * (ang + PI) / (2*PI));
	if (ang < 0)
		ang = 0;
	if (ang > (1 << numbits)-1)
		ang = (F32)((1 << numbits)-1);
	return qtrunc(ang);
}

F32 unpackEuler(U32 val,int numbits)
{
F32		ang;

	ang = (F32)(((val * 2*PI) / (1 << numbits)) - PI);
	return ang;
}

U32 packPos(F32 pos)
{
int		t;

	t = (int)(pos * POS_SCALE);
	t += (1 << (POS_BITS-1));
	return qtrunc((F32)t);
}

F32 unpackPos(U32 ut)
{
int		t;

	t = ut;
	t -= (1 << (POS_BITS-1));
	return t / POS_SCALE;

}

F32 quantizePos(F32 pos)
{
	return unpackPos(packPos(pos));
}

#define NEARZERO 1e-16

void unitZeroMat(Mat4 mat,int *unit,int *zero)
{
	if (nearSameVec3Tol(mat[0],unitmat[0],NEARZERO)
		&& nearSameVec3Tol(mat[1],unitmat[1],NEARZERO)
		&& nearSameVec3Tol(mat[2],unitmat[2],NEARZERO))
		*unit = 1;
	else
		*unit = 0;

	if (nearSameVec3Tol(mat[3],zerovec3,NEARZERO))
		*zero = 1;
	else
		*zero = 0;
}

void unpackMat(PackMat *pm,Mat4 mat)
{
	copyMat4(unitmat,mat);
	if (pm->has_pyr)
		createMat3YPR(mat,pm->pyr);
	if (pm->has_scale)
		scaleMat3Vec3(mat,pm->scale);
	if (pm->has_pos)
		copyVec3(pm->pos,mat[3]);
}

// returns 1 if it was able to pack with no float error
int packMat(const Mat4 mat_in,PackMat *pm)
{
	int		unit,zero;
	Mat4	mat,test_mat;

	pm->has_pos = pm->has_pyr = pm->has_scale = 0;
	copyMat4(mat_in,mat);
	unitZeroMat(mat,&unit,&zero);
	if (!unit)
	{
		extractScale(mat,pm->scale);
		if (!nearSameVec3(pm->scale,onevec3))
			pm->has_scale = 1;
		getMat3YPR(mat,pm->pyr);
		pm->has_pyr = 1;
	}
	if (!zero)
	{
		copyVec3(mat[3],pm->pos);
		pm->has_pos = 1;
	}
	unpackMat(pm,test_mat);
	return memcmp(test_mat,mat,sizeof(mat))==0;
		
}


#define INT_SCALE 2
void pktSendF32Comp(Packet *pak,F32 val)
{
int		ival;

	ival = (int)(val*INT_SCALE);
	if (val*INT_SCALE == ival)
	{
		pktSendBits(pak,1,1);
		pktSendBits(pak,1,(ival >> 31) & 1);
		pktSendBitsPack(pak,5,ABS(ival));
	}
	else
	{
		pktSendBits(pak,1,0);
		pktSendF32(pak,val);
	}
}

F32 pktGetF32Comp(Packet *pak)
{
int		is_int,negate;
F32		val;

	is_int = pktGetBits(pak,1);
	if (is_int)
	{
		negate = pktGetBits(pak,1);
		val = (1.f/INT_SCALE) * pktGetBitsPack(pak,5);
		if (negate)
			val = -val;
	}
	else
		val = pktGetF32(pak);
	return val;
}

void pktSendF32Deg(Packet *pak,F32 val)
{
	pktSendF32Comp(pak,val);
}

F32 pktGetF32Deg(Packet *pak)
{
F32		rad;

	rad = pktGetF32Comp(pak);
	return rad;
}

void pktSendMat4(Packet *pak,const Mat4 mat)
{
	int		i;
	PackMat	pm;

	if (packMat(mat,&pm))
	{
		pktSendBits(pak,1,1); // packed
		pktSendBits(pak,1,pm.has_pos);
		pktSendBits(pak,1,pm.has_pyr);
		pktSendBits(pak,1,pm.has_scale);
		if (pm.has_pos)
		{
			for(i=0;i<3;i++)
				pktSendF32Comp(pak,pm.pos[i]);
		}
		if (pm.has_pyr)
		{
			for(i=0;i<3;i++)
				pktSendF32Deg(pak,pm.pyr[i]);
		}
		if (pm.has_scale)
		{
			for(i=0;i<3;i++)
				pktSendF32(pak,pm.scale[i]);
		}
	}
	else
	{
		pktSendBits(pak,1,0); // unpacked
		pktSendMat4Full(pak,mat);
	}
}

void pktGetMat4(Packet *pak,Mat4 mat)
{
	PackMat	pm;
	int		i,packed;

	packed		= pktGetBits(pak,1);
	if (packed)
	{
		pm.has_pos	= pktGetBits(pak,1);
		pm.has_pyr	= pktGetBits(pak,1);
		pm.has_scale= pktGetBits(pak,1);
		if (pm.has_pos)
		{
			for(i=0;i<3;i++)
				pm.pos[i] = pktGetF32Comp(pak);
		}
		if (pm.has_pyr)
		{
			for(i=0;i<3;i++)
				pm.pyr[i] = pktGetF32Deg(pak);
		}
		if (pm.has_scale)
		{
			for(i=0;i<3;i++)
				pm.scale[i] = pktGetF32(pak);
		}
		unpackMat(&pm,mat);
	}
	else
	{
		pktGetMat4Full(pak,mat);
	}
}

void pktSendZippedAlready(Packet *pak,int numbytes,int zipbytes,void *zipdata)
{
	pktSendBitsPack(pak,1,zipbytes);
	pktSendBitsPack(pak,1,numbytes);
	pktSendBytes(pak,zipbytes,zipdata);
}

void pktSendZipped(Packet *pak,int numbytes,void *data)
{
	U8		*zip_data;
	U32		zip_size;
	int		ret;

	zip_size = (U32)(numbytes*1.0125+12); // 1% + 12 bigger, so says the zlib docs
	zip_data = malloc(zip_size);
	ret = compress2(zip_data,&zip_size,data,numbytes,5);
	assert(ret == Z_OK);
	pktSendZippedAlready(pak,numbytes,zip_size,zip_data);
	free(zip_data);
}

U8 *pktGetZippedInfo(Packet *pak,U32 *zipbytes_p,U32 *rawbytes_p)
{
	U32		zip_size,numbytes;
	U8		*zip_data;

	zip_size = pktGetBitsPack(pak,1);
	numbytes = pktGetBitsPack(pak,1);

	zip_data = malloc(zip_size);
	pktGetBytes(pak,zip_size,zip_data);
	if (zipbytes_p)
		*zipbytes_p = zip_size;
	if (rawbytes_p)
		*rawbytes_p = numbytes;
	return zip_data;
}

U8 *pktGetZipped(Packet *pak,U32 *numbytes_p)
{
	U32		zip_size,numbytes;
	U8		*zip_data,*data;

	zip_data = pktGetZippedInfo(pak,&zip_size,&numbytes);
	data = malloc(numbytes+1);
	data[numbytes] = 0;
	uncompress(data,&numbytes,zip_data,zip_size);
	free(zip_data);
	if (numbytes_p)
		*numbytes_p = numbytes;
	return data;
}

void pktSendColor(Packet *pak, Color clr)
{
	pktSendBits(pak, 8, clr.r);
	pktSendBits(pak, 8, clr.g);
	pktSendBits(pak, 8, clr.b);
	pktSendBits(pak, 8, clr.a);
}

Color pktGetColor(Packet *pak)
{
	Color clr;
	clr.r = pktGetBits(pak, 8);
	clr.g = pktGetBits(pak, 8);
	clr.b = pktGetBits(pak, 8);
	clr.a = pktGetBits(pak, 8);
	return clr;
}

void pktGetVec2(Packet *pak, Vec2 vec)
{
	Vec2 tempVec;

	vec = vec ? vec : tempVec;
	
	vec[0] = pktGetF32(pak);
	vec[1] = pktGetF32(pak);
}

void pktGetVec3(Packet *pak, Vec3 vec)
{
	Vec3 tempVec;

	vec = vec ? vec : tempVec;
	
	vec[0] = pktGetF32(pak);
	vec[1] = pktGetF32(pak);
	vec[2] = pktGetF32(pak);
}

void pktSendVec2(Packet *pak, const Vec2 vec)
{
	pktSendF32(pak, vec[0]);
	pktSendF32(pak, vec[1]);
}

void pktSendVec3(Packet *pak, const Vec3 vec)
{
	pktSendF32(pak, vec[0]);
	pktSendF32(pak, vec[1]);
	pktSendF32(pak, vec[2]);
}

void pktGetIVec2(Packet* pak, IVec2 vec)
{
	IVec2 tempVec;

	vec = vec ? vec : tempVec;

	vec[0] = pktGetBits(pak, 32);
	vec[1] = pktGetBits(pak, 32);
}

void pktGetIVec3(Packet* pak, IVec3 vec)
{
	IVec3 tempVec;

	vec = vec ? vec : tempVec;

	vec[0] = pktGetBits(pak, 32);
	vec[1] = pktGetBits(pak, 32);
	vec[2] = pktGetBits(pak, 32);
}

void pktSendIVec2(Packet *pak, const IVec2 vec)
{
	pktSendBits(pak, 32, vec[0]);
	pktSendBits(pak, 32, vec[1]);
}

void pktSendIVec3(Packet *pak, const IVec3 vec)
{
	pktSendBits(pak, 32, vec[0]);
	pktSendBits(pak, 32, vec[1]);
	pktSendBits(pak, 32, vec[2]);
}

void pktSendVec4(Packet *pak, const Vec4 vec)
{
	pktSendF32(pak, vec[0]);
	pktSendF32(pak, vec[1]);
	pktSendF32(pak, vec[2]);
	pktSendF32(pak, vec[3]);
}

void pktGetVec4(Packet *pak, Vec4 vec)
{
	vec[0] = pktGetF32(pak);
	vec[1] = pktGetF32(pak);
	vec[2] = pktGetF32(pak);
	vec[3] = pktGetF32(pak);
}

void pktSendQuat(Packet *pak, const Quat quat)
{
	pktSendF32(pak, quat[0]);
	pktSendF32(pak, quat[1]);
	pktSendF32(pak, quat[2]);
	pktSendF32(pak, quat[3]);
}

void pktGetQuat(Packet *pak, Quat quat)
{
	if(quat){
		quat[0] = pktGetF32(pak);
		quat[1] = pktGetF32(pak);
		quat[2] = pktGetF32(pak);
		quat[3] = pktGetF32(pak);
	}else{
		pktGetF32(pak);
		pktGetF32(pak);
		pktGetF32(pak);
		pktGetF32(pak);
	}
}

void pktSendMat4Full(Packet *pak, const Mat4 mat)
{
	pktSendVec3(pak, mat[0]);
	pktSendVec3(pak, mat[1]);
	pktSendVec3(pak, mat[2]);
	pktSendVec3(pak, mat[3]);
}

void pktGetMat4Full(Packet *pak, Mat4 mat)
{
	pktGetVec3(pak, mat[0]);
	pktGetVec3(pak, mat[1]);
	pktGetVec3(pak, mat[2]);
	pktGetVec3(pak, mat[3]);
}

