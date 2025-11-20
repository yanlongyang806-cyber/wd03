#ifndef NO_EDITORS

#include "GfxPrimitive.h"
#include "GfxCamera.h"


#include "WorldGrid.h"




#include "SplineEditUI.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// Drawing

void splineUIDrawControlPointWidget(Vec3 pos, Vec3 dir, Vec3 up, Color color, bool attached, F32 scale)
{
	Vec3 corners[4];
	Vec3 left, up2;
	Color corner_color[4];

	corner_color[0] = ColorDarken(color, 0);
	corner_color[1] = ColorDarken(color, 30);
	corner_color[2] = ColorDarken(color, 60);
	corner_color[3] = ColorDarken(color, 90);
	crossVec3(up, dir, left);
	scaleVec3(up, 2.5, up2);
	scaleVec3(left, 4, left);
	if(scale)
	{
		scaleVec3(up2, scale, up2);
		scaleVec3(left, scale, left);
		scaleVec3(dir, -12*scale, corners[0]);
	}
    else if (attached)
		scaleVec3(dir, -12, corners[0]);
    else
		scaleVec3(dir, -6, corners[0]);
	copyVec3(corners[0], corners[1]);
	copyVec3(corners[0], corners[2]);
	copyVec3(corners[0], corners[3]);
	addVec3(corners[0], left, corners[0]);
	subVec3(corners[1], left, corners[1]);
	addVec3(corners[3], left, corners[3]);
	subVec3(corners[2], left, corners[2]);
	addVec3(corners[0], up2, corners[0]);
	subVec3(corners[2], up2, corners[2]);
	addVec3(corners[1], up2, corners[1]);
	subVec3(corners[3], up2, corners[3]);
	addVec3(corners[0], pos, corners[0]);
	addVec3(corners[1], pos, corners[1]);
	addVec3(corners[2], pos, corners[2]);
	addVec3(corners[3], pos, corners[3]);
	gfxDrawTriangle3D_3(pos, corners[0], corners[1], corner_color[0], corner_color[0], corner_color[1]);
	gfxDrawTriangle3D_3(pos, corners[1], corners[2], corner_color[1], corner_color[1], corner_color[2]);
	gfxDrawTriangle3D_3(pos, corners[2], corners[3], corner_color[2], corner_color[2], corner_color[3]);
	gfxDrawTriangle3D_3(pos, corners[3], corners[0], corner_color[3], corner_color[3], corner_color[0]);
	gfxDrawTriangle3D_3(corners[0], corners[1], corners[2], corner_color[0], corner_color[1], corner_color[2]);
	gfxDrawTriangle3D_3(corners[0], corners[2], corners[3], corner_color[0], corner_color[2], corner_color[3]);
}

void splineUIDrawCurve(Spline *spline, bool selected)
{
	int i;
	if (!spline) return;
	for (i = 0; i < eafSize(&spline->spline_points)-3; i+=3)
	{
		Vec3 control_points[4], offset;
		Color color1;

		setVec3(offset, 0, 1, 0);

		splineGetControlPoints(spline, i, control_points);

		addVec3(control_points[0], offset, control_points[0]);
		addVec3(control_points[1], offset, control_points[1]);
		addVec3(control_points[2], offset, control_points[2]);
		addVec3(control_points[3], offset, control_points[3]);

		color1.r = color1.g = 0x7F; color1.b = color1.a = 0xFF;
		if (selected)
			color1.g = 0xFF;
		gfxDrawBezier3D(control_points, color1, color1, 5);
	}
}

void splineUIDrawControlPointVectors(Vec3 point, Vec3 delta, Vec3 up, bool selected, bool highlighted, bool attached, bool welded)
{
	Vec3 dir1, dir2, min, max, min2, max2, dir_norm;
	Mat4 mat;

	Color color1, color2;
	setVec3(max, 4, 2.5, 6);
	setVec3(min, -4, -2.5, -6);
	setVec3(max2, 1.5, 1, 1.5);
	setVec3(min2, -1.5, -1, -1.5);
	color1.r = color1.g = 0x5F; color1.b = color1.a = 0xFF;
	color2.g = color2.b = 0x5F; color2.r = color2.a = 0xFF;

    if (attached)
    {
        if (welded)
        {
            if (selected)
                color1.b = 0xFF;
            color1.g = color1.r = 0;
        }
        else
        {
            if (selected)
                color1.r = 0xFF;
            color1.g = color1.b = 0;
        }
    }
	else if (selected)
    {
		color1.r = color1.g = color1.b = 0xFF;
    }
	else if (highlighted)
	{
		color1.r = 0; color1.g = color1.b = 0xFF;
	}

	identityMat4(mat);
	copyVec3(delta, dir_norm);
	normalVec3(dir_norm);
	orientMat3Yvec(*(Mat3*)&mat, dir_norm, up);
	copyVec3(point, mat[3]);

    if (!attached)
		gfxDrawBox3D(min, max, mat, color1, 5);
	splineUIDrawControlPointWidget(point, dir_norm, up, color1, attached, 0);

	scaleVec3(delta, 20.f, dir1);
	copyVec3(dir1, dir2);
	addVec3(point, dir1, dir1);
	copyVec3(dir1, mat[3]);

    if (!attached)
		gfxDrawBox3D(min2, max2, mat, color2, 5);

	if (1) //index > 0) // JE: This was checking if a global function in mathutil.h was > 0 (always true for functions!)
	{
		subVec3(point, dir2, dir2);
		copyVec3(dir2, mat[3]);

        if (!attached)
			gfxDrawBox3D(min2, max2, mat, color2, 5);
	}
	else copyVec3(point, dir2);

    if (!attached)
		gfxDrawLine3DWidth(dir1, dir2, color2, color2, 5);
}

