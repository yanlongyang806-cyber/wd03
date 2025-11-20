#ifndef NO_EDITORS

#include "EditLib.h"
#include "EditLibGizmos.h"
#include "EditLibGizmosToolbar.h"
#include "quat.h"
#include "LineDist.h"
#include "gfxprimitive.h"
#include "GfxSpriteText.h"
#include "EditorManager.h"
#include "GfxCommandParse.h"
#include "WorldGrid.h"
#include "ObjectLibrary.h"
#include "RdrDevice.h"
#include "wininclude.h"
#include "Prefs.h"
#include "Materials.h"
#include "EditorPrefs.h"
#include "inputLib.h"

#include "EditLibGizmos_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define angle(v1, v2) acos(dotVec3(v1, v2) / (lengthVec3(v1) * lengthVec3(v2)))
#define setLength(v, l) normalVec3(v);\
	scaleVec3(v, l, v)
#define checkMatOutOfBounds(mat) \
	(mat[3][0] < -1e6 || mat[3][0] > 1e6 || \
	mat[3][1] < -1e6 || mat[3][1] > 1e6 || \
	mat[3][2] < -1e6 || mat[3][2] > 1e6) \

typedef struct ConeGizmo
{
	float startAngle;					// the initial half angle of the cone
	float snapResolution;				// the angular snap resolution of the gizmo
	Vec2 mouseDiff;						// stored motion of the mouse; used to determine how to re-angle the cone
	Vec3 arrowDir;						// stored direction in world coordinates of the arrow that determines
										// where to move the mouse to increase the cone angle
	bool active;						// this determines whether the gizmo is being dragged or not
	bool snapEnabled;					// this determines whether snap is enabled on the gizmo
} ConeGizmo;

typedef struct PyramidGizmo
{
	float startAngle;					// the initial half angle of the cone
	float snapResolution;				// the angular snap resolution of the gizmo
	Vec2 mouseDiff;						// stored motion of the mouse; used to determine how to re-angle the cone
	Vec3 arrowDir;						// stored direction in world coordinates of the arrow that determines
										// where to move the mouse to increase the cone angle
	int angleOver;						// determines which angle will be modified depending on what the mouse is currently over
	int angleActive;					// same as angleOver, except after the mouse has clicked the angle
	bool active;						// this determines whether the gizmo is being dragged or not
	bool snapEnabled;					// this determines whether snap is enabled on the gizmo
} PyramidGizmo;

typedef struct RadialGizmo
{
	float startRadius;					// the initial radius of the sphere
	float clickedRadius;				// the radius determined by how far the user clicked from the center
	float snapResolution;				// the radial snap resolution of the gizmo
	Vec3 axisDir;						// direction of the scaling axis
	Vec3 tickDir;						// direction of the ticks
	Vec3 intersection;					// closest point on the axis direction to where the mouse is
	bool active;						// this determines whether the gizmo is being dragged or not
	bool snapEnabled;					// this determines whether snap is enabled on the gizmo
	bool relativeDragMode;
} RadialGizmo;

typedef struct ScaleMinMaxGizmo
{
	Vec3 scaleMinMax[2];
	Mat4 activeMatrix;
	int selectedAxis;
	F32 selectedOffset;
	bool activated;
	F32 snapResolution;
	bool mirrored;

	void *fn_context;
	GizmoActivateScaleMinMaxFn activate_fn;		// function called when the mouse button is pressed
	GizmoActivateScaleMinMaxFn deactivate_fn;	// function called when the mouse button is released

} ScaleMinMaxGizmo;

static int angleSnaps[GIZMO_NUM_ANGLES] = {1, 5, 10, 15, 45, 90};
static float widthSnaps[GIZMO_NUM_WIDTHS] = {0.5, 1, 5, 10, 20, 30, 40, 50, 100, 200, 300, 400, 500, 1000};

int GizmoGetSnapAngle(int res)
{
	return angleSnaps[CLAMP(res, 1, GIZMO_NUM_ANGLES) - 1];
}

int *GizmoGetSnapAngles(void)
{
	return angleSnaps;
}

float GizmoGetSnapWidth(int res)
{
	return widthSnaps[CLAMP(res, 1, GIZMO_NUM_WIDTHS) - 1];
}

float *GizmoGetSnapWidths(void)
{
	return widthSnaps;
}

ConeGizmo *ConeGizmoCreate(void)
{
	ConeGizmo *gizmo = calloc(1, sizeof(ConeGizmo));
	gizmo->active = false;
	gizmo->snapEnabled = true;
	gizmo->snapResolution = 1;
	return gizmo;
}

PyramidGizmo *PyramidGizmoCreate(void)
{
	PyramidGizmo *gizmo = calloc(1, sizeof(PyramidGizmo));
	gizmo->active = false;
	gizmo->snapEnabled = true;
	gizmo->snapResolution = 1;
	gizmo->angleOver = 0;
	gizmo->angleActive = 0;
	return gizmo;
}

RadialGizmo *RadialGizmoCreate(void)
{
	RadialGizmo *gizmo = calloc(1, sizeof(RadialGizmo));
	gizmo->active = false;
	gizmo->snapEnabled = false;
	gizmo->snapResolution = 2;
	gizmo->relativeDragMode = true;
	return gizmo;
}

void ConeGizmoDestroy(ConeGizmo *gizmo)
{
	free(gizmo);
}

void PyramidGizmoDestroy(PyramidGizmo *gizmo)
{
	free(gizmo);
}

void RadialGizmoDestroy(RadialGizmo *gizmo)
{
	free(gizmo);
}

/*****************************************/
/*          UTILITY FUNCTIONS            */
/*****************************************/
/******
* This function snaps a transformation matrix to align with a specified normal vector.
* PARAMS:
*   snapNormalAxis - int 0=x, 1=y, 2=z
*   snapNormalInverse - bool indicating whether to snap to the inverse of snapNormalAxis
*   mat - the Mat4 to align
*   norm - the Vec3 normal to which the y-axis of mat will be aligned
******/
void GizmoSnapToNormal(int snapNormalAxis, bool snapNormalInverse, Mat4 mat, Vec3 norm)
{
	Vec3 startVec, rotAxis;
	Quat rotQuat;
	Mat3 rotMat;
	Mat3 newMat;
	F32 dot;

	// determine vector being snapped to the normal
	assert(snapNormalAxis >= 0 && snapNormalAxis < 3);
	if (snapNormalInverse)
		scaleVec3(mat[snapNormalAxis], -1, startVec);
	else
		copyVec3(mat[snapNormalAxis], startVec);

	// create a rotation axis by crossing the old and new y-axes
	normalVec3(startVec);
	dot = dotVec3(norm, startVec);
	crossVec3(norm, startVec, rotAxis);

	// rotate around the rotation axes to align the two axes
	if (dot >= -1 && dot <= 1)
	{
		if (axisAngleToQuat(rotAxis, acos(dot), rotQuat))
		{
			quatToMat(rotQuat, rotMat);
			mulMat3(rotMat, mat, newMat);
			copyMat3(newMat, mat);
		}
		else if (axisAngleToQuat(mat[(snapNormalAxis + 1) % 3], acos(dot), rotQuat))
		{
			quatToMat(rotQuat, rotMat);
			mulMat3(rotMat, mat, newMat);
			copyMat3(newMat, mat);
		}
	}
}

/******
* This function draws text at the specified location in the world.
* PARAMS:
*   text - string to draw
*   v - Vec3 location in the world at which to draw text
*   flags - int flags to apply to the drawn text; same flags passed to gfxfont_Printf
******/
static void drawTextInWorld(char *text, Vec3 v, int flags)
{
	Vec2 screenPos;
	editLibGetScreenPos(v, screenPos);
	gfxfont_Printf(screenPos[0], screenPos[1], 100, 0.8, 0.8, flags, "%s", text);
}

static int findCoplanarLineIntersection(Vec3 x1, Vec3 x2, Vec3 x3, Vec3 x4, Vec3 intersection)
{
	Vec3 a,b,c;
	Vec3 cxb,axb,buf;
	float mag1,mag2;
	subVec3(x2,x1,a);
	subVec3(x4,x3,b);
	subVec3(x3,x1,c);
	crossVec3(c,b,cxb);
	crossVec3(a,b,axb);
	mag1=dotVec3(cxb,axb);
	mag2=lengthVec3(axb);
	mag2=mag2*mag2;
	scaleVec3(a,mag1/mag2,buf);
	addVec3(buf,x1,intersection);
	return 1;
}

// intersection is set the the point on one line closest to the other line
// TODO: needs to return 0 when lines are parallel
// TODO: figure out which line is which as far as that goes, and fix these comments
int findSkewLineIntersection(Vec3 C, Vec2 D, Vec3 A, Vec3 B, Vec3 intersection)
{
	Vec3 u1,u2,u3;
	Vec3 Cprime,Dprime;
	Vec3 E;
	float magnitude;

	subVec3(B,A,u1);
	normalVec3(u1);

	subVec3(D,C,u2);
	normalVec3(u2);

	crossVec3(u1,u2,u3);
	normalVec3(u3);

	subVec3(A,C,E);
	magnitude=dotVec3(E,u3);
	scaleVec3(u3,magnitude,E);

	addVec3(C,E,Cprime);
	addVec3(D,E,Dprime);

	findCoplanarLineIntersection(A,B,Cprime,Dprime,intersection);
	return 1; // needs to return 0 for lines that lie in parallel planes
}

/******
* Calculates the closest distance between two skew lines
* PARAMS:
*   A - Vec3 first line's start point
*   B - Vec3 first line's end point
*   C - Vec3 second line's start point
*   D - Vec3 second line's end point
* RETURNS:
*   float closest distance between the two lines (A, B) and (C, D)
******/
static float findSkewLineDistance(Vec3 A, Vec3 B, Vec3 C, Vec3 D)
{
	Vec3 p1,p2;
	findSkewLineIntersection(A,B,C,D,p1);
	findSkewLineIntersection(C,D,A,B,p2);
	return distance3(p1,p2);
}

/******
* Calculates the distance between a point and a plane.
* PARAMS:
*   p - Vec3 point
*   planePoint - Vec3 point on the plane
*   planeNormal - Vec3 normal to the plane
* RETURNS:
*   float distance between p and the plane
******/
static float findPointPlaneDistance(Vec3 p,Vec3 planePoint,Vec3 planeNormal)
{
	Vec3 buf;
	subVec3(p,planePoint,buf);
	return dotVec3(buf,planeNormal);
}

/******
* This function calculates the intersection of a line with a plane.
* PARAMS:
*   pointOnPlane - Vec3 point on the plane
*   planeNormal - Vec3 normal to the plane
*   p1 - Vec3 first point of the line
*   p2 - Vec3 second point of the line
*   intersection - Vec3 of intersection between line (p1, p2) and plane
* RETURNS:
*   bool true if an intersection was found, false otherwise
******/
static int findPlaneLineIntersection(Vec3 pointOnPlane, Vec3 planeNormal,Vec3 p1,Vec3 p2,Vec3 intersection)
{
	Vec3 p3mp1,p2mp1;
	float numerator,denominator;
	subVec3(pointOnPlane,p1,p3mp1); //p3 minus p1
	subVec3(p2,p1,p2mp1);     //p2 minus p1
	numerator = dotVec3(planeNormal,p3mp1);
	denominator = dotVec3(planeNormal,p2mp1);
	if (ABS(denominator)<.0001)
		return 0;
	subVec3(p2,p1,intersection);
	scaleVec3(intersection,numerator/denominator,intersection);
	addVec3(intersection,p1,intersection);
	return 1;
}

/******
* This function returns the color associated with a particular axis; axes 0-2 correspond to
* x, y, and z respectively, which are colored blue, green, and red respectively.
* PARAMS:
*   i - the number of the axis whose color is to be retrieved
* RETURNS:
*   int argb value corresponding to the axis
******/
static int axisColor(int i)
{
	i = (i % 3 + 3) % 3;
	return (0xff000000 | (0x00ff0000 >> (i * 8)));
}

/******
* This function finds the closest distance between a point and a line.
* PARAMS:
*   p1 - Vec3 first point of the line
*   p2 - Vec3 end point of the line
*   q - Vec3 point
*   intersection - Vec3 output of the closest point on (p1, p2) to q
* RETURNS:
*   double distance between (p1, p2) and q
******/
static double findPointLineIntersection(Vec3 p1, Vec3 p2, Vec3 q, Vec3 intersection)
{
	Vec3 ray;   // direction from p1 to p2
	Vec3 skew;    // ray from p1 to q
	Vec3 up;    // skew crossed with ray
	Vec3 side;    // ray crossed with up, gives us a direct line from the ray to the point
	double dist;  // distance between ray and q

	subVec3(p2,p1,ray);
	subVec3(q,p1,skew);
	crossVec3(ray,skew,up);
	crossVec3(ray,up,side);
	normalVec3(side);
	dist=dotVec3(side,skew);
	scaleVec3(side,dist,side);
	if (intersection!=NULL)
	{
		subVec3(q,side,intersection);
	}
	return ABS(dist);
}

/******
* This function finds the intersection of a line and a sphere.  The returned intersection will
* be that which is closer to the first point of the specified line.
* PARAMS:
*   p1 - Vec3 first point of line
*   p2 - Vec3 second point of line
*   center - Vec3 center of the sphere
*   radius - double radius of the sphere
*   intersection - Vec3 intersection of (p1, p2) and sphere will be stored here
* RETURNS:
*   bool true if intersection was found, false otherwise
******/
int findSphereLineIntersection(Vec3 p1, Vec3 p2, Vec3 center, double radius, Vec3 intersection)
{
	Vec3 plisect;
	double dist=findPointLineIntersection(p1,p2,center,plisect);
	double offset;
	Vec3 ray;
	Vec3 isect1,isect2;

	if (dist>radius)
		return false;
	offset = sqrt(radius*radius-dist*dist);
	subVec3(p2,p1,ray);
	normalVec3(ray);
	scaleVec3(ray,offset,ray);
	addVec3(plisect,ray,isect1);
	subVec3(plisect,ray,isect2);
	if (distance3Squared(p1,isect1)<distance3Squared(p1,isect2))
		copyVec3(isect1,intersection);
	else
		copyVec3(isect2,intersection);
	return true;
}

/******
* This function draws an arrow of a specified size and color in a specified direction. This is used
* primarily for the translation and rotation gizmos.
* PARAMS:
*   tail - the tail end vector of the arrow
*   direction - the direction the arrow should point (NOT the endpoint of the arrow)
*   up - a perpendicular vector to the arrow
*   size - the length of the arrow
*   headColor - color to make the arrow head
*   tailColor - color to make the arrow line
******/
void drawLineArrow(const Vec3 tail, const Vec3 direction, const Vec3 up, float size, int headColor, int lineColor)
{
	Vec3 head, arrowEnd, side, normUp, normDir;
	Mat3 basis, rotation, result;
	int i;
	int segments = 10;
	Vec3 rot1 = {1, 0, 0};

	// get some important points along the arrow's axis and draw the axis itself
	copyVec3(direction, normDir);
	normalVec3(normDir);
	copyVec3(normDir, head);
	scaleVec3(head, size * 0.9, arrowEnd);
	scaleVec3(head, size, head);
	addVec3(head, tail, head);
	addVec3(arrowEnd, tail, arrowEnd);
	gfxDrawLine3DWidthARGB(tail, head, lineColor, lineColor, 2);

	// form an orthonormal basis from the up and direction vectors
	copyVec3(up, normUp);
	normalVec3(normUp);
	crossVec3(normDir, normUp, side);
	copyVec3(normDir, basis[0]);
	copyVec3(normUp, basis[1]);
	copyVec3(side, basis[2]);

	// render the conic tip
	copyVec3(rot1, rotation[0]);
	for (i = 0; i < segments; i++)
	{
		// generate a rotation matrix, rotate the basis, and take the rotated "up" vector to generate the
		// points along the base of the cone
		Vec3 rot2 = {0, (cos((double)i/(double)segments*TWOPI)), -(sin((double)i/(double)segments*TWOPI))};
		Vec3 rot3 = {0, (sin((double)i/(double)segments*TWOPI)), (cos((double)i/(double)segments*TWOPI))};
		copyVec3(rot2, rotation[1]);
		copyVec3(rot3, rotation[2]);
		mulMat3(basis, rotation, result);
		scaleVec3(result[1], size * 0.03, result[1]);
		addVec3(result[1], arrowEnd, result[1]);

		// draw the cone segments
		gfxDrawLine3DWidthARGB(arrowEnd, result[1], headColor, headColor, 2);
		gfxDrawLine3DWidthARGB(result[1], head, headColor, headColor, 2);
	}
}

/******
* This draws an optionally tickmarked circle.
* PARAMS:
*   center - Vec3 center of the circle
*   normal - Vec3 normal to the surface of the circle
*   zero - Vec3 in a direction along the place defined by the circle; points to "zero",
*          where the ticking should begin
*   size - float scale to apply to the circle
*   argb - int color
*   alphafar - int alpha at the far end of the circle
*   tickmarks - int angular interval at which to put ticks; 0 for no tickmarks
******/
void drawFullCircleShaded(Vec3 center, Vec3 normal, Vec3 zero, float size, int argb, int alphafar, int tickmarks)
{
	int segments = 360;
	int i;
	Vec3 side,front;
	Mat4 orientation;
	Mat4 cam;
	double distToCenter;
	double distToFarthest = 0;

	gfxGetActiveCameraMatrix(cam);

	// generate orthonormal axes to determine the circle's normal and perpendicular axes for its sides
	copyVec3(zero,side);
	crossVec3(normal,side,front);
	crossVec3(normal,front,side);
	normalVec3(normal);
	normalVec3(side);
	normalVec3(front);
	copyVec3(front, orientation[0]);
	copyVec3(normal, orientation[1]);
	copyVec3(side, orientation[2]);
	copyVec3(center, orientation[3]);

	// scale the size of the circles according to the distance from the camera
	distToCenter = distance3(center, cam[3]);
	size = size * distToCenter;

	for (i = 0; i < segments; i++)
	{
		Vec3 start, end;
		Vec3 startFinal, endFinal;
		double argbVal;
		int argbFinal;
		double dist,distMin;

		start[0]=size*(cos((double)i/(double)segments*TWOPI));
		start[1]=0;
		start[2]=size*(sin((double)i/(double)segments*TWOPI));
		end[0]=size*(cos((double)(i+1)/(double)segments*TWOPI));
		end[1]=0;
		end[2]=size*(sin((double)(i+1)/(double)segments*TWOPI));
		mulVecMat4(start,orientation,startFinal);
		mulVecMat4(end,orientation,endFinal);
		dist=distance3(startFinal,cam[3]);
		distMin=distance3(center,cam[3])-size;
		argbVal=(dist-distMin)/(size*2);
		argbVal=1-argbVal;
		argbVal=argbVal*255+(1-argbVal)*alphafar;
		if (argbVal<0)
		{
			argbVal=0;
		}
		argbFinal=((int)argbVal)<<24;
		argbFinal|=argb & 0x00ffffff;

		gfxDrawLine3DWidthARGB(startFinal,endFinal,argbFinal,argbFinal,2);
		if (tickmarks)
		{
			Vec3 extension;
			int tickDegree=1;
			double tickSize;
			int color;
			if (i%5==0)
				tickDegree+=1;
			if (i%15==0)
				tickDegree+=1;
			if (i%45==0)
				tickDegree+=1;
			if (i%90==0)
				tickDegree+=1;
			if (i%tickmarks==0)
				color=argbFinal;
			else
				color=0x3f7f7f7f;
			tickSize=(double)tickDegree/15.0;
			tickSize*=size;
			subVec3(center,startFinal,extension);
			normalVec3(extension);
			scaleVec3(extension,tickSize,extension);
			addVec3(extension,startFinal,extension);
			gfxDrawLine3DWidthARGB(startFinal,extension,color,color,1.5);
		}
	}
}

/******
* This function snaps an angle to the nearest snappable angle given the snap resolution.
* PARAMS:
*   angle - float angle to snap
*   snapRes - float snap resolution
* RETURNS:
*   float snapped angle
******/
static float GizmoSnapAngle(float angle, float snapRes)
{
	float snap = GizmoGetSnapAngle(snapRes);
	float ret = angle;
	ret += snap / 2.0 * PI / 180.0;
	ret /= snap * PI /180.0;
	if (ret < 0)
	{
		ret -= 1;
	}
	ret = ((int)ret) * snap * PI/180.0;
	return ret;
}


/******
* This function draws a small box at the specified point.
* PARAMS:
*   p - Vec3 where the indicator will be drawn
******/
static void drawPointIndicator(Vec3 p)
{
	Mat4 cam;
	Vec3 threeDirs[3];
	int i, j, k;
	float size;
	gfxGetActiveCameraMatrix(cam);
	size = 0.005 * distance3(cam[3], p);

	// create cardinal directions
	for (i = 0; i < 3; i++)
	{
		zeroVec3(threeDirs[i]);
		threeDirs[i][i] = size / 2;
	}

	// calculate corners and draw lines between the appropriate ones
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 2; j++)
		{
			Vec3 tempj;
			zeroVec3(tempj);
			if (j)
			{
				addVec3(tempj, threeDirs[i], tempj);
			}
			else
			{
				subVec3(tempj, threeDirs[i], tempj);
			}
			for (k = 0; k < 2; k++)
			{
				Vec3 tempk;
				Vec3 end1, end2;
				copyVec3(threeDirs[(i + 1) % 3], tempk);
				if (k)
				{
					addVec3(tempj, tempk, tempk);
				}
				else
				{
					subVec3(tempj, tempk, tempk);
				}
				addVec3(tempk, threeDirs[(i + 2) % 3], end1);
				addVec3(end1, p, end1);
				subVec3(tempk, threeDirs[(i + 2) % 3], end2);
				addVec3(end2, p, end2);
				gfxDrawLine3D(end1, end2, colorFromRGBA(0xFFFF00FF));
			}
		}
	}
}

