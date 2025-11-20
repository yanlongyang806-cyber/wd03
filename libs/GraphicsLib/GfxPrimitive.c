#include "GfxPrimitive.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "LineDist.h"
#include "Quat.h"
#include "Cbox.h"
#include "WorldGrid.h"
#include "dynClothMesh.h"
#include "dynNode.h"
#include "dynNodeInline.h"
#include "GfxGeo.h"
#include "GfxMaterials.h"
#include "GfxLights.h"
#include "GfxTexturesInline.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

typedef enum SimplePrimitiveShape
{
	SPS_SPHERE,
	SPS_BOX,
} SimplePrimitiveShape;

typedef struct SimplePrimitive
{
	SimplePrimitiveShape shape;
	Mat4 world_matrix;
	Vec3 local_min, local_max;
	F32 radius;
	Vec4 color;
	bool no_ztest, no_zwrite, tonemap, requeued;
	F32 time_to_draw;
} SimplePrimitive;

MP_DEFINE(SimplePrimitive);
MP_DEFINE(RdrDrawablePrimitive);

static RdrDrawablePrimitive	**queuedprims = NULL;
static SimplePrimitive **queuedshapes = NULL;
static int prim_no_ztest = 0;
static int prim_tonemap = 0;
static int maxGfxPrims = 16000;
static bool ignoreMaxPrims = false;

// Sets the maximum number of gfx primitives to draw in a frame
AUTO_CMD_INT(maxGfxPrims, maxPrimitives) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Performance) ACMD_CMDLINE;

__forceinline static bool canAddPrim(void)
{
	return ignoreMaxPrims || maxGfxPrims <= 0 || (eaSize(&queuedprims) + eaSize(&queuedshapes)) < maxGfxPrims;
}

void gfxSetPrimZTest(int ztest)
{
	prim_no_ztest = !ztest;
}

void gfxSetPrimToneMap(int tonemap)
{
	prim_tonemap = !!tonemap;
}

void gfxSetPrimIgnoreMax(bool ignore)
{
	ignoreMaxPrims = ignore;
}

static void primitiveFree(RdrDrawablePrimitive *prim)
{
	if (prim && !prim->requeued)
		MP_FREE(RdrDrawablePrimitive, prim);
}

static void shapeFree(SimplePrimitive *shape)
{
	if (!shape->requeued)
		MP_FREE(SimplePrimitive, shape);
}

int gfxGetQueuedPrimCount(void)
{
	return eaSize(&queuedprims) + eaSize(&queuedshapes);
}

void gfxQueueSimplePrims(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i;

	for (i = 0; i < eaSize(&queuedshapes); i++)
	{
		RdrMaterialFlags add_flags = 0;
		Mat4 inv_world_matrix;
		Vec3 local_cam_pos;

		if (queuedshapes[i]->no_ztest)
			add_flags |= RMATERIAL_NOZTEST;

		if (queuedshapes[i]->no_zwrite)
			add_flags |= RMATERIAL_NOZWRITE;

		switch (queuedshapes[i]->shape)
		{
			xcase SPS_BOX:
				gfxQueueBox(queuedshapes[i]->local_min, queuedshapes[i]->local_max, queuedshapes[i]->world_matrix, gdraw->visual_frustum_bit, ROC_PRIMITIVE, RMATERIAL_BACKFACE | add_flags, queuedshapes[i]->color, queuedshapes[i]->tonemap);
				invertMat4Copy(queuedshapes[i]->world_matrix, inv_world_matrix);
				mulVecMat4(gdraw->cam_pos, inv_world_matrix, local_cam_pos);
 				if (!pointBoxCollision(local_cam_pos, queuedshapes[i]->local_min, queuedshapes[i]->local_max))
					gfxQueueBox(queuedshapes[i]->local_min, queuedshapes[i]->local_max, queuedshapes[i]->world_matrix, gdraw->visual_frustum_bit, ROC_PRIMITIVE, add_flags, queuedshapes[i]->color, queuedshapes[i]->tonemap);

			xcase SPS_SPHERE:
				gfxQueueSphere(zerovec4, queuedshapes[i]->radius, queuedshapes[i]->world_matrix, gdraw->visual_frustum_bit, ROC_PRIMITIVE, RMATERIAL_BACKFACE | add_flags, queuedshapes[i]->color, queuedshapes[i]->tonemap);
				if (distance3Squared(gdraw->cam_pos, queuedshapes[i]->world_matrix[3]) >= SQR(queuedshapes[i]->radius))
					gfxQueueSphere(zerovec4, queuedshapes[i]->radius, queuedshapes[i]->world_matrix, gdraw->visual_frustum_bit, ROC_PRIMITIVE, add_flags, queuedshapes[i]->color, queuedshapes[i]->tonemap);

			xdefault:
				devassert(0);
		}
	}
}

void gfxFreeOrRequeueSimplePrims()
{
	static SimplePrimitive **requeuedshapes = NULL;
	int i;
	eaSetSize(&requeuedshapes, 0);

	for (i = 0; i < eaSize(&queuedshapes); i++)
	{
		queuedshapes[i]->time_to_draw -= gfx_state.frame_time;
		if (queuedshapes[i]->time_to_draw > 0)
		{
			eaPush(&requeuedshapes, queuedshapes[i]);
			queuedshapes[i]->requeued = 1;
		}
	}

	eaClearEx(&queuedshapes, shapeFree);

	eaPushEArray(&queuedshapes, &requeuedshapes);
	eaClear(&requeuedshapes);
	FOR_EACH_IN_EARRAY_FORWARDS(queuedshapes, SimplePrimitive, QueuedShape)
		queuedshapes[iQueuedShapeIndex]->requeued = 0;
	FOR_EACH_END;
}

void gfxDrawQueuedPrims(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i;

	gfxBeginSection(__FUNCTION__);

	for (i = 0; i < eaSize(&queuedprims); i++)
	{
		if (queuedprims[i]) // shouldn't need to do this check, but i'm getting null prims on occasion and don't have time to debug at the moment
		{
			if (queuedprims[i]->in_3d)
			{
				RdrDrawablePrimitive *prim = rdrDrawListAllocPrimitive(gdraw->draw_list, RTYPE_PRIMITIVE, false);
				if (prim)
				{
					memcpy(prim, queuedprims[i], sizeof(RdrDrawablePrimitive));
					rdrDrawListAddPrimitive(gdraw->draw_list, prim, prim->no_ztest?RST_ALPHA:RST_AUTO, ROC_PRIMITIVE);
				}
			}
			else
			{
				assert(0); // Nothing queues non-3D prims
			}
		}
		else
		{
			// Should have an assert here and track it down eventually.
		}
	}

	gfxEndSection();
}

void gfxFreeOrRequeueDrawablePrims()
{
	static RdrDrawablePrimitive **requeuedprims = NULL;
	int i;
	eaSetSize(&requeuedprims, 0);

	for (i = 0; i < eaSize(&queuedprims); i++)
	{
		queuedprims[i]->time_to_draw -= gfx_state.frame_time;
		if (queuedprims[i]->time_to_draw > 0)
		{
			eaPush(&requeuedprims, queuedprims[i]);
			queuedprims[i]->requeued = 1;
		}
	}
	eaClearEx(&queuedprims, primitiveFree);

	// All of these should be valid, since they were added to the primitive queue before
	eaPushEArray(&queuedprims, &requeuedprims);
	FOR_EACH_IN_EARRAY_FORWARDS(queuedprims, RdrDrawablePrimitive, QueuedPrim)
		queuedprims[iQueuedPrimIndex]->requeued = 0;
	FOR_EACH_END;
}

static CRITICAL_SECTION csQueuePrim;

static void queuePrim(RdrDrawablePrimitive *prim)
{
	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&csQueuePrim);
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&csQueuePrim);

	if (canAddPrim())
	{
		RdrDrawablePrimitive *newprim;
		
		MP_CREATE(RdrDrawablePrimitive, 512);
		newprim = MP_ALLOC(RdrDrawablePrimitive);
		CopyStructs(newprim, prim, 1);

		newprim->base_drawable.draw_type = RTYPE_PRIMITIVE;
		if (prim_no_ztest)
			newprim->no_ztest = 1;
		if (prim_tonemap)
			newprim->tonemapped = 1;
		assert(FINITEVEC3(newprim->vertices[0].pos));
		assert(FINITEVEC3(newprim->vertices[1].pos));
		if (newprim->type == RP_TRI || newprim->type == RP_QUAD)
			assert(FINITEVEC3(newprim->vertices[2].pos));
		if (newprim->type == RP_QUAD)
			assert(FINITEVEC3(newprim->vertices[3].pos));

		eaPush(&queuedprims, newprim);
	}
			
	LeaveCriticalSection(&csQueuePrim);
}


//////////////////////////////////////////////////////////////////////////
void gfxDrawLine(int x1, int y1, F32 z, int x2, int y2, Color color)
{
	gfxDrawLineEx(x1, y1, z, x2, y2, color, color, 1.f, false);
}

void gfxDrawLine2(int x1, int y1, F32 z, int x2, int y2, Color color1, Color color2)
{
	gfxDrawLineEx(x1, y1, z, x2, y2, color1, color2, 1.f, false);
}

void gfxDrawLineWidth(int x1, int y1, F32 z, int x2, int y2, Color color, F32 width)
{
	gfxDrawLineEx(x1, y1, z, x2, y2, color, color, width, false);
}

void gfxDrawLineEx(F32 x1, F32 y1, F32 z, F32 x2, F32 y2, Color color1, Color color2, F32 width, bool antialiased)
{
	U32 rgba1 = RGBAFromColor(color1), rgba2 = RGBAFromColor(color2);
	display_line_as_sprite_ex(x1, y1, z, x2, y2, rgba1, rgba2, width, antialiased, false, clipperGetCurrent());
}