void splineUIDrawControlPoint(Spline *spline, int index, bool selected, bool highlighted, bool attached, bool welded)
{
	splineUIDrawControlPointVectors(&spline->spline_points[index], &spline->spline_deltas[index], &spline->spline_up[index],
		selected, highlighted, attached, welded);
}

bool splineUIDoMatrixUpdate(Spline *spline, int selected_point, const Mat4 parent_matrix, const Mat4 new_matrix, Vec3 pos, Vec3 delta, Vec3 up)
{
	float curve_delta_length = normalVec3(&spline->spline_deltas[selected_point]);
	Vec3 in, out, out_delta;
	bool ret = false;
	Mat4 rot_mat;
	Mat4 local_matrix;
	Mat4 inv_parent_rotation;
	Mat4 temp_matrix;

	copyMat4(new_matrix, temp_matrix);
	subVec3(new_matrix[3], parent_matrix[3], temp_matrix[3]);
	copyMat3(parent_matrix, inv_parent_rotation);
	transposeMat3(inv_parent_rotation);
	setVec3(inv_parent_rotation[3], 0, 0, 0);
	mulMat4(inv_parent_rotation, temp_matrix, local_matrix); 

	copyVec3(local_matrix[3], pos);

	copyMat4(local_matrix, rot_mat);
	setVec3(rot_mat[3], 0, 0, 0);
	setVec3(in, 0, 0, 1);
	mulVecMat4(in, rot_mat, out);
	scaleVec3(out, curve_delta_length, out_delta);

	setVec3(in, 0, 1, 0);
	mulVecMat4(in, rot_mat, out);

	copyVec3(out_delta, delta);
	copyVec3(out, up);
	if (!nearSameVec3(pos, &spline->spline_points[selected_point]) ||
		!nearSameVec3(out_delta, &spline->spline_deltas[selected_point]) ||
		!nearSameVec3(out, &spline->spline_up[selected_point]))
	{
		return true;
	}

	return false;
}

void splineUIDrawGrid(Vec3 pos, F32 snap)
{
	F32 dist, halfRange;
	Mat4 cam;
	int i, j, color, width;
	Vec3 half, lineMid, lineBegin, lineEnd;

	// Do the snapping
	pos[0] = round(pos[0]/snap) * snap;
	pos[2] = round(pos[2]/snap) * snap;

	// determine how large the grid should be; we wish for it to go as far to infinity as possible
 	gfxGetActiveCameraMatrix(cam);
	dist = distance3(cam[3], pos);
	dist *= 8;
	halfRange = dist / snap;
	if (fabs(dist) > 100000) return; // -TomY This causes a near-infinite loop

	// draw the grid
	for (j = 0; j < 2; j++)
	{
		setVec3(half, (1-j) * snap * halfRange, 0, j * snap * halfRange);
		for (i = -halfRange; i <= halfRange; i++)
		{
			setVec3(lineMid, j * i * snap, 0, (1-j) * i * snap);
			addVec3(lineMid, half, lineBegin);
			subVec3(lineMid, half, lineEnd);
			addVec3(lineBegin, pos, lineBegin);
			addVec3(lineEnd, pos, lineEnd);
			addVec3(lineMid, pos, lineMid);

			/*if (i == 0)
			{
				color = axisColor(usedAxes[!j]);
				width = 4;
			}
			else if (i == transGizmo->translationSnapCoords[usedAxes[j]])
			{
				color = 0xffffff00;
				width = 2;
			}
			else */if (abs(i) % 10 == 0)
			{
				color = 0xaa000000;
				width = 2;
			}
			else if (abs(i) % 5 == 0)
			{
				color = 0x66000000;
				width = 2;
			}
			else
			{
				color = 0x66ffffff;
				width = 1;
			}
			gfxDrawLine3DWidthARGB(lineBegin, lineMid, color, color, width);
			gfxDrawLine3DWidthARGB(lineEnd, lineMid, color, color, width);
		}
	}
}

#endif