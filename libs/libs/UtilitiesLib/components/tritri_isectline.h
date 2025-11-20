#ifndef _TRITRI_ISECTLINE_H
#define _TRITRI_ISECTLINE_H

int tri_tri_intersect_with_isectline(const float V0[3],const float V1[3],const float V2[3], const float U0[3],const float U1[3],const float U2[3],int *coplanar, float isectpt1[3],float isectpt2[3]);
int tri_tri_overlap_test_2d(const float p1[2], const float q1[2], const float r1[2], const float p2[2], const float q2[2], const float r2[2]);

#endif