bool gfxDrawLineExCollides(F32 x1, F32 y1, F32 x2, F32 y2, F32 width, int test_x, int test_y)
{
	F32 tempx=0, tempy=0;
	F32 dist;
	dist = sqrt(PointLineDist2DSquared(test_x, test_y, x1, y1, x2-x1, y2-y1, &tempx, &tempy));
	return dist <= width;
}


//////////////////////////////////////////////////////////////////////////
void gfxDrawBezier3D(const Vec3 controlPoints[4], Color color1, Color color2, F32 width)
{
	//Draw control points and polygon 
	if(gfx_state.debug.draw_bezier_control_points) { 
		int i;
		Vec3 off, min, max;
		Mat4 mat;
		identityMat4(mat);
		off[0] = off[1] = off[2] = 2;
		color1.r = color1.g = 0x5F;
		color2.r = color2.g = 0;
		color1.b = 0xFF;
		color2.b = 0xFF;
		color1.a = color2.a = 0xFF;
		for(i=0; i<4; i++) { 
			subVec3(controlPoints[i], off, min);
			addVec3(controlPoints[i], off, max);
			gfxDrawBox3D(min, max, mat, color1, width);
		} 
	}
	//Calculate and draw bezier curve 
	if(!gfx_state.debug.no_draw_bezier) { 
		F32 t; //the time interval 
		F32 k = 0.025; //time step value for drawing curve 
		Vec3 p1, p2;
		int argb1 = ARGBFromColor(color1);
		int argb2 = ARGBFromColor(color2);
		int argb_interp1;
		int argb_interp2;
		copyVec3(controlPoints[0], p1);
		argb_interp1 = argb1;
		for(t=k; t<=1/*+k*/; t+=k)
		{
			//use Bernstein polynomials 
			p2[0]=(controlPoints[0][0]+t*(-controlPoints[0][0]*3+t*(3*controlPoints[0][0]- 
				controlPoints[0][0]*t)))+t*(3*controlPoints[1][0]+t*(-6*controlPoints[1][0]+ 
				controlPoints[1][0]*3*t))+t*t*(controlPoints[2][0]*3-controlPoints[2][0]*3*t)+ 
				controlPoints[3][0]*t*t*t; 
			p2[1]=(controlPoints[0][1]+t*(-controlPoints[0][1]*3+t*(3*controlPoints[0][1]- 
				controlPoints[0][1]*t)))+t*(3*controlPoints[1][1]+t*(-6*controlPoints[1][1]+ 
				controlPoints[1][1]*3*t))+t*t*(controlPoints[2][1]*3-controlPoints[2][1]*3*t)+ 
				controlPoints[3][1]*t*t*t; 
			p2[2]=(controlPoints[0][2]+t*(-controlPoints[0][2]*3+t*(3*controlPoints[0][2]- 
				controlPoints[0][2]*t)))+t*(3*controlPoints[1][2]+t*(-6*controlPoints[1][2]+ 
				controlPoints[1][2]*3*t))+t*t*(controlPoints[2][2]*3-controlPoints[2][2]*3*t)+ 
				controlPoints[3][2]*t*t*t; 
			//draw curve 
			interp_rgba(t/(1+k), argb1, argb2, &argb_interp2);
			setColorFromARGB(&color1, argb_interp1);
			setColorFromARGB(&color2, argb_interp2);
			gfxDrawLine3DWidth(p1, p2, color1, color2, width);
			copyVec3(p2, p1);
			argb_interp1 = argb_interp2;
		} 
	} 
}

void gfxDrawBezier(const Vec2 controlPoints[4], F32 z, Color color1, Color color2, F32 width)
{
	//Draw control points and polygon 
	if(gfx_state.debug.draw_bezier_control_points) { 
		int i;
		color1.r = color1.g = color1.b = 0x7F;
		color2.r = color2.g = 0;
		color2.b = 0xFF;
		color1.a = color2.a = 0xFF;
		for(i=0; i<4; i++) { 
			gfxDrawQuad(controlPoints[i][0]-2, controlPoints[i][1]-2, 
				controlPoints[i][0]+2, controlPoints[i][1]+2, z, color1);
			if(i<3) {
				gfxDrawLine(controlPoints[i][0],controlPoints[i][1], z, 
					controlPoints[i+1][0],controlPoints[i+1][1], color2); 
			}
		} 
	} 
	//Calculate and draw bezier curve 
	if(!gfx_state.debug.no_draw_bezier) { 
		F32 t; //the time interval 
		F32 k = 0.025; //time step value for drawing curve 
		F32 x1,x2,y1,y2;
		int argb1 = ARGBFromColor(color1);
		int argb2 = ARGBFromColor(color2);
		int argb_interp1;
		int argb_interp2;
		x1 = controlPoints[0][0]; 
		y1 = controlPoints[0][1]; 
		argb_interp1 = argb1;
		for(t=k; t<=1/*+k*/; t+=k)
		{
			//use Berstein polynomials 
			x2=(controlPoints[0][0]+t*(-controlPoints[0][0]*3+t*(3*controlPoints[0][0]- 
				controlPoints[0][0]*t)))+t*(3*controlPoints[1][0]+t*(-6*controlPoints[1][0]+ 
				controlPoints[1][0]*3*t))+t*t*(controlPoints[2][0]*3-controlPoints[2][0]*3*t)+ 
				controlPoints[3][0]*t*t*t; 
			y2=(controlPoints[0][1]+t*(-controlPoints[0][1]*3+t*(3*controlPoints[0][1]- 
				controlPoints[0][1]*t)))+t*(3*controlPoints[1][1]+t*(-6*controlPoints[1][1]+ 
				controlPoints[1][1]*3*t))+t*t*(controlPoints[2][1]*3-controlPoints[2][1]*3*t)+ 
				controlPoints[3][1]*t*t*t; 
			//draw curve 
			interp_rgba(t/(1+k), argb1, argb2, &argb_interp2);
			setColorFromARGB(&color1, argb_interp1);
			setColorFromARGB(&color2, argb_interp2);
			gfxDrawLineEx(x1, y1, z, x2, y2, color1, color2, width, true);
			x1 = x2;
			y1 = y2;
			argb_interp1 = argb_interp2;
		} 
	} 
}

bool gfxDrawBezierCollides(Vec2 controlPoints[4], F32 width, int test_x, int test_y)
{
	bool ret = false;
	F32 t; //the time interval 
	F32 k = .025; //time step value for drawing curve 
	F32 x1,x2,y1,y2; 
	x1 = controlPoints[0][0]; 
	y1 = controlPoints[0][1]; 
	for(t=k; t<=1/*+k*/; t+=k)
	{
		//use Berstein polynomials 
		x2=(controlPoints[0][0]+t*(-controlPoints[0][0]*3+t*(3*controlPoints[0][0]- 
			controlPoints[0][0]*t)))+t*(3*controlPoints[1][0]+t*(-6*controlPoints[1][0]+ 
			controlPoints[1][0]*3*t))+t*t*(controlPoints[2][0]*3-controlPoints[2][0]*3*t)+ 
			controlPoints[3][0]*t*t*t; 
		y2=(controlPoints[0][1]+t*(-controlPoints[0][1]*3+t*(3*controlPoints[0][1]- 
			controlPoints[0][1]*t)))+t*(3*controlPoints[1][1]+t*(-6*controlPoints[1][1]+ 
			controlPoints[1][1]*3*t))+t*t*(controlPoints[2][1]*3-controlPoints[2][1]*3*t)+ 
			controlPoints[3][1]*t*t*t; 
		//draw curve 
		ret |= gfxDrawLineExCollides(x1, y1, x2, y2, width, test_x, test_y);
		x1 = x2;
		y1 = y2;
	}
	return ret;
}


//////////////////////////////////////////////////////////////////////////
void gfxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, Color color)
{
	gfxDrawLineEx(x1, y1, z, x2, y1, color, color, 1, false);
	gfxDrawLineEx(x2, y1, z, x2, y2, color, color, 1, false);
	gfxDrawLineEx(x2, y2, z, x1, y2, color, color, 1, false);
	gfxDrawLineEx(x1, y2, z, x1, y1, color, color, 1, false);
}

void gfxDrawCBox(CBox* pCbox, F32 z, Color color)
{
	gfxDrawBox(pCbox->lx, pCbox->ly, pCbox->hx, pCbox->hy, z, color);
}

void gfxDrawEllipse(F32 x1, F32 y1, F32 x2, F32 y2, F32 z, U32 cnt, Color color)
{
	U32 i;
	F32 w = (x2-x1)/2.0f;
	F32 h = (y2-y1)/2.0f;
	if(w == 0 || h == 0 || cnt <= 1)
		return;

	for( i=1; i <= cnt; i++ )
	{
		F32 t1 = (i-1)*(2*PI)/(F32)cnt;
		F32 t2 = (i)*(2*PI)/(F32)cnt;
		gfxDrawLineEx(x1 + w + w*cos(t1), y1 + h + h*sin(t1), z, x1 + w + w*cos(t2), y1 + h + h*sin(t2), color, color, 1, false);
	}
}

void gfxDrawQuad(int x1, int y1, int x2, int y2, F32 z, Color color)
{
	gfxDrawQuad4(x1, y1, x2, y2, z, color, color, color, color);
}

