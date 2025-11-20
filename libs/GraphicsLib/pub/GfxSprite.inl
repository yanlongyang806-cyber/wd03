/*
This file is for holding functions that are in-lined through the sprite system.
However, because this is difficult to debug, they are switched to standard functions when in FullDebug.
*/


SPRITE_INLINE_OPTION
void spriteFromDispSprite(RdrSpriteVertex *vertices, AtlasTex* atex1, BasicTexture* btex1, AtlasTex* atex2, BasicTexture* btex2, int rgba, int rgba2, int rgba3, int rgba4, 
	float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
	float u2_0, float v2_0, float u2_1, float v2_1, float u2_2, float v2_2, float u2_3, float v2_3,
	float skew, Vec2 ul, Vec2 lr)
{
	Vec4 u1, v1, u2, v2;
	PERFINFO_AUTO_START_FUNC();

	if (atex1)
	{
		atlasGetModifiedUVs(atex1, u1_0, v1_0, &u1[0], &v1[0]);
		atlasGetModifiedUVs(atex1, u1_1, v1_1, &u1[1], &v1[1]);
		atlasGetModifiedUVs(atex1, u1_2, v1_2, &u1[2], &v1[2]);
		atlasGetModifiedUVs(atex1, u1_3, v1_3, &u1[3], &v1[3]);
	}
	else if (btex1)
	{
		float umult = ((float)btex1->width)  / ((float)btex1->realWidth);
		float vmult = ((float)btex1->height) / ((float)btex1->realHeight);

		u1[0] = u1_0 * umult;
		u1[1] = u1_1 * umult;
		u1[2] = u1_2 * umult;
		u1[3] = u1_3 * umult;

		v1[0] = v1_0 * vmult;
		v1[1] = v1_1 * vmult;
		v1[2] = v1_2 * vmult;
		v1[3] = v1_3 * vmult;
	}
	else
	{
		u1[0] = u1_0;
		u1[1] = u1_1;
		u1[2] = u1_2;
		u1[3] = u1_3;

		v1[0] = v1_0;
		v1[1] = v1_1;
		v1[2] = v1_2;
		v1[3] = v1_2;
	}


	if (atex2)
	{
		atlasGetModifiedUVs(atex2, u2_0, v2_0, &u2[0], &v2[0]);
		atlasGetModifiedUVs(atex2, u2_1, v2_1, &u2[1], &v2[1]);
		atlasGetModifiedUVs(atex2, u2_2, v2_2, &u2[2], &v2[2]);
		atlasGetModifiedUVs(atex2, u2_3, v2_3, &u2[3], &v2[3]);
	}
	else if (btex2)
	{
		float umult = ((float)btex2->width)  / ((float)btex2->realWidth);
		float vmult = ((float)btex2->height) / ((float)btex2->realHeight);

		u2[0] = u2_0 * umult;
		u2[1] = u2_1 * umult;
		u2[2] = u2_2 * umult;
		u2[3] = u2_3 * umult;

		v2[0] = v2_0 * vmult;
		v2[1] = v2_1 * vmult;
		v2[2] = v2_2 * vmult;
		v2[3] = v2_3 * vmult;
	}
	else
	{
		u2[0] = u2_0;
		u2[1] = u2_1;
		u2[2] = u2_2;
		u2[3] = u2_3;

		v2[0] = v2_0;
		v2[1] = v2_1;
		v2[2] = v2_2;
		v2[3] = v2_2;
	}

	// increase x first
	vertices[0].point[0] = ul[0] + skew;
	vertices[0].point[1] = ul[1];
	setSpriteVertColorFromRGBA(&vertices[0], rgba);
	setVec4(vertices[0].texcoords, u1[0], v1[0], u2[0], v2[0]);

	vertices[1].point[0] = lr[0] + skew;
	vertices[1].point[1] = ul[1];
	setSpriteVertColorFromRGBA(&vertices[1], rgba2);
	setVec4(vertices[1].texcoords, u1[1], v1[1], u2[1], v2[1]);

	vertices[2].point[0] = lr[0] - skew;
	vertices[2].point[1] = lr[1];
	setSpriteVertColorFromRGBA(&vertices[2], rgba3);
	setVec4(vertices[2].texcoords, u1[2], v1[2], u2[2], v2[2]);

	vertices[3].point[0] = ul[0] - skew;
	vertices[3].point[1] = lr[1];
	setSpriteVertColorFromRGBA(&vertices[3], rgba4);
	setVec4(vertices[3].texcoords, u1[3], v1[3], u2[3], v2[3]);

	PERFINFO_AUTO_STOP_FUNC();

}