/******
* This function draws a triangle and fills it with lines parallel to the line between
* the second and third points.
* PARAMS:
*   verts - Mat3 triangle vertices
*   c - Color to use
*   numSegs - number of parallel lines to draw
******/
static void drawTriSlice(Mat3 verts, Color c, int numSegs)
{
	Vec3 base1, base2, end1, end2;
	float len1, len2;
	int i;

	assert(numSegs > 0);

	// draw bounding edges
	gfxDrawLine3D(verts[0], verts[1], c);
	gfxDrawLine3D(verts[1], verts[2], c);
	gfxDrawLine3D(verts[2], verts[0], c);

	// fill with parallel lines
	subVec3(verts[1], verts[0], base1);
	len1 = distance3(verts[1], verts[0]);
	normalVec3(base1);
	subVec3(verts[2], verts[0], base2);
	len2 = distance3(verts[2], verts[0]);
	normalVec3(base2);
	for (i = 1; i < numSegs; i++)
	{
		scaleVec3(base1, i * len1 / numSegs, end1);
		addVec3(end1, verts[0], end1);
		scaleVec3(base2, i * len2 / numSegs, end2);
		addVec3(end2, verts[0], end2);
		gfxDrawLine3D(end1, end2, c);
	}
}

/******
* This function takes a point and some triangle vertices and indicates whether the point is inside the triangle.
* PARAMS:
*   p - Vec3 point to test
*   tri - Mat3 triangle vertices
* RETURNS:
*   bool indicating whether p lies in tri
******/
static bool pointInTri(Vec3 p, Mat3 tri)
{
	int i;
	F32 total = 0;
	for (i = 0; i < 3; i++)
	{
		Vec3 cv1, cv2;
		F32 dp;
		subVec3(tri[i], p, cv1);
		subVec3(tri[(i + 1) % 3], p, cv2);
		dp = dotVec3(cv1, cv2);
		dp /= (distance3(zerovec3, cv1) * distance3(zerovec3, cv2));
		total += acos(dp);
	}
	if (TWOPI - total > 0.00001)
		return false;
	else
		return true;
}

/******
* This function takes an angle and a point and indicates whether the point lies "within" the angle.
* The angle is defined as follows:
*     p1/
*      /
*    v/____p2
* PARAMS:
*   v - Vec3 v in the angle as depicted above
*   p1 - Vec3 p1 in the angle as depicted above
*   p2 - Vec3 p2 in the angle as depicted above
*   p - Vec3 point to test
* RETURNS:
*   bool indicating whether p lies in the angle formed by p1-v-p2
******/
static bool pointInAngle(Vec3 v, Vec3 p1, Vec3 p2, Vec3 p)
{
	Vec3 p1c, p2c, pc;
	float aPP1, aPP2, aP1P2;
	subVec3(p1, v, p1c);
	subVec3(p2, v, p2c);
	subVec3(p, v, pc);
	aPP1 = angle(pc, p1c);
	aPP2 = angle(pc, p2c);
	aP1P2 = angle(p1c, p2c);
	if (fabs(aP1P2 - aPP1 - aPP2) > 0.001)
		return false;
	else
		return true;
}

/******
* This function sets an output vector to snap with an input vertex.
* PARAMS:
*   vert - the Vec3 to snap to
*   vOut - the snapped Vec3
******/
static void snapToVertex(Vec3 vert, Vec3 vOut)
{
	copyVec3(vert, vOut);
}

/******
* This function sets an output vector to snap with an input edge.
* PARAMS:
*   start - start Vec3 of the cast ray
*   end - end Vec3 of the cast ray
*   edgev1 - one endpoint Vec3 of the edge
*   edgev2 - the other endpoint Vec3 of the edge
*   vOut - the snapped Vec3
*   snapDist - snap distance used for clamping to vertices/midpoint on the edge
*   cam - the Vec3 camera location
*   restrict - bool indicating whether to clamp the snapped vector to the endpoints of the edge or not
******/
static void snapToEdge(Vec3 start, Vec3 end, Vec3 edgev1, Vec3 edgev2, Vec3 vOut, float snapDist, Vec3 cam, bool restrict)
{
	Vec3 intersection, mid;
	double dist1, dist2, dist3;
	findSkewLineIntersection(start, end, edgev1, edgev2, intersection);

	// ensure intersection is on the SEGMENT defined by the edge vertices
	addVec3(edgev1, edgev2, mid);
	scaleVec3(mid, 0.5, mid);
	dist1 = distance3(intersection, edgev1);
	dist2 = distance3(intersection, edgev2);
	dist3 = distance3(intersection, mid);

	// if restriction is enabled, then snap to end vertices or midpoints if they are close enough
	if (restrict && dist1 < snapDist * distance3(cam, edgev1) && dist1 < dist2 && dist1 < dist3)
		snapToVertex(edgev1, vOut);
	else if (restrict && dist2 < snapDist * distance3(cam, edgev2) && dist2 < dist1 && dist2 < dist3)
		snapToVertex(edgev2, vOut);
	else if (restrict && dist3 < snapDist * distance3(cam, mid) && dist3 < dist1 && dist3 < dist2)
	{
		snapToVertex(mid, vOut);
		drawPointIndicator(mid);
	}

	// otherwise, snap to the edge segment as long as the snapped location does not go past the endpoints,
	// or if restriction is disabled
	else if (!restrict || fabs(distance3(edgev1, edgev2) - dist1 - dist2) < 0.001)
		copyVec3(intersection, vOut);
	// when the user is mousing to the edge of the segment, snap to the closest edge vertex
	else
	{
		if (dist1 < dist2)
			snapToVertex(edgev1, vOut);
		else
			snapToVertex(edgev2, vOut);
	}
}

/******
* This function sets an output vector to snap with an input face.
* PARAMS:
*   start - start Vec3 of the cast ray
*   end - end Vec3 of the cast ray
*   verts - Mat3 of the three vertices of the triangular face
*   vOut - the snapped Vec3
*   snapDist - snap distance used for clamping to edges/vertices/midpoints on the triangle
*   cam - the Vec3 camera location
*   restrict - bool indicating whether to clamp the snapped vector to the subcomponents of the triangle
******/
static void snapToFace(Vec3 start, Vec3 end, Mat3 verts, Vec3 vOut, float snapDist, Vec3 cam, bool restrict)
{
	Vec3 temp1, temp2, planeNorm, intersection;
	Mat3 closePoints;
	double closeDists[3];
	int i;
	int closestEdge = -1;
	double closestDist = -1;

	// check distance for edge snapping
	for (i = 0; i < 3; i++)
	{
		closeDists[i] = findSkewLineDistance(start, end, verts[i], verts[(i + 1) % 3]);
		findSkewLineIntersection(start, end, verts[i], verts[(i + 1) % 3], closePoints[i]);
		if (closeDists[i] < snapDist * distance3(cam, closePoints[i]) && (closestDist == -1 || closeDists[i] < closestDist))
		{
			closestDist = closeDists[i];
			closestEdge = i;
		}
	}

	// snap to edge if close enough to an edge
	if (closestEdge != -1 && restrict)
		snapToEdge(start, end, verts[closestEdge], verts[(closestEdge + 1) % 3], vOut, snapDist, cam, restrict);
	else
	{
		// find the intersection on the triangle's plane
		subVec3(verts[1], verts[0], temp1);
		subVec3(verts[2], verts[0], temp2);
		crossVec3(temp1, temp2, planeNorm);
		normalVec3(planeNorm);
		findPlaneLineIntersection(verts[0], planeNorm, start, end, intersection);

		// ensure the intersection lies in the triangle
		if (!restrict || pointInTri(intersection, verts))
			copyVec3(intersection, vOut);
		// for points outside the triangle, we check for intersection location in one of six hotspots
		else
		{
			for (i = 0; i < 3; i++)			
			{
				if (pointInAngle(verts[i], verts[(i + 1) % 3], verts[(i + 2) % 3], intersection))
				{
					findSkewLineIntersection(verts[i], intersection, verts[(i + 1) % 3], verts[(i + 2) % 3], vOut);
					break;
				}
				else
				{
					Vec3 iP1, iP2;
					subVec3(verts[i], verts[(i + 1) % 3], iP1);
					addVec3(iP1, verts[i], iP1);
					subVec3(verts[i], verts[(i + 2) % 3], iP2);
					addVec3(iP2, verts[i], iP2);
					if (pointInAngle(verts[i], iP1, iP2, intersection))
					{
						copyVec3(verts[i], vOut);
						break;
					}
				}
			}
		}
	}
}

/*****************************************/
/*             ROTATE GIZMO              */
/*****************************************/
typedef struct RotateGizmo
{
	Mat4 axes;
	float rotationSnapResolution;		// 0=nosnap, 1-5 = snap to {1,5,15,45,90} degrees
	float rotateAngle;					// how much we've rotated the object so far
	float rotateAngleStart;			    // what angle we started rotating from
	int rotationAxis;
	Vec3 freeRotateStart;				// the starting point at which the user clicked to start a free rotation;
	// found on a sphere surrounding the center of the rotation
	Mat4 startMat;						// original matrix before rotation
	Vec3 pyr;
	U32 activated : 1;					// Gizmo has been activated by user input
	U32 rotationSnapEnabled : 1;		// this indicates whether rotation snap is on or off
	U32 relativeDragMode : 1;			// indicates whether to move directly to the angular location of the
	// mouse or to drag the angle in a relative motion
	U32 alignedToWorld : 1;				// translate in world space, not object space
	bool enabledAxis[ 3 ];

	void *fn_context;
	GizmoActivateFn activate_fn;		// function called when the mouse button is pressed
	GizmoDeactivateFn deactivate_fn;	// function called when the mouse button is released

	Mat4 activeMatrix;
	Mat4 visibleMatrix;
	float size;
	double drawnSize;
	Vec3 line1;
	Vec3 line2;
	Vec3 text_mid;
	double currentAngle;
	U32 color1;
	U32 color2;

	EditLibGizmosToolbar *toolbar;			// a toolbar to update when this gizmo's various parameters change
} RotateGizmo;

RotateGizmo *RotateGizmoCreate(void)
{
	RotateGizmo* newGizmo = calloc(1, sizeof(RotateGizmo));
	newGizmo->rotationSnapEnabled = 1;
	newGizmo->rotationSnapResolution = 1;
	newGizmo->relativeDragMode = 1;
	newGizmo->size = 0.15f;
	newGizmo->enabledAxis[ 0 ] = newGizmo->enabledAxis[ 1 ] = newGizmo->enabledAxis[ 2 ] = true;
	identityMat4(newGizmo->activeMatrix);
	return newGizmo;
}

void RotateGizmoDestroy(RotateGizmo* rotGizmo)
{
	free(rotGizmo);
}

/******
* This function snaps a particular angle to nearest increment of the snap resolution.
* PARAMS:
*   rotGizmo - RotateGizmo being used
*   angle - angle to snap
* RETURNS:
*   float final, snapped angle
******/
static float RotateGizmoSnapAngle(RotateGizmo* rotGizmo, float angle)
{
	float ret;
	if (rotGizmo->rotationSnapEnabled)
		ret = GizmoSnapAngle(angle, rotGizmo->rotationSnapResolution);
	else
		ret = angle;
	return ret;
}

static float getCursorRotationAngle(Vec3 center, Mat4 axes, int rotAxis, Vec3 start, Vec3 end, Vec3 intersection)
{
	Vec3 inter;
	float angle;
	float dp;

	findPlaneLineIntersection(center, axes[rotAxis], start, end, inter);
	if (intersection)
		copyVec3(inter, intersection);
	subVec3(inter, center, inter);
	normalVec3(inter);
	dp = dotVec3(inter, axes[(rotAxis + 1) % 3]);
	dp = CLAMP(dp, -1.0f, 1.0f);
	angle = acos(dp);
	if (dotVec3(inter, axes[(rotAxis + 2) % 3]) < 0)
		angle = TWOPI - angle;
	if (rotAxis == 0 || rotAxis == 2)
		angle = -angle;
	return angle;
}

/*
 * This function handles the user clicking on the rotation gizmo; if they clicked on
 * one of the exes, it properly sets up some variables in editState so that editRotateMove
 * can handle the user rotating the object; otherwise, variables are set to allow free
 * rotation.
 */
void RotateGizmoHandleInput(RotateGizmo* rotGizmo)
{
	copyMat4(rotGizmo->visibleMatrix, rotGizmo->startMat);

	rotGizmo->rotateAngle = 0;

	getMat3YPR(rotGizmo->activeMatrix, rotGizmo->pyr);

	// free rotation
	if (rotGizmo->rotationAxis == -1)
	{
		Mat4 cam;
		Vec3 start, end;
		float fardist;
		gfxGetActiveCameraMatrix(cam);
		fardist = distance3(cam[3], rotGizmo->visibleMatrix[3]);

		// store the starting point of rotation, found on the sphere surrounding the pivot
		editLibCursorRay(start, end);  // cast a ray into the world from the mouse cursor
		if (!findSphereLineIntersection(start, end, rotGizmo->visibleMatrix[3], 0.30 * fardist, rotGizmo->freeRotateStart))
			return;
		subVec3(rotGizmo->freeRotateStart, rotGizmo->visibleMatrix[3], rotGizmo->freeRotateStart);
		normalVec3(rotGizmo->freeRotateStart);
	}

	// fixed-axis rotation
	else
	{
		Vec3 start, end;

		editLibCursorRay(start, end);
		rotGizmo->rotateAngleStart = getCursorRotationAngle(rotGizmo->visibleMatrix[3], rotGizmo->axes, rotGizmo->rotationAxis, start, end, NULL);
	}
}

void RotateGizmoApplyChanges(RotateGizmo* rotGizmo)
{
	Mat4 cam;
	Vec3 start,end;

	gfxGetActiveCameraMatrix(cam);
	editLibCursorRay(start, end);

	// free rotation
	if (rotGizmo->rotationAxis == -1)
	{
		Vec3 newUnitPoint, rotationAxis, camVec;
		Mat3 rotationMatrix;
		int intersect;
		float fardist = distance3(cam[3], rotGizmo->startMat[3]);
		subVec3(cam[3], rotGizmo->visibleMatrix[3], camVec);
		normalVec3(camVec);

		// determine where the user is currently pointing
		intersect = findSphereLineIntersection(start, end, rotGizmo->startMat[3], 2 * rotGizmo->size * fardist, newUnitPoint);
		subVec3(newUnitPoint, rotGizmo->startMat[3], newUnitPoint);
		normalVec3(newUnitPoint);

		// check to ensure that the user has indeed performed a rotation
		if (distance3(rotGizmo->freeRotateStart, newUnitPoint) > 0.01 && intersect)
		{
			// calculate the axis about which to rotate
			Quat rotQuat;
			Mat4 tempMat;
			crossVec3(newUnitPoint, rotGizmo->freeRotateStart, rotationAxis);

			// rotate the object around the rotation axis using quaternions
			axisAngleToQuat(rotationAxis, 2.0 * acos(dotVec3(newUnitPoint, rotGizmo->freeRotateStart)), rotQuat);

			// convert back from a quaternion to a matrix
			quatToMat(rotQuat, rotationMatrix);

			// apply the rotation to the local matrix
			mulMat3(rotationMatrix, rotGizmo->startMat, rotGizmo->visibleMatrix);

			// apply the changes to the original matrix as well
			mulMat3(rotationMatrix, rotGizmo->activeMatrix, tempMat);
			copyMat3(tempMat, rotGizmo->activeMatrix);

			copyMat3(rotGizmo->visibleMatrix, rotGizmo->startMat);
			copyVec3(newUnitPoint, rotGizmo->freeRotateStart);
		}
	}
	else
	{
		Vec3 rpy, mid;
		Mat3 axesInverted, buf, buf2;
		Mat3 tempGhost;
		Vec3 intersection;
		float angle = getCursorRotationAngle(rotGizmo->visibleMatrix[3], rotGizmo->axes, rotGizmo->rotationAxis, start, end, intersection);
		double distToCenter;

		distToCenter = distance3(rotGizmo->visibleMatrix[3], cam[3]);   // distance from the camera to the object
		rotGizmo->drawnSize = rotGizmo->size*distToCenter;          // how big the gizmo is being drawn

		if (!gfxIsActiveCameraControllerRotating())
		{
			if (rotGizmo->relativeDragMode)
				rotGizmo->rotateAngle = RotateGizmoSnapAngle(rotGizmo, angle) - RotateGizmoSnapAngle(rotGizmo, rotGizmo->rotateAngleStart);
			else
			{
				if (rotGizmo->alignedToWorld)
					rotGizmo->rotateAngle = angle - rotGizmo->pyr[rotGizmo->rotationAxis];
				else
					rotGizmo->rotateAngle = angle;
			}
		}


		if (!rotGizmo->alignedToWorld || rotGizmo->relativeDragMode)
			rotGizmo->currentAngle = RotateGizmoSnapAngle(rotGizmo, rotGizmo->rotateAngle);
		else
			rotGizmo->currentAngle = RotateGizmoSnapAngle(rotGizmo, rotGizmo->pyr[rotGizmo->rotationAxis] + rotGizmo->rotateAngle) - rotGizmo->pyr[rotGizmo->rotationAxis];
		while (rotGizmo->currentAngle <= -PI || rotGizmo->currentAngle > PI)
		{
			if (rotGizmo->currentAngle <= -PI)
				rotGizmo->currentAngle += TWOPI;
			else if(rotGizmo->currentAngle > PI)
				rotGizmo->currentAngle -= TWOPI;
		}

		// handle actually rotating the object
		// rotate only around the one axis, not around the others at all
		rpy[(rotGizmo->rotationAxis + 0) % 3] = rotGizmo->currentAngle;
		rpy[(rotGizmo->rotationAxis + 1) % 3] = 0;
		rpy[(rotGizmo->rotationAxis + 2) % 3] = 0;

		// first multiply the rotGizmo->visibleMatrix by the inverse of the rotation axes, then do the
		// rotation on it, and then multiply by the rotation axes, this applies the
		// rotation on rotGizmo->visibleMatrix as though it were rotated around the rotation axes
		copyMat3(rotGizmo->visibleMatrix, tempGhost);
		invertMat3Copy(rotGizmo->axes, axesInverted);
		copyMat3(rotGizmo->axes, buf2);
		rotateMat3(rpy, buf2);
		mulMat3(buf2, axesInverted, buf);
		mulMat3(buf, rotGizmo->startMat, rotGizmo->visibleMatrix);

		// apply the changes to the local matrix as well
		{
			Mat3 inv, trans, newLocal;
			invertMat3Copy(tempGhost, inv);
			mulMat3(rotGizmo->visibleMatrix, inv, trans);
			mulMat3(trans, rotGizmo->activeMatrix, newLocal);
			copyMat3(newLocal, rotGizmo->activeMatrix);
		}

		// draw starting and ending lines in relative drag mode
		if (rotGizmo->relativeDragMode)
		{
			Vec3 axis1, axis2;
			float theta;
			theta = RotateGizmoSnapAngle(rotGizmo, angle);
			if (rotGizmo->rotationAxis == 0 || rotGizmo->rotationAxis == 2)
				theta = -theta;
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize * cos(theta), axis1);
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 2) % 3], rotGizmo->drawnSize * sin(theta), axis2);
			addVec3(axis1, axis2, axis2);
			copyVec3(axis2, mid);
			addVec3(axis2, rotGizmo->visibleMatrix[3], axis2);
			copyVec3(axis2, rotGizmo->line1);
			assert(FINITEVEC3(rotGizmo->line1));

			theta = RotateGizmoSnapAngle(rotGizmo, rotGizmo->rotateAngleStart);
			if (rotGizmo->rotationAxis == 0 || rotGizmo->rotationAxis == 2)
				theta = -theta;
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize * cos(theta), axis1);
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 2) % 3], rotGizmo->drawnSize * sin(theta), axis2);
			addVec3(axis1, axis2, axis2);
			addVec3(mid, axis2, mid);
			addVec3(axis2, rotGizmo->visibleMatrix[3], axis2);
			copyVec3(axis2, rotGizmo->line2);
			copyVec3(mid, rotGizmo->text_mid);
			assert(FINITEVEC3(rotGizmo->line2));
			rotGizmo->color1 = 0xFFFFFF00;
			rotGizmo->color2 = 0xFF555500;
		}
		// draw the line to correspond with the PYR, both starting and ending lines
		else if (rotGizmo->alignedToWorld)
		{
			Vec3 axis1, axis2;
			float theta = rotGizmo->pyr[rotGizmo->rotationAxis] + rpy[rotGizmo->rotationAxis];
			if (rotGizmo->rotationAxis == 0 || rotGizmo->rotationAxis == 2)
				theta = -theta;
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize * cos(theta), axis1);
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 2) % 3], rotGizmo->drawnSize * sin(theta), axis2);
			addVec3(axis1, axis2, axis2);
			copyVec3(axis2, mid);
			addVec3(axis2, rotGizmo->visibleMatrix[3], axis2);
			copyVec3(axis2, rotGizmo->line1);

			theta = rotGizmo->pyr[rotGizmo->rotationAxis];
			if (rotGizmo->rotationAxis == 0 || rotGizmo->rotationAxis == 2)
				theta = -theta;
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize * cos(theta), axis1);
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 2) % 3], rotGizmo->drawnSize * sin(theta), axis2);
			addVec3(axis1, axis2, axis2);
			addVec3(mid, axis2, mid);
			addVec3(axis2, rotGizmo->visibleMatrix[3], axis2);
			copyVec3(axis2, rotGizmo->line2);
			rotGizmo->color1 = 0xFFFFFFFF;
			rotGizmo->color2 = 0xFF555555;
		}

		// in local rotation, draw the line to correspond with the next axis
		else
		{
			Vec3 lineEnd;
			scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize, lineEnd);
			copyVec3(lineEnd, mid);
			addVec3(lineEnd, rotGizmo->visibleMatrix[3], lineEnd);
			copyVec3(lineEnd, rotGizmo->line1);
			scaleVec3(rotGizmo->visibleMatrix[(rotGizmo->rotationAxis + 1) % 3], rotGizmo->drawnSize, lineEnd);
			addVec3(mid, lineEnd, mid);
			addVec3(lineEnd, rotGizmo->visibleMatrix[3], lineEnd);
			copyVec3(lineEnd, rotGizmo->line2);
			copyVec3(mid, rotGizmo->text_mid);
			rotGizmo->color1 = 0xFFFFFFFF;
			rotGizmo->color2 = 0xFF555555;
		}
	}
}

static bool RotateGizmoDonutLineCollision(Vec3 donutCenter, Vec3 axis1, Vec3 axis2, F32 radius, F32 thickness, Vec3 start, Vec3 end)
{
	int t;
	F32 angleAllowance = cos(RAD(95));
	Vec3 prevPoint={0,0,0};
	Vec3 centerToCam, centerToPointOnCircle;
	F32 thicknessSqr = SQR(thickness);
	Vec3 lineDir;
	F32 lineDist;
	bool prevTooFarAway = false;

	subVec3(end, start, lineDir);
	lineDist = normalVec3(lineDir);

	subVec3(start, donutCenter, centerToCam);
	normalVec3(centerToCam);

	for ( t=0; t <= 360; t+= 18 ) {
		Vec3 nextPoint;
		Vec3 comp1, comp2;
		bool nextTooFarAway = false;

		scaleVec3(axis1, cos(RAD(t)), comp1);
		scaleVec3(axis2, sin(RAD(t)), comp2);
		addVec3(comp1, comp2, centerToPointOnCircle);
		scaleVec3(centerToPointOnCircle, radius, nextPoint);
		addVec3(nextPoint, donutCenter, nextPoint);

		if(dotVec3(centerToCam, centerToPointOnCircle) < angleAllowance)
			nextTooFarAway = true;

		if(t>0) {
			if(!nextTooFarAway || !prevTooFarAway) {
				Vec3 segDir, ret1, ret2;
				F32 segLength;
				subVec3(nextPoint, prevPoint, segDir);
				segLength = normalVec3(segDir);

				if(LineLineDistSquared(prevPoint, segDir, segLength, ret1, start, lineDir, lineDist, ret2) < thicknessSqr)
					return true;
			}
		}

		prevTooFarAway = nextTooFarAway;
		copyVec3(nextPoint, prevPoint);
	}
	return false;
}

void RotateGizmoUpdate(RotateGizmo* rotGizmo)
{
	int i;
	Mat4 cam;
	Vec3 start, end;
	Vec3 sphereIntersection;
	double fardist;
	double gizmoSize;
	bool swallow_input = false;

	editLibCursorRay(start, end);

	if (!rotGizmo->activated)
	{
		rotGizmo->rotationAxis = -1;

		// get the distance between the camera and the pivot
		gfxGetActiveCameraMatrix(cam);
		fardist = distance3(cam[3], rotGizmo->visibleMatrix[3]);
		gizmoSize = rotGizmo->size * fardist;

		// check to see if the user has moused over the rotation gizmo sphere
		if (!gfxIsActiveCameraControllerRotating() && 
			sphereLineCollisionWithHitPoint(start, end, rotGizmo->visibleMatrix[3], gizmoSize*1.07, sphereIntersection))
		{
			for ( i=0; i < 3; i++ )
			{
				Vec3 axis1, axis2;
				if( !rotGizmo->enabledAxis[ i ]) {
					continue;
				}
				
				switch(i)
				{
				case 0:
					copyVec3(rotGizmo->visibleMatrix[1], axis1);
					copyVec3(rotGizmo->visibleMatrix[2], axis2);
					break;
				case 1:
					copyVec3(rotGizmo->visibleMatrix[0], axis1);
					copyVec3(rotGizmo->visibleMatrix[2], axis2);
					break;
				case 2:
					copyVec3(rotGizmo->visibleMatrix[0], axis1);
					copyVec3(rotGizmo->visibleMatrix[1], axis2);
					break;
				}

				if(RotateGizmoDonutLineCollision(rotGizmo->visibleMatrix[3], axis1, axis2, gizmoSize, gizmoSize*0.07, start, end))
				{
					rotGizmo->rotationAxis = i;
					break;					
				}
			}

			swallow_input = true;
		}
	}

	if ((mouseClick(MS_LEFT) || mouseDown(MS_LEFT)) && !rotGizmo->activated)
	{
		if (swallow_input)
		{
			RotateGizmoHandleInput(rotGizmo);
			rotGizmo->activated = true;
			if (rotGizmo->activate_fn)
				rotGizmo->activate_fn(rotGizmo->activeMatrix, rotGizmo->fn_context);
			inpHandled();
		}
	}
	else if (!mouseIsDown(MS_LEFT))
	{
		if (rotGizmo->activated)
		{
			rotGizmo->activated = false;
			if (rotGizmo->deactivate_fn)
				rotGizmo->deactivate_fn(rotGizmo->activeMatrix, rotGizmo->fn_context);
			inpHandled();
		}
	}

	if (rotGizmo->activated)
		RotateGizmoApplyChanges(rotGizmo);
	else
	{
		if (rotGizmo->alignedToWorld)
		{
			identityMat4(rotGizmo->visibleMatrix);
			copyVec3(rotGizmo->activeMatrix[3], rotGizmo->visibleMatrix[3]);
		}
		else
		{
			copyMat4(rotGizmo->activeMatrix, rotGizmo->visibleMatrix);
		}
		copyMat4(rotGizmo->visibleMatrix, rotGizmo->startMat);
		copyMat3(rotGizmo->visibleMatrix, rotGizmo->axes);
	}
}

void RotateGizmoDraw(RotateGizmo* rotGizmo)
{
	int i;
	Mat4 cam;
	Vec3 camdir;
	double fardist;

	gfxSetPrimZTest(0);
	gfxSetPrimIgnoreMax(true);

	gfxGetActiveCameraMatrix(cam);

	// get the vector from camera to the pivot
	subVec3(cam[3], rotGizmo->visibleMatrix[3], camdir);
	fardist = distance3(cam[3], rotGizmo->visibleMatrix[3]);

	if (rotGizmo->activated)
	{
		if (rotGizmo->rotationAxis == -1)
		{
			for (i = 0; i < 3; i++)
				drawLineArrow(rotGizmo->visibleMatrix[3], rotGizmo->visibleMatrix[i], rotGizmo->visibleMatrix[(i + 1) % 3], rotGizmo->size * fardist * 0.25, axisColor(i), axisColor(i));
		}
		else
		{
			// draw the activated gizmo
			char angleStr[10];
			Vec3 mid;

			drawFullCircleShaded(rotGizmo->visibleMatrix[3], rotGizmo->axes[rotGizmo->rotationAxis], rotGizmo->axes[(rotGizmo->rotationAxis+1)%3], rotGizmo->size, axisColor(rotGizmo->rotationAxis), 64, angleSnaps[MAX((int) rotGizmo->rotationSnapResolution - 1, 0)]);
			for (i = 0; i < 4; i++)
			{
				Vec3 axis1, axis2;
				scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 1) % 3], 1.2 * rotGizmo->drawnSize * cos(PI * (float) i / 2.0), axis1);
				scaleVec3(rotGizmo->axes[(rotGizmo->rotationAxis + 2) % 3], 1.2 * rotGizmo->drawnSize * sin(PI * (float) i / 2.0), axis2);
				addVec3(axis1, axis2, axis2);
				addVec3(axis2, rotGizmo->visibleMatrix[3], axis2);
				gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF);
				switch (i)
				{
					xcase 0:
						drawTextInWorld("0", axis2, CENTER_XY);
					xcase 1:
						drawTextInWorld("90", axis2, CENTER_XY);
					xcase 2:
						drawTextInWorld("180", axis2, CENTER_XY);
					xcase 3:
						drawTextInWorld("270", axis2, CENTER_XY);
				}
			}
			gfxDrawLine3DWidthARGB(rotGizmo->visibleMatrix[3], rotGizmo->line1, rotGizmo->color1, rotGizmo->color2, 3);
			gfxDrawLine3DWidthARGB(rotGizmo->visibleMatrix[3], rotGizmo->line2, rotGizmo->color1, rotGizmo->color2, 3);

			// draw angle indicator
			sprintf(angleStr, "%.1f", ((rotGizmo->rotationAxis == 0 || rotGizmo->rotationAxis == 2) ? -1 : 1) * rotGizmo->currentAngle * 180.0 / PI);
			copyVec3(rotGizmo->text_mid, mid);
			normalVec3(mid);
			scaleVec3(mid, 0.2 * rotGizmo->drawnSize, mid);
			addVec3(mid, rotGizmo->visibleMatrix[3], mid);
			drawTextInWorld(angleStr, mid, CENTER_XY);
		}
	}
	else
	{

		// draw the bounding gray circle
		drawFullCircleShaded(rotGizmo->visibleMatrix[3], camdir, rotGizmo->visibleMatrix[0], rotGizmo->size, 0x3fffffff, 0x1f, 0);

		// draw the circles corresponding to each axis and the inner axes
		for (i = 0; i < 3; i++)
		{
			if( !rotGizmo->enabledAxis[ i ]) {
				continue;
			}
			
			if (i==rotGizmo->rotationAxis)
			{
				drawFullCircleShaded(rotGizmo->visibleMatrix[3], rotGizmo->axes[i], rotGizmo->axes[(i+1)%3], rotGizmo->size, 0xffffff00,-100,angleSnaps[MAX((int) rotGizmo->rotationSnapResolution - 1, 0)]);
				drawLineArrow(rotGizmo->visibleMatrix[3], rotGizmo->axes[i], rotGizmo->axes[(i + 1) % 3], rotGizmo->size * fardist * 0.25, 0xffffff00, 0xffffff00);
			}
			else
			{
				drawFullCircleShaded(rotGizmo->visibleMatrix[3], rotGizmo->axes[i], rotGizmo->axes[(i+1)%3], rotGizmo->size, axisColor(i),-100,0);
				drawLineArrow(rotGizmo->visibleMatrix[3], rotGizmo->axes[i], rotGizmo->axes[(i + 1) % 3], rotGizmo->size * fardist * 0.25, axisColor(i), axisColor(i));
			}
		}
	}
	gfxSetPrimIgnoreMax(false);
	gfxSetPrimZTest(1);
}

void RotateGizmoSetCallbackContext(RotateGizmo* rotGizmo, void *context)
{
	rotGizmo->fn_context = context;
}

void RotateGizmoSetActivateCallback(RotateGizmo* rotGizmo, GizmoActivateFn activate_fn)
{
	rotGizmo->activate_fn = activate_fn;
}

void RotateGizmoSetDeactivateCallback(RotateGizmo* rotGizmo, GizmoDeactivateFn deactivate_fn)
{
	rotGizmo->deactivate_fn = deactivate_fn;
}

EditLibGizmosToolbar *RotateGizmoGetToolbar(RotateGizmo *rotGizmo)
{
	return rotGizmo->toolbar;
}

void RotateGizmoSetToolbar(RotateGizmo *rotGizmo, EditLibGizmosToolbar *toolbar)
{
	rotGizmo->toolbar = toolbar;
}

void RotateGizmoSetMatrix(RotateGizmo* rotGizmo, const Mat4 startMat)
{
	copyMat4(startMat, rotGizmo->activeMatrix);
	if (rotGizmo->alignedToWorld)
	{
		identityMat4(rotGizmo->visibleMatrix);
		copyVec3(rotGizmo->activeMatrix[3], rotGizmo->visibleMatrix[3]);
	}
	else
		copyMat4(startMat, rotGizmo->visibleMatrix);
	copyMat4(rotGizmo->visibleMatrix, rotGizmo->startMat);
}

void RotateGizmoGetMatrix(RotateGizmo* rotGizmo, Mat4 mat)
{
	copyMat4(rotGizmo->activeMatrix, mat);
}

void RotateGizmoSetAlignedToWorld(RotateGizmo *rotGizmo, bool aligned)
{
	if (!RotateGizmoIsActive(rotGizmo))
	{
		rotGizmo->alignedToWorld = aligned;
		if (rotGizmo->toolbar)
			elGizmosToolbarUpdate(rotGizmo->toolbar);
	}
}

bool RotateGizmoGetAlignedToWorld(RotateGizmo *rotGizmo)
{
	return rotGizmo->alignedToWorld;
}

void RotateGizmoResetRotation(RotateGizmo* rotGizmo)
{
	copyMat3(unitmat, rotGizmo->activeMatrix);
}

bool RotateGizmoIsActive(RotateGizmo* rotGizmo)
{
	return rotGizmo->activated;
}

void RotateGizmoSetSnapResolution(RotateGizmo *rotGizmo, int res)
{
	rotGizmo->rotationSnapResolution = CLAMP(res, 1, GIZMO_NUM_ANGLES);
	if (rotGizmo->toolbar)
		elGizmosToolbarUpdate(rotGizmo->toolbar);
}

int RotateGizmoGetSnapResolution(RotateGizmo *rotGizmo)
{
	return rotGizmo->rotationSnapResolution;
}

void RotateGizmoEnableSnap(RotateGizmo* rotGizmo, bool enabled)
{
	rotGizmo->rotationSnapEnabled = !!enabled;
	if (rotGizmo->toolbar)
		elGizmosToolbarUpdate(rotGizmo->toolbar);
}

void RotateGizmoEnableAxis(RotateGizmo* rotGizmo, bool pitchEnabled, bool yawEnabled, bool rollEnabled)
{
	rotGizmo->enabledAxis[ 0 ] = pitchEnabled;
	rotGizmo->enabledAxis[ 1 ] = yawEnabled;
	rotGizmo->enabledAxis[ 2 ] = rollEnabled;
}

bool RotateGizmoIsSnapEnabled(RotateGizmo* rotGizmo)
{
	return rotGizmo->rotationSnapEnabled;
}

void RotateGizmoSetRelativeDrag(RotateGizmo* rotGizmo, bool relDrag)
{
	rotGizmo->relativeDragMode = relDrag;
}

void RotateGizmoActivateAxis(RotateGizmo *rotGizmo, int axis)
{
	assert(axis > -1 && axis < 3);
	rotGizmo->activated = 1;
	rotGizmo->rotationAxis = axis;
	if (rotGizmo->alignedToWorld)
	{
		identityMat4(rotGizmo->visibleMatrix);
		copyVec3(rotGizmo->activeMatrix[3], rotGizmo->visibleMatrix[3]);
	}
	else
	{
		copyMat4(rotGizmo->activeMatrix, rotGizmo->visibleMatrix);
	}
	copyMat4(rotGizmo->visibleMatrix, rotGizmo->startMat);
	copyMat3(rotGizmo->visibleMatrix, rotGizmo->axes);
	getMat3YPR(rotGizmo->activeMatrix, rotGizmo->pyr);
	rotGizmo->rotateAngleStart = 0;
}

/******
* AUTO SNAP GIZMO
******/
typedef struct AutoSnapGizmo
{
	EditSpecialSnapMode currSnapAction;
	AutoSnapTriGetter snapTriFunc;		// a callback to retrieve a triangle given a cast ray from camera to mouse;
										// this is used in snap detection
	AutoSnapPointGetter snapTerrainFunc;// a callback to retrieve a point and normal on the terrain given a cast ray
										// from camera to mouse; this is used in snap detection
	AutoSnapNullGetter snapNullFunc;	// a callback used to determine whether a non-snappable object is intersected by
										// the ray cast from camera to mouse pointer
	Vec3 normal;
	Mat3 preSnapVerts;
	Mat3 snapVerts;
} AutoSnapGizmo;

/******
* A global gizmo is maintained as a default for gizmos that integrate with this.  Gizmos should be able to overwrite
* the default by creating a new gizmo separately, if necessary.
******/
static AutoSnapGizmo elAutoSnapGizmo;

/******
* This function creates a new snap settings structure.  This should only ever be called internally by gizmos
* that are overwriting the global settings.
******/
static AutoSnapGizmo *AutoSnapGizmoCreate(void)
{
	AutoSnapGizmo *snap = calloc(1, sizeof(AutoSnapGizmo));
	snap->currSnapAction = EditSnapNone;
	snap->snapTriFunc = elAutoSnapGizmo.snapTriFunc;
	snap->snapTerrainFunc = elAutoSnapGizmo.snapTerrainFunc;
	snap->snapNullFunc = elAutoSnapGizmo.snapNullFunc;
	return snap;
}

/******
* Set the function used to detect triangles in auto-snap.
* PARAMS:
*   triF - AutoSnapTriGetter callback to detect triangles
******/
void AutoSnapGizmoSetTriGetter(AutoSnapTriGetter triF)
{
	elAutoSnapGizmo.snapTriFunc = triF;
}

/******
* Sets the function used to detect individual snapping points.
* PARAMS:
*   terrainF - AutoSnapPointGetter callback to detect points
******/
void AutoSnapGizmoSetTerrainF(AutoSnapPointGetter terrainF)
{
	elAutoSnapGizmo.snapTerrainFunc = terrainF;
}

/******
* Sets the function used to detect individual snapping points.
* PARAMS:
*   terrainF - AutoSnapPointGetter callback to detect points
******/
void AutoSnapGizmoSetNullF(AutoSnapNullGetter nullF)
{
	elAutoSnapGizmo.snapNullFunc = nullF;
}

/******
* This function detects whether snapping should occur and where given a triangle to (possibly) snap to, and a ray
* cast to determine where the user is clicking.
* PARAMS:
*   gizmo - AutoSnapGizmo doing the detection
*   specSnap - EditSpecialSnapMode being used
*   start - start Vec3 of the ray cast
*   end - end Vec3 of the ray cast
*   verts - the Vec3 comprising the vertices of the triangle
*   normal - the Vec3 normal of the triangle's plane
*   cam - the camera Mat4
* RETURNS:
*   bool indicating whether snap was detected
******/
static bool AutoSnapGizmoDetectTriSnap(AutoSnapGizmo *gizmo, EditSpecialSnapMode specSnap, Vec3 start, Vec3 end, Mat3 verts, Vec3 normal, Mat4 cam)
{
	float snapDist = 0.01;
	bool foundSnap = false;
	int i;
	int closestVert = -1;
	double closestDist = -1;

	// vertex/midpoint snapping
	if (specSnap == EditSnapSmart || specSnap == EditSnapVertex || specSnap == EditSnapMidpoint)
	{
		Mat3 mids;
		for (i = 0; i < 3; i++)
		{
			double dist = findPointLineIntersection(start, end, verts[i], NULL);
			if (dist < snapDist * distance3(cam[3], verts[i]) && (closestDist == -1 || dist < closestDist) && specSnap != EditSnapMidpoint)
			{
				closestDist = dist;
				closestVert = i;
			}

			// test midpoint
			addVec3(verts[i], verts[(i + 1) % 3], mids[i]);
			scaleVec3(mids[i], 0.5, mids[i]);
			dist = findPointLineIntersection(start, end, mids[i], NULL);
			if (dist < snapDist * distance3(cam[3], mids[i]) && (closestDist == -1 || dist < closestDist) && specSnap != EditSnapVertex)
			{
				closestDist = dist;
				closestVert = i + 3;
			}
		}
		if (closestVert != -1)
		{
			// midpoint
			if (closestVert > 2)
			{
				drawPointIndicator(mids[closestVert - 3]);
				gfxDrawLine3D(verts[closestVert - 3], verts[(closestVert - 2) %3], colorFromRGBA(0xFFFF00FF));
				copyVec3(mids[closestVert - 3],  gizmo->preSnapVerts[0]);
				copyVec3(verts[closestVert - 3], gizmo->preSnapVerts[1]);
				copyVec3(verts[(closestVert - 2) %3], gizmo->preSnapVerts[2]);
			}
			// end vertex
			else
			{
				drawPointIndicator(verts[closestVert]);
				copyVec3(verts[closestVert], gizmo->preSnapVerts[0]);
			}
			foundSnap = true;
			gizmo->currSnapAction = EditSnapVertex;
			copyVec3(normal, gizmo->normal);
		}
	}

	// edge snapping
	if (!foundSnap && (specSnap == EditSnapSmart || specSnap == EditSnapEdge))
	{
		closestDist = -1;
		for (i = 0; i < 3; i++)
		{
			Vec3 scaleTemp;
			double dist = findSkewLineDistance(start, end, verts[i], verts[(i + 1) % 3]);
			findSkewLineIntersection(start, end, verts[i], verts[(i + 1) % 3], scaleTemp);
			if (dist < snapDist * distance3(cam[3], scaleTemp) && (closestDist == -1 || dist < closestDist))
			{
				closestDist = dist;
				closestVert = i;
			}
		}
		if (closestVert != -1)
		{
			gfxDrawLine3D(verts[closestVert], verts[(closestVert + 1) % 3], colorFromRGBA(0xFFFF00FF));
			foundSnap = true;
			gizmo->currSnapAction = EditSnapEdge;
			copyVec3(verts[closestVert], gizmo->preSnapVerts[0]);
			copyVec3(verts[(closestVert + 1) % 3], gizmo->preSnapVerts[1]);
			copyVec3(normal, gizmo->normal);
		}
	}

	// face snapping
	if (!foundSnap && (specSnap == EditSnapSmart || specSnap == EditSnapFace))
	{
		gfxDrawLine3D(verts[0], verts[1], colorFromRGBA(0xFFFF00FF));
		gfxDrawLine3D(verts[1], verts[2], colorFromRGBA(0xFFFF00FF));
		gfxDrawLine3D(verts[2], verts[0], colorFromRGBA(0xFFFF00FF));
		gizmo->currSnapAction = EditSnapFace;
		copyVec3(verts[0], gizmo->preSnapVerts[0]);
		copyVec3(verts[1], gizmo->preSnapVerts[1]);
		copyVec3(verts[2], gizmo->preSnapVerts[2]);
		copyVec3(normal, gizmo->normal);
		foundSnap = true;
	}

	return foundSnap;
}