void gfxDrawQuad4(int x1, int y1, int x2, int y2, F32 z, Color color_ul, Color color_ur, Color color_ll, Color color_lr)
{
	AtlasTex *white = atlasLoadTexture("white");
	U32 rgba1 = RGBAFromColor(color_ul), rgba2 = RGBAFromColor(color_ur);
	U32 rgba3 = RGBAFromColor(color_ll), rgba4 = RGBAFromColor(color_lr);
	if (x1 > x2)
		SWAP32(x1, x2);
	if (y1 > y2)
		SWAP32(y1, y2);
	display_sprite_4Color(white, x1, y1, z, (x2-x1) / (F32)white->width, (y2-y1) / (F32)white->height, rgba1, rgba2, rgba3, rgba4, 0);
}


//////////////////////////////////////////////////////////////////////////
__forceinline static void gfxDrawLine3DWidthTime_internal(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 width, F32 time)
{
	RdrDrawablePrimitive line={0};
	line.in_3d = 1;
	line.type = RP_LINE;
	copyVec3(p1,line.vertices[0].pos);
	copyVec3(p2,line.vertices[1].pos);
	colorToVec4(line.vertices[0].color, color1);
	colorToVec4(line.vertices[1].color, color2);
	line.linewidth = width;
	line.time_to_draw = time;
	queuePrim(&line);
}

__forceinline static void gfxDrawLine3D_internal(const Vec3 p1, const Vec3 p2, Color color)
{
	gfxDrawLine3DWidthTime_internal(p1,p2,color,color,1,0.0f);
}

__forceinline static void gfxDrawLine3D_2_internal(const Vec3 p1, const Vec3 p2, Color color1, Color color2)
{
	gfxDrawLine3DWidthTime_internal(p1,p2,color1,color2,1,0.0f);
}

__forceinline static void gfxDrawTriangle3D_3_internal(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color1, Color color2, Color color3, F32 time, F32 width, bool filled)
{
	RdrDrawablePrimitive tri={0};
	tri.in_3d = 1;
	tri.type = RP_TRI;
	tri.filled = filled;
	tri.linewidth = width;

	if (dbg_state.test3)
	{
		float scale = dbg_state.test1;
		float bias = dbg_state.test2;
		Vec3 v1, v2, normal;
		Vec3 sundir;
		float ndotl;
		int dark;

		setVec3(sundir, cos(timerGetSecondsAsFloat()/10), 0.8, sin(timerGetSecondsAsFloat()/10));
		normalVec3(sundir);
		subVec3(p2, p1, v1);
		subVec3(p3, p1, v2);
		normalVec3(v1);
		normalVec3(v2);
		crossVec3(v1, v2, normal);
		normalVec3(normal);
		ndotl = dotVec3(normal, sundir);
		dark = CLAMP((1-(bias + scale *ABS(ndotl))) * 255, 0, 255);
		color1 = ColorDarken(color1, dark);
		color2 = ColorDarken(color2, dark);
		color3 = ColorDarken(color3, dark);
		color1.a = 255;
		color2.a = 255;
		color3.a = 255;
		//vec3ToColor(&color1, normal);
		color2 = color3 = color1;
	}

	copyVec3(p1,tri.vertices[0].pos);
	copyVec3(p2,tri.vertices[1].pos);
	copyVec3(p3,tri.vertices[2].pos);
	colorToVec4(tri.vertices[0].color, color1);
	colorToVec4(tri.vertices[1].color, color2);
	colorToVec4(tri.vertices[2].color, color3);
	tri.time_to_draw = time;
	queuePrim(&tri);
}

__forceinline static void gfxDrawTriangle3D_internal(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color)
{
	gfxDrawTriangle3D_3_internal(p1, p2, p3, color, color, color, 0.0f, 1.0, true);
}

__forceinline static void gfxDrawTriangle3D_3vc_internal(const Vec3 p1, const Vec4 c1, const Vec3 p2, const Vec4 c2, const Vec3 p3, const Vec4 c3, bool filled, F32 time)
{
	RdrDrawablePrimitive tri={0};
	tri.in_3d = 1;
	tri.type = RP_TRI;
	tri.filled = filled;
	copyVec3(p1,tri.vertices[0].pos);
	copyVec3(p2,tri.vertices[1].pos);
	copyVec3(p3,tri.vertices[2].pos);
	if ( filled )
	{
		copyVec4(c1, tri.vertices[0].color);
		copyVec4(c2, tri.vertices[1].color);
		copyVec4(c3, tri.vertices[2].color);
	}
	else
	{
		copyVec4(unitvec4, tri.vertices[0].color);
		copyVec4(unitvec4, tri.vertices[1].color);
		copyVec4(unitvec4, tri.vertices[2].color);
	}
	tri.time_to_draw = time;
	queuePrim(&tri);
}

__forceinline static void gfxDrawQuad3D_internal(const Vec3 a, const Vec3 b, const Vec3 c, const Vec3 d, Color color, F32 line_width, F32 time)
{
	PERFINFO_AUTO_START_FUNC();
	if (!line_width)
	{
		RdrDrawablePrimitive quad={0};
		quad.in_3d = 1;
		quad.type = RP_QUAD;
		quad.filled = 1;
		copyVec3(a,quad.vertices[0].pos);
		copyVec3(b,quad.vertices[1].pos);
		copyVec3(c,quad.vertices[2].pos);
		copyVec3(d,quad.vertices[3].pos);
		colorToVec4(quad.vertices[0].color, color);
		colorToVec4(quad.vertices[1].color, color);
		colorToVec4(quad.vertices[2].color, color);
		colorToVec4(quad.vertices[3].color, color);
		quad.time_to_draw = time;
		queuePrim(&quad);
	}
	else
	{
		gfxDrawLine3DWidthTime_internal(a,b,color,color,line_width,time);
		gfxDrawLine3DWidthTime_internal(b,c,color,color,line_width,time);
		gfxDrawLine3DWidthTime_internal(c,d,color,color,line_width,time);
		gfxDrawLine3DWidthTime_internal(d,a,color,color,line_width,time);
	}
	PERFINFO_AUTO_STOP();
}


void gfxDrawLine3DWidth(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 width)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawLine3DWidthTime_internal(p1, p2, color1, color2, width, 0.0f);
}

void gfxDrawLine3DTime(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 time)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawLine3DWidthTime_internal(p1, p2, color1, color2, 1, time);
}

void gfxDrawLine3DWidthTime(const Vec3 p1, const Vec3 p2, Color color1, Color color2, F32 width, F32 time)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawLine3DWidthTime_internal(p1, p2, color1, color2, width, time);
}

void gfxDrawTriangle3D_3(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color1, Color color2, Color color3)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);
	vec3RunningMinMax(p3, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawTriangle3D_3_internal(p1, p2, p3, color1, color2, color3, 0.0f, 1.0, true);
}

void gfxDrawTriangle3D_3Ex(const Vec3 p1, const Vec3 p2, const Vec3 p3, Color color1, Color color2, Color color3, F32 width, bool filled)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);
	vec3RunningMinMax(p3, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawTriangle3D_3_internal(p1, p2, p3, color1, color2, color3, 0, width, filled);
}

void gfxDrawTriangle3D_3vc(const Vec3 p1, const Vec4 c1, const Vec3 p2, const Vec4 c2, const Vec3 p3, const Vec4 c3, bool filled)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(p1, p2, vmin, vmax);
	vec3RunningMinMax(p3, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawTriangle3D_3vc_internal(p1, c1, p2, c2, p3, c3, filled, 0.0f);
}

void gfxDrawQuad3D(const Vec3 a, const Vec3 b, const Vec3 c, const Vec3 d, Color color, F32 line_width)
{
	Vec3 vmin, vmax;

	if (!canAddPrim())
		return;

	vec3MinMax(a, b, vmin, vmax);
	vec3RunningMinMax(c, vmin, vmax);
	vec3RunningMinMax(d, vmin, vmax);

	if (gfx_state.currentVisFrustum && frustumCheckBoundingBox(gfx_state.currentVisFrustum, vmin, vmax, NULL, false))
		gfxDrawQuad3D_internal(a, b, c, d, color, line_width, 0.0f);
}

void gfxDrawBox3DEx(const Vec3 min, const Vec3 max, const Mat4 mat, Color color, F32 line_width, VolumeFaces faceBits)
{
	Vec3	minmax[2], pos;
	Vec3	corners[2][4];
	int		i,y;
	Color	colors[3];

	if (!canAddPrim())
		return;

	if (!gfx_state.currentVisFrustum || !frustumCheckBoundingBox(gfx_state.currentVisFrustum, min, max, mat, false))
		return;

	if (!faceBits)
		faceBits = VOLFACE_ALL;

	if (faceBits == VOLFACE_ALL && !line_width)
	{
		SimplePrimitive *sprim;
		MP_CREATE(SimplePrimitive, 512);
		sprim = MP_ALLOC(SimplePrimitive);
		sprim->shape = SPS_BOX;
		colorToVec4(sprim->color, color);
		copyMat4(mat, sprim->world_matrix);
		copyVec3(min, sprim->local_min);
		copyVec3(max, sprim->local_max);
		sprim->no_ztest = prim_no_ztest;
		sprim->tonemap = prim_tonemap;
		sprim->no_zwrite = (color.a != 255);
		eaPush(&queuedshapes, sprim);
		return;
	}

	// Change brightness of color on each side of box to give it the illusion of 3D/lighting
	for (i=0; i<3; i++) {
		if (!line_width) {
			colors[i].r = (127 + i * 64) * color.r * U8TOF32_COLOR;
			colors[i].g = (127 + i * 64) * color.g * U8TOF32_COLOR;
			colors[i].b = (127 + i * 64) * color.b * U8TOF32_COLOR;
			colors[i].a = color.a;
		} else {
			colors[i] = color;
		}
	}

	copyVec3(min, minmax[0]);
	copyVec3(max, minmax[1]);

	for(y=0;y<2;y++)
	{
		for(i=0;i<4;i++)
		{
			pos[0] = minmax[i==1 || i==2][0];
			pos[1] = minmax[y][1];
			pos[2] = minmax[i/2][2];
			mulVecMat4(pos, mat, corners[y][i]);
		}
	}

	if (faceBits & VOLFACE_NEGY)
		gfxDrawQuad3D(corners[0][0],corners[0][1],corners[0][2],corners[0][3],colors[2],line_width);
	if (faceBits & VOLFACE_POSY)
		gfxDrawQuad3D(corners[1][0],corners[1][1],corners[1][2],corners[1][3],colors[2],line_width);

	if (faceBits & VOLFACE_NEGZ)
		gfxDrawQuad3D(corners[1][0],corners[1][1],corners[0][1],corners[0][0],colors[0],line_width);
	if (faceBits & VOLFACE_POSZ)
		gfxDrawQuad3D(corners[1][2],corners[1][3],corners[0][3],corners[0][2],colors[0],line_width);

	if (faceBits & VOLFACE_NEGX)
		gfxDrawQuad3D(corners[1][3],corners[1][0],corners[0][0],corners[0][3],colors[1],line_width);
	if (faceBits & VOLFACE_POSX)
		gfxDrawQuad3D(corners[1][1],corners[1][2],corners[0][2],corners[0][1],colors[1],line_width);
}

