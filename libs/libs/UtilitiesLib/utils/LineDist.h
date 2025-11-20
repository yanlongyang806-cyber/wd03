#pragma once

F32 LineSegLineSegDistSquared(SA_PRE_NN_RELEMS(3) const Vec3 vP1, SA_PRE_NN_RELEMS(3) const Vec3 vQ1, SA_PRE_NN_RELEMS(3) const Vec3 vP2, SA_PRE_NN_RELEMS(3) const Vec3 vQ2, SA_PRE_NN_FREE SA_POST_NN_VALID F32* pfS, SA_PRE_NN_FREE SA_POST_NN_VALID F32* pfT, Vec3 vC1, Vec3 vC2);
F32 LineLineDistSquared(SA_PRE_NN_RELEMS(3) const Vec3 L1pt, SA_PRE_NN_RELEMS(3) const Vec3 L1dir, F32 L1len, Vec3 L1isect, SA_PRE_NN_RELEMS(3) const Vec3 L2pt, SA_PRE_NN_RELEMS(3) const Vec3 L2dir, F32 L2len, Vec3 L2isect );
F32 pointLineDistSquared(SA_PRE_NN_RELEMS(3) const Vec3 pt, SA_PRE_NN_RELEMS(3) const Vec3 start, SA_PRE_NN_RELEMS(3) const Vec3 end, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 col);

/* Point: px,pz Line: (lAx,lAz),(lAx+ldx,LAz+ldx) */
F32 PointLineDist2DSquared(	F32 px, F32 pz,
							F32 lAx, F32 lAz, F32 ldx, F32 ldz,
							SA_PARAM_NN_VALID F32 *lx, SA_PARAM_NN_VALID F32 *lz);
void PointLineSegClosestPoint(Vec3 p, Vec3 a, Vec3 b, F32* t, Vec3 d);
void PointLineClosestPoint(const Vec3 pt, const Vec3 Lpt, const Vec3 Ldir, const F32 Llen, Vec3 closePt);
F32 PointLineDistSquared(SA_PRE_NN_RELEMS(3) const Vec3 pt, SA_PRE_NN_RELEMS(3) const Vec3 Lpt, SA_PRE_NN_RELEMS(3) const Vec3 Ldir, const F32 Llen, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 col);
F32 PointLineDistSquaredXZ(SA_PRE_NN_RELEMS(3) const Vec3 pt, SA_PRE_NN_RELEMS(3) const Vec3 Lpt, SA_PRE_NN_RELEMS(3) const Vec3 Ldir, const F32 Llen, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 col);
F32 pointLineDistSquaredXZ(SA_PRE_NN_RELEMS(3) const Vec3 pt, SA_PRE_NN_RELEMS(3) const Vec3 start, SA_PRE_NN_RELEMS(3) const Vec3 end, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 col);
F32 LineLineDist2DSquared(	F32 l1Ax, F32 l1Az, F32 l1Bx, F32 l1Bz, SA_PARAM_NN_VALID F32 *x1, SA_PARAM_NN_VALID F32 *z1,
							F32 l2Ax, F32 l2Az, F32 l2Bx, F32 l2Bz);
F32 LineLineDistSquaredXZ(SA_PRE_NN_RELEMS(3) const Vec3 a1, SA_PRE_NN_RELEMS(3) const Vec3 a2, SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 acoll, SA_PRE_NN_RELEMS(3) const Vec3 b1, SA_PRE_NN_RELEMS(3) const Vec3 b2);