SPRITE_INLINE_OPTION
int create_sprite_ex(AtlasTex *atex1, BasicTexture *btex1, 
	AtlasTex *atex2, BasicTexture *btex2, 
	float xp, float yp, float zp, 
	float xscale, float yscale, 
	int rgba, int rgba2, int rgba3, int rgba4, 
	float u1_0, float v1_0, float u1_1, float v1_1, float u1_2, float v1_2, float u1_3, float v1_3,
	float u2_0, float v2_0, float u2_1, float v2_1, float u2_2, float v2_2, float u2_3, float v2_3,
	float angle, int additive, Clipper2D *clipper, 
	RdrSpriteEffect sprite_effect, F32 effect_weight, SpriteFlags sprite_flags)
{
	float sprite_width, sprite_height;
	F32 ratio = 1.0f;
	int screen_width, screen_height;
	float half_sprite_width, half_sprite_height, center_x, center_y;
	Mat4 m;
	Mat3 *pMatrix;
	Vec2 bbox_ul, bbox_lr;
	Vec3 in_point, out_point;
	Vec2 sprite_ul, sprite_lr;
	CBox *glBox = clipperGetGLBox(clipper);
	GfxSpriteListEntry* sprite;
	RdrSpriteState* sstate;
	RdrSpriteVertex* sverts4;
	bool b3D = sprite_flags & SPRITE_3D;

	assert(sprite_effect < RdrSpriteEffect_DistField1Layer || sprite_effect > RdrSpriteEffect_DistField2LayerGradient);
	assertmsg(FINITE(zp), "Invalid sprite z depth.");

	if ( gbNoGraphics )
		return 0;

	if (atex1)
	{
		sprite_width = (float)atex1->width;
		sprite_height = (float)atex1->height;
		btex1 = NULL;
	}
	else if (btex1)
	{
		sprite_width = (float)btex1->width;
		sprite_height = (float)btex1->height;
	}
	else
		return 0;

	if (atex2)
		btex2 = NULL;

	if (xscale == 0 || yscale == 0)
		return 0;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (sprite_override_effect)
	{
		sprite_effect = sprite_override_effect;
		effect_weight = sprite_override_effect_weight;
		rgba = spriteEffectOverrideColor(rgba);
		rgba2 = spriteEffectOverrideColor(rgba2);
		rgba3 = spriteEffectOverrideColor(rgba3);
		rgba4 = spriteEffectOverrideColor(rgba4);
	}

	gfxGetActiveSurfaceSizeInline(&screen_width, &screen_height);

	// width and height scaling
	sprite_width *= xscale;
	sprite_height *= yscale;

	// x positions.
	sprite_ul[0] = xp;
	sprite_lr[0] = sprite_ul[0] + sprite_width;
	if (xscale < 0) {
		F32 temp = sprite_ul[0];
		sprite_ul[0] = sprite_lr[0];
		sprite_lr[0] = temp;
		SWAPF32(u1_0, u1_1);
		SWAPF32(u1_2, u1_3);
	}

	// y positions.  Flip the y axis for sprites
	sprite_ul[1] = screen_height - yp;
	sprite_lr[1] = sprite_ul[1] - sprite_height;
	if (yscale < 0) {
		F32 temp = sprite_ul[1];
		sprite_ul[1] = sprite_lr[1];
		sprite_lr[1] = temp;
		SWAPF32(v1_0, v1_3);
		SWAPF32(v1_1, v1_2);
	}

	// bounding box
	if (angle != 0)
	{
		copyMat4(unitmat, m);
		yawMat3(angle, m);

		half_sprite_width = sprite_width * 0.5f;
		half_sprite_height = sprite_height * 0.5f;

		in_point[1] = 0;

		// add UL rotated point
		in_point[0] = -half_sprite_width;
		in_point[2] = half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		bbox_ul[0] = bbox_lr[0] = out_point[0];
		bbox_ul[1] = bbox_lr[1] = out_point[2];

		// add LL rotated point
		//in_point[0] = -half_sprite_width;
		in_point[2] = -half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// add LR rotated point
		in_point[0] = half_sprite_width;
		//in_point[2] = -half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// add UR rotated point
		//in_point[0] = half_sprite_width;
		in_point[2] = half_sprite_height;
		mulVecMat4(in_point, m, out_point);
		MIN1(bbox_ul[0], out_point[0]);
		MAX1(bbox_lr[0], out_point[0]);
		MAX1(bbox_ul[1], out_point[2]);
		MIN1(bbox_lr[1], out_point[2]);

		// center bbox on unrotated box
		center_x = sprite_ul[0] + half_sprite_width;
		bbox_ul[0] += center_x;
		bbox_lr[0] += center_x;

		center_y = sprite_lr[1] + half_sprite_height;
		bbox_ul[1] += center_y;
		bbox_lr[1] += center_y;
	}
	else
	{
		bbox_ul[0] = sprite_ul[0];
		bbox_ul[1] = sprite_ul[1];
		bbox_lr[0] = sprite_lr[0];
		bbox_lr[1] = sprite_lr[1];
	}

	// Test clipper rejection
	if (!clipperTestValuesGLSpace(clipper, bbox_ul, bbox_lr))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	sprite = gfxStartAddSpriteToList(current_sprite_list, zp, b3D, &sstate, &sverts4);

	sstate->bits_for_test = 0;
	sstate->sprite_effect_weight = effect_weight;
	sstate->additive = additive;
	sstate->is_triangle = 0;
	sstate->sprite_effect = sprite_effect;
	sstate->ignore_depth_test = !!(sprite_flags & SPRITE_IGNORE_Z_TEST);

	gfxSpriteListHookupTextures(current_sprite_list, sprite, atex1, btex1, atex2, btex2, b3D);

	if (glBox)
	{
		if (b3D && sprite_lr[0] > glBox->right)
		{
			ratio = 1.0f - ((sprite_lr[0] - glBox->right) / (sprite_lr[0] - sprite_ul[0]));
			sprite_lr[0] = min(sprite_lr[0],glBox->right);
			u1_1 = (u1_1 - u1_0) * ratio + u1_0;
			u1_2 = (u1_2 - u1_3) * ratio + u1_3;
			u2_1 = (u2_1 - u2_0) * ratio + u2_0;
			u2_2 = (u2_2 - u2_3) * ratio + u2_3;
		}
	}

	if (angle)
		spriteFromRotDispSprite(sverts4, atex1, btex1, atex2, btex2, rgba, rgba2, rgba3, rgba4, 
		u1_0, v1_0, u1_1, v1_1, u1_2, v1_2, u1_3, v1_3, 
		u2_0, v2_0, u2_1, v2_1, u2_2, v2_2, u2_3, v2_3, 
		angle, sprite_ul, sprite_lr);
	else
		spriteFromDispSprite(sverts4, atex1, btex1, atex2, btex2, rgba, rgba2, rgba3, rgba4, 
		u1_0, v1_0, u1_1, v1_1, u1_2, v1_2, u1_3, v1_3, 
		u2_0, v2_0, u2_1, v2_1, u2_2, v2_2, u2_3, v2_3, 
		0, sprite_ul, sprite_lr);

	pMatrix = matrixStackGet(&eaSpriteMatrixStack);
	if (pMatrix)
	{
		gfxTransformAndClipVerticies(sverts4, pMatrix, clipper);
	}

	// Everything else.
	if (glBox && !pMatrix && !b3D)
	{
		sstate->scissor_x = (U16)MAXF(glBox->lx, 0);
		sstate->scissor_y = (U16)MAXF(glBox->ly, 0);
		sstate->scissor_width = (U16)MAXF(glBox->hx - sstate->scissor_x, 0);
		sstate->scissor_height = (U16)MAXF(glBox->hy - sstate->scissor_y, 0);
		checkScissor(sstate);
		sstate->use_scissor = 1;
	}
	else
	{
		sstate->use_scissor = 0;
	}

	gfxInsertSpriteListEntry(current_sprite_list, sprite, b3D);

	PERFINFO_AUTO_STOP();

	return 1;
}