static void sphereCoord(Vec3 coord, F32 radius, F32 a, F32 b)
{
	F32 sina, cosa;
	F32 sinb, cosb;
	sincosf(a, &sina, &cosa);
	sincosf(b, &sinb, &cosb);
	setVec3(coord, radius * cosa * cosb, radius * sinb, radius * sina * cosb);
}

void gfxDrawCylinder3D(const Vec3 pt1, const Vec3 pt2, F32 rad, int nsegs, int capped, Color color, F32 line_width)
{
	// Could easily be sped up many times
	Vec3 dir, dir2;
	int i;
	Vec3 ptt, ptb, pttn, ptbn;
	bool draw_tris = (line_width <= 0.0f);
	Quat q;

	subVec3(pt2, pt1, dir);

	normalVec3(dir);

	if(!getPerpVec3(dir, dir2))
	{
		return;
	}
	
	normalVec3(dir2);

	// Draw caps
	for(i=0; i<nsegs; i++)
	{
		F32 angle = 2*PI*i/nsegs, next = 2*PI*(i+1)/nsegs;
		Vec3 dirtemp;

		axisAngleToQuat(dir, angle, q);
		quatRotateVec3(q, dir2, dirtemp);

		scaleAddVec3(dirtemp, rad, pt1, ptb);
		scaleAddVec3(dirtemp, rad, pt2, ptt);

		if (!draw_tris)
			gfxDrawLine3DWidth(ptb,ptt,color,color,line_width);

		if(capped || draw_tris)
		{
			axisAngleToQuat(dir, next, q);
			quatRotateVec3(q, dir2, dirtemp);

			scaleAddVec3(dirtemp, rad, pt1, ptbn);
			scaleAddVec3(dirtemp, rad, pt2, pttn);

			if (draw_tris)
			{
				gfxDrawQuad3D(ptt, pttn, ptbn, ptb, color, line_width);
				if (capped)
				{
					gfxDrawTriangle3D(ptb, ptbn, pt1, color);
					gfxDrawTriangle3D(ptt, pttn, pt2, color);
				}
			}
			else
			{
				gfxDrawLine3DWidth(ptb,ptbn,color,color,line_width);
				gfxDrawLine3DWidth(ptt,pttn,color,color,line_width);
			}
		}	
	}
}

void gfxDrawHemisphere(const Vec3 center, const Vec3 top, F32 rad, int nsegs, int capped, Color color, F32 line_width)
{
	Vec3 p1, p2, p3;
	Quat q1, q2;
	Vec3 dir1, dir2, dir3, temp;
	int i=0, j;

	subVec3(top, center, dir1);
	normalVec3(dir1);
	if(!getPerpVec3(dir1, dir2))
	{
		return;
	}
	normalVec3(dir2);
	crossVec3(dir1, dir2, dir3);
	normalVec3(dir3);
	axisAngleToQuat(dir3, (PI/2)/(nsegs/2), q1);
	quatNormalize(q1);
	axisAngleToQuat(dir1, 2*PI/nsegs, q2);
	quatNormalize(q2);

	copyVec3(dir2, dir3);
	copyVec3(dir2, dir1);
	quatRotateVec3(q1, dir3, temp);
	copyVec3(temp, dir3);
	quatRotateVec3(q2, dir1, temp);
	copyVec3(temp, dir1);

	for(i=0; i<(nsegs/2); i++)
	{
		for(j=0; j<nsegs; j++)
		{
			scaleAddVec3(dir1, rad, center, p1);
			scaleAddVec3(dir2, rad, center, p2);
			scaleAddVec3(dir3, rad, center, p3);

			gfxDrawLine3DWidth(p2, /*top*/p1, color, color, line_width);
			gfxDrawLine3DWidth(p2, p3, color, color, line_width);

			if(capped && i==0)
			{
				gfxDrawLine3DWidth(p2, center, color, color, line_width);
			}
			
			quatRotateVec3(q2, dir1, temp);
			copyVec3(temp, dir1);
			quatRotateVec3(q2, dir2, temp);
			copyVec3(temp, dir2);
			quatRotateVec3(q2, dir3, temp);
			copyVec3(temp, dir3);
		}

		quatRotateVec3(q1, dir1, temp);
		copyVec3(temp, dir1);
		quatRotateVec3(q1, dir2, temp);
		copyVec3(temp, dir2);
		quatRotateVec3(q1, dir3, temp);
		copyVec3(temp, dir3);

		copyVec3(dir2, dir3);
		copyVec3(dir2, dir1);
		quatRotateVec3(q1, dir3, temp);
		copyVec3(temp, dir3);
		quatRotateVec3(q2, dir1, temp);
		copyVec3(temp, dir1);

	}
}

void gfxDrawVerticalHash(const Vec3 pt, F32 len, Color color, F32 line_width)
{
	Vec3 p1, p2;

	if (!canAddPrim())
		return;

	copyVec3(pt, p1);
	copyVec3(pt, p2);

	p1[1] -= len;
	p2[1] += len;

	gfxDrawLine3DWidth(p1, p2, color, color, line_width);
}

void gfxDrawCapsule3D(const Vec3 bottom, const Vec3 top, F32 rad, int nsegs, Color color, F32 line_width)
{
	Vec3 p1, p2, mid;
	F32 len;

	subVec3(top, bottom, mid);
	len = normalVec3(mid);
	
	if(len <= 0.f){
		copyVec3(unitmat[1], mid);
	}

	if (!canAddPrim())
		return;

	copyVec3(top, p1);
	copyVec3(bottom, p2);
	
	scaleAddVec3(mid, rad, top, p2);
	scaleAddVec3(mid, -rad, bottom, p1);

	scaleAddVec3(mid, len/2, bottom, mid);

	if (!frustumCheckSphereWorld(gfx_state.currentVisFrustum, mid, len/2+rad)) // Eh, close enough
		return;

	gfxDrawCylinder3D(bottom, top, rad, nsegs, 0, color, line_width);

	gfxDrawHemisphere(top, p2, rad, nsegs, 0, color, line_width);
	gfxDrawHemisphere(bottom, p1, rad, nsegs, 0, color, line_width);
}

void gfxDrawSphere3D_internal(const Vec3 mid, F32 rad, int nsegs, Color color, F32 line_width, F32 time)
{
	int		i,j;
	Vec3	lna,lnb;

	if (!canAddPrim())
		return;

	if (!frustumCheckSphereWorld(gfx_state.currentVisFrustum, mid, rad))
		return;

	if (!line_width)
	{
		SimplePrimitive *sprim;
		MP_CREATE(SimplePrimitive, 512);
		sprim = MP_ALLOC(SimplePrimitive);
		sprim->shape = SPS_SPHERE;
		colorToVec4(sprim->color, color);
		copyMat3(unitmat, sprim->world_matrix);
		copyVec3(mid, sprim->world_matrix[3]);
		sprim->radius = rad;
		sprim->no_ztest = prim_no_ztest;
		sprim->tonemap = prim_tonemap;
		sprim->no_zwrite = (color.a != 255);
		sprim->time_to_draw = time;
		eaPush(&queuedshapes, sprim);
		return;
	}

	if (nsegs <= 0)
	{
		if (rad < 50)
			nsegs = round(rad);
		else
			nsegs = 50 + round((rad - 50.f) * 0.1f);
	}
	if (nsegs % 2 == 1)
	{
		nsegs++;
	}

	// cap nsegs to prevent overdrawing
	if (nsegs > 30)
		nsegs = 30;

	if (line_width)
	{
		Vec3	dva,dvb;
		Vec3	l2a,l2b;
		F32		yaw,r2,ratio;

		for(i=0;i<=nsegs;i++)
		{
			for(j=0;j<=nsegs;j++)
			{
				yaw = (j * 2 * PI) / nsegs;
				ratio = (i - nsegs/2) * 2.f / nsegs;
				r2 = rad * cos(ratio * PI * 0.5f);
				r2 = rad * sqrt(1 - ratio * ratio);
				lnb[0] = sin(yaw) * r2;
				lnb[1] = rad * ratio;
				lnb[2] = cos(yaw) * r2;
				if (j)
				{
					addVec3(lna,mid,dva);
					addVec3(lnb,mid,dvb);
					gfxDrawLine3DWidthTime_internal(dva,dvb,color,color,line_width,time);

					l2a[0] = lna[1];
					l2a[1] = lna[0];
					l2a[2] = lna[2];
					l2b[0] = lnb[1];
					l2b[1] = lnb[0];
					l2b[2] = lnb[2];

					addVec3(l2a,mid,dva);
					addVec3(l2b,mid,dvb);
					gfxDrawLine3DWidthTime_internal(dva,dvb,color,color,line_width,time);
				}
				copyVec3(lnb,lna);
			}
		}
	}
	else
	{
		F32 piover2 = PI * 0.5f;
		F32 pioversegs = PI / nsegs;
		F32 angle = 2.f * pioversegs;
		Vec3 lnc, lnd;

		setVec3(lnc, mid[0], mid[1] + rad, mid[2]);
		for (i = 0; i <= nsegs; i++)
		{
			sphereCoord(lnb, rad, piover2-i*angle, piover2 - pioversegs);
			addVec3(lnb, mid, lnb);
			if (i)
				gfxDrawTriangle3D_3_internal(lna, lnb, lnc, color, color, color, time, 1.0, true);
			copyVec3(lnb, lna);
		}

		setVec3(lnc, mid[0], mid[1] - rad, mid[2]);
		for (i = 0; i <= nsegs; i++)
		{
			sphereCoord(lnb, rad, piover2-i*angle, -piover2 + pioversegs);
			addVec3(lnb, mid, lnb);
			if (i)
				gfxDrawTriangle3D_3_internal(lna, lnb, lnc, color, color, color, time, 1.0, true);
			copyVec3(lnb, lna);
		}

		for (i = 1; i < nsegs-1; i++)
		{
			for (j = 0; j <= nsegs; j++)
			{
				sphereCoord(lna, rad, j*angle, piover2-i*pioversegs);
				sphereCoord(lnb, rad, j*angle, piover2-(i+1)*pioversegs);
				sphereCoord(lnc, rad, (j+1)*angle, piover2-(i+1)*pioversegs);
				sphereCoord(lnd, rad, (j+1)*angle, piover2-i*pioversegs);
				addVec3(lna, mid, lna);
				addVec3(lnb, mid, lnb);
				addVec3(lnc, mid, lnc);
				addVec3(lnd, mid, lnd);
				gfxDrawQuad3D_internal(lna, lnb, lnc, lnd, color, 0, time);
			}
		}
	}
}

void gfxDrawSphere3D(const Vec3 mid, F32 rad, int nsegs, Color color, F32 line_width)
{
	gfxDrawSphere3D_internal(mid, rad, nsegs, color, line_width, 0.0f);
}

void gfxDrawSphere3DTime(const Vec3 mid, F32 rad, int nsegs, Color color, F32 line_width, F32 time)
{
	gfxDrawSphere3D_internal(mid, rad, nsegs, color, line_width, time);
}

void gfxDrawCircle3D(const Vec3 mid, const Vec3 norm, const Vec3 zerov, int nsegs, Color color, F32 radius)
{
	int i;
	Vec3 side,front;
	Mat4 orientation;

	// generate orthonormal axes to determine the circle's normal and perpendicular axes for its sides
	copyVec3(zerov,side);
	crossVec3(norm,side,front);
	crossVec3(norm,front,side);
	copyVec3(front, orientation[0]);
	copyVec3(norm, orientation[1]);
	copyVec3(side, orientation[2]);
	copyVec3(mid, orientation[3]);

	for (i = 0; i < nsegs; i++)
	{
		Vec3 start, end;
		Vec3 startFinal, endFinal;

		start[0]=radius*(cos((double)i/(double)nsegs*TWOPI));
		start[1]=0;
		start[2]=radius*(sin((double)i/(double)nsegs*TWOPI));
		end[0]=radius*(cos((double)(i+1)/(double)nsegs*TWOPI));
		end[1]=0;
		end[2]=radius*(sin((double)(i+1)/(double)nsegs*TWOPI));
		mulVecMat4(start,orientation,startFinal);
		mulVecMat4(end,orientation,endFinal);

		gfxDrawLine3DWidth(startFinal,endFinal,color,color,2);
	}
}

/*
 * This function draws a cone along the negative y-axis of a particular transformation matrix.  The cone is drawn such
 * that its boundaries end along the cone's intersection with a sphere of a specified radius.  The cone's half
 * angle and color are also specified.
 * PARAMS:
 *   mat - the transformation Mat4; the negative y-axis determines the cone's direction, and the translation of the
 *         matrix determines the cone's position
 *   minRad - float radius of an inner sphere that could be used to denote a "min radius"
 *   rad - float radius of the sphere determining where the cone drawing ends
 *   halfAngle - float equal to half of the desired inner angle of the cone; in RADIANS
 *   nsegs - int number of segments to be used in the cone's various circular/angular parts
 *   c - the Color of the cone
 *   cInner - the Color of the inner sphere located at minRad
 *   cOuter - the Color of the outer sphere located at rad
 *   drawInner - bool indicating whether to draw the inner sphere at all
 *   drawOuter - bool indicating whether to draw the outer sphere at all
 */
#define MIN_CONE_RAD RAD(0.1f)
#define MIN_CONE_ANG RAD(0.5f)
void gfxDrawCone3D_min(	const Mat4 mat, 
						F32 innerRadius, 
						F32 outerRadius, 
						F32 halfAngle,
						F32 startingRadius,
						int nsegs, 
						Color c, 
						Color cInner, 
						Color cOuter, 
						bool drawInner, 
						bool drawOuter)
{
	int i, j;
	F32 f;
	Vec3 temp1, temp2, temp3, old, oldMin;
	Vec3 outCenter, outMinCenter, newCenter, newMinCenter, first, firstMin, minRadEnd;
	F32 forwDist = outerRadius * cos(halfAngle);
	F32 forwMinDist = innerRadius * cos(halfAngle);
	F32 smallRad = outerRadius * sin(halfAngle);
	F32 smallMinRad = innerRadius * sin(halfAngle);
	F32 increment, twopiovernsegs;
	int halfnsegs;

	// don't bother with negligibly sized cones
	if ((innerRadius < MIN_CONE_RAD && outerRadius < MIN_CONE_RAD) || halfAngle < MIN_CONE_ANG)
		return;

	if (!canAddPrim())
		return;

	if (!frustumCheckSphereWorld(gfx_state.currentVisFrustum, mat[3], MAX(innerRadius, outerRadius)))
		return;

	if (nsegs <= 0)
	{
		if (outerRadius < 50)
			nsegs = round(outerRadius * 0.5);
		else
			nsegs = 25 + round((outerRadius - 50.f) * 0.1f);
	}
	// nsegs must be even;
	if (nsegs % 2 == 1)
	{
		nsegs++;
	}

	// cap nsegs in case of overdrawing
	if (nsegs > 30)
		nsegs = 30;

	increment = halfAngle / nsegs;
	halfnsegs = nsegs / 2;
	twopiovernsegs = TWOPI / nsegs;

	for (f = 0; f < halfAngle; f += increment)
	{
		float tempForwDist = outerRadius * cos(halfAngle - f);
		float tempSmallRad = outerRadius * sin(halfAngle - f);
		float tempForwMinDist = innerRadius * cos(halfAngle - f);
		float tempSmallMinRad = innerRadius * sin(halfAngle - f);
		scaleVec3(mat[1], -tempForwDist, outCenter);
		addVec3(mat[3], outCenter, newCenter);
		scaleVec3(mat[1], -tempForwMinDist, outMinCenter);
		addVec3(mat[3], outMinCenter, newMinCenter);

		// draw the radial cone lines
		for (i = 0; i < nsegs; i++)
		{
			Vec3 vStartPoint;
			Vec3 vStartingOffset;
			F32 fCosSegment = cos(i * twopiovernsegs);
			F32 fSinSegment = sin(i * twopiovernsegs);

			scaleVec3(mat[2], tempSmallRad * fCosSegment, temp1);
			scaleVec3(mat[0], tempSmallRad * fSinSegment, temp2);
			addVec3(temp1, temp2, temp2);
			addVec3(newCenter, temp2, temp2);
			scaleVec3(mat[2], tempSmallMinRad * fCosSegment, temp3);
			scaleVec3(mat[0], tempSmallMinRad * fSinSegment, minRadEnd);
			addVec3(minRadEnd, temp3, minRadEnd);
			addVec3(newMinCenter, minRadEnd, minRadEnd);
			
			scaleVec3(mat[2], startingRadius * fCosSegment, temp1);
			scaleVec3(mat[0], startingRadius * fSinSegment, temp3);
			addVec3(temp1, temp3, vStartingOffset);
			addVec3(mat[3], vStartingOffset, vStartPoint);

			if (f == 0)
			{
				gfxDrawLine3D_internal(vStartPoint, temp2, c);
			}
			if (i)
			{
				if (drawInner)
				{
					gfxDrawLine3D_internal(oldMin, minRadEnd, cInner);
				}
				if (drawOuter)
				{
					gfxDrawLine3D_internal(old, temp2, cOuter);
				}
			}
			else
			{
				copyVec3(temp2, first);
				copyVec3(minRadEnd, firstMin);
			}
			copyVec3(temp2, old);
			copyVec3(minRadEnd, oldMin);

			// draw the radial arcs on the inside of the cone
			if (drawInner && (i < halfnsegs) && f == 0)
			{
				Vec3 angleVec1, angleVec2, rotAxis;
				Quat rotQuat;
				Mat3 rotMat;
				int k;
				subVec3(minRadEnd, mat[3], temp3);
				scaleVec3(mat[2], cos((i + halfnsegs) * twopiovernsegs), angleVec1);
				scaleVec3(mat[0], sin((i + halfnsegs) * twopiovernsegs), angleVec2);
				addVec3(angleVec1, angleVec2, angleVec2);
				if (halfAngle < (PI / 2.0))
				{
					crossVec3(angleVec2, outMinCenter, rotAxis);
				}
				else
				{
					crossVec3(mat[1], angleVec2, rotAxis);
				}

				axisAngleToQuat(rotAxis, halfAngle / nsegs, rotQuat);
				quatToMat(rotQuat, rotMat);

				for (k = 0; k < nsegs * 2; k++)
				{
					Vec3 shiftedEnd1, shiftedEnd2;
					mulVecMat3(temp3, rotMat, shiftedEnd2);
					addVec3(temp3, mat[3], shiftedEnd1);
					copyVec3(shiftedEnd2, temp3);
					addVec3(shiftedEnd2, mat[3], shiftedEnd2);
					gfxDrawLine3D_internal(shiftedEnd1, shiftedEnd2, cInner);
				}
			}
	
			// draw the radial arcs on the outside of the cone
			if (drawOuter && (i < halfnsegs) && f == 0)
			{
				Vec3 angleVec1, angleVec2, rotAxis;
				Quat rotQuat;
				Mat3 rotMat;
				int k;
				subVec3(temp2, mat[3], temp1);
				scaleVec3(mat[2], cos((i + halfnsegs) * twopiovernsegs), angleVec1);
				scaleVec3(mat[0], sin((i + halfnsegs) * twopiovernsegs), angleVec2);
				addVec3(angleVec1, angleVec2, angleVec2);
				if (halfAngle < (PI / 2.0))
				{
					crossVec3(angleVec2, outCenter, rotAxis);
				}
				else
				{
					crossVec3(mat[1], angleVec2, rotAxis);
				}

				axisAngleToQuat(rotAxis, halfAngle / nsegs, rotQuat);
				quatToMat(rotQuat, rotMat);

				for (k = 0; k < nsegs * 2; k++)
				{
					Vec3 shiftedEnd1, shiftedEnd2;
					mulVecMat3(temp1, rotMat, shiftedEnd2);
					addVec3(temp1, mat[3], shiftedEnd1);
					copyVec3(shiftedEnd2, temp1);
					addVec3(shiftedEnd2, mat[3], shiftedEnd2);
					gfxDrawLine3D_internal(shiftedEnd1, shiftedEnd2, cOuter);
				}
			}
		}
		if (drawInner)
		{
			gfxDrawLine3D_internal(firstMin, oldMin, cInner);
		}
		if (drawOuter)
		{
			gfxDrawLine3D_internal(first, old, cOuter);
		}
	}

	// draw the circles along the cone
	for (j = 0; j < halfnsegs; j++)
	{
		F32 dist = j / (F32)halfnsegs;
		
		scaleVec3(mat[1], -forwDist * dist, newCenter);
		addVec3(mat[3], newCenter, newCenter);
		for (i = 0; i < nsegs; i++)
		{
			F32 startingWidthAdjustment = startingRadius * (1.f - dist);
			F32 scaleZ = (startingWidthAdjustment + (smallRad) * dist) * cos(i * twopiovernsegs);
			F32 scaleX = (startingWidthAdjustment + (smallRad) * dist) * sin(i * twopiovernsegs);

			scaleVec3(mat[2], scaleZ, temp1);
			scaleVec3(mat[0], scaleX, temp2);
			addVec3(temp1, temp2, temp2);
			addVec3(newCenter, temp2, temp2);
			if (i)
			{
				gfxDrawLine3D_internal(old, temp2, c);
			}
			else
			{
				copyVec3(temp2, first);
			}
			copyVec3(temp2, old);
		}
		gfxDrawLine3D_internal(first, old, c);
	}
}