/******
* This function determines whether terrain snap should occur, given a ray cast.
* PARAMS:
*   gizmo - AutoSnapGizmo gizmo doing detection
*   start - start Vec3 of the ray cast
*   end - end Vec3 of the ray cast
*   vert - the Vec3 of the intersection with the terrain
*   normal - the Vec3 normal to the terrain point
*   cam - the camera Mat4
* RETURNS:
*   bool indicating whether snap occurred
******/
static bool AutoSnapGizmoDetectTerrainSnap(AutoSnapGizmo *gizmo, Vec3 start, Vec3 end, Vec3 vert, Vec3 normal, Mat4 cam)
{
	// straightforward for now; just set snap to the passed-in values
	gizmo->currSnapAction = EditSnapTerrain;
	copyVec3(vert, gizmo->preSnapVerts[0]);
	copyVec3(normal, gizmo->normal);
	return true;
}

/******
* This function is the main snap detection function that uses the specified gizmo's terrain and triangle getting
* callbacks to determine whether snap should occur.
* PARAMS:
*   gizmo - AutoSnapGizmo used for detection
*   specSnap - EditSpecialSnapMode to determine how to snap
*   cam - Mat4 camera matrix
*   start - start Vec3 of the ray cast
*   end - end Vec3 of the ray cast
******/
void AutoSnapGizmoDetectSnap(AutoSnapGizmo *gizmo, EditSpecialSnapMode specSnap, Mat4 cam, Vec3 start, Vec3 end)
{
	Mat3 verts;
	Vec3 terrainPoint, nullPoint, triNormal, terrainNormal;
	int checkOrder[3] = {-1, -1, -1};
	double dists[3] = {-1, -1, -1};
	int i;
	bool foundSnap = false;

	if (specSnap == EditSnapNone || specSnap == EditSnapGrid)
	{
		gizmo->currSnapAction = EditSnapNone;
		return;
	}

	// go through each of the snap types and determine which one returns a target that is closest to the camera
	// dists array holds distance to snap target for different callbacks: 0 - triangle, 1 - terrain
	if ((specSnap == EditSnapSmart || specSnap == EditSnapVertex || specSnap == EditSnapEdge || specSnap == EditSnapFace || specSnap == EditSnapMidpoint)
		&& gizmo->snapTriFunc && gizmo->snapTriFunc(start, end, verts, triNormal))
	{
		// triangle-based snap (vertices, edges, midpoints, and faces)
		Vec3 temp;
		findPlaneLineIntersection(verts[0], triNormal, start, end, temp);
		dists[0] = distance3(temp, cam[3]);
	}
	if ((specSnap == EditSnapSmart || specSnap == EditSnapTerrain)
		&& gizmo->snapTerrainFunc && gizmo->snapTerrainFunc(start, end, terrainPoint, terrainNormal))
	{
		dists[1] = distance3(terrainPoint, cam[3]);
	}

	if ((specSnap != EditSnapNone && specSnap != EditSnapGrid)
		&& gizmo->snapNullFunc && gizmo->snapNullFunc(start, end, nullPoint))
	{
		dists[2] = distance3(nullPoint, cam[3]);
	}

	// determine the order in which to attempt to detect the snap by ordering the returned points
	// from the detection callbacks in order of their distance from the camera
	for (i = 0; i < 3; i++)
	{
		int j;
		double minDist = -1;
		for (j = 0; j < 3; j++)
		{
			if (dists[j] != -1 && (dists[j] < minDist || minDist == -1))
			{
				minDist = dists[j];
				checkOrder[i] = j;
			}
		}

		// clear the just-processed distance so that it is ignored for the next iteration
		if (checkOrder[i] != -1)
			dists[checkOrder[i]] = -1;
	}

	for (i = 0; i < 3 && !foundSnap; i++)
	{
		// tri
		if (checkOrder[i] == 0)
			foundSnap = AutoSnapGizmoDetectTriSnap(gizmo, specSnap, start, end, verts, triNormal, cam);
		// terrain
		else if (checkOrder[i] == 1)
			foundSnap = AutoSnapGizmoDetectTerrainSnap(gizmo, start, end, terrainPoint, terrainNormal, cam);
		// no snapping (i.e. null point)
		else if (checkOrder[i] == 2)
			break;
	}

	if (!foundSnap)
		gizmo->currSnapAction = EditSnapNone;
}

/******
* This function is the top-level function that deals with snapping an input matrix.
* PARAMS:
*   transGizmo - the TranslateGizmo being used
*   ghostMat - the Mat4 to snap
******/
void AutoSnapGizmoApplySnap(AutoSnapGizmo *gizmo, Mat4 cam, Vec3 start, Vec3 end, Mat4 mat, bool snapNormal, int snapNormalAxis, bool snapNormalInverse, bool specSnapRestrict)
{
	float snapDist = 0.01;

	gfxSetPrimZTest(0);

	// snap normal if it is enabled
	if (snapNormal && gizmo->currSnapAction != EditSnapNone && gizmo->currSnapAction != EditSnapTerrain)
		GizmoSnapToNormal(snapNormalAxis, snapNormalInverse, mat, gizmo->normal);

	// snapping to vertices or midpoints
	if (gizmo->currSnapAction == EditSnapVertex || gizmo->currSnapAction == EditSnapMidpoint)
	{
		drawPointIndicator(gizmo->snapVerts[0]);
		snapToVertex(gizmo->snapVerts[0], mat[3]);
		if (gizmo->currSnapAction == EditSnapMidpoint)
			gfxDrawLine3D(gizmo->snapVerts[1], gizmo->snapVerts[2], colorFromRGBA(0xFFFF00FF));
	}
	// snapping to edges
	else if (gizmo->currSnapAction == EditSnapEdge)
	{
		gfxDrawLine3D(gizmo->snapVerts[0], gizmo->snapVerts[1], colorFromRGBA(0xFFFF00FF));
		snapToEdge(start, end, gizmo->snapVerts[0], gizmo->snapVerts[1], mat[3], snapDist, cam[3], specSnapRestrict);
	}
	// snapping to faces
	else if (gizmo->currSnapAction == EditSnapFace)
	{
		gfxDrawLine3D(gizmo->snapVerts[0], gizmo->snapVerts[1], colorFromRGBA(0xFFFF00FF));
		gfxDrawLine3D(gizmo->snapVerts[1], gizmo->snapVerts[2], colorFromRGBA(0xFFFF00FF));
		gfxDrawLine3D(gizmo->snapVerts[2], gizmo->snapVerts[0], colorFromRGBA(0xFFFF00FF));
		snapToFace(start, end, gizmo->snapVerts, mat[3], snapDist, cam[3], specSnapRestrict);
	}
	// snapping to terrain
	else if (gizmo->currSnapAction == EditSnapTerrain)
	{
		Vec3 snapped, nTemp;
		if (gizmo->snapTerrainFunc(start, end, snapped, nTemp))
		{
			copyVec3(snapped, gizmo->snapVerts[0]);
			copyVec3(nTemp, gizmo->normal);
		}
		else
		{
			copyVec3(gizmo->snapVerts[0], snapped);
			copyVec3(gizmo->normal, nTemp);
		}

		if (snapNormal)
			GizmoSnapToNormal(snapNormalAxis, snapNormalInverse, mat, nTemp);
		copyVec3(snapped, mat[3]);
	}
	gfxSetPrimZTest(1);
	if (gizmo->currSnapAction != EditSnapNone)
		inpHandled();
}

/******
* This function should be called at some point before querying the matrix or calling SnapSettingsApplySnap,
* in order to ensure that the correct snap target is being used for calculating the final destination.
* PARAMS:
*   gizmo - AutoSnapGizmo to lock
******/
void AutoSnapGizmoLockSnapTarget(AutoSnapGizmo *gizmo)
{
	if (!gizmo)
		gizmo = &elAutoSnapGizmo;

	copyMat3(gizmo->preSnapVerts, gizmo->snapVerts);
}

/******
* This function does detection of snap targets and handles input.
*   gizmo - AutoSnapGizmo doing detection
*   specSnap - EditSpecialSnapMode in which detection should occur
******/
void AutoSnapGizmoUpdate(AutoSnapGizmo *gizmo, EditSpecialSnapMode specSnap)
{
	Mat4 cam;
	Vec3 start, end;

	if (!gizmo)
		gizmo = &elAutoSnapGizmo;

	gfxGetActiveCameraMatrix(cam);
	editLibCursorRay(start, end);
	AutoSnapGizmoDetectSnap(gizmo, specSnap, cam, start, end);
}

/******
* This function gets the matrix for the current snap target underneath the mouse.
* PARAMS:
*   gizmo - AutoSnapGizmo that did detection
*   mat - Mat4 mat to move to the snapped location; MUST be a valid transformation matrix
*   snapNormal - bool whether to snap mat to the snap target's surface normal
*   snapNormalAxis - int indicating which of mat's axes to snap to surface normal
*   snapNormalInverse - bool indicating whether to snap the inverse of snapNormalAxis to surface normal
*   specSnapRestrict - bool indicating whether to restrict snap to edges/boundaries of snap target
******/
void AutoSnapGizmoGetMat(AutoSnapGizmo *gizmo, Mat4 mat, bool snapNormal, int snapNormalAxis, bool snapNormalInverse, bool specSnapRestrict)
{
	Mat4 cam;
	Vec3 start, end;

	if (!gizmo)
		gizmo = &elAutoSnapGizmo;

	gfxGetActiveCameraMatrix(cam);
	editLibCursorRay(start, end);

	AutoSnapGizmoApplySnap(gizmo, cam, start, end, mat, snapNormal, snapNormalAxis, snapNormalInverse, specSnapRestrict);
}

/******
* TRANSLATE GIZMO
******/
const float TRANS_GIZMO_BOX_SCALE = 0.2;
const float TRANS_GIZMO_SIZE_SCALE = 0.15;
const float TRANS_GIZMO_GRID_SCALE = 6;
typedef struct TranslateGizmo
{
	// data
	AutoSnapGizmo *autoSnapGizmo;			// holds auto-snap gizmo and data
	Mat4 startMat;							// original matrix before translation
	Vec3 startIntersect;					// where mouse intersected with axis/plane when translation began
	Mat4 activeMat;							// holds correctly-transformed location of the gizmo
	Mat4 drawMat;							// holds the matrix used to draw the gizmo (differs from active mat when world-aligned)
	bool translationAxesOver[3];			// indicates when a user has moused over particular axes
	bool translationAxesUsed[3];			// indicates the currently "active" axes used for translation
	bool mousedOver;

	// drawing
	int highlight[3];
	Vec3 tickDirection;
	float arrowLength;
	float boxLength;
	bool hide_grid;
	int currentSnapCoords[3];				// the indexes (i.e. number of partitions) along the grid where
											// the object is currently snapped

	// gizmo modes
	bool activated;							// gizmo has been activated by user input
	bool relativeDragMode;					// indicates whether to move directly to the location of the
											// mouse or to drag the object in a relative motion
	bool alignedToWorld;					// translate in world space, not object space
	bool specSnapRestrict;					// determines whether the user is allowed to drag past the bounds of
											// the snapping polygon/edge.
	bool snapNormal;						// determines whether snapping will rotate to align the z axis with the
											// normal
	bool snapNormalInverse;					// determines whether to snap the *inverse* of the selected axis to the normal
	int snapNormalAxis;						// 0 = x, 1 = y, 2 = z
	bool snapAlongAxes;						// determines whether to snap along the selected axes in special snap mode
	EditSpecialSnapMode specSnap;			// gizmo's snap mode
	EditSpecialSnapMode lastSnap;			// the previous (non-NONE) snap mode
	float translationSnapResolution;		// a number indicating the level of the translation
											// gizmo's snap resolution
	bool ignoreSmartSnap;					// If in SmartSnap mode and you select an axis, treat as if where GridSnap

	// interface
	void *funcContext;						// user data to assign to this gizmo
	GizmoActivateFn activateFunc;			// function called when the mouse button is pressed
	GizmoDeactivateFn deactivateFunc;		// function called when the mouse button is released
	EditLibGizmosToolbar *toolbar;			// a toolbar to update when this gizmo's various parameters change
} TranslateGizmo;

TranslateGizmo *TranslateGizmoCreate(void)
{
	TranslateGizmo* newGizmo = calloc(1, sizeof(TranslateGizmo));
	newGizmo->autoSnapGizmo = &elAutoSnapGizmo;
	newGizmo->translationSnapResolution = 2;
	newGizmo->relativeDragMode = 1;
	newGizmo->specSnap = EditSnapNone;
	newGizmo->specSnapRestrict = false;
	newGizmo->snapNormal = false;
	newGizmo->snapNormalInverse = false;
	newGizmo->snapNormalAxis = 1;
	newGizmo->translationAxesUsed[0] = 1;
	newGizmo->translationAxesUsed[1] = 0;
	newGizmo->translationAxesUsed[2] = 1;
	newGizmo->snapAlongAxes = false;
	identityMat4(newGizmo->activeMat);
	return newGizmo;
}

void TranslateGizmoDestroy(TranslateGizmo* transGizmo)
{
	free(transGizmo);
}

/******
* This function sets the draw matrix to the appropriate value, depending on whether
* the gizmo has world alignment enabled.
* PARAMS:
*   transGizmo - TranslateGizmo to set the draw matrix on
******/
static void TranslateGizmoSetDrawMat(TranslateGizmo *transGizmo)
{
	if (transGizmo->alignedToWorld)
	{
		copyMat3(unitmat, transGizmo->drawMat);
		copyVec3(transGizmo->activeMat[3], transGizmo->drawMat[3]);
	}
	else
		copyMat4(transGizmo->activeMat, transGizmo->drawMat);
}

/******
* This function populates the one or two used axis values and returns the first unused axis number
* If there is only one axis, the second axis value will be set to -1.
******/
static int TranslateGizmoGetUsedAxes(TranslateGizmo *transGizmo, int *axis1, int *axis2)
{
	int i, unusedAxis = -1;
	*axis1 = *axis2 = -1;

	for (i = 0; i < 3; i++)
	{
		if (transGizmo->translationAxesUsed[i])
		{
			if (*axis1 >= 0)
				*axis2 = i;
			else
				*axis1 = i;
		}
		else
			unusedAxis = i;
	}
	return unusedAxis;
}

/******
* This function takes an input point/vector and snaps the coordinates along the selected translation
* axes to the nearest increment of the translation snap resolution.
* PARAMS:
*   transGizmo - TranslateGizmo gizmo being manipulated
*   startMat - Mat4 of the starting location of transGizmo
*   location - the original location to be snapped
*   snapped - the result vector after snapping the location parameter
******/
static void TranslateGizmoSnapLocation(TranslateGizmo* transGizmo, Mat4 startMat, Vec3 location, Vec3 snapped)
{
	// snap resolution must be set
	if (transGizmo->specSnap == EditSnapGrid || transGizmo->specSnap == EditSnapSmart)
	{
		float snap = GizmoGetSnapWidth((int) transGizmo->translationSnapResolution);
		Mat4 inverse;
		Vec3 transformed;
		int i;

		// work around the origin
		if (!transGizmo->alignedToWorld)
		{
			invertMat4Copy(startMat, inverse);
			mulVecMat4(location, inverse, transformed);
		}
		else
			copyVec3(location, transformed);

		// for every selected axis, we perform the snap
		for (i = 0; i < 3; i++)
		{
			if (!transGizmo->translationAxesUsed[i])
				continue;

			transformed[i] += snap/2.0;
			if (transformed[i] < 0)
				transformed[i] -= snap;
			transformed[i] = ((int)(transformed[i] / snap)) * snap;
			transGizmo->currentSnapCoords[i] = transformed[i] / snap;
		}

		if (!transGizmo->alignedToWorld)
			mulVecMat4(transformed, startMat, snapped);
		else
			copyVec3(transformed, snapped);
	}

	// if snap resolution is not set, return the original location
	else
		copyVec3(location,snapped);
}

/******
* This function takes ray endpoints and finds their intersection on a translate gizmo's used axes along
* the gizmo's start matrix.
******/
static void TranslateGizmoGetIntersection(TranslateGizmo *transGizmo, Vec3 rayStart, Vec3 rayEnd, Vec3 outIntersect)
{
	Mat4 cam;

	// axis indexes for used and unused axes
	int usedAxes[2];
	int unusedAxis = TranslateGizmoGetUsedAxes(transGizmo, &usedAxes[0], &usedAxes[1]);

	// cast a ray into the world from the mouse cursor
	gfxGetActiveCameraMatrix(cam);

	// case of one translation axis
	if (usedAxes[1] == -1)
	{
		Vec3 cameraVec, planeNormal, planeIntersect, arrowEnd;

		assert(usedAxes[0] >= 0);

		// get the vector from camera to object, cross it with the axis we're moving along to get the
		// direction we draw the ticks in
		subVec3(transGizmo->drawMat[3], cam[3], cameraVec);
		crossVec3(cameraVec, transGizmo->drawMat[usedAxes[0]], transGizmo->tickDirection);
		normalVec3(transGizmo->tickDirection);

		// now make a plane out of it, and find where the mouse cursor intersects it
		crossVec3(transGizmo->tickDirection, transGizmo->drawMat[usedAxes[0]], planeNormal);
		findPlaneLineIntersection(transGizmo->drawMat[3], planeNormal, rayStart, rayEnd, planeIntersect);

		// find the point on the axis that is nearest to that plane intersection, and move the object there
		addVec3(transGizmo->drawMat[3], transGizmo->drawMat[usedAxes[0]], arrowEnd);
		findPointLineIntersection(transGizmo->drawMat[3], arrowEnd, planeIntersect, outIntersect);
	}

	// case of two translation axes
	else
	{
		// find the intersection of the ray-cast line with the plane defined by the two selected axes
		findPlaneLineIntersection(transGizmo->drawMat[3], transGizmo->drawMat[unusedAxis], rayStart, rayEnd, outIntersect);
	}
}

