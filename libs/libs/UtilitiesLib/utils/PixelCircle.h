#pragma once
GCC_SYSTEM


//this simple little module generates circular "pens" of coordinates pixels of varying radii. For instance, if you pass in radius 3,
//you will get back out "(-3, 0) (-2,-2) (-2, -1) (-2, 0) (-2, 1) (-2, 2) (-1, -2) etc."
//
//note that a "radius" of 0 effectively means just a single dot, radius of 1 means a 5-pixel cross, etc.


AUTO_STRUCT;
typedef struct PixelCircleCoords
{
	int x;
	int y;
} PixelCircleCoords;

AUTO_STRUCT;
typedef struct PixelCircle
{
	PixelCircleCoords **ppCoords;
} PixelCircle;


PixelCircle *pFindCircleCoordsForRadius(int iRadius);