void gfxDrawCone3DEx(const Mat4 mat, F32 radius, F32 halfAngle, F32 startingRadius, int nsegs, Color c)
{
	gfxDrawCone3D_min(mat, 0, radius, halfAngle, startingRadius, nsegs, c, c, c, false, true);
}

void gfxDrawPyramid3D(const Mat4 mat, F32 innerRadius, F32 outerRadius, F32 angle1, F32 angle2, Color fillColor, Color lineColor)
{
	F32 uMax, vMax;
	Vec3 boundsMin, boundsMax, temp, p[8];

	if (!canAddPrim())
		return;

	if (!outerRadius && !innerRadius)
		return;

	uMax = outerRadius * tanf(angle1);
	vMax = outerRadius * tanf(angle2);

	setVec3(boundsMin, -uMax, -outerRadius, -vMax);
	setVec3(boundsMax, uMax, 0, vMax);

	if (!frustumCheckBoundingBox(gfx_state.currentVisFrustum, boundsMin, boundsMax, mat, false))
		return;

	setVec3(temp, -uMax, -outerRadius, -vMax);
	mulVecMat4(temp, mat, p[0]);

	setVec3(temp, uMax, -outerRadius, -vMax);
	mulVecMat4(temp, mat, p[1]);

	setVec3(temp, uMax, -outerRadius, vMax);
	mulVecMat4(temp, mat, p[2]);

	setVec3(temp, -uMax, -outerRadius, vMax);
	mulVecMat4(temp, mat, p[3]);

	// draw outer quad
	if (fillColor.a)
		gfxDrawQuad3D_internal(p[0], p[1], p[2], p[3], fillColor, 0, 0);
	if (lineColor.a)
		gfxDrawQuad3D_internal(p[0], p[1], p[2], p[3], lineColor, 1, 0);

	if (innerRadius == 0)
	{
		// draw connecting lines and tris
		if (lineColor.a)
		{
			gfxDrawLine3D_internal(mat[3], p[0], lineColor);
			gfxDrawLine3D_internal(mat[3], p[1], lineColor);
			gfxDrawLine3D_internal(mat[3], p[2], lineColor);
			gfxDrawLine3D_internal(mat[3], p[3], lineColor);
		}
		if (fillColor.a)
		{
			gfxDrawTriangle3D_internal(mat[3], p[0], p[1], fillColor);
			gfxDrawTriangle3D_internal(mat[3], p[1], p[2], fillColor);
			gfxDrawTriangle3D_internal(mat[3], p[2], p[3], fillColor);
			gfxDrawTriangle3D_internal(mat[3], p[3], p[0], fillColor);
		}
	}
	else
	{
		uMax = innerRadius * tanf(angle1);
		vMax = innerRadius * tanf(angle2);

		setVec3(temp, -uMax, -innerRadius, -vMax);
		mulVecMat4(temp, mat, p[4]);

		setVec3(temp, uMax, -innerRadius, -vMax);
		mulVecMat4(temp, mat, p[5]);

		setVec3(temp, uMax, -innerRadius, vMax);
		mulVecMat4(temp, mat, p[6]);

		setVec3(temp, -uMax, -innerRadius, vMax);
		mulVecMat4(temp, mat, p[7]);

		// draw inner quad
		if (fillColor.a)
			gfxDrawQuad3D_internal(p[4], p[5], p[6], p[7], fillColor, 0, 0);
		if (lineColor.a)
			gfxDrawQuad3D_internal(p[4], p[5], p[6], p[7], lineColor, 1, 0);

		// draw connecting lines and quads
		if (lineColor.a)
		{
			gfxDrawLine3D_internal(p[4], p[0], lineColor);
			gfxDrawLine3D_internal(p[5], p[1], lineColor);
			gfxDrawLine3D_internal(p[6], p[2], lineColor);
			gfxDrawLine3D_internal(p[7], p[3], lineColor);
		}
		if (fillColor.a)
		{
			gfxDrawQuad3D_internal(p[0], p[1], p[5], p[4], fillColor, 0, 0);
			gfxDrawQuad3D_internal(p[1], p[2], p[6], p[5], fillColor, 0, 0);
			gfxDrawQuad3D_internal(p[2], p[3], p[7], p[6], fillColor, 0, 0);
			gfxDrawQuad3D_internal(p[3], p[0], p[4], p[7], fillColor, 0, 0);
		}
	}
}

void gfxDrawAxes3D(const Mat4 mat, F32 fLength)
{
	int i;
	U32 uiColor[3] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF };
	for (i=0; i<3; ++i)
	{
		Vec3 vEnd;
		scaleAddVec3(mat[i], fLength, mat[3], vEnd);
		gfxDrawLine3DARGB(mat[3], vEnd, uiColor[i]);
	}
}

void gfxDrawAxesFromTransform(const DynTransform* pxTransform, F32 fLength)
{
	Mat4 mat;
	dynTransformToMat4(pxTransform, mat);
	gfxDrawAxes3D(mat, fLength);
}


void gfxDrawClothMeshPrimitive(const Mat4 mat, const Vec4 color, const DynClothMesh* cloth_mesh)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	RdrDrawList* draw_list = gdraw->draw_list;
	RdrDrawableMeshPrimitive* mesh = rdrDrawListAllocMeshPrimitive(draw_list, RTYPE_PRIMITIVE_MESH, cloth_mesh->NumPoints, cloth_mesh->NumStrips, false);
	int i;

	if (!mesh)
		return;

	for (i=0; i<cloth_mesh->NumStrips; ++i)
	{
		RdrDrawableMeshPrimitiveStrip* strip = rdrDrawListAllocMeshPrimitiveStrip(draw_list, mesh, i, cloth_mesh->Strips[i].NumIndices);
		memcpy(strip->indices, cloth_mesh->Strips[i].IndicesCCW, sizeof(U16) * strip->num_indices);
		switch (cloth_mesh->Strips[i].Type)
		{
			xcase CLOTHMESH_TRILIST:
				strip->type = RP_TRILIST;
			xcase CLOTHMESH_TRISTRIP:
				strip->type = RP_TRISTRIP;
		}
	}
	for (i=0; i<cloth_mesh->NumPoints; ++i)
	{
		RdrPrimitiveVertex* vert = &mesh->verts[i];
		copyVec3(cloth_mesh->Points[i], vert->pos);
		copyVec4(color, vert->color);
	}

	mesh->no_zwrite = 1;
	rdrDrawListAddMeshPrimitive(draw_list, mesh, mat, RST_AUTO, RTYPE_PRIMITIVE_MESH);
}

//////////////////////////////////////////////////////////////////////////

static Model *pSysUnitBox;
static Model *pSysUnitSphere;
static Model *pSysUnitCylinder;
static Model *pSysUnitHemisphere;
static Model *pSysUnitCapsule;
static RdrDrawableGeo *pSysUnitBoxGeoDrawCached;
static RdrDrawableGeo *pSysUnitSphereGeoDrawCached;
static RdrDrawableGeo *pSysUnitCylinderGeoDrawCached;
static RdrDrawableGeo *pSysUnitHemisphereGeoDrawCached;
static RdrDrawableGeo *pSysUnitCapsuleGeoDrawCached;
static RdrSubobject *pSysUnitBoxSubobjectCached;
static RdrSubobject *pSysUnitSphereSubobjectCached;
static RdrSubobject *pSysUnitCylinderSubobjectCached;
static RdrSubobject *pSysUnitHemisphereSubobjectCached;
static RdrSubobject *pSysUnitCapsuleSubobjectCached;

void gfxPrimitiveDoDataLoading(void)
{
	if (gbNo3DGraphics)
		return;
	pSysUnitBox = modelFind("sys_unit_box", true, WL_FOR_ENTITY);
	pSysUnitSphere = modelFind("sys_unit_sphere", true, WL_FOR_ENTITY);
	pSysUnitCylinder = modelFind("sys_unit_cylinder", true, WL_FOR_ENTITY);
	pSysUnitHemisphere = modelFind("sys_unit_hemisphere", true, WL_FOR_ENTITY);
	pSysUnitCapsule = modelFind("sys_unit_capsule_01", true, WL_FOR_ENTITY);
	assertmsg(pSysUnitBox && pSysUnitSphere && pSysUnitCylinder && pSysUnitHemisphere && pSysUnitCapsule, "Failed to load basic shapes, data is corrupt or missing");
}

void gfxPrimitiveClearDrawables(void)
{
	pSysUnitBoxGeoDrawCached = NULL;
	pSysUnitSphereGeoDrawCached = NULL;
	pSysUnitCylinderGeoDrawCached = NULL;
	pSysUnitHemisphereGeoDrawCached = NULL;
	pSysUnitCapsuleGeoDrawCached = NULL;
	pSysUnitBoxSubobjectCached = NULL;
	pSysUnitSphereSubobjectCached = NULL;
	pSysUnitCylinderSubobjectCached = NULL;
	pSysUnitHemisphereSubobjectCached = NULL;
	pSysUnitCapsuleSubobjectCached = NULL;
}

void gfxPrimitiveCleanupPerFrame()
{
	gfxPrimitiveClearDrawables();
	gfxFreeOrRequeueSimplePrims();
	gfxFreeOrRequeueDrawablePrims();
}

void gfxQueueBox(const Vec3 vMin, const Vec3 vMax, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	static RdrInstancePerDrawableData per_drawable_data;
	Vec3 vMid, vTemp, vRadius;
	Model *pModel = pSysUnitBox;

	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	// use LOD 0
	if (!gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if (!models[0].geo_handle_primary)
	{
		devassert(0);
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	subVec3(vMax, vMin, vRadius);
	scaleVec3(vRadius, 0.5f, vRadius);
	addVec3(vMin, vRadius, vMid);

	mulVecMat3(vMid, mWorldMatrix, vTemp);
	scaleVec3(mWorldMatrix[0], vRadius[0], instance_params.instance.world_matrix[0]);
	scaleVec3(mWorldMatrix[1], vRadius[1], instance_params.instance.world_matrix[1]);
	scaleVec3(mWorldMatrix[2], vRadius[2], instance_params.instance.world_matrix[2]);
	addVec3(vTemp, mWorldMatrix[3], instance_params.instance.world_matrix[3]);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);
	instance_params.distance_offset = MAX(vRadius[0], vRadius[1]);
	MAX1(instance_params.distance_offset, vRadius[2]);
	instance_params.frustum_visible = frustum_visible;

	if (!tonemap)
		add_material_flags |= RMATERIAL_NOBLOOM;

	if (!pSysUnitBoxGeoDrawCached)
	{
		pSysUnitBoxGeoDrawCached = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, models[0].model->geo_render_info->subobject_count, 0, 0);

		if (!pSysUnitBoxGeoDrawCached)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}

		pSysUnitBoxGeoDrawCached->geo_handle_primary = models[0].geo_handle_primary;

		assert(pSysUnitBoxGeoDrawCached->subobject_count==1);
		pSysUnitBoxSubobjectCached = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_count);
		gfxDemandLoadMaterialAtQueueTime(pSysUnitBoxSubobjectCached, models[0].model->materials[0], NULL, NULL, NULL, NULL, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
	}

	RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
	instance_params.subobjects[0] = pSysUnitBoxSubobjectCached;

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		gfxGetUnlitLight(&light_params);
		instance_params.light_params = &light_params;
		if (!tonemap)
		{
			Vec3 ambient;
			setVec3same(ambient, 0.5f * gfx_state.currentCameraView->adapted_light_range);
			light_params.ambient_light = gfxGetOverrideAmbientLight(ambient, zerovec3, zerovec3, zerovec3);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	instance_params.per_drawable_data = &per_drawable_data;
	instance_params.add_material_flags |= add_material_flags;
	rdrDrawListAddGeoInstance(gdraw->draw_list, pSysUnitBoxGeoDrawCached, &instance_params, RST_AUTO, object_category, true);

	PERFINFO_AUTO_STOP_L2();
}

void gfxQueueSphere(const Vec3 vMid, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	static RdrInstancePerDrawableData per_drawable_data;
	Vec3 vTemp, vMin, vMax;
	Model *pModel = pSysUnitSphere;

	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	// use LOD 0
	if (!gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if (!models[0].geo_handle_primary)
	{
		devassert(0);
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	addVec3same(vMid, -fRadius, vMin);
	addVec3same(vMid, fRadius, vMax);

	mulVecMat3(vMid, mWorldMatrix, vTemp);
	scaleVec3(mWorldMatrix[0], fRadius, instance_params.instance.world_matrix[0]);
	scaleVec3(mWorldMatrix[1], fRadius, instance_params.instance.world_matrix[1]);
	scaleVec3(mWorldMatrix[2], fRadius, instance_params.instance.world_matrix[2]);
	addVec3(vTemp, mWorldMatrix[3], instance_params.instance.world_matrix[3]);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);
	instance_params.distance_offset = fRadius;
	instance_params.frustum_visible = frustum_visible;

	if (!tonemap)
		add_material_flags |= RMATERIAL_NOBLOOM;

	if (!pSysUnitSphereGeoDrawCached)
	{
		pSysUnitSphereGeoDrawCached = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, models[0].model->geo_render_info->subobject_count, 0, 0);

		if (!pSysUnitSphereGeoDrawCached)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}

		pSysUnitSphereGeoDrawCached->geo_handle_primary = models[0].geo_handle_primary;

		assert(pSysUnitSphereGeoDrawCached->subobject_count==1);
		pSysUnitSphereSubobjectCached = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_count);
		gfxDemandLoadMaterialAtQueueTime(pSysUnitSphereSubobjectCached, models[0].model->materials[0], NULL, NULL, NULL, NULL, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
	}

	RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
	instance_params.subobjects[0] = pSysUnitSphereSubobjectCached;

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		gfxGetUnlitLight(&light_params);
		instance_params.light_params = &light_params;
		if (!tonemap)
		{
			Vec3 ambient;
			setVec3same(ambient, 0.5f * gfx_state.currentCameraView->adapted_light_range);
			light_params.ambient_light = gfxGetOverrideAmbientLight(ambient, zerovec3, zerovec3, zerovec3);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	instance_params.per_drawable_data = &per_drawable_data;
	instance_params.add_material_flags |= add_material_flags;
	rdrDrawListAddGeoInstance(gdraw->draw_list, pSysUnitSphereGeoDrawCached, &instance_params, RST_AUTO, object_category, true);

	PERFINFO_AUTO_STOP_L2();
}