// called when mouse is first clicked/held down during a translate gizmo operation
static bool TranslateGizmoBeginMove(TranslateGizmo* transGizmo, Mat4 cam, Vec3 start, Vec3 end)
{
	bool ret = false;
	float snapDist = 0.01;

	// lock in the selected translation axes when the click occurs
	if ((transGizmo->translationAxesOver[0] || transGizmo->translationAxesOver[1] || transGizmo->translationAxesOver[2]) &&
		(transGizmo->specSnap == EditSnapGrid || transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapSmart))
	{
		if(transGizmo->specSnap == EditSnapSmart)
			transGizmo->ignoreSmartSnap = true;
		copyVec3(transGizmo->translationAxesOver, transGizmo->translationAxesUsed);
		transGizmo->snapAlongAxes = true;
		ret = true;
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
	else
		transGizmo->ignoreSmartSnap = false;

	// if not in a special snap mode
	if (((transGizmo->specSnap == EditSnapSmart && transGizmo->ignoreSmartSnap) || transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapGrid) && (transGizmo->translationAxesUsed[0] || transGizmo->translationAxesUsed[1] || transGizmo->translationAxesUsed[2]))
	{
		if (transGizmo->relativeDragMode)
			TranslateGizmoGetIntersection(transGizmo, start, end, transGizmo->startIntersect);
		else
			copyVec3(transGizmo->startMat[3], transGizmo->startIntersect);
	}
	// if snapping is enabled, find a "start point" in relative drag mode; also, lock in the current snap target
	else if (transGizmo->specSnap != EditSnapNone && transGizmo->specSnap != EditSnapGrid && (transGizmo->specSnap != EditSnapSmart || !transGizmo->ignoreSmartSnap))
	{
		AutoSnapGizmoLockSnapTarget(transGizmo->autoSnapGizmo);
		if (transGizmo->relativeDragMode)
		{
			Mat4 intersection;
			AutoSnapGizmoApplySnap(transGizmo->autoSnapGizmo, cam, start, end, intersection, transGizmo->snapNormal, transGizmo->snapNormalAxis, transGizmo->snapNormalInverse, transGizmo->specSnapRestrict);
			copyVec3(intersection[3], transGizmo->startIntersect);
			if (transGizmo->autoSnapGizmo->currSnapAction == EditSnapNone)
				ret = false;
			else
				ret = true;
		}
	}
	if (!transGizmo->relativeDragMode || ret)
	{
		ret = true;
		inpHandled();
	}
	return ret;
}

/******
* This function handles translation of the object (i.e. when the object is being dragged).  This
* function also deals with snapping and drawing of the ticks/grids/arrows during object translation.
******/
void TranslateGizmoApplyChanges(TranslateGizmo* transGizmo, Mat4 cam, Vec3 start, Vec3 end)
{
	Mat4 origMat;

	copyMat4(transGizmo->activeMat, origMat);
	// ultimately, we need to calculate the updated matrix and store it
	// if a snap is enabled, apply a snap
	if (transGizmo->specSnap != EditSnapNone && transGizmo->specSnap != EditSnapGrid && (transGizmo->specSnap != EditSnapSmart || !transGizmo->ignoreSmartSnap))
	{
		int axis1, axis2;
		int unused = TranslateGizmoGetUsedAxes(transGizmo, &axis1, &axis2);

		AutoSnapGizmoApplySnap(transGizmo->autoSnapGizmo, cam, start, end, transGizmo->activeMat, transGizmo->snapNormal, transGizmo->snapNormalAxis, transGizmo->snapNormalInverse, transGizmo->specSnapRestrict);

		if (!(transGizmo->autoSnapGizmo->currSnapAction == EditSnapVertex || transGizmo->autoSnapGizmo->currSnapAction == EditSnapMidpoint) && transGizmo->relativeDragMode)
		{
			subVec3(transGizmo->activeMat[3], transGizmo->startIntersect, transGizmo->activeMat[3]);
			addVec3(transGizmo->activeMat[3], transGizmo->startMat[3], transGizmo->activeMat[3]);
		}

		// deal with snap-to-axis/axes modes
		if (transGizmo->snapAlongAxes)
		{
			if (axis1 != -1)
			{
				Vec3 snappedSoFar, lineEnd;

				if (!nearSameVec3(transGizmo->startMat[3], transGizmo->activeMat[3]))
				{
					copyVec3(transGizmo->activeMat[3], snappedSoFar);

					// snap along single axis
					if (axis2 == -1)
					{
						addVec3(transGizmo->startMat[3], transGizmo->drawMat[axis1], lineEnd);
						findPointLineIntersection(transGizmo->startMat[3], lineEnd, snappedSoFar, transGizmo->activeMat[3]);
					}
					// snap along two axes
					else if (unused != -1)
					{				
						addVec3(snappedSoFar, transGizmo->drawMat[unused], lineEnd);
						findPlaneLineIntersection(transGizmo->startMat[3], transGizmo->drawMat[unused], snappedSoFar, lineEnd, transGizmo->activeMat[3]);
					}
				}
			}
		}
	}
	else
	{
		// suspend motion of the object if camera is rotating
		if (!gfxIsActiveCameraControllerRotating())
		{
			Vec3 intersection, offset, snapped;

			TranslateGizmoGetIntersection(transGizmo, start, end, intersection);
			subVec3(intersection, transGizmo->startIntersect, offset);
			addVec3(transGizmo->startMat[3], offset, transGizmo->activeMat[3]);
			TranslateGizmoSnapLocation(transGizmo, transGizmo->startMat, transGizmo->activeMat[3], snapped);
			copyVec3(snapped, transGizmo->activeMat[3]);
		}
	}

	// if matrix goes out of bounds, reset it to old location
	if (checkMatOutOfBounds(transGizmo->activeMat))
		copyMat4(origMat, transGizmo->activeMat);
}

void TranslateGizmoUpdate(TranslateGizmo* transGizmo)
{
	Mat4 cam;
	float camDist;
	Vec3 start, end;
	float skewMax, boxSkewMax;
	bool mousedOver = false;
	int i;

	TranslateGizmoSetDrawMat(transGizmo);

	// INPUT PARAMETERS
	editLibCursorRay(start, end);
	gfxGetActiveCameraMatrix(cam);
	camDist = distance3(cam[3], transGizmo->startMat[3]);
	transGizmo->arrowLength = TRANS_GIZMO_SIZE_SCALE * camDist;
	transGizmo->boxLength = transGizmo->arrowLength * TRANS_GIZMO_BOX_SCALE;
	skewMax = transGizmo->arrowLength / 10.0;
	boxSkewMax = skewMax / 2.0;

	// HANDLE INPUTS
	// check closeness of the mouse to each of the three double-axis translation boxes
	// 1->(x,y); 2->(y,z); 3->(z,x)
	//   | 
	// c1|_p
	//   |_|__
	//     c2
	if (!transGizmo->activated)
	{
		Vec3 skewDist;
		Vec3 p[3], corner1[3], corner2[3];
		float closestDist = -1;
		int closestAxis = -1;

		for (i = 0; i < 3; i++)
		{
			Vec3 intersection;
			bool valid = false;

			scaleVec3(transGizmo->drawMat[i], (transGizmo->arrowLength / 5.0), corner1[i]);
			scaleVec3(transGizmo->drawMat[(i + 1) % 3], (transGizmo->arrowLength / 5.0), corner2[i]);
			addVec3(corner1[i], corner2[i], p[i]);
			addVec3(corner1[i], transGizmo->drawMat[3], corner1[i]);
			addVec3(corner2[i], transGizmo->drawMat[3], corner2[i]);
			addVec3(p[i], transGizmo->drawMat[3], p[i]);

			// check closeness of mouse to segment c1,p
			findSkewLineIntersection(start, end, corner1[i], p[i], intersection);
			if (findPointLineIntersection(transGizmo->drawMat[3], corner1[i], intersection, NULL) < transGizmo->boxLength && findPointLineIntersection(corner2[i], p[i], intersection, NULL) < transGizmo->boxLength)
			{
				skewDist[i] = findSkewLineDistance(start, end, corner1[i], p[i]);
				valid = true;
			}
			// check closeness of mouse to segment p,c2
			findSkewLineIntersection(start, end, p[i], corner2[i], intersection);
			if (findPointLineIntersection(corner1[i], p[i], intersection, NULL) < transGizmo->boxLength && findPointLineIntersection(transGizmo->drawMat[3], corner2[i], intersection, NULL) < transGizmo->boxLength)
			{
				F32 tempDist = findSkewLineDistance(start, end, p[i], corner2[i]);
				if (valid)
					skewDist[i] = MIN(skewDist[i], tempDist);
				else
				{
					skewDist[i] = tempDist;
					valid = true;
				}
			}
			// determine whether the user's cursor is closer to the current axis box being checked
			if (valid && skewDist[i] < boxSkewMax && (closestAxis == -1 || skewDist[i] < closestDist))
			{
				closestDist = skewDist[i];
				closestAxis = i;
			}
		}
		if (closestAxis != -1)
		{
			transGizmo->translationAxesOver[closestAxis] = true;
			transGizmo->translationAxesOver[(closestAxis + 1) % 3] = true;
			transGizmo->translationAxesOver[(closestAxis + 2) % 3] = false;
			mousedOver = true;	
		}

		// if none of the double-axis boxes are moused over, check for single-axis proximity
		if (!mousedOver)
		{
			for (i = 0; i < 3; i++)
			{
				Vec3 axisEnd, intersection;
				F32 origDist;

				addVec3(transGizmo->drawMat[3], transGizmo->drawMat[i], axisEnd);

				// find distance between ray and each axis
				skewDist[i] = findSkewLineDistance(transGizmo->drawMat[3], axisEnd, start, end);
				findSkewLineIntersection(transGizmo->drawMat[3], axisEnd, start, end, intersection);
				origDist = findPointPlaneDistance(intersection, transGizmo->drawMat[3], transGizmo->drawMat[i]);
				if (origDist >= 0 && origDist < transGizmo->arrowLength && skewDist[i] < skewMax && (closestAxis == -1 || skewDist[i] < closestDist))
				{
					closestDist = skewDist[i];
					closestAxis = i;
				}
			}
			if (closestAxis != -1)
			{
				transGizmo->translationAxesOver[closestAxis] = true;
				transGizmo->translationAxesOver[(closestAxis + 1) % 3] = false;
				transGizmo->translationAxesOver[(closestAxis + 2) % 3] = false;
				mousedOver = true;	
			}
			else
			{
				transGizmo->translationAxesOver[0] = false;
				transGizmo->translationAxesOver[1] = false;
				transGizmo->translationAxesOver[2] = false;
			}
		}

		// look for snap targets is using a special snap mode
		if (transGizmo->specSnap != EditSnapNone && transGizmo->specSnap != EditSnapGrid &&
			!(transGizmo->specSnap == EditSnapSmart && (transGizmo->translationAxesOver[0] || transGizmo->translationAxesOver[1] || transGizmo->translationAxesOver[2])))
			AutoSnapGizmoDetectSnap(transGizmo->autoSnapGizmo, transGizmo->specSnap, cam, start, end);
	}

	// determine whether to begin/end translation by activating/deactivating the gizmo
	if ((mouseClick(MS_LEFT) || mouseDown(MS_LEFT)) && !transGizmo->activated)
	{
		if (TranslateGizmoBeginMove(transGizmo, cam, start, end))
		{
			transGizmo->activated = true;
			if (transGizmo->activateFunc)
				transGizmo->activateFunc(transGizmo->activeMat, transGizmo->funcContext);
		}
	}
	else if (!mouseIsDown(MS_LEFT))
	{		
		if (transGizmo->activated)
		{
			transGizmo->activated = false;
			copyMat4(transGizmo->activeMat, transGizmo->startMat);
			if (transGizmo->deactivateFunc)
				transGizmo->deactivateFunc(transGizmo->activeMat, transGizmo->funcContext);
			inpHandled();
		}
		if(transGizmo->ignoreSmartSnap)
		{
			setVec3(transGizmo->translationAxesUsed,0,0,0);
			if (transGizmo->toolbar)
				elGizmosToolbarUpdate(transGizmo->toolbar);
			transGizmo->ignoreSmartSnap = false;
		}
	}

	// APPLY CHANGES
	if (transGizmo->activated)
		TranslateGizmoApplyChanges(transGizmo, cam, start, end);

	// SET DRAW PARAMETERS
	// set the highlights on the axes
	if (transGizmo->snapAlongAxes || transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapGrid || (mousedOver&&transGizmo->specSnap == EditSnapSmart))
	{
		if (transGizmo->activated || !mousedOver || !(transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapGrid || transGizmo->specSnap == EditSnapSmart))
			copyVec3(transGizmo->translationAxesUsed, transGizmo->highlight);
		else
			copyVec3(transGizmo->translationAxesOver, transGizmo->highlight);
	}
	else
		copyVec3(zerovec3, transGizmo->highlight);

	transGizmo->mousedOver = mousedOver;
}

void TranslateGizmoDraw(TranslateGizmo* transGizmo)
{
	const float axisTailBoxScale = 0.5;
	const float axisOriginBoxScale = 0.2;

	int i;
	Mat4 cam;

	gfxSetPrimZTest(0);
	gfxSetPrimIgnoreMax(true);
	gfxGetActiveCameraMatrix(cam);

	// set the draw matrix appropriately
	TranslateGizmoSetDrawMat(transGizmo);

	// draw the activated gizmo (with grid)
	if (transGizmo->activated)
	{
		int usedAxes[2];
		int unusedAxis = TranslateGizmoGetUsedAxes(transGizmo, &usedAxes[0], &usedAxes[1]);
		float unusedArrowSize = distance3(cam[3], transGizmo->startMat[3]) * TRANS_GIZMO_SIZE_SCALE;

		if (transGizmo->specSnap != EditSnapGrid && transGizmo->specSnap != EditSnapNone && !(transGizmo->specSnap == EditSnapSmart && transGizmo->ignoreSmartSnap))
			gfxSetPrimZTest(1);

		// draw the unselected axis arrows atop the ticks/grid
		for (i = 0; i < 3; i++)
		{
			int color = axisColor(i);
			if (!transGizmo->translationAxesUsed[i] || (transGizmo->specSnap != EditSnapGrid && transGizmo->specSnap != EditSnapNone && !(transGizmo->specSnap == EditSnapSmart && transGizmo->ignoreSmartSnap)))
			{
				// draw the start location axes ghosted so we know where the object was originally
				drawLineArrow(transGizmo->startMat[3], transGizmo->drawMat[i], transGizmo->drawMat[(i + 1) % 3], unusedArrowSize, color & 0x3fffffff, color & 0x3fffffff);
				// draw the new location axes where the object is now, not ghosted, and with perspective
				drawLineArrow(transGizmo->activeMat[3], transGizmo->drawMat[i], transGizmo->drawMat[(i + 1) % 3], unusedArrowSize, color, color);
			}
		}

		if (transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapGrid || (transGizmo->specSnap == EditSnapSmart && transGizmo->ignoreSmartSnap) )
		{
			// determine the snap distance
			Mat4 gridMat;
			Vec3 half[2];
			Vec3 lineBegin, lineMid, lineEnd;
			float snap = GizmoGetSnapWidth((int) transGizmo->translationSnapResolution);
			float gridDist;
			int halfRange, j;
			int numAxes = (usedAxes[1] == -1 ? 1 : 2);

			gfxSetPrimZTest(1);

			// backup the original matrix if we need to draw the grid aligned to the world
			if (transGizmo->alignedToWorld)
			{
				copyMat4(unitmat, gridMat);
				for(i = 0; i < 3; i++)
				{
					if (!transGizmo->translationAxesUsed[i])
						gridMat[3][i] = transGizmo->startMat[3][i];
				}
			}
			else
				copyMat4(transGizmo->startMat, gridMat);

			// determine how large the grid should be; we wish for it to go as far to infinity as possible
			// from the user's perspective
			gridDist = distance3(cam[3], gridMat[3]);
			if (fabs(gridDist) > 10000)
				gridDist = 10000;
			gridDist *= TRANS_GIZMO_GRID_SCALE;
			halfRange = gridDist / snap;


			// extend the unused axis (in double-axis drag) to infinity and draw that lines
			if (usedAxes[1] != -1)
			{
				scaleVec3(gridMat[unusedAxis], snap * halfRange, half[0]);
				subVec3(transGizmo->activeMat[3], half[0], half[1]);
				addVec3(transGizmo->activeMat[3], half[0], half[0]);
				gfxDrawLine3DWidthARGB(half[0], half[1], axisColor(unusedAxis), axisColor(unusedAxis), 1);
			}

			// draw rest of grid
			if (usedAxes[1] == -1)
			{
				scaleVec3(gridMat[usedAxes[0]], snap * halfRange, half[0]);
				scaleVec3(transGizmo->tickDirection, snap * halfRange, half[1]);
			}
			else
			{
				scaleVec3(gridMat[usedAxes[0]], snap * halfRange, half[0]);
				scaleVec3(gridMat[usedAxes[1]], snap * halfRange, half[1]);
			}
			for (j = 0; j < numAxes; j++)
			{
				if (numAxes == 1)
				{
					scaleVec3(half[0], -1, lineEnd);
					addVec3(half[0], gridMat[3], lineBegin);
					addVec3(lineEnd, gridMat[3], lineEnd);
					gfxDrawLine3DWidthARGB(lineBegin, lineEnd, axisColor(usedAxes[0]), axisColor(usedAxes[0]), 4);
				}
				if(!transGizmo->hide_grid)
				{
					for (i = -halfRange; i <= halfRange; i++)
					{
						int color;

						scaleVec3(gridMat[usedAxes[j]], i * snap, lineMid);
						addVec3(lineMid, half[!j], lineBegin);
						subVec3(lineMid, half[!j], lineEnd);
						addVec3(lineBegin, gridMat[3], lineBegin);
						addVec3(lineEnd, gridMat[3], lineEnd);
						addVec3(lineMid, gridMat[3], lineMid);

						if (i == 0)
						{
							if (numAxes > 1)
								color = axisColor(usedAxes[!j]);
							else
								color = axisColor(unusedAxis);
						}
						else if (i == transGizmo->currentSnapCoords[usedAxes[j]] && (transGizmo->specSnap == EditSnapGrid || (transGizmo->specSnap == EditSnapSmart && transGizmo->ignoreSmartSnap)))
							color = 0xFFFFFF00;
						else if (abs(i) % 10 == 0)
							color = 0xAA000000;
						else if (abs(i) % 5 == 0)
							color = 0x66000000;
						else
							color = 0x66FFFFFF;

						gfxDrawLine3DWidthARGB(lineBegin, lineEnd, color, color, 1);
					}
				}

				// draw freeform yellow line when snap is not on
				if (transGizmo->specSnap == EditSnapNone)
				{
					Vec3 buf;
					int yellow = ARGBFromColor(ColorYellow);

					subVec3(transGizmo->activeMat[3], gridMat[3], buf);
					scaleVec3(gridMat[usedAxes[j]], dotVec3(buf, gridMat[usedAxes[j]]), lineMid);

					addVec3(lineMid, half[!j], lineBegin);
					subVec3(lineMid, half[!j], lineEnd);
					addVec3(lineBegin, gridMat[3], lineBegin);
					addVec3(lineEnd, gridMat[3], lineEnd);
					addVec3(lineMid, gridMat[3], lineMid);
					gfxDrawLine3DWidthARGB(lineBegin, lineEnd, yellow, yellow, 1);
				}
			}
		}
	}

	// draw gizmo normally
	else
	{
		Vec3 p[3], corner1[3], corner2[3];

		// calculate the corner box points
		//   | 
		// c1|_p
		//   |_|__
		//     c2
		for (i = 0; i < 3; i++)
		{
			scaleVec3(transGizmo->drawMat[i], (transGizmo->boxLength), corner1[i]);
			scaleVec3(transGizmo->drawMat[(i + 1) % 3], (transGizmo->boxLength), corner2[i]);
			addVec3(corner1[i], corner2[i], p[i]);
			addVec3(corner1[i], transGizmo->drawMat[3], corner1[i]);
			addVec3(corner2[i], transGizmo->drawMat[3], corner2[i]);
			addVec3(p[i], transGizmo->drawMat[3], p[i]);
		}

		for (i = 0; i < 3; i++)
		{
			Vec3 tail;
			int headColor, lineColor;
			int highlightColor = (transGizmo->mousedOver ? ARGBFromColor(ColorYellow) : ARGBFromColor(ColorOrange));

			// draw the axis arrow
			headColor = axisColor(i);
			if (transGizmo->highlight[i])
				lineColor = highlightColor;
			else
				lineColor = headColor;
			scaleVec3(transGizmo->drawMat[i], transGizmo->boxLength * axisTailBoxScale, tail);
			addVec3(transGizmo->drawMat[3], tail, tail);
			drawLineArrow(tail, transGizmo->drawMat[i], transGizmo->drawMat[(i + 1) % 3], transGizmo->arrowLength - transGizmo->boxLength * axisTailBoxScale, headColor, lineColor);
			scaleVec3(transGizmo->drawMat[i], transGizmo->boxLength * axisOriginBoxScale, tail);
			addVec3(transGizmo->drawMat[3], tail, tail);
			gfxDrawLine3DWidthARGB(transGizmo->drawMat[3], tail, lineColor, lineColor, 1);

			// draw the double axis box lines touching the current axis
			if (transGizmo->highlight[i] && transGizmo->highlight[(i + 1) % 3])
				lineColor = highlightColor;
			else
				lineColor = headColor;
			gfxDrawLine3DWidthARGB(p[i], corner1[i], lineColor, lineColor, 1);

			if (transGizmo->highlight[i] && transGizmo->highlight[(i + 2) % 3])
				lineColor = highlightColor;
			else
				lineColor = headColor;
			gfxDrawLine3DWidthARGB(corner1[i], p[(i + 2) % 3], lineColor, lineColor, 1);
		}
	}

	gfxSetPrimIgnoreMax(false);
	gfxSetPrimZTest(1);
}

EditLibGizmosToolbar *TranslateGizmoGetToolbar(TranslateGizmo *transGizmo)
{
	return transGizmo->toolbar;
}

void TranslateGizmoSetToolbar(TranslateGizmo *transGizmo, EditLibGizmosToolbar *toolbar)
{
	transGizmo->toolbar = toolbar;
}

void TranslateGizmoSetTriGetter(TranslateGizmo *transGizmo, AutoSnapTriGetter triF)
{
	if (transGizmo->autoSnapGizmo == &elAutoSnapGizmo)
		transGizmo->autoSnapGizmo = AutoSnapGizmoCreate();
	transGizmo->autoSnapGizmo->snapTriFunc = triF;
}

void TranslateGizmoSetTerrainF(TranslateGizmo *transGizmo, AutoSnapPointGetter terrainF)
{
	if (transGizmo->autoSnapGizmo == &elAutoSnapGizmo)
		transGizmo->autoSnapGizmo = AutoSnapGizmoCreate();
	transGizmo->autoSnapGizmo->snapTerrainFunc = terrainF;
}

void TranslateGizmoSetCallbackContext(TranslateGizmo* transGizmo, void *context)
{
	transGizmo->funcContext = context;
}

void TranslateGizmoSetActivateCallback(TranslateGizmo* transGizmo, GizmoActivateFn activateFunc)
{
	transGizmo->activateFunc = activateFunc;
}

void TranslateGizmoSetDeactivateCallback(TranslateGizmo* transGizmo, GizmoDeactivateFn deactivateFunc)
{
	transGizmo->deactivateFunc = deactivateFunc;
}

void TranslateGizmoGetMatrix(TranslateGizmo* transGizmo, Mat4 mat)
{
	copyMat4(transGizmo->activeMat, mat);
}

void TranslateGizmoSetMatrix(TranslateGizmo* transGizmo, const Mat4 startMat)
{
	if (!checkMatOutOfBounds(startMat))
	{
		copyMat4(startMat, transGizmo->startMat);
		copyMat4(startMat, transGizmo->activeMat);
	}
}

void TranslateGizmoSetAlignedToWorld(TranslateGizmo *transGizmo, bool aligned)
{
	if (!TranslateGizmoIsActive(transGizmo))
	{
		transGizmo->alignedToWorld = aligned;
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
}

bool TranslateGizmoGetAlignedToWorld(TranslateGizmo *transGizmo)
{
	return transGizmo->alignedToWorld;
}

void TranslateGizmoSetSpecSnap(TranslateGizmo *transGizmo, EditSpecialSnapMode mode)
{
	if (!TranslateGizmoIsActive(transGizmo))
	{
		transGizmo->specSnap = mode;
		if (mode != EditSnapNone)
			transGizmo->lastSnap = mode;
		if (mode == EditSnapGrid || mode == EditSnapNone)
		{
			int axis1, axis2;
			TranslateGizmoGetUsedAxes(transGizmo, &axis1, &axis2);
			if (axis1 == -1 && axis2 == -1)
				TranslateGizmoSetAxes(transGizmo, true, false, true);
		}
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
}

void TranslateGizmoSetHideGrid(TranslateGizmo *transGizmo, bool hide)
{
	transGizmo->hide_grid = hide;
}

EditSpecialSnapMode TranslateGizmoGetSpecSnap(TranslateGizmo *transGizmo)
{
	return transGizmo->specSnap;
}

EditSpecialSnapMode TranslateGizmoGetLastSnap(TranslateGizmo *transGizmo)
{
	return transGizmo->lastSnap;
}

void TranslateGizmoSetSnapClamp(TranslateGizmo *transGizmo, bool clamp)
{
	transGizmo->specSnapRestrict = clamp;
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}

bool TranslateGizmoGetSnapClamp(TranslateGizmo *transGizmo)
{
	return transGizmo->specSnapRestrict;
}

void TranslateGizmoSetSnapNormal(TranslateGizmo *transGizmo, bool snap)
{
	transGizmo->snapNormal = snap;
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}

void TranslateGizmoSetSnapNormalInverse(TranslateGizmo *transGizmo, bool snapInverse)
{
	transGizmo->snapNormalInverse = snapInverse;
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}

void TranslateGizmoSetSnapNormalAxis(TranslateGizmo *transGizmo, int axis)
{
	if (axis >= 0 && axis < 3)
	{
		transGizmo->snapNormalAxis = axis;
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
}

bool TranslateGizmoGetSnapNormal(TranslateGizmo *transGizmo)
{
	return transGizmo->snapNormal;
}

bool TranslateGizmoGetSnapNormalInverse(TranslateGizmo *transGizmo)
{
	return transGizmo->snapNormalInverse;
}

int TranslateGizmoGetSnapNormalAxis(TranslateGizmo *transGizmo)
{
	return transGizmo->snapNormalAxis;
}

bool TranslateGizmoIsActive(TranslateGizmo* transGizmo)
{
	return transGizmo->activated;
}

void TranslateGizmoSetSnapResolution(TranslateGizmo *transGizmo, int res)
{
	transGizmo->translationSnapResolution = CLAMP(res, 1, GIZMO_NUM_WIDTHS);
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}

int TranslateGizmoGetSnapResolution(TranslateGizmo *transGizmo)
{
	return transGizmo->translationSnapResolution;
}

bool TranslateGizmoIsSnapEnabled(TranslateGizmo* transGizmo)
{
	return (transGizmo->specSnap != EditSnapNone);
}

bool TranslateGizmoIsDragEnabled(TranslateGizmo* transGizmo)
{
	return transGizmo->relativeDragMode;
}

void TranslateGizmoToggleRelativeDrag(TranslateGizmo* transGizmo)
{
	transGizmo->relativeDragMode = !transGizmo->relativeDragMode;
}

void TranslateGizmoSetRelativeDrag(TranslateGizmo* transGizmo, bool relDrag)
{
	transGizmo->relativeDragMode = relDrag;
}

void TranslateGizmoSetAxes(TranslateGizmo* transGizmo, bool axisX, bool axisY, bool axisZ)
{
	bool oldx, oldy, oldz;
	oldx = transGizmo->translationAxesUsed[0];
	oldy = transGizmo->translationAxesUsed[1];
	oldz = transGizmo->translationAxesUsed[2];

	transGizmo->translationAxesUsed[0] = axisX;
	transGizmo->translationAxesUsed[1] = axisY;
	transGizmo->translationAxesUsed[2] = axisZ;

	// if validation fails, revert
	if ((transGizmo->translationAxesUsed[0] && transGizmo->translationAxesUsed[1] && transGizmo->translationAxesUsed[2]) ||
		(!transGizmo->translationAxesUsed[0] && !transGizmo->translationAxesUsed[1] && !transGizmo->translationAxesUsed[2]))
	{
		transGizmo->translationAxesUsed[0] = oldx;
		transGizmo->translationAxesUsed[1] = oldy;
		transGizmo->translationAxesUsed[2] = oldz;
	}
	else
	{
		transGizmo->snapAlongAxes = true;
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
}

void TranslateGizmoToggleAxes(TranslateGizmo* transGizmo, bool toggleX, bool toggleY, bool toggleZ)
{
	bool oldx, oldy, oldz;
	oldx = transGizmo->translationAxesUsed[0];
	oldy = transGizmo->translationAxesUsed[1];
	oldz = transGizmo->translationAxesUsed[2];

	if (toggleX)
		transGizmo->translationAxesUsed[0] = !transGizmo->translationAxesUsed[0];
	if (toggleY)
		transGizmo->translationAxesUsed[1] = !transGizmo->translationAxesUsed[1];
	if (toggleZ)
		transGizmo->translationAxesUsed[2] = !transGizmo->translationAxesUsed[2];

	// if validation fails, revert
	if ((transGizmo->translationAxesUsed[0] && transGizmo->translationAxesUsed[1] && transGizmo->translationAxesUsed[2]) ||
		(!transGizmo->translationAxesUsed[0] && !transGizmo->translationAxesUsed[1] && !transGizmo->translationAxesUsed[2]))
	{
		transGizmo->translationAxesUsed[0] = oldx;
		transGizmo->translationAxesUsed[1] = oldy;
		transGizmo->translationAxesUsed[2] = oldz;
	}
	else
	{
		transGizmo->snapAlongAxes = true;
		if (transGizmo->toolbar)
			elGizmosToolbarUpdate(transGizmo->toolbar);
	}
}

void TranslateGizmoCycleAxes(TranslateGizmo* transGizmo)
{
	int x = transGizmo->translationAxesUsed[0];
	int y = transGizmo->translationAxesUsed[1];
	int z = transGizmo->translationAxesUsed[2];
	if (x && !y && !z)
		transGizmo->translationAxesUsed[1] = 1;
	else if (x && y && !z)
		transGizmo->translationAxesUsed[0] = 0;
	else if (!x && y && !z)
		transGizmo->translationAxesUsed[2] = 1;
	else if (!x && y && z)
		transGizmo->translationAxesUsed[1] = 0;
	else if (!x && !y && z)
		transGizmo->translationAxesUsed[0] = 1;
	else if (x && !y && z)
		transGizmo->translationAxesUsed[2] = 0;
	transGizmo->snapAlongAxes = true;
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}

void TranslateGizmoGetAxesEnabled(TranslateGizmo *transGizmo, bool *x, bool *y, bool *z)
{
	bool snapAlongAxes = (transGizmo->specSnap == EditSnapNone || transGizmo->specSnap == EditSnapGrid) || transGizmo->snapAlongAxes;
	*x = transGizmo->translationAxesUsed[0] && snapAlongAxes;
	*y = transGizmo->translationAxesUsed[1] && snapAlongAxes;
	*z = transGizmo->translationAxesUsed[2] && snapAlongAxes;
}

bool TranslateGizmoIsSnapAlongAxes(TranslateGizmo* transGizmo)
{
	return transGizmo->snapAlongAxes;
}

// This function turns off snap-to-axis/axes 
void TranslateGizmoDisableAxes(TranslateGizmo* transGizmo)
{
	transGizmo->snapAlongAxes = false;
	if (transGizmo->toolbar)
		elGizmosToolbarUpdate(transGizmo->toolbar);
}
/*****************************************/
/*             CONE GIZMO                */
/*****************************************/
void ConeGizmoDraw(ConeGizmo *gizmo, Mat4 mat, float radius, float halfAngle, Color c)
{
	Vec3 dir;
	Color paleC = colorFromRGBA((RGBAFromColor(c) & 0xFFFFFF00) | 0x00000055);
	Color brightC = colorFromRGBA((RGBAFromColor(c) & 0xFFFFFF00) | 0x000000FF);

	gfxSetPrimIgnoreMax(true);

	// when the gizmo is being dragged
	if (gizmo->active)
	{
		Mat4 cam;
		Vec3 camDir, circPlane, circNormal;
		Vec3 lineEnd1, lineEnd2;
		Vec3 arrowTail;
		float r;

		// draw the tickmarked circle
		gfxGetActiveCameraMatrix(cam);
		r = 0.15 * distance3(cam[3], mat[3]);
		subVec3(mat[3], cam[3], camDir);
		crossVec3(mat[1], camDir, circPlane);
		crossVec3(circPlane, mat[1], circNormal);
		normalVec3(circPlane);
		gfxSetPrimZTest(0);
		drawFullCircleShaded(mat[3], circNormal, mat[2], 0.15, 0xFFFFFFFF, 0, angleSnaps[MAX((int) gizmo->snapResolution - 1, 0)]);

		// draw the movement direction arrows
		scaleVec3(mat[1], -r, arrowTail);
		addVec3(arrowTail, mat[3], arrowTail);
		copyVec3(circPlane, dir);
		copyVec3(circPlane, gizmo->arrowDir);
		normalVec3(gizmo->arrowDir);
		drawLineArrow(arrowTail, dir, circNormal, r / 4, 0xFF00DD00, 0xFF00DD00);
		scaleVec3(dir, -1, dir);
		drawLineArrow(arrowTail, dir, circNormal, r / 4, 0xFFFF0000, 0xFFFF0000);

		// draw the cone boundary lines
		scaleVec3(circPlane, r * sin(halfAngle), circPlane);
		scaleVec3(mat[1], -r * cos(halfAngle), dir);
		addVec3(circPlane, dir, lineEnd1);
		subVec3(dir, circPlane, lineEnd2);
		addVec3(lineEnd1, mat[3], lineEnd1);
		addVec3(lineEnd2, mat[3], lineEnd2);
		gfxDrawLine3D(mat[3], lineEnd1, brightC);
		gfxDrawLine3D(mat[3], lineEnd2, brightC);
		gfxSetPrimZTest(1);
	}

	// draw the current direction vector
	copyVec3(mat[1], dir);
	scaleVec3(dir, -1, dir);
	drawLineArrow(mat[3], dir, mat[2], radius, ARGBFromColor(brightC), ARGBFromColor(brightC));

	// draw the cone
	gfxDrawCone3D(mat, radius, halfAngle, -1, paleC);
	gfxSetPrimIgnoreMax(false);
}

void ConeGizmoHandleInput(ConeGizmo *gizmo, float startAngle)
{
	gizmo->startAngle = startAngle;
	gizmo->mouseDiff[0] = 0;
	gizmo->mouseDiff[1] = 0;
}

void ConeGizmoDrag(ConeGizmo *gizmo, float *halfAngle)
{
	int dx, dy;
	Vec2 arrowScreenDir;
	Mat4 cam;

	if (!gizmo->active)
		return;

	mouseDiffLegacy(&dx,&dy);
	gfxGetActiveCameraMatrix(cam);
	gizmo->mouseDiff[0] += dx;
	gizmo->mouseDiff[1] += dy;
	arrowScreenDir[0] = dotVec3(gizmo->arrowDir, cam[0]);
	arrowScreenDir[1] = dotVec3(gizmo->arrowDir, cam[1]);
	*halfAngle = gizmo->startAngle - (dotVec2(arrowScreenDir, gizmo->mouseDiff) / 3);
	if (gizmo->snapEnabled)
		*halfAngle = GizmoSnapAngle((*halfAngle / 360) * TWOPI, gizmo->snapResolution) * 360 / TWOPI;
	if (*halfAngle < 0)
		*halfAngle = 0;
	else if (*halfAngle > 180)
		*halfAngle = 180;
}

void ConeGizmoActivate(ConeGizmo *gizmo, bool active)
{
	gizmo->active = active;
}

bool ConeGizmoGetActive(ConeGizmo *gizmo)
{
	return gizmo->active;
}

bool ConeGizmoIsSnapEnabled(ConeGizmo *gizmo)
{
	return gizmo->snapEnabled;
}

void ConeGizmoEnableSnap(ConeGizmo *gizmo, bool enabled)
{
	gizmo->snapEnabled = enabled;
}

int ConeGizmoGetSnapResolution(ConeGizmo *gizmo)
{
	return gizmo->snapResolution;
}

void ConeGizmoSetSnapResolution(ConeGizmo *gizmo, int res)
{
	gizmo->snapResolution = CLAMP(res, 1, GIZMO_NUM_ANGLES);
}

/*****************************************/
/*            PYRAMID GIZMO              */
/*****************************************/
static void findPyramidCorners(Mat4 mat, float radius, float halfAngle1, float halfAngle2, Mat4 results)
{
	float x1, x2, y;
	Vec3 sideVec1, sideVec2, temp;
	float tan1 = tanf(halfAngle1), tan2 = tanf(halfAngle2);
	x1 = radius * tan1 / sqrtf(1.0f + powf(tan1, 2) + powf(tan2, 2));
	x2 = x1 * tan2 / tan1;
	y = x1 / tan1;

	scaleVec3(mat[1], -y, temp);
	addVec3(mat[3], temp, temp);
	scaleVec3(mat[0], x1, sideVec1);
	scaleVec3(mat[2], x2, sideVec2);
	addVec3(sideVec1, sideVec2, results[0]);
	subVec3(sideVec1, sideVec2, results[1]);
	scaleVec3(sideVec1, -1, results[2]);
	addVec3(results[2], sideVec2, results[3]);
	subVec3(results[2], sideVec2, results[2]);
	addVec3(results[0], temp, results[0]);
	addVec3(results[1], temp, results[1]);
	addVec3(results[2], temp, results[2]);
	addVec3(results[3], temp, results[3]);
}

void PyramidGizmoDraw(PyramidGizmo *gizmo, Mat4 mat, float radius, float halfAngle1, float halfAngle2, Color c)
{
	Vec3 dir;
	Color paleC = colorFromRGBA((RGBAFromColor(c) & 0xFFFFFF00) | 0x00000055);
	Color brightC = colorFromRGBA((RGBAFromColor(c) & 0xFFFFFF00) | 0x000000FF);
	Mat4 corners;

	gfxSetPrimIgnoreMax(true);

	findPyramidCorners(mat, radius, halfAngle1, halfAngle2, corners);

	// when the gizmo is being dragged
	if (gizmo->active)
	{
		Mat4 cam;
		Vec3 circPlane, circNormal;
		Vec3 lineEnd1, lineEnd2;
		Vec3 arrowTail;
		float r;

		// draw the tickmarked circle
		gfxGetActiveCameraMatrix(cam);
		r = 0.15 * distance3(cam[3], mat[3]);
		copyVec3(gizmo->angleActive == 2 ? mat[2] : mat[0], circPlane);
		copyVec3(gizmo->angleActive == 2 ? mat[0] : mat[2], circNormal);
		drawFullCircleShaded(mat[3], circNormal, mat[1], 0.15, 0xFFFFFFFF, 0, angleSnaps[MAX((int) gizmo->snapResolution - 1, 0)]);

		// draw the movement direction arrows
		scaleVec3(mat[1], -r, arrowTail);
		addVec3(arrowTail, mat[3], arrowTail);
		copyVec3(circPlane, dir);
		copyVec3(dir, gizmo->arrowDir);
		normalVec3(gizmo->arrowDir);
		drawLineArrow(arrowTail, dir, circNormal, r / 4, 0xFF00DD00, 0xFF00DD00);
		scaleVec3(dir, -1, dir);
		drawLineArrow(arrowTail, dir, circNormal, r / 4, 0xFFFF0000, 0xFFFF0000);

		// draw the angular boundary lines
		scaleVec3(circPlane, r * sin(gizmo->angleActive == 2 ? halfAngle2 : halfAngle1), circPlane);
		scaleVec3(mat[1], -r * cos(gizmo->angleActive == 2 ? halfAngle2 : halfAngle1), dir);
		addVec3(circPlane, dir, lineEnd1);
		subVec3(dir, circPlane, lineEnd2);
		addVec3(lineEnd1, mat[3], lineEnd1);
		addVec3(lineEnd2, mat[3], lineEnd2);
		gfxDrawLine3D(mat[3], lineEnd1, brightC);
		gfxDrawLine3D(mat[3], lineEnd2, brightC);
		gfxSetPrimZTest(1);
	}
	else
	{
		Vec3 start, end, intersection;
		Mat3 tri;
		int i, interI = -1;
		float dist = -1;

		editLibCursorRay(start, end);

		// check for intersection with each side of the pyramid
		for(i = 0; i < 4; i++)
		{
			Vec3 norm, tempInter, end1, end2;
			float tempDist;
			copyVec3(mat[3], tri[0]);
			copyVec3(corners[i], tri[1]);
			copyVec3(corners[(i + 1) % 4], tri[2]);
			subVec3(corners[i], mat[3], end1);
			subVec3(corners[(i + 1) % 4], mat[3], end2);
			crossVec3(end1, end2, norm);
			findPlaneLineIntersection(mat[3], norm, start, end, tempInter);
			tempDist = distance3(tempInter, mat[3]);
			if (pointInTri(tempInter, tri) && (tempDist < dist || interI == -1))
			{
				copyVec3(tempInter, intersection);
				dist = tempDist;
				interI = i;
			}
		}

		// render the active sides with a yellow highlight
		if (interI == 0 || interI == 2 || (interI == -1 && gizmo->angleActive == 1))
		{
			copyVec3(mat[3], tri[0]);
			copyVec3(corners[0], tri[1]);
			copyVec3(corners[1], tri[2]);
			drawTriSlice(tri, ColorYellow, 20);
			copyVec3(mat[3], tri[0]);
			copyVec3(corners[2], tri[1]);
			copyVec3(corners[3], tri[2]);
			drawTriSlice(tri, ColorYellow, 20);
		}
		else if (interI == 1 || interI == 3 || (interI == -1 && gizmo->angleActive == 2))
		{
			copyVec3(mat[3], tri[0]);
			copyVec3(corners[1], tri[1]);
			copyVec3(corners[2], tri[2]);
			drawTriSlice(tri, ColorYellow, 20);
			copyVec3(mat[3], tri[0]);
			copyVec3(corners[3], tri[1]);
			copyVec3(corners[0], tri[2]);
			drawTriSlice(tri, ColorYellow, 20);
		}

		if (interI == 0 || interI == 2)
			gizmo->angleOver = 1;
		else if (interI == 1 || interI == 3)
			gizmo->angleOver = 2;
		else
			gizmo->angleOver = 0;
	}

	// draw the current direction vector
	copyVec3(mat[1], dir);
	scaleVec3(dir, -1, dir);
	drawLineArrow(mat[3], dir, mat[2], radius, ARGBFromColor(brightC), ARGBFromColor(brightC));

	// draw the pyramid
	gfxDrawPyramid3D(mat, 0, radius, halfAngle1, halfAngle2, ColorTransparent, paleC);

	gfxSetPrimIgnoreMax(false);
}

void PyramidGizmoHandleInput(PyramidGizmo *gizmo, float startAngle1, float startAngle2)
{
	if (gizmo->angleOver)
		gizmo->angleActive = gizmo->angleOver;
	gizmo->startAngle = gizmo->angleActive == 2 ? startAngle2 : startAngle1;
	gizmo->mouseDiff[0] = 0;
	gizmo->mouseDiff[1] = 0;
}

void PyramidGizmoDrag(PyramidGizmo *gizmo, float *halfAngle1, float *halfAngle2)
{
	int dx, dy;
	Vec2 arrowScreenDir;
	Mat4 cam;
	float *halfAngle = gizmo->angleActive == 2 ? halfAngle2 : halfAngle1;

	if (!gizmo->active || !gizmo->angleActive)
		return;

	mouseDiffLegacy(&dx,&dy);
	gfxGetActiveCameraMatrix(cam);
	gizmo->mouseDiff[0] += dx;
	gizmo->mouseDiff[1] += dy;
	arrowScreenDir[0] = dotVec3(gizmo->arrowDir, cam[0]);
	arrowScreenDir[1] = dotVec3(gizmo->arrowDir, cam[1]);
	*halfAngle = gizmo->startAngle - (dotVec2(arrowScreenDir, gizmo->mouseDiff) / 3);
	if (gizmo->snapEnabled)
		*halfAngle = GizmoSnapAngle((*halfAngle / 360) * TWOPI, gizmo->snapResolution) * 360 / TWOPI;
	if (*halfAngle < 0)
		*halfAngle = 0;
	else if (*halfAngle > 180)
		*halfAngle = 180;
}

void PyramidGizmoActivate(PyramidGizmo *gizmo, bool active)
{
	gizmo->active = active;
}

bool PyramidGizmoGetActive(PyramidGizmo *gizmo)
{
	return gizmo->active;
}

bool PyramidGizmoIsSnapEnabled(PyramidGizmo *gizmo)
{
	return gizmo->snapEnabled;
}

void PyramidGizmoEnableSnap(PyramidGizmo *gizmo, bool enabled)
{
	gizmo->snapEnabled = enabled;
}

int PyramidGizmoGetSnapResolution(PyramidGizmo *gizmo)
{
	return gizmo->snapResolution;
}

void PyramidGizmoSetSnapResolution(PyramidGizmo *gizmo, int res)
{
	gizmo->snapResolution = CLAMP(res, 1, GIZMO_NUM_ANGLES);
}

/*****************************************/
/*            RADIAL GIZMO               */
/*****************************************/
static void findAxisIntersection(Mat4 mat, Vec3 axisDir, Vec3 tickDir, Vec3 start, Vec3 end, Vec3 intersection)
{
	Vec3 norm;
	crossVec3(axisDir, tickDir, norm);
	findPlaneLineIntersection(mat[3], norm, start, end, intersection);
}

void RadialGizmoDraw(RadialGizmo *gizmo, Vec3 pos, float radius, Color color, bool drawSphere)
{
	Mat4 cam;
	Vec3 camDir;

	gfxSetPrimIgnoreMax(true);

	gfxGetActiveCameraMatrix(cam);
	subVec3(pos, cam[3], camDir);

	// when the gizmo is being dragged
	if (gizmo->active)
	{
		Vec3 endP, tickDir, tickLeft, tickRight, tickTemp;
		float length = 2 * distance3(cam[3], pos);
		int i, div;
		float scaleInt = GizmoGetSnapWidth(gizmo->snapResolution);
		int baseColor = 0xFFFF0000;
		Color c;

		// draw tickmarks
		gfxSetPrimZTest(0);
		crossVec3(gizmo->axisDir, camDir, tickDir);
		normalVec3(tickDir);
		copyVec3(tickDir, gizmo->tickDir);
		copyVec3(pos, tickLeft);
		copyVec3(pos, tickRight);
		scaleVec3(tickDir, length / 25, endP);
		addVec3(endP, tickLeft, endP);
		c = colorFromRGBA(0x000000FF);
		gfxDrawLine3D(pos, endP, c);
		i = 1;
		while((float)i * scaleInt < length)
		{
			if (i % 10 == 0)
			{
				c = colorFromRGBA(baseColor | 0x000000FF);
				div = 50;
			}
			else if (i % 5 == 0)
			{
				c = colorFromRGBA(baseColor | 0x00000099);
				div = 100;
			}
			else
			{
				c = colorFromRGBA(baseColor | 0x00000044);
				div = 200;
			}
			scaleVec3(gizmo->axisDir, (float) i * scaleInt, tickRight);
			addVec3(tickRight, pos, tickRight);
			scaleVec3(gizmo->axisDir, (float) -i * scaleInt, tickLeft);
			addVec3(tickLeft, pos, tickLeft);
			scaleVec3(tickDir, length / div, tickTemp);
			addVec3(tickRight, tickTemp, endP);
			gfxDrawLine3D(tickRight, endP, c);
			addVec3(tickLeft, tickTemp, endP);
			gfxDrawLine3D(tickLeft, endP, c);
			i++;
		}

		// draw the scale line
		c = colorFromRGBA(baseColor | 0x000000FF);
		length = (float)(i - 1) * scaleInt;
		scaleVec3(gizmo->axisDir, length, endP);
		addVec3(pos, endP, endP);
		gfxDrawLine3D(pos, endP, c);

		scaleVec3(gizmo->axisDir, -length, endP);
		addVec3(pos, endP, endP);
		gfxDrawLine3D(pos, endP, c);

		// draw radius indicator
		scaleVec3(tickDir, length / 50, tickDir);
		scaleVec3(gizmo->axisDir, radius, endP);
		addVec3(pos, endP, endP);
		addVec3(endP, tickDir, tickTemp);
		subVec3(endP, tickDir, endP);
		gfxDrawLine3D(endP, tickTemp, ColorRed);
		scaleVec3(gizmo->axisDir, -radius, endP);
		addVec3(pos, endP, endP);
		addVec3(endP, tickDir, tickTemp);
		subVec3(endP, tickDir, endP);
		gfxDrawLine3D(endP, tickTemp, ColorRed);
		gfxSetPrimZTest(1);

		gfxDrawSphere3D(pos, radius, -1, colorFromRGBA(0xFFFFFF33), 1);
	}
	else
	{
		Vec3 mouseInt;
		Vec3 start, end;

		// find the direction of the axis
		editLibCursorRay(start, end);
		findPlaneLineIntersection(pos, camDir, start, end, mouseInt);
		subVec3(mouseInt, pos, gizmo->axisDir);
		normalVec3(gizmo->axisDir);

		// find the direction of the ticks
		crossVec3(gizmo->axisDir, camDir, gizmo->tickDir);
		normalVec3(gizmo->tickDir);

		// find the "starting point" of the mouse along the axis
		gizmo->clickedRadius = distance3(mouseInt, pos);

		if (drawSphere)
		{
			gfxDrawSphere3D(pos, radius, -1, color, 1);
		}
	}

	gfxSetPrimIgnoreMax(false);
}

void RadialGizmoHandleInput(RadialGizmo *gizmo, Vec3 pos, float startRadius)
{
	if (!gizmo->relativeDragMode)
	{
		gizmo->startRadius = gizmo->clickedRadius;
	}
	else
	{
		Vec3 norm, start, end;
		crossVec3(gizmo->axisDir, gizmo->tickDir, norm);
		editLibCursorRay(start, end);
		findPlaneLineIntersection(pos, norm, start, end, gizmo->intersection);
		copyVec3(gizmo->intersection, start);
		addVec3(pos, gizmo->axisDir, end);
		findPointLineIntersection(pos, end, start, gizmo->intersection);
		gizmo->startRadius = startRadius;
	}
}

void RadialGizmoDrag(RadialGizmo *gizmo, Vec3 pos, float *radius)
{
	Vec3 norm, start, end, intersection;

	if (!gizmo->active)
	{
		return;
	}

	editLibCursorRay(start, end);
	crossVec3(gizmo->axisDir, gizmo->tickDir, norm);
	findPlaneLineIntersection(pos, norm, start, end, intersection);
	copyVec3(intersection, start);
	addVec3(pos, gizmo->axisDir, end);
	findPointLineIntersection(pos, end, start, intersection);

	if (!gizmo->relativeDragMode)
	{
		*radius = distance3(intersection, pos);
	}
	else
	{
		Vec3 oldInt, newInt;
		float newDist = distance3(intersection, pos);
		float oldDist = distance3(gizmo->intersection, pos);
		float snapWidth;

		subVec3(intersection, pos, newInt);
		subVec3(gizmo->intersection, pos, oldInt);

		// opposite ends of the axes
		if (dotVec3(newInt, oldInt) < 0)
		{
			*radius = gizmo->startRadius  - (newDist + oldDist);
		}
		// same side of the axes
		else
		{
			*radius = gizmo->startRadius + (newDist - oldDist);
		}

		// snap radius if appropriate
		if (gizmo->snapEnabled)
		{
			snapWidth = GizmoGetSnapWidth(gizmo->snapResolution);
			*radius = (int)(*radius / snapWidth) * snapWidth;
		}

		if (*radius < 0)
		{
			*radius = 0;
		}
	}
}

void RadialGizmoActivate(RadialGizmo *gizmo, bool active)
{
	gizmo->active = active;
}

bool RadialGizmoGetActive(RadialGizmo *gizmo)
{
	return gizmo->active;
}

void RadialGizmoSetRelativeDrag(RadialGizmo *gizmo, bool drag)
{
	gizmo->relativeDragMode = drag;
}

void RadialGizmoEnableSnap(RadialGizmo *gizmo, bool enabled)
{
	gizmo->snapEnabled = enabled;
}

int RadialGizmoGetSnapResolution(RadialGizmo *gizmo)
{
	return gizmo->snapResolution;
}

void RadialGizmoSetSnapResolution(RadialGizmo *gizmo, int res)
{
	gizmo->snapResolution = CLAMP(res, 1, GIZMO_NUM_WIDTHS);
}

/*****************************************/
/*            RADIAL GIZMO               */
/*****************************************/

ScaleMinMaxGizmo *ScaleMinMaxGizmoCreate(void)
{
	ScaleMinMaxGizmo* newGizmo = calloc(1, sizeof(ScaleMinMaxGizmo));
	return newGizmo;
}

void ScaleMinMaxGizmoDestroy(ScaleMinMaxGizmo* scaleGizmo)
{
	SAFE_FREE(scaleGizmo);
}

static F32 ScaleMinMaxGizmoSnapToGrid(ScaleMinMaxGizmo* scaleGizmo, F32 val)
{
	if(scaleGizmo->snapResolution >= 0)
	{
		F32 snap = GizmoGetSnapWidth((int) scaleGizmo->snapResolution);
		val = roundFloatWithPrecision(val, snap);
	}
	return val;
}

void ScaleMinMaxGizmoUpdate(ScaleMinMaxGizmo* scaleGizmo)
{
	int i;
	Vec3 start, end;
	Vec3 volMid, volMin, volMax;
	Vec3 absMid;
	Vec3 boxPos;
	Mat4 cam;
	bool overAxis = false;
	bool firstFrameActive = false;

	gfxGetActiveCameraMatrix(cam);
	editLibCursorRay(start, end);

	addVec3(scaleGizmo->activeMatrix[3], scaleGizmo->scaleMinMax[0], volMin);
	addVec3(scaleGizmo->activeMatrix[3], scaleGizmo->scaleMinMax[1], volMax);
	addVec3(volMin, volMax, volMid);
	scaleVec3(volMid, 0.5f, volMid);
	subFromVec3(scaleGizmo->activeMatrix[3], volMid);
	mulVecMat3(volMid, scaleGizmo->activeMatrix, absMid);
	addToVec3(scaleGizmo->activeMatrix[3], volMid);
	addToVec3(scaleGizmo->activeMatrix[3], absMid);

	if (!scaleGizmo->activated)
	{
		F32 nearestDist = -1;
		scaleGizmo->selectedAxis = -1;
		for (i=0; i<6; ++i) 
		{
			Vec3 hit;
			int idx = i%3;
			F32 halfBoxScale = 0.01f;
			F32 fardist;

			scaleVec3(scaleGizmo->activeMatrix[idx], volMax[idx]-volMid[idx], boxPos);
			if(i<3)
				scaleVec3(boxPos, -1.0f, boxPos);
			addToVec3(absMid, boxPos);

			fardist = distance3(cam[3], boxPos);
			halfBoxScale *= fardist; 

			if(i<3)
				boxPos[idx] -= halfBoxScale*1.01f;
			else 
				boxPos[idx] += halfBoxScale*1.01f;

			if(sphereLineCollisionWithHitPoint( start, end, boxPos, halfBoxScale*SQRT3, hit ))
			{
				Vec3 dir;
				F32 newDist;
				subVec3(hit, start, dir);
				newDist = lengthVec3Squared(dir);
				if(nearestDist < 0 || newDist < nearestDist) 
				{
					nearestDist = newDist;
					scaleGizmo->selectedAxis = i;
					overAxis = true;
				}
			}
		}
	}

	if ((mouseClick(MS_LEFT) || mouseDown(MS_LEFT)) && !scaleGizmo->activated)
	{
		if (overAxis)
		{
			scaleGizmo->activated = true;
			if (scaleGizmo->activate_fn)
				scaleGizmo->activate_fn(scaleGizmo->activeMatrix, scaleGizmo->scaleMinMax, scaleGizmo->fn_context);
			firstFrameActive = true;
			inpHandled();
		}
	}
	else if (!mouseIsDown(MS_LEFT))
	{
		if (scaleGizmo->activated)
		{
			scaleGizmo->activated = false;
			if (scaleGizmo->deactivate_fn)
				scaleGizmo->deactivate_fn(scaleGizmo->activeMatrix, scaleGizmo->scaleMinMax, scaleGizmo->fn_context);
			inpHandled();
		}
	}

	if (scaleGizmo->activated)
	{
		int idx = scaleGizmo->selectedAxis%3;
		Vec3 planeISect, normal, camDir;
		F32 angle1, angle2;

		subVec3(end, start, camDir);
		normalVec3(camDir);
		angle1 = fabs(dotVec3(camDir, scaleGizmo->activeMatrix[(idx+1)%3]));
		angle2 = fabs(dotVec3(camDir, scaleGizmo->activeMatrix[(idx+2)%3]));
		if(angle1 > angle2)
			copyVec3(scaleGizmo->activeMatrix[(idx+1)%3], normal);
		else
			copyVec3(scaleGizmo->activeMatrix[(idx+2)%3], normal);

		if(findPlaneLineIntersection(absMid, normal, start, end, planeISect))
		{
			Vec3 nextPoint, lineISect;
			Vec3 relNewPos;
			F32 newVal;

			addVec3(absMid, scaleGizmo->activeMatrix[idx], nextPoint);
			findPointLineIntersection(absMid, nextPoint, planeISect, lineISect);
			subFromVec3(scaleGizmo->activeMatrix[3], lineISect);
			mulVecMat3Transpose(lineISect, scaleGizmo->activeMatrix, relNewPos);
			newVal = relNewPos[idx];

			if(scaleGizmo->selectedAxis < 3) {
				if(firstFrameActive)
					scaleGizmo->selectedOffset = (newVal - scaleGizmo->scaleMinMax[0][idx]);
				newVal -= scaleGizmo->selectedOffset;
				newVal = ScaleMinMaxGizmoSnapToGrid(scaleGizmo, newVal);
				if(scaleGizmo->mirrored)
					newVal = MIN(newVal, -0.001);
				else
					newVal = MIN(newVal, scaleGizmo->scaleMinMax[1][idx]);
				scaleGizmo->scaleMinMax[0][idx] = newVal;
				newVal = -newVal;
			} else {
				if(firstFrameActive)
					scaleGizmo->selectedOffset = (newVal - scaleGizmo->scaleMinMax[1][idx]);
				newVal -= scaleGizmo->selectedOffset;
				newVal = ScaleMinMaxGizmoSnapToGrid(scaleGizmo, newVal);
				if(scaleGizmo->mirrored)
					newVal = MAX(newVal, 0.001);
				else
					newVal = MAX(newVal, scaleGizmo->scaleMinMax[0][idx]);
				scaleGizmo->scaleMinMax[1][idx] = newVal;
			}

			if(scaleGizmo->mirrored) {
				setVec3same(scaleGizmo->scaleMinMax[0], -newVal);
				setVec3same(scaleGizmo->scaleMinMax[1],  newVal);
			}
		}
	}
}

void ScaleMinMaxGizmoDraw(ScaleMinMaxGizmo* scaleGizmo)
{
	int idx, i;
	Vec3 volMid, volMin, volMax;
	Vec3 absMid;
	Mat4 cam;
	Mat4 boxMat;

	gfxGetActiveCameraMatrix(cam);

	addVec3(scaleGizmo->activeMatrix[3], scaleGizmo->scaleMinMax[0], volMin);
	addVec3(scaleGizmo->activeMatrix[3], scaleGizmo->scaleMinMax[1], volMax);
	addVec3(volMin, volMax, volMid);
	scaleVec3(volMid, 0.5f, volMid);
	subFromVec3(scaleGizmo->activeMatrix[3], volMid);
	mulVecMat3(volMid, scaleGizmo->activeMatrix, absMid);
	addToVec3(scaleGizmo->activeMatrix[3], volMid);
	addToVec3(scaleGizmo->activeMatrix[3], absMid);

	copyMat3(scaleGizmo->activeMatrix, boxMat);

	if(scaleGizmo->activated)
	{
		Color color;
		setColorFromARGB(&color, 0xffffffff);
		gfxSetPrimZTest(0);
		gfxSetPrimIgnoreMax(true);
		gfxDrawBox3D(scaleGizmo->scaleMinMax[0], scaleGizmo->scaleMinMax[1], scaleGizmo->activeMatrix, color, 1);
		gfxSetPrimIgnoreMax(false);
		gfxSetPrimZTest(1);
	}

	for (i=0; i<6; ++i) 
	{
		Color color;
		Vec3 boxMin, boxMax;
		F32 halfBoxScale = 0.01f;
		F32 fardist;

		idx = i%3;

		if(scaleGizmo->selectedAxis == i) {
			setColorFromARGB(&color, 0xffffff00);
		} else {
			setColorFromARGB(&color, axisColor(idx));
			if(scaleGizmo->activated)
				continue;
		}

		scaleVec3(scaleGizmo->activeMatrix[idx], volMax[idx]-volMid[idx], boxMat[3]);
		if(i<3)
			scaleVec3(boxMat[3], -1.0f, boxMat[3]);
		addToVec3(absMid, boxMat[3]);

		fardist = distance3(cam[3], boxMat[3]);
		halfBoxScale *= fardist; 
		setVec3same(boxMin, -halfBoxScale);
		setVec3same(boxMax,  halfBoxScale);

		if(i<3)
			boxMat[3][idx] -= halfBoxScale*1.01f;
		else 
			boxMat[3][idx] += halfBoxScale*1.01f;

		gfxDrawBox3D(boxMin, boxMax, boxMat, color, 0);
 		gfxSetPrimZTest(0);
 		gfxSetPrimIgnoreMax(true);
 		gfxDrawBox3D(boxMin, boxMax, boxMat, color, 1);
 		gfxSetPrimIgnoreMax(false);
 		gfxSetPrimZTest(1);
	}
}

void ScaleMinMaxGizmoSetMatrix(ScaleMinMaxGizmo* scaleGizmo, const Mat4 startMat)
{
	copyMat4(startMat, scaleGizmo->activeMatrix);
}

void ScaleMinMaxGizmoSetMinMax(ScaleMinMaxGizmo* scaleGizmo, const Vec3 scaleMin, const Vec3 scaleMax)
{
	copyVec3(scaleMin, scaleGizmo->scaleMinMax[0]);
	copyVec3(scaleMax, scaleGizmo->scaleMinMax[1]);
}

void ScaleMinMaxGizmoSetCallbackContext(ScaleMinMaxGizmo* scaleGizmo, void *context)
{
	scaleGizmo->fn_context = context;
}

void ScaleMinMaxGizmoSetActivateCallback(ScaleMinMaxGizmo* scaleGizmo, GizmoActivateScaleMinMaxFn activate_fn)
{
	scaleGizmo->activate_fn = activate_fn;
}

void ScaleMinMaxGizmoSetDeactivateCallback(ScaleMinMaxGizmo* scaleGizmo, GizmoActivateScaleMinMaxFn deactivate_fn)
{
	scaleGizmo->deactivate_fn = deactivate_fn;
}

void ScaleMinMaxGizmoSetSnapResolution(ScaleMinMaxGizmo *scaleGizmo, int res)
{
	scaleGizmo->snapResolution = res;
}

void ScaleMinMaxGizmoSetMirrored(ScaleMinMaxGizmo *scaleGizmo, bool mirrored)
{
	scaleGizmo->mirrored = mirrored;
}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//	ModelOptionsToolbar
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static char **lighting_options;
static char *lighting_options_static[] = 
{
	"Standard Lighting",
	"HDR Lighting",
	"Comic Shading"
};

static char **time_options;
static char *time_options_static[] = 
{
	"<current map time>",
	"Time 1",
	"Time 2",
	"Time 3",
	"Time 4",
	"Time 5",
	"Time 6",
	"Time 7",
	"Time 8",
	"Time 9",
	"Time 10",
	"Time 11",
	"Time 12",
	"Time 13",
	"Time 14",
	"Time 15",
	"Time 16",
	"Time 17",
	"Time 18",
	"Time 19",
	"Time 20",
	"Time 21",
	"Time 22",
	"Time 23",
	"Time 24"
};

typedef enum LightOption
{
	LOPTION_STANDARD,
	LOPTION_HDR,
	LOPTION_COMIC,
} LightOption;

//This replaces ModelEditToolbar in AM2(Editor Manager)
//CO = Common Options
typedef struct ModelOptionsToolbar
{
	EMToolbar *em_toolbar;
	UILabel *ui_current_distance;
	UIComboBox *ui_time;
	UIComboBox *ui_skyfile;

	ModelEditToolbarOptions options;

	GfxCameraController *camera;

	bool show_grid;
	bool wireframe1, wireframe2;
	bool unlit;
	bool ortho;
	bool always_on_top;
	bool was_always_on_top;

	Vec3 bounds_min, bounds_max;
	F32 bounds_radius;

	char **sky_names;

	bool camera_inited;
	F32 camdist;
	Vec3 camcenter;
	Vec3 campyr;
	F32 camorthozoom;
	F32 current_camdist;

	const char *sky_override;
	F32 time_override;
	LightOption lighting;

	Vec4 tint0, tint1;

	char *editor_pref_key;

} ModelOptionsToolbar;


static void motSetTintColor0(UIColorButton *color, bool finished, ModelOptionsToolbar *toolbar)
{
	ui_ColorButtonGetColor(color, toolbar->tint0);
}

static void motSetTintColor1(UIColorButton *color, bool finished, ModelOptionsToolbar *toolbar)
{
	ui_ColorButtonGetColor(color, toolbar->tint1);
}

static void motSetLighting(ModelOptionsToolbar *toolbar, LightOption lighting)
{
	toolbar->lighting = lighting;

	if (!(toolbar->options & MET_LIGHTING))
		return;

	switch (toolbar->lighting)
	{
	xcase LOPTION_STANDARD:
		globCmdParse("postprocessing 0");
		globCmdParse("outlining 0");

	xcase LOPTION_HDR:
		globCmdParse("postprocessing 1");
		globCmdParse("outlining 0");

	xcase LOPTION_COMIC:
		globCmdParse("postprocessing 1");
		globCmdParse("outlining 1");
	}
}

static void motRegularLightingCB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	motSetLighting(toolbar, LOPTION_STANDARD);
}

static void motHDRLightingCB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	motSetLighting(toolbar, LOPTION_HDR);
}

static void motComicLightingCB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	motSetLighting(toolbar, LOPTION_COMIC);
}

static void motWireframe0CB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	toolbar->wireframe1 = false;
	toolbar->wireframe2 = false;
}

static void motWireframe1CB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	toolbar->wireframe1 = true;
	toolbar->wireframe2 = false;
}

static void motWireframe2CB(UIButton *button, ModelOptionsToolbar *toolbar)
{
	toolbar->wireframe1 = false;
	toolbar->wireframe2 = true;
}

static void motBasicButtonComboCB(UIButtonCombo *button_combo, bool *value)
{
	(*value) = !(*value);
	ui_ButtonComboSetActive(button_combo, (*value));
}

static void motSetSky(ModelOptionsToolbar *toolbar, const char *sky_name)
{
	toolbar->sky_override = sky_name;

	if (!(toolbar->options & MET_SKIES))
		return;

	if (toolbar->camera)
		gfxCameraControllerSetSkyOverride(toolbar->camera, sky_name, NULL);

	if (toolbar->editor_pref_key)
		EditorPrefStoreString(toolbar->editor_pref_key, "Option", "SkyOverride", sky_name);
}

static void motSetSkyTime(ModelOptionsToolbar *toolbar, F32 time)
{
	toolbar->time_override = time;

	if (!(toolbar->options & MET_TIME))
		return;

	if (toolbar->camera)
	{
		if (time <= 0)
		{
			toolbar->camera->override_time = 0;
		}
		else
		{
			toolbar->camera->override_time = 1;
			toolbar->camera->time_override = time;
		}
	}

	if (toolbar->editor_pref_key)
		EditorPrefStoreFloat(toolbar->editor_pref_key, "Option", "TimeOverride", time);
}

static void motSwitchTime(UIComboBox *combo, ModelOptionsToolbar *toolbar)
{
	motSetSkyTime(toolbar, ui_ComboBoxGetSelected(combo));
}

static void motSwitchSky(UIComboBox *combo, ModelOptionsToolbar *toolbar)
{
	if (combo->iSelected == 0)
		motSetSky(toolbar, NULL);
	else
		motSetSky(toolbar, toolbar->sky_names[combo->iSelected]);
}

static void motResetCameraCB(void *unused, ModelOptionsToolbar *toolbar)
{
	motResetCamera(toolbar);
}

void motResetCamera(ModelOptionsToolbar *toolbar)
{
	if (toolbar->camera && (toolbar->options & MET_CAMRESET))
	{
		centerVec3(toolbar->bounds_min, toolbar->bounds_max, toolbar->camera->camcenter);
		toolbar->camera->camcenter[1] -= toolbar->bounds_min[1];
		toolbar->camera->camdist = toolbar->bounds_radius + 50;
		toolbar->camera->ortho_zoom = 100;
		setVec3(toolbar->camera->campyr, 1, 0, 0);

		toolbar->camdist = toolbar->camera->camdist;
		toolbar->camorthozoom = toolbar->camera->ortho_zoom;
		copyVec3(toolbar->camera->camcenter, toolbar->camcenter);
		copyVec3(toolbar->camera->campyr, toolbar->campyr);

		toolbar->camera->inited = 1;
		toolbar->camera->override_disable_shadows = 1;
	}
}