void gfxQueueCylinder(const Vec3 vMid, F32 fHeightRadius, F32 fRadialRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	static RdrInstancePerDrawableData per_drawable_data;
	Vec3 vTemp, vMin, vMax;
	Model *pModel = pSysUnitCylinder;

	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	// use LOD 0
	if (!gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if (!models[0].geo_handle_primary)
	{
		devassert(0);
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	setVec3(vMin, vMid[0] - fRadialRadius, vMid[1] - fHeightRadius, vMid[2] - fRadialRadius);
	setVec3(vMax, vMid[0] + fRadialRadius, vMid[1] + fHeightRadius, vMid[2] + fRadialRadius);

	mulVecMat3(vMid, mWorldMatrix, vTemp);
	scaleVec3(mWorldMatrix[0], fRadialRadius, instance_params.instance.world_matrix[0]);
	scaleVec3(mWorldMatrix[1], fHeightRadius, instance_params.instance.world_matrix[1]);
	scaleVec3(mWorldMatrix[2], fRadialRadius, instance_params.instance.world_matrix[2]);
	addVec3(vTemp, mWorldMatrix[3], instance_params.instance.world_matrix[3]);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);
	instance_params.distance_offset = distance3(vMid, vMax);
	instance_params.frustum_visible = frustum_visible;

	if (!tonemap)
		add_material_flags |= RMATERIAL_NOBLOOM;

	if (!pSysUnitCylinderGeoDrawCached)
	{
		pSysUnitCylinderGeoDrawCached = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, models[0].model->geo_render_info->subobject_count, 0, 0);

		if (!pSysUnitCylinderGeoDrawCached)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}

		pSysUnitCylinderGeoDrawCached->geo_handle_primary = models[0].geo_handle_primary;

		assert(pSysUnitCylinderGeoDrawCached->subobject_count==1);
		pSysUnitCylinderSubobjectCached = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_count);
		gfxDemandLoadMaterialAtQueueTime(pSysUnitCylinderSubobjectCached, models[0].model->materials[0], NULL, NULL, NULL, NULL, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
	}

	RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
	instance_params.subobjects[0] = pSysUnitCylinderSubobjectCached;

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		gfxGetUnlitLight(&light_params);
		instance_params.light_params = &light_params;
		if (!tonemap)
		{
			Vec3 ambient;
			setVec3same(ambient, 0.5f * gfx_state.currentCameraView->adapted_light_range);
			light_params.ambient_light = gfxGetOverrideAmbientLight(ambient, zerovec3, zerovec3, zerovec3);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	instance_params.per_drawable_data = &per_drawable_data;
	instance_params.add_material_flags |= add_material_flags;
	rdrDrawListAddGeoInstance(gdraw->draw_list, pSysUnitCylinderGeoDrawCached, &instance_params, RST_AUTO, object_category, true);

	PERFINFO_AUTO_STOP_L2();
}

void gfxQueueHemisphere(const Vec3 vMid, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	static RdrInstancePerDrawableData per_drawable_data;
	Vec3 vTemp, vMin, vMax;
	Model *pModel = pSysUnitHemisphere;

	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	// use LOD 0
	if (!gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if (!models[0].geo_handle_primary)
	{
		devassert(0);
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	setVec3(vMin, vMid[0] - fRadius, vMid[1], vMid[2] - fRadius);
	addVec3same(vMid, fRadius, vMax);

	mulVecMat3(vMid, mWorldMatrix, vTemp);
	scaleVec3(mWorldMatrix[0], fRadius, instance_params.instance.world_matrix[0]);
	scaleVec3(mWorldMatrix[1], fRadius, instance_params.instance.world_matrix[1]);
	scaleVec3(mWorldMatrix[2], fRadius, instance_params.instance.world_matrix[2]);
	addVec3(vTemp, mWorldMatrix[3], instance_params.instance.world_matrix[3]);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);
	instance_params.distance_offset = fRadius;
	instance_params.frustum_visible = frustum_visible;

	if (!tonemap)
		add_material_flags |= RMATERIAL_NOBLOOM;

	if (!pSysUnitHemisphereGeoDrawCached)
	{
		pSysUnitHemisphereGeoDrawCached = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, models[0].model->geo_render_info->subobject_count, 0, 0);

		if (!pSysUnitHemisphereGeoDrawCached)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}

		pSysUnitHemisphereGeoDrawCached->geo_handle_primary = models[0].geo_handle_primary;

		assert(pSysUnitHemisphereGeoDrawCached->subobject_count==1);
		pSysUnitHemisphereSubobjectCached = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_count);
		gfxDemandLoadMaterialAtQueueTime(pSysUnitHemisphereSubobjectCached, models[0].model->materials[0], NULL, NULL, NULL, NULL, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
	}

	RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
	instance_params.subobjects[0] = pSysUnitHemisphereSubobjectCached;

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		gfxGetUnlitLight(&light_params);
		instance_params.light_params = &light_params;
		if (!tonemap)
		{
			Vec3 ambient;
			setVec3same(ambient, 0.5f * gfx_state.currentCameraView->adapted_light_range);
			light_params.ambient_light = gfxGetOverrideAmbientLight(ambient, zerovec3, zerovec3, zerovec3);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	instance_params.per_drawable_data = &per_drawable_data;
	instance_params.add_material_flags |= add_material_flags;
	rdrDrawListAddGeoInstance(gdraw->draw_list, pSysUnitHemisphereGeoDrawCached, &instance_params, RST_AUTO, object_category, true);

	PERFINFO_AUTO_STOP_L2();
}

void gfxQueueCapsuleExact(F32 fHeightMin, F32 fHeightMax, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap)
{
	Mat4 mTempMatrix;
	F32 fHalfHeight;
	Vec3 vTemp;

	setVec3(vTemp, 0, -fHeightMin, 0);
	scaleVec3(mWorldMatrix[0], -1, mTempMatrix[0]);
	scaleVec3(mWorldMatrix[1], -1, mTempMatrix[1]);
	copyVec3(mWorldMatrix[2], mTempMatrix[2]);
	copyVec3(mWorldMatrix[3], mTempMatrix[3]);
	gfxQueueHemisphere(vTemp, fRadius, mTempMatrix, frustum_visible, object_category, add_material_flags, tint_color, tonemap);

	setVec3(vTemp, 0, fHeightMax, 0);
	gfxQueueHemisphere(vTemp, fRadius, mWorldMatrix, frustum_visible, object_category, add_material_flags, tint_color, tonemap);

	fHalfHeight = 0.5f * (fHeightMax - fHeightMin);
	setVec3(vTemp, 0, fHeightMin + fHalfHeight, 0);
	gfxQueueCylinder(vTemp, fHalfHeight, fRadius, mWorldMatrix, frustum_visible, object_category, add_material_flags, tint_color, tonemap);
}

void gfxQueueCapsule(F32 fHeightMin, F32 fHeightMax, F32 fRadius, const Mat4 mWorldMatrix, int frustum_visible, RdrObjectCategory object_category, RdrMaterialFlags add_material_flags, const Vec4 tint_color, bool tonemap, int wireframe)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	static RdrInstancePerDrawableData per_drawable_data;
	Vec3 vMin, vMax;
	F32 fMidPoint;
	F32 fHalfHeight;
	Model *pModel = pSysUnitCapsule;

	PERFINFO_AUTO_START_L2(__FUNCTION__, 1);

	fHalfHeight = 0.5f * (fHeightMax - fHeightMin);
	fMidPoint = fHeightMin + fHalfHeight;

	// use LOD 0
	if (!gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, 0))
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	if (!models[0].geo_handle_primary)
	{
		devassert(0);
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	setVec3(vMax, fRadius, fRadius, fHalfHeight + fRadius);
	negateVec3(vMax, vMin);

	scaleVec3(mWorldMatrix[0], fRadius, instance_params.instance.world_matrix[0]);
	// So the capsule model we use has a half-axis of 2 (including the hemisphere caps),
	// centered on the origin, and we want the actual scaled length to be the calculated 
	// half-axis fHalfHeight, plus the radius of the cylinder. The scale factor is that 
	// value divided by 2, which is the actual half-axis of the capsule model.
	scaleVec3(mWorldMatrix[1], ( fHalfHeight + fRadius ) * 0.5f, instance_params.instance.world_matrix[1]);
	scaleVec3(mWorldMatrix[2], fRadius, instance_params.instance.world_matrix[2]);
	scaleAddVec3(mWorldMatrix[1],
		fMidPoint, 
		//fHeightMin, 
		mWorldMatrix[3],
		instance_params.instance.world_matrix[3]);
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);
	instance_params.distance_offset = fRadius;
	instance_params.frustum_visible = frustum_visible;

	if (!tonemap)
		add_material_flags |= RMATERIAL_NOBLOOM;

	if (!pSysUnitCapsuleGeoDrawCached)
	{
		pSysUnitCapsuleGeoDrawCached = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, models[0].model->geo_render_info->subobject_count, 0, 0);

		if (!pSysUnitCapsuleGeoDrawCached)
		{
			PERFINFO_AUTO_STOP_L2();
			return;
		}

		pSysUnitCapsuleGeoDrawCached->geo_handle_primary = models[0].geo_handle_primary;

		assert(pSysUnitCapsuleGeoDrawCached->subobject_count==1);
		pSysUnitCapsuleSubobjectCached = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_count);
		gfxDemandLoadMaterialAtQueueTime(pSysUnitCapsuleSubobjectCached, models[0].model->materials[0], NULL, NULL, NULL, NULL, per_drawable_data.instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, models[0].model->geo_render_info->subobject_count, true);
	}

	RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
	instance_params.subobjects[0] = pSysUnitCapsuleSubobjectCached;

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		gfxGetUnlitLight(&light_params);
		instance_params.light_params = &light_params;
		if (!tonemap)
		{
			Vec3 ambient;
			setVec3same(ambient, 0.5f * gfx_state.currentCameraView->adapted_light_range);
			light_params.ambient_light = gfxGetOverrideAmbientLight(ambient, zerovec3, zerovec3, zerovec3);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);
	copyVec4(tint_color, instance_params.instance.color);
	instance_params.per_drawable_data = &per_drawable_data;
	instance_params.add_material_flags |= add_material_flags;
	instance_params.wireframe = wireframe;
	rdrDrawListAddGeoInstance(gdraw->draw_list, pSysUnitCapsuleGeoDrawCached, &instance_params, RST_AUTO, object_category, true);

	PERFINFO_AUTO_STOP_L2();
}