void motSetObjectBounds(ModelOptionsToolbar *toolbar, const Vec3 min, const Vec3 max, F32 radius)
{
	copyVec3(min, toolbar->bounds_min);
	copyVec3(max, toolbar->bounds_max);
	toolbar->bounds_radius = radius;
}

void motSetTime(ModelOptionsToolbar *toolbar, F32 time)
{
	int sel;

	if (time < 0)
		sel = 0;
	else
		sel = round(time);

	if (sel > 24)
		sel = 24;

	ui_ComboBoxSetSelectedAndCallback(toolbar->ui_time, sel);
}

void motSetSkyFile(ModelOptionsToolbar *toolbar, const char *sky_name)
{
	int sel = 0;

	if (sky_name && sky_name[0])
	{
		for (sel = 0; sel < eaSize(&toolbar->sky_names); ++sel)
		{
			if (stricmp(toolbar->sky_names[sel], sky_name)==0)
				break;
		}
		if (sel >= eaSize(&toolbar->sky_names))
			sel = 0;
	}

	ui_ComboBoxSetSelectedAndCallback(toolbar->ui_skyfile, sel);
}

int motGetWireframeSetting(ModelOptionsToolbar *toolbar)
{
	return toolbar->wireframe2?2:(toolbar->wireframe1?1:0);
}

bool motGetUnlitSetting(ModelOptionsToolbar *toolbar)
{
	return toolbar->unlit;
}

void motGetTintColor0(ModelOptionsToolbar *toolbar, Vec3 tint)
{
	copyVec3(toolbar->tint0, tint);
}

U8 motGetTintAlpha(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar)
{
	return CLAMP(toolbar->tint0[3] * 255, 0, 255);
}

void motGetTintColor1(ModelOptionsToolbar *toolbar, Vec3 tint)
{
	copyVec3(toolbar->tint1, tint);
}

MaterialNamedConstant **motGetNamedParams(ModelOptionsToolbar *toolbar)
{
	// TODO: Support adding any number of named parameter color widgets?
	return gfxMaterialStaticTintColorArray(toolbar->tint1);
}

void motSetAlwaysOnTop(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, bool alwaysOnTop)
{
	toolbar->always_on_top = alwaysOnTop;
}

F32 motGetCamDist(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar)
{
	return toolbar->current_camdist;
}

void motApplyValues(ModelOptionsToolbar *toolbar)
{
	if (toolbar->camera && toolbar->camera_inited)
	{
		// set camera parameters
		toolbar->camera->camdist = toolbar->camdist;
		toolbar->camera->ortho_zoom = toolbar->camorthozoom;
		copyVec3(toolbar->camcenter, toolbar->camera->camcenter);
		copyVec3(toolbar->campyr, toolbar->camera->campyr);
	}

	motSetSky(toolbar, toolbar->sky_override);
	motSetLighting(toolbar, toolbar->lighting);
	motSetSkyTime(toolbar, toolbar->time_override);
}

EMToolbar *motGetToolbar(ModelOptionsToolbar *toolbar)
{
	return toolbar->em_toolbar;
}

void motLostFocus(ModelOptionsToolbar *toolbar)
{
	// get camera parameters
	if (toolbar->camera)
	{
		toolbar->camdist = toolbar->camera->camdist;
		toolbar->camorthozoom = toolbar->camera->ortho_zoom;
		copyVec3(toolbar->camera->camcenter, toolbar->camcenter);
		copyVec3(toolbar->camera->campyr, toolbar->campyr);
		toolbar->camera_inited = 1;
	}
}

void motGotFocus(ModelOptionsToolbar *toolbar)
{
	if (!toolbar->camera_inited && (toolbar->options & MET_CAMRESET))
	{
		motResetCamera(toolbar);
		toolbar->camera_inited = 1;
	}

	motApplyValues(toolbar);
}

void motUpdateAndDraw(ModelOptionsToolbar *toolbar)
{
	Vec3 mid, mid_world, campos;
	char buf[1024];
	Mat4 mat;
	F32 dist;

	copyMat4(unitmat, mat);

	if (toolbar->camera)
		toolbar->camera->ortho_mode = !!toolbar->ortho;

	if (toolbar->show_grid)
	{
		int uid = objectLibraryUIDFromObjName("Plane_Doublesided_2000ft");
		if (uid) {
			GroupDef *ground_plane = objectLibraryGetGroupDef(uid, true);
			int x, y;

			groupPostLoad(ground_plane); //< need to force the model
										 //< to be loaded
			for (y = -3; y <= 3; ++y)
			{
				for (x = -3; x <= 3; ++x)
				{
					SingleModelParams params = {0};
					setVec3(mat[3], x * (ground_plane->bounds.max[0] - ground_plane->bounds.min[0]), 0, y * (ground_plane->bounds.max[2] - ground_plane->bounds.min[2]));

					params.model = ground_plane->model;
					copyMat4(mat, params.world_mat);
					params.unlit = true;
					gfxQueueSingleModel(&params);
				}
			}
		}
	}

	centerVec3(toolbar->bounds_min, toolbar->bounds_max, mid);
	setVec3(mat[3], 0, -toolbar->bounds_min[1], 0);
	mulVecMat4(mid, mat, mid_world);
	gfxGetActiveCameraPos(campos);
	dist = distance3(mid_world, campos);
	toolbar->current_camdist = dist;
	sprintf(buf, "Camera Distance: %.2f", dist);
	ui_LabelSetText(toolbar->ui_current_distance, buf);

	if (toolbar->always_on_top != toolbar->was_always_on_top) {
		void* rdrHandle;
		toolbar->was_always_on_top = toolbar->always_on_top;
#if !PLATFORM_CONSOLE
		rdrHandle = rdrGetWindowHandle(gfxGetActiveDevice());
		if (rdrHandle)
		{
			ANALYSIS_ASSUME(rdrHandle != NULL);
			SetWindowPos(rdrHandle, toolbar->always_on_top?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
		}
#endif
		if (toolbar->editor_pref_key)
			EditorPrefStoreInt(toolbar->editor_pref_key, "Option", "AlwaysOnTop", toolbar->always_on_top);
	}
}

ModelOptionsToolbar *motCreateToolbar(ModelEditToolbarOptions options, GfxCameraController *camera, const Vec3 min, const Vec3 max, F32 radius, const char *editor_pref_key)
{
	ModelOptionsToolbar *toolbar = calloc(1,sizeof(*toolbar));
	int i;
	F32 x;
	char buf[256];
	EMToolbar *em_toolbar;
	UIComboBox *combo;
	UICheckButton *check;
	UIButton *button;
	UILabel *label;
	UIColorButton *color;
	UIButtonCombo *button_combo;

	if (editor_pref_key)
		toolbar->editor_pref_key = strdup(editor_pref_key);

	toolbar->options = options;

	copyVec3(min, toolbar->bounds_min);
	copyVec3(max, toolbar->bounds_max);
	toolbar->bounds_radius = radius;
	toolbar->camera = camera;

	if (!eaSize(&lighting_options))
	{
		for (i = 0; i < ARRAY_SIZE_CHECKED(lighting_options_static); ++i)
			eaPush(&lighting_options, lighting_options_static[i]);
	}

	if (!eaSize(&time_options))
	{
		for (i = 0; i < ARRAY_SIZE_CHECKED(time_options_static); ++i)
			eaPush(&time_options, time_options_static[i]);
	}

	toolbar->sky_names = gfxGetAllSkyNames(true);
	eaInsert(&toolbar->sky_names, strdup("<current map sky>"), 0);


	//////////////////////////////////////////////////////////////////////////
	// toolbar
	em_toolbar = emToolbarCreate(0);
	toolbar->em_toolbar = em_toolbar;

	x = 0;

	if (options & MET_GRID)
	{
		toolbar->show_grid = 1;
		button_combo = ui_ButtonComboCreate(x, 2, 20, 20, POP_DOWN, false);
		ui_ButtonComboSetChangedCallback(button_combo, motBasicButtonComboCB, &toolbar->show_grid);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_hide_grid.tga", "eui_button_show_grid.tga", NULL, NULL, NULL);
		sprintf(buf, "Show or hide the grid.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, toolbar->show_grid);
		emToolbarAddChild(em_toolbar, button_combo, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button_combo)) + 5;
	}

	if (options & MET_WIREFRAME)
	{
		button_combo = ui_ButtonComboCreate(x, 2, 20, 20, POP_DOWN, false);
		ui_ButtonComboSetChangedCallback(button_combo, NULL, NULL);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_wireframe_0.tga", NULL, NULL, motWireframe0CB, toolbar);
		sprintf(buf, "Shaded");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_wireframe_1.tga", NULL, NULL, motWireframe1CB, toolbar);
		sprintf(buf, "Shaded and Wireframe");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_wireframe_2.tga", NULL, NULL, motWireframe2CB, toolbar);
		sprintf(buf, "Wireframe");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, true);
		emToolbarAddChild(em_toolbar, button_combo, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button_combo)) + 5;
	}

	if (options & MET_UNLIT)
	{
		button_combo = ui_ButtonComboCreate(x, 2, 20, 20, POP_DOWN, false);
		ui_ButtonComboSetChangedCallback(button_combo, motBasicButtonComboCB, &toolbar->unlit);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_lit.tga", "eui_button_unlit.tga", NULL, NULL, NULL);
		sprintf(buf, "Lit or unlit.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, toolbar->unlit);
		emToolbarAddChild(em_toolbar, button_combo, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button_combo)) + 5;
	}

	if (options & MET_ORTHO)
	{
		button_combo = ui_ButtonComboCreate(x, 2, 20, 20, POP_DOWN, false);
		ui_ButtonComboSetChangedCallback(button_combo, motBasicButtonComboCB, &toolbar->ortho);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_reg_view.tga", "eui_button_ortho_view.tga", NULL, NULL, NULL);
		sprintf(buf, "Orthographic or Regular view mode.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, toolbar->ortho);
		emToolbarAddChild(em_toolbar, button_combo, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button_combo)) + 5;
	}

	if (options & MET_ALWAYS_ON_TOP)
	{
		check = ui_CheckButtonCreate(x, 2, "Always on Top", false);
		check->statePtr = &toolbar->always_on_top;
		check->widget.height = 20;
		emToolbarAddChild(em_toolbar, check, true);
		x += ui_WidgetGetWidth(UI_WIDGET(check)) + 5;
	}

	if (gfxDoingPostprocessing())
	{
		if (gfxDoingOutlining())
			toolbar->lighting = LOPTION_COMIC;
		else
			toolbar->lighting = LOPTION_HDR;
	}
	else
	{
		toolbar->lighting = LOPTION_STANDARD;
	}

	if (options & MET_LIGHTING)
	{
		button_combo = ui_ButtonComboCreate(x, 2, 20, 20, POP_DOWN, false);
		ui_ButtonComboSetChangedCallback(button_combo, NULL, NULL);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_reg_lighting.tga", NULL, NULL, motRegularLightingCB, toolbar);
		sprintf(buf, "Standard Lighting.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_hdr_lighting.tga", NULL, NULL, motHDRLightingCB, toolbar);
		sprintf(buf, "HDR Lighting.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		button = ui_ButtonComboAddItem(button_combo, "eui_button_comic_lighting.tga", NULL, NULL, motComicLightingCB, toolbar);
		sprintf(buf, "Comic Shading Lighting.");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, true);
		emToolbarAddChild(em_toolbar, button_combo, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button_combo)) + 5;
	}

	if (options & MET_SKIES)
	{
		combo = ui_ComboBoxCreate(x, 2, 145, NULL, &toolbar->sky_names, NULL);
		ui_ComboBoxSetSelectedCallback(combo, motSwitchSky, toolbar);
		ui_ComboBoxSetSelected(combo, 0);
		combo->widget.height = 20;
		emToolbarAddChild(em_toolbar, combo, true);
		toolbar->ui_skyfile = combo;
		x += ui_WidgetGetWidth(UI_WIDGET(combo)) + 5;
	}

	if (options & MET_TIME)
	{
		combo = ui_ComboBoxCreate(x, 2, 145, NULL, &time_options, NULL);
		combo->widget.height = 20;
		ui_ComboBoxSetSelectedCallback(combo, motSwitchTime, toolbar);
		ui_ComboBoxSetSelected(combo, 0);
		emToolbarAddChild(em_toolbar, combo, true);
		toolbar->ui_time = combo;
		x += ui_WidgetGetWidth(UI_WIDGET(combo)) + 5;
	}

	if (options & (MET_SKIES|MET_TIME))
	{
		x += 5;
	}

	if (options & MET_TINT)
	{
		label = ui_LabelCreate("Tint Colors:", x, 2);
		emToolbarAddChild(em_toolbar, label, true);
		x += ui_WidgetGetWidth(UI_WIDGET(label)) + 5;

		copyVec4(unitvec4, toolbar->tint0);
		color = ui_ColorButtonCreateEx(x, 2, 0, 15, toolbar->tint0);
		ui_ColorButtonSetChangedCallback(color, motSetTintColor0, toolbar);
		//color->noAlpha = 1; // MaterialEditor uses Alpha
		color->liveUpdate = 1;
		color->widget.height = 20;
		color->widget.width = 20;
		emToolbarAddChild(em_toolbar, color, true);
		x += ui_WidgetGetWidth(UI_WIDGET(color)) + 5;

		copyVec4(unitvec4, toolbar->tint1);
		color = ui_ColorButtonCreateEx(x, 2, 0, 15, toolbar->tint1);
		ui_ColorButtonSetChangedCallback(color, motSetTintColor1, toolbar);
		color->noAlpha = 1;
		color->liveUpdate = 1;
		color->widget.height = 20;
		color->widget.width = 20;
		emToolbarAddChild(em_toolbar, color, true);
		x += ui_WidgetGetWidth(UI_WIDGET(color)) + 5;
		
		x += 5;
	}
	
	if (options & MET_CAMDIST)
	{
		
		label = ui_LabelCreate("Camera Distance: 0", x, 2);
		emToolbarAddChild(em_toolbar, label, true);
		toolbar->ui_current_distance = label;
		x += ui_WidgetGetWidth(UI_WIDGET(label)) + 35;
	}

	if (options & MET_CAMRESET)
	{
		button = ui_ButtonCreateImageOnly("button_center", x, 2, motResetCameraCB, toolbar);
		button->widget.height = 20;
		ui_WidgetSetTooltipString(UI_WIDGET(button), "Reset Camera");
		emToolbarAddChild(em_toolbar, button, true);
		x += ui_WidgetGetWidth(UI_WIDGET(button)) + 5;
	}

	if (toolbar->editor_pref_key)
	{
		if (options & MET_TIME)
			motSetTime(toolbar, EditorPrefGetFloat(toolbar->editor_pref_key, "Option", "TimeOverride", 0));
		if (options & MET_SKIES)
			motSetSkyFile(toolbar, EditorPrefGetString(toolbar->editor_pref_key, "Option", "SkyOverride", ""));
	}
	else
	{
		if (options & MET_TIME)
			motSetTime(toolbar, 0);
		if (options & MET_SKIES)
			motSetSkyFile(toolbar, NULL);
	}


	return toolbar;
}

void motFreeToolbar(ModelOptionsToolbar *toolbar)
{
	emToolbarDestroy(toolbar->em_toolbar);
	eaDestroyEx(&toolbar->sky_names, NULL);
	SAFE_FREE(toolbar->editor_pref_key);
	free(toolbar);
}

void InertAxesGizmoDraw( const Mat4 mat, float fAxisLength )
{
	int i;
	for (i=0; i<3; ++i)
		drawLineArrow(mat[3], mat[i], mat[(i+1)%3], fAxisLength, axisColor(i), axisColor(i));
}

void InertAxesGizmoDrawColored( const Mat4 mat, float fAxisLength, int color)
{
	int i;
	for (i=0; i<3; ++i)
		drawLineArrow(mat[3], mat[i], mat[(i+1)%3], fAxisLength, color, color);
}

#endif
