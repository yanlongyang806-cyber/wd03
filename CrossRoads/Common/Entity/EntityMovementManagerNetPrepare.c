
#include "EntityMovementManagerPrivate.h"
#include "qsortG.h"
#include "mutex.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static S32 mmCompareAnimBits(	const U32* bit1,
								const U32* bit2)
{
	return *bit1 - *bit2;
}

static void mmGetStancesAfterOutput(MovementManager* mm,
									const MovementOutput* oBefore,
									U32** stancesOut)
{
	MovementThreadData*		td = MM_THREADDATA_FG(mm);
	const MovementOutput*	o;
	
	mmCopyAnimValueToSizedStack(stancesOut,
								td->toFG.stanceBits,
								__FUNCTION__);
	
	for(o = td->toFG.outputList.tail;
		o && o != oBefore;
		o = o->prev)
	{
		mmAnimValuesApplyStanceDiff(mm,
									&o->data.anim,
									1,
									stancesOut,
									__FUNCTION__, 1);
	}
}

static void mmCreateNetAnimUpdate(	MovementManager* mm,
									MovementThreadData* td,
									MovementNetOutput* no,
									MovementOutput* o)
{
	const MovementAnimValues*const	anim = &o->data.anim;
	U32								tempSwapU32;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(gConf.bNewAnimationSystem){
		if(o == td->toFG.outputList.head){
			// Manually construct a diff against the last thing sent.

			U32* stancesAfter = NULL;
			
			eaiStackCreate(&stancesAfter, MM_ANIM_VALUE_STACK_SIZE_MODEST);
			mmGetStancesAfterOutput(mm, o, &stancesAfter);
			
			EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
			{
				switch(MM_ANIM_VALUE_GET_TYPE(anim->values[i])){
					xcase MAVT_LASTANIM_ANIM:{
						// Skip PC.
						i++;
					}
					xcase MAVT_STANCE_OFF:
					acase MAVT_STANCE_ON:{
						eaiRemove(&o->dataMutable.anim.values, i);
						i--;
						isize--;
					}
					xcase MAVT_ANIM_TO_START:{
						mm->fg.netSend.flags.hasAnimUpdate = 1;
					}
					xcase MAVT_FLAG:{
						mm->fg.netSend.flags.hasAnimUpdate = 1;
					}
				}
			}
			EARRAY_FOREACH_END;

			EARRAY_INT_CONST_FOREACH_BEGIN(stancesAfter, i, isize);
			{
				if(eaiFind(&mm->fg.net.stanceBits, stancesAfter[i]) < 0){
					mm->fg.netSend.flags.hasAnimUpdate = 1;
					eaiPush(&o->dataMutable.anim.values,
							MM_ANIM_VALUE(stancesAfter[i], MAVT_STANCE_ON));
				}
			}
			EARRAY_FOREACH_END;
			
			EARRAY_INT_CONST_FOREACH_BEGIN(mm->fg.net.stanceBits, i, isize);
			{
				const U32 bit = mm->fg.net.stanceBits[i];
				if(eaiFind(&stancesAfter, bit) < 0){
					mm->fg.netSend.flags.hasAnimUpdate = 1;
					eaiPush(&o->dataMutable.anim.values,
							MM_ANIM_VALUE(bit, MAVT_STANCE_OFF));
				}
			}
			EARRAY_FOREACH_END;
			
			eaiCopy(&mm->fg.net.stanceBitsMutable, &stancesAfter);
			eaiDestroy(&stancesAfter);
		}
		else if(eaiSize(&o->data.anim.values)){
			mm->fg.netSend.flags.hasAnimUpdate = 1;

			mmAnimValuesApplyStanceDiff(mm,
										&o->data.anim,
										0,
										&mm->fg.net.stanceBitsMutable,
										__FUNCTION__, 0);
		}
	}else{
		#define COMPARE_AND_SWAP(a, b)							\
				((anim->values[a] > anim->values[b]) ?			\
					(	(tempSwapU32 = anim->values[a]),		\
						(anim->values[a] = anim->values[b]),	\
						(anim->values[b] = tempSwapU32),		\
						1) :									\
					0)
		
		#define SORT_CASE(a)									\
			xcase a:											\
				PERFINFO_AUTO_START("eaiQSortG(" #a ")", 1);	\
					eaiQSortG(	anim->values,					\
								mmCompareAnimBits);				\
				PERFINFO_AUTO_STOP()

		switch(eaiSize(&anim->values)){
			xcase 0:
				// Ignored.
			xcase 1:
				// Ignored.
			xcase 2:
				COMPARE_AND_SWAP(0, 1);
			xcase 3:
				COMPARE_AND_SWAP(0, 1);

				if(COMPARE_AND_SWAP(1, 2)){
					COMPARE_AND_SWAP(0, 1);
				}
			SORT_CASE(4);
			SORT_CASE(5);
			SORT_CASE(6);
			SORT_CASE(7);
			SORT_CASE(8);
			SORT_CASE(9);
			SORT_CASE(10);
			xdefault:{
				PERFINFO_AUTO_START("eaiQSortG(>10)", 1);
					eaiQSortG(	anim->values,
								mmCompareAnimBits);
				PERFINFO_AUTO_STOP();
			}
		}
		
		#undef COMPARE_AND_SWAP
		#undef SORT_CASE
						
		mmRegisteredAnimBitComboFind(	&mgState.animBitRegistry,
										&no->animBitCombo,
										mm->fg.net.cur.animBits.combo,
										anim->values);
										
		if(mm->fg.net.cur.animBits.combo != no->animBitCombo){
			mm->fg.net.cur.animBits.combo = no->animBitCombo;
			
			mm->fg.netSend.flags.hasAnimUpdate = 1;
		}
	}

	PERFINFO_AUTO_STOP();
}

#if 0
//AUTO_RUN;
void mmTestFPU(void){
	if(1){
		Vec3	t;
		F32		r;
		Vec3	q;
		
		SET_FP_CONTROL_WORD_DEFAULT

		*(S32*)&t[0] = -1080972420;
		*(S32*)&t[1] = -1056515236;
		*(S32*)&t[2] = 1081996017;

		//---------
		
		r = SQR(t[0]) + SQR(t[1]) + SQR(t[2]);
		printf("tr0: %f [%x]\n",
				r,
				*(S32*)&r);

		//---------
		
		q[0] = SQR(t[0]);
		q[1] = SQR(t[1]);
		q[2] = SQR(t[2]);
		printf(	"tr1: %f, %f, %f [%x, %x, %x]\n",
				vecParamsXYZ(q),
				vecParamsXYZ((S32*)q));
				
		//---------
		
		r = q[0] + q[1];
		printf("tr2.0: %f [%x]\n",
				r,
				*(S32*)&r);

		//---------
		
		r += q[2];
		printf("tr2.1: %f [%x]\n",
				r,
				*(S32*)&r);

		//---------
		
		r = q[0] + q[1] + q[2];
		printf("tr3: %f [%x]\n",
				r,
				*(S32*)&r);

		//---------
		
		r = q[0] + q[1];
		r += q[2];
		printf("tr4: %f [%x]\n",
				r,
				*(S32*)&r);

		//---------
	}
}
#endif

#define S24_8_FRACTION_BITS			(12)
#define S24_8_FRACTION_SCALE		((S32)BIT(S24_8_FRACTION_BITS))
#define S24_8_FROM_S32(x)			((S32)((x) << S24_8_FRACTION_BITS))

S32 mmConvertS32ToS24_8(S32 s32){
	return S24_8_FROM_S32(s32);
}

void mmConvertIVec3ToVec3(	const IVec3 vec,
							Vec3 vecOut)
{
	FOR_BEGIN(i, 3);
		vecOut[i] = (F32)vec[i] / S24_8_FRACTION_SCALE;
	FOR_END;
}

void mmConvertVec3ToIVec3(	const Vec3 vec,
							IVec3 vecOut)
{
	FOR_BEGIN(i, 3);
		vecOut[i] = (S32)floor(vec[i] * S24_8_FRACTION_SCALE + 0.5f);
	FOR_END;
}

void mmEncodeQuatToPyr(const Quat rot, IVec3 pyrOut)
{
	Vec3 pyr;
	quatToPYR(	rot,
				pyr);
	FOR_BEGIN(i, 3);
	{
		S32 encMag = (S32)floor(0.5f + (F32)MM_NET_ROTATION_ENCODED_MAX * pyr[i] / PI);
		MINMAX1(encMag, -MM_NET_ROTATION_ENCODED_MAX, MM_NET_ROTATION_ENCODED_MAX);
		pyrOut[i] = encMag;
	}
	FOR_END;
}

void mmDecodePyr(const IVec3 encodedPyr, Vec3 decodedPyr)
{
	decodedPyr[0] = PI * (F32)encodedPyr[0] / (F32)MM_NET_ROTATION_ENCODED_MAX;
	decodedPyr[1] = PI * (F32)encodedPyr[1] / (F32)MM_NET_ROTATION_ENCODED_MAX;
	decodedPyr[2] = PI * (F32)encodedPyr[2] / (F32)MM_NET_ROTATION_ENCODED_MAX;
}


static S32 mmDivideUntilInRange(IVec3 v,
								const U32 maxValue)
{
	S32 invScale = 1;
	U32 len =	abs(v[0]) +
				abs(v[1]) +
				abs(v[2]);
				
	while(len > maxValue){
		invScale *= 2;
		len /= 2;
	}
	
	if(invScale != 1){
		v[0] /= invScale;
		v[1] /= invScale;
		v[2] /= invScale;
		
		return 1;
	}
	
	return 0;
}

void mmMakeBasisFromOffset(	MovementManager* mm,
							const IVec3 encOffset,
							IMat3 encMatOut,
							U32* encOffsetLenOut,
							Mat3 matOut)
{
	nprintf("basis offset: %d, %d, %d\n",
			vecParamsXYZ(encOffset));

	copyVec3(	encOffset,
				encMatOut[2]);
				
	//if(matOut){
	//	mmConvertIVec3ToVec3(	encOffset,
	//							matOut[2]);
	//}
								
	*encOffsetLenOut =	abs(encOffset[0]) +
						abs(encOffset[1]) +
						abs(encOffset[2]);

	nprintf("encOffsetLen: %d\n",
			*encOffsetLenOut);

	#define ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3(x, y)	(	abs((x)[0]) >= y ||	\
															abs((x)[1]) >= y ||	\
															abs((x)[2]) >= y)

	if(	abs(encOffset[1]) > abs(encOffset[0]) &&
		abs(encOffset[1]) > abs(encOffset[2]))
	{
		// Make basis using X-axis.

		IVec3 xAxis = {	S24_8_FROM_S32(1),
						S24_8_FROM_S32(0),
						S24_8_FROM_S32(0)};
		
		// Create the Y-component.
							
		crossVec3(	encMatOut[2],
					xAxis,
					encMatOut[1]);
					
		nprintf("basis y (not divided): %d, %d, %d\n",
				vecParamsXYZ(encMatOut[1]));

		if(ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3(encMatOut[1], S24_8_FRACTION_SCALE)){
			FOR_BEGIN(i, 3);
				encMatOut[1][i] /= S24_8_FRACTION_SCALE;
			FOR_END;

			nprintf("basis y: %d, %d, %d\n",
					vecParamsXYZ(encMatOut[1]));
		}

		// Create the X-component.

		crossVec3(	encMatOut[1],
					encMatOut[2],
					encMatOut[0]);

		nprintf("basis x (not divided): %d, %d, %d\n",
				vecParamsXYZ(encMatOut[0]));

		if(ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3(encMatOut[0], S24_8_FRACTION_SCALE)){
			FOR_BEGIN(i, 3);
				encMatOut[0][i] /= S24_8_FRACTION_SCALE;
			FOR_END;
			
			nprintf("basis x: %d, %d, %d\n",
					vecParamsXYZ(encMatOut[0]));
		}

		if(mmDivideUntilInRange(encMatOut[0], *encOffsetLenOut)){
			nprintf("basis x (after scaling into range): %d, %d, %d\n",
					vecParamsXYZ(encMatOut[0]));
		}
	}else{
		// Make basis using Y-axis.

		IVec3 yAxis = {	S24_8_FROM_S32(0),
						S24_8_FROM_S32(1),
						S24_8_FROM_S32(0)};
		
		// Create the X-component.

		crossVec3(	yAxis,
					encMatOut[2],
					encMatOut[0]);
					
		nprintf("basis x (not divided): %d, %d, %d\n",
				vecParamsXYZ(encMatOut[0]));

		if(ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3(encMatOut[0], S24_8_FRACTION_SCALE)){
			FOR_BEGIN(i, 3);
				encMatOut[0][i] /= S24_8_FRACTION_SCALE;
			FOR_END;

			nprintf("basis x: %d, %d, %d\n",
					vecParamsXYZ(encMatOut[0]));
		}
		
		// Create the Y-component.

		crossVec3(	encMatOut[2],
					encMatOut[0],
					encMatOut[1]);

		nprintf("basis y (not divided): %d, %d, %d\n",
				vecParamsXYZ(encMatOut[1]));

		if(ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3(encMatOut[1], S24_8_FRACTION_SCALE)){
			FOR_BEGIN(i, 3);
				encMatOut[1][i] /= S24_8_FRACTION_SCALE;
			FOR_END;

			nprintf("basis y: %d, %d, %d\n",
					vecParamsXYZ(encMatOut[1]));
		}
		
		if(mmDivideUntilInRange(encMatOut[1], *encOffsetLenOut)){
			nprintf("basis y (after scaling into range): %d, %d, %d\n",
					vecParamsXYZ(encMatOut[1]));
		}
	}
	
	#undef ANY_GREATER_THAN_OR_EQUAL_S24_8_VEC3
	
	if(matOut){
		FOR_BEGIN(i, 3);
			mmConvertIVec3ToVec3(	encMatOut[i],
									matOut[i]);
		FOR_END;
	}

	if(	vec3IsZero(encMatOut[0]) ||
		vec3IsZero(encMatOut[1]))
	{
		*encOffsetLenOut = 0;
	}
}

static U32 mmNetPosEncodeThreshold = 4;
AUTO_CMD_INT(mmNetPosEncodeThreshold, mmNetPosEncodeThreshold) ACMD_SERVERONLY;

static void mmCreateNetPosUpdate(	MovementManager* mm,
									MovementNetOutputEncoded* noe,
									const Vec3 posCurReal)
{
	MovementNetOutput*	no = noe->no;
	IMat3				encMat;
	U32					encOffsetLen;
	Mat3 				mat;
	
	PERFINFO_AUTO_START_FUNC();
	
	nprintf("---------------------------------------------\nCreating %d\n",
			no->pc.server);
	
	mmMakeBasisFromOffset(	mm,
							mm->fg.net.cur.encoded.posOffset,
							encMat,
							&encOffsetLen,
							mat);
							
	nprintf("curPos: %f, %f, %f\n",
			vecParamsXYZ(posCurReal));

	nprintf("prevDecPos: %f, %f, %f\n",
			vecParamsXYZ(mm->fg.net.cur.decoded.pos));

	nprintf("prevOffset: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));

	noe->pos.flags.noChange = 1;
			
	if(!encOffsetLen){
		noe->pos.flags.isAbsolute = 1;
	}else{
		Vec3 	curOffset;
		Vec3 	offsetDeltaInMat;
		Mat3 	matInv;
		IVec3	offsetDelta = {0};
		S32		overThreshold = 0;
		
		subVec3(posCurReal,
				mm->fg.net.cur.decoded.pos,
				curOffset);
				
		FOR_BEGIN(i, 3);
			nprintf("encMat[%d]: %d, %d, %d\n",
					i,
					vecParamsXYZ(encMat[i]));
		FOR_END;

		FOR_BEGIN(i, 3);
			nprintf("mat[%d]: %1.5f, %1.5f, %1.5f\n",
					i,
					vecParamsXYZ(mat[i]));
		FOR_END;

		if(!invertMat3(	mat,
						matInv))
		{
			nprintf("invertMat3 failed.\n");

			setVec3same(offsetDeltaInMat, 200);
		}else{
			Vec3 curOffsetInMat;

			FOR_BEGIN(i, 3);
				nprintf("matInv[%d]: %f, %f, %f\n",
						i,
						vecParamsXYZ(matInv[i]));
			FOR_END;

			nprintf("curOffset: %f, %f, %f\n",
					vecParamsXYZ(curOffset));
		
			mulVecMat3(	curOffset,
						matInv,
						curOffsetInMat);

			nprintf("curOffsetInMat: %f, %f, %f\n",
					vecParamsXYZ(curOffsetInMat));

			subVec3(curOffsetInMat,
					unitmat[2],
					offsetDeltaInMat);

			nprintf("offsetDeltaInMat: %f, %f, %f\n",
					vecParamsXYZ(offsetDeltaInMat));
		}

		FOR_BEGIN(i, 3);
		{
			F32	ratio = offsetDeltaInMat[i];
			S32	isNegative = ratio < 0.f;
			S32 signScale = isNegative ? -1 : 1;
			U32	offsetDeltaScale;
			U32	byteCount;
			
			if(isNegative){
				ratio *= -1.f;
			}
			
			if(ratio <= 1.f){
				#if MM_NET_USE_LINEAR_ENCODING
				{
					offsetDeltaScale = (S32)(ratio * MM_NET_SMALL_CHANGE_VALUE_COUNT + 0.5f);
					MINMAX1(offsetDeltaScale, 0, MM_NET_SMALL_CHANGE_VALUE_COUNT);
				}
				#else
				{
					const F32 minRatio = 1.f / (S32)BIT(MM_NET_SMALL_CHANGE_MAX_SHIFT -
														MM_NET_SMALL_CHANGE_SHIFT_BITS);

					offsetDeltaScale = MM_NET_SMALL_CHANGE_SHIFT_BITS;
					
					if(ratio < minRatio){
						FOR_BEGIN(j, MM_NET_SMALL_CHANGE_SHIFT_BITS);
						{
							F32 value = 1.f / (S32)BIT(MM_NET_SMALL_CHANGE_MAX_SHIFT - j);
							
							if(ratio < value){
								nprintf("offsetDeltaScale[%d,1] = %s%d because %1.4f < %1.4f (1/%d)\n",
										i,
										isNegative ? "-" : "+",
										j,
										ratio,
										value,
										(S32)BIT(MM_NET_SMALL_CHANGE_MAX_SHIFT - j));

								if(	i == 2 &&
									j &&
									isNegative)
								{
									// Step back one when reversing direction, so as not to bounce.

									j--;

									nprintf("offsetDeltaScale[%d,1] = %s%d (%1.4f > %1.4f (1/%d)) to reduce negation bounce\n",
											i,
											isNegative ? "-" : "+",
											j,
											ratio,
											1.f / (S32)BIT(MM_NET_SMALL_CHANGE_MAX_SHIFT - j),
											(S32)BIT(MM_NET_SMALL_CHANGE_MAX_SHIFT - j));
								}

								offsetDeltaScale = j;
								break;
							}
						}
						FOR_END;
					}
					
					if(offsetDeltaScale == MM_NET_SMALL_CHANGE_SHIFT_BITS){
						ratio = (ratio - minRatio) /
								(1.f - minRatio);

						if(	i == 2 &&
							isNegative)
						{
							// Reduce bouncing back too much.

							offsetDeltaScale = (S32)(	ratio *
														MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT);
						}else{
							offsetDeltaScale = (S32)(	ratio *
														MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT +
														0.5f);
						}
													
						MINMAX1(offsetDeltaScale, 0, MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT);
						offsetDeltaScale += MM_NET_SMALL_CHANGE_SHIFT_BITS;
					}
				}
				#endif

				if(!offsetDeltaScale){
					byteCount = 0;
					
					nprintf("offsetDeltaScale[%d,0] = unused\n",
							i);
				}else{
					byteCount = 1;
					offsetDeltaScale--;
					
					nprintf("offsetDeltaScale[%d,1] = %s%d\n",
							i,
							isNegative ? "-" : "+",
							offsetDeltaScale);

					if(offsetDeltaScale >= mmNetPosEncodeThreshold){
						overThreshold = 1;
					} 
				}
			}
			else if(ratio <= 100.f){
				overThreshold = 1;

				offsetDeltaScale = (S32)((ratio - 1.f) * (1.f/99.f) * ((S32)BIT(13) - 1));
				MINMAX1(offsetDeltaScale, 0, (S32)BIT(13) - 1);
				
				byteCount = 2;

				nprintf("offsetDeltaScale[%d,2] = %s%d\n",
						i,
						isNegative ? "-" : "+",
						offsetDeltaScale);
			}else{
				// Too big to encode, just send a full update.
				
				nprintf("too big: %f, switching to absolute\n",
						ratio);
				
				noe->pos.flags.xyzMask = 0;
				noe->pos.flags.isAbsolute = 1;
				break;
			}

			noe->pos.component[i].flags.sendByteCount = byteCount;

			if(byteCount){
				IVec3 curOffsetDelta;
				
				noe->pos.flags.xyzMask |= BIT(i);

				noe->pos.component[i].offsetDeltaScale = offsetDeltaScale;
				noe->pos.component[i].flags.isNegative = isNegative;
				
				if(byteCount == 1){
					#if MM_NET_USE_LINEAR_ENCODING
					{
						S32 scale = offsetDeltaScale;
						
						scale++;

						if(isNegative){
							scale *= -1;
						}

						ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
						{
							curOffsetDelta[j] = encMat[i][j] * scale;
							curOffsetDelta[j] /= MM_NET_SMALL_CHANGE_VALUE_COUNT;
						}
						ARRAY_FOREACH_END;
					}
					#else
					{
						if(offsetDeltaScale < MM_NET_SMALL_CHANGE_SHIFT_BITS){
							U32 shift = MM_NET_SMALL_CHANGE_MAX_SHIFT -
										offsetDeltaScale;

							ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
							{
								curOffsetDelta[j] = signScale * (encMat[i][j] >> shift);
							}
							ARRAY_FOREACH_END;
						}else{
							S32 scale = offsetDeltaScale + 1 - MM_NET_SMALL_CHANGE_SHIFT_BITS;
							
							if(isNegative){
								scale *= -1;
							}

							ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
							{
								U32 shift = MM_NET_SMALL_CHANGE_MAX_SHIFT -
											MM_NET_SMALL_CHANGE_SHIFT_BITS;
											
								curOffsetDelta[j] = encMat[i][j] * scale;
								curOffsetDelta[j] /= MM_NET_SMALL_CHANGE_LINEAR_VALUE_COUNT;
								curOffsetDelta[j] += signScale * (encMat[i][j] >> shift);
							}
							ARRAY_FOREACH_END;
						}
					}
					#endif
				}
				else if(byteCount == 2){
					S32 scale = offsetDeltaScale;
					
					if(isNegative){
						scale *= -1;
					}

					ARRAY_FOREACH_BEGIN(curOffsetDelta, j);
					{
						curOffsetDelta[j] = encMat[i][j] * scale;
						curOffsetDelta[j] *= 99;
						curOffsetDelta[j] /= (S32)BIT(13) - 1;
						curOffsetDelta[j] += (isNegative ? -1 : 1) * encMat[i][j];
					}
					ARRAY_FOREACH_END;
				}

				addVec3(curOffsetDelta,
						offsetDelta,
						offsetDelta);
			}
		}
		FOR_END;
		
		if(!noe->pos.flags.isAbsolute){
			mm->fg.net.cur.flags.isAbsoluteSmall = 0;

			if(!overThreshold){
				noe->pos.flags.xyzMask = 0;
			}
			else if(noe->pos.flags.xyzMask){
				noe->pos.flags.noChange = 0;

				addVec3(offsetDelta,
						mm->fg.net.cur.encoded.posOffset,
						mm->fg.net.cur.encoded.posOffset);
			}
					
			addVec3(mm->fg.net.cur.encoded.posOffset,
					mm->fg.net.cur.encoded.pos,
					mm->fg.net.cur.encoded.pos);
		}
	}
	
	if(noe->pos.flags.isAbsolute){
		Vec3 offsetCurPosRealFromDecodedPos;
		
		noe->pos.flags.noChange = 0;

		subVec3(posCurReal,
				mm->fg.net.cur.decoded.pos,
				offsetCurPosRealFromDecodedPos);
		
		nprintf("using absolute with offset: %f, %f, %f",
				vecParamsXYZ(offsetCurPosRealFromDecodedPos));
		
		FOR_BEGIN(i, 3);
		{
			F32 mag = offsetCurPosRealFromDecodedPos[i];
			S32 isNegative = mag < 0.f;
			U32 encMag;
			
			mag = fabs(mag);
			
			if(mag > 5.f){
				noe->pos.flags.isAbsoluteFull = 1;
				break;
			}
			
			encMag = (S32)(0.5f + 127.f * mag / 5.f);
			
			MINMAX1(encMag, 0, 127);
			
			if(encMag){
				noe->pos.flags.xyzMask |= BIT(i);
			
				nprintf("encMag[%d]: %s%d\n",
						i,
						isNegative ? "-" : "+",
						encMag);
			
				noe->pos.component[i].offsetDeltaScale = encMag;
				noe->pos.component[i].flags.isNegative = isNegative;
				
				mm->fg.net.cur.encoded.posOffset[i] = mmConvertS32ToS24_8(5 * encMag) / 127;
				
				if(isNegative){
					mm->fg.net.cur.encoded.posOffset[i] *= -1;
				}
			}else{
				mm->fg.net.cur.encoded.posOffset[i] = 0;
			}
		}
		FOR_END;
		
		if(!noe->pos.flags.isAbsoluteFull){
			if(	!FALSE_THEN_SET(mm->fg.net.cur.flags.isAbsoluteSmall) &&
				!noe->pos.flags.xyzMask &&
				!mm->fg.net.cur.absoluteSmall.xyzMask)
			{
				noe->pos.flags.noChange = 1;
			}
			
			mm->fg.net.cur.absoluteSmall.xyzMask = noe->pos.flags.xyzMask;
				
			nprintf("absolute small: %d, %d, %d\n",
					vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));
					
			if(noe->pos.flags.xyzMask){
				addVec3(mm->fg.net.cur.encoded.posOffset,
						mm->fg.net.cur.encoded.pos,
						mm->fg.net.cur.encoded.pos);
			}
		}else{
			mm->fg.net.cur.flags.isAbsoluteSmall = 0;

			zeroVec3(mm->fg.net.cur.encoded.posOffset);
			
			mmConvertVec3ToIVec3(	posCurReal,
									mm->fg.net.cur.encoded.pos);

			ARRAY_FOREACH_BEGIN(noe->pos.component, i);
				noe->pos.component[i].value = mm->fg.net.cur.encoded.pos[i];
			ARRAY_FOREACH_END;
						
			nprintf("absolute full: %d, %d, %d\n",
					vecParamsXYZ(mm->fg.net.cur.encoded.pos));
		}
	}
	
	if(!noe->pos.flags.noChange){
		mm->fg.netSend.flags.hasPosUpdate = 1;
	}
	
	mmConvertIVec3ToVec3(	mm->fg.net.cur.encoded.pos,
							mm->fg.net.cur.decoded.pos);
							
	if(MMLOG_IS_ENABLED(mm)){
		Vec3 offsetToRealPos;
		
		nprintf("decPos: %f, %f, %f",
				vecParamsXYZ(mm->fg.net.cur.decoded.pos));

		subVec3(posCurReal,
				mm->fg.net.cur.decoded.pos,
				offsetToRealPos);
				
		nprintf("offset to real pos: %f, %f, %f",
				vecParamsXYZ(offsetToRealPos));
	}
	
	MM_CHECK_DYNPOS_DEVONLY(mm->fg.net.cur.decoded.pos);

	copyVec3(	mm->fg.net.cur.decoded.pos,
				no->dataMutable.pos);
				
	#if MM_NET_VERIFY_DECODED
	{
		copyVec3(	mm->fg.net.cur.encoded.pos,
					noe->pos.debug.encoded.pos);

		copyVec3(	mm->fg.net.cur.encoded.posOffset,
					noe->pos.debug.encoded.posOffset);
	}
	#endif
		
	nprintf("encPos:    %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.pos));

	nprintf("encOffset: %d, %d, %d\n",
			vecParamsXYZ(mm->fg.net.cur.encoded.posOffset));

	PERFINFO_AUTO_STOP();
}

static void mmCreateNetRotUpdate(	MovementManager* mm,
									MovementNetOutputEncoded* noe,
									const Quat rotCurReal)
{
	MovementNetOutput* no = noe->no;

	copyQuat(	rotCurReal,
				noe->rot.rotOrig);
	
	if(sameQuat(rotCurReal, mm->fg.net.preEncoded.rot)){
		// Log something here.
	}else{
		Vec3 pyr;
		
		PERFINFO_AUTO_START_FUNC();
	
		copyQuat(	rotCurReal,
					mm->fg.net.preEncoded.rot);
		
		quatToPYR(	rotCurReal,
					pyr);
					
		mmLog(	mm,
				NULL,
				"[net.encoding] Encoding"
				" PYR (%1.3f, %1.3f, %1.3f)"
				" [%8.8x, %8.8x, %8.8x]"
				" from ROT (%1.3f, %1.3f, %1.3f, %1.3f)"
				" [%8.8x, %8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(pyr),
				vecParamsXYZ((S32*)pyr),
				quatParamsXYZW(rotCurReal),
				quatParamsXYZW((S32*)rotCurReal));

		FOR_BEGIN(i, 3);
			S32 encMag = (S32)floor(0.5f + (F32)MM_NET_ROTATION_ENCODED_MAX * pyr[i] / PI);
			
			MINMAX1(encMag, -MM_NET_ROTATION_ENCODED_MAX, MM_NET_ROTATION_ENCODED_MAX);
			
			if(encMag != mm->fg.net.cur.encoded.pyr[i]){
				mm->fg.netSend.flags.hasRotFaceUpdate = 1;

				noe->rot.flags.pyrMask |= BIT(i);

				mm->fg.net.cur.encoded.pyr[i] = encMag;
			}
			
			pyr[i] = PI * (F32)encMag / (F32)MM_NET_ROTATION_ENCODED_MAX;
		FOR_END;
		
		mmLog(	mm,
				NULL,
				"[net.encoding] Encoded"
				" PYR (%1.3f, %1.3f, %1.3f)"
				" [%8.8x, %8.8x, %8.8x]"
				" as [%d, %d, %d]",
				vecParamsXYZ(pyr),
				vecParamsXYZ((S32*)pyr),
				vecParamsXYZ(mm->fg.net.cur.encoded.pyr));

		PYRToQuat(	pyr,
					mm->fg.net.cur.decoded.rot);

		PERFINFO_AUTO_STOP();
	}
					
	copyVec3(	mm->fg.net.cur.encoded.pyr,
				noe->rot.pyr);

	copyQuat(	mm->fg.net.cur.decoded.rot,
				no->dataMutable.rot);
}

static void mmCreateNetFaceUpdate(	MovementManager* mm,
									MovementNetOutputEncoded* noe,
									const Vec2 pyFaceReal)
{
	MovementNetOutput* no = noe->no;

	if(sameVec2(pyFaceReal,
				mm->fg.net.preEncoded.pyFace))
	{
		// Log something here.

		mmLog(	mm,
				NULL,
				"[net.encoding] Skipping encoding same"
				" Face PY (%1.3f, %1.3f)"
				" [%8.8x, %8.8x]"
				" as [%d, %d]",
				pyFaceReal[0],
				pyFaceReal[1],
				*(S32*)&pyFaceReal[0],
				*(S32*)&pyFaceReal[1],
				mm->fg.net.cur.encoded.pyFace[0],
				mm->fg.net.cur.encoded.pyFace[1]);
	}else{
		Vec2 pyFace;
		
		PERFINFO_AUTO_START_FUNC();
	
		copyVec2(	pyFaceReal,
					mm->fg.net.preEncoded.pyFace);
		
		mmLog(	mm,
				NULL,
				"[net.encoding] Encoding"
				" Face PY (%1.3f, %1.3f)"
				" [%8.8x, %8.8x]",
				pyFaceReal[0],
				pyFaceReal[1],
				*(S32*)&pyFaceReal[0],
				*(S32*)&pyFaceReal[1]);

		FOR_BEGIN(i, 2);
			S32 encMag = (S32)floor(0.5f + (F32)MM_NET_ROTATION_ENCODED_MAX * pyFaceReal[i] / PI);
			
			MINMAX1(encMag, -MM_NET_ROTATION_ENCODED_MAX, MM_NET_ROTATION_ENCODED_MAX);
			
			if(encMag != mm->fg.net.cur.encoded.pyFace[i]){
				mm->fg.netSend.flags.hasRotFaceUpdate = 1;

				noe->pyFace.flags.pyMask |= BIT(i);

				mm->fg.net.cur.encoded.pyFace[i] = encMag;
			}
			
			pyFace[i] = PI * (F32)encMag / (F32)MM_NET_ROTATION_ENCODED_MAX;
		FOR_END;
		
		mmLog(	mm,
				NULL,
				"[net.encoding] Encoded"
				" Face PY (%1.3f, %1.3f)"
				" [%8.8x, %8.8x]"
				" as [%d, %d]",
				vecParamsXY(pyFace),
				vecParamsXY((S32*)pyFace),
				mm->fg.net.cur.encoded.pyFace[0],
				mm->fg.net.cur.encoded.pyFace[1]);
				
		copyVec2(	pyFace,
					mm->fg.net.cur.decoded.pyFace);

		PERFINFO_AUTO_STOP();
	}
					
	copyVec2(	mm->fg.net.cur.encoded.pyFace,
				noe->pyFace.py);

	copyVec2(	mm->fg.net.cur.decoded.pyFace,
				no->dataMutable.pyFace);
}

void mmCreateNetOutputsFG(MovementManager* mm){
	MovementThreadData* td = MM_THREADDATA_FG(mm);
	U32					outputsToCreate = 0;
	MovementOutput*		oFirst = NULL;
	MovementOutput*		o;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(mm->fg.flags.destroyed){
		PERFINFO_AUTO_STOP();
		return;
	}

	writeLockU32(&mm->fg.netSend.prepareLock, 0);

	if(mm->fg.netSend.flags.prepared){
		writeUnlockU32(&mm->fg.netSend.prepareLock);
		PERFINFO_AUTO_STOP();
		return;
	}

	if((U32)mm->fg.collisionSet != mm->fg.netSend.collisionSetSent){
		mm->fg.netSend.collisionSetSent = mm->fg.collisionSet;
		mm->fg.netSend.flags.collisionSetDoSend = 1;
	}else{
		mm->fg.netSend.flags.collisionSetDoSend = 0;
	}

	if(mm->fg.collisionGroup != mm->fg.netSend.collisionGroupSent){
		mm->fg.netSend.collisionGroupSent = mm->fg.collisionGroup;
		mm->fg.netSend.flags.collisionGroupDoSend = 1;
	}else{
		mm->fg.netSend.flags.collisionGroupDoSend = 0;
	}

	if(mm->fg.collisionGroupBits != mm->fg.netSend.collisionGroupBitsSent){
		mm->fg.netSend.collisionGroupBitsSent = mm->fg.collisionGroupBits;
		mm->fg.netSend.flags.collisionGroupBitsDoSend = 1;
	}else{
		mm->fg.netSend.flags.collisionGroupBitsDoSend = 0;
	}
	
	if(mm->fg.flags.noCollision != mm->fg.netSend.flags.noCollisionSent){
		mm->fg.netSend.flags.noCollisionSent = mm->fg.flags.noCollision;
		mm->fg.netSend.flags.noCollisionDoSend = 1;
	}else{
		mm->fg.netSend.flags.noCollisionDoSend = 0;
	}

	if(mm->fg.flags.ignoreActorCreate != mm->fg.netSend.flags.ignoreActorCreateSent){
		mm->fg.netSend.flags.ignoreActorCreateSent = mm->fg.flags.ignoreActorCreate;
		mm->fg.netSend.flags.ignoreActorCreateDoSend = 1;
	}else{
		mm->fg.netSend.flags.ignoreActorCreateDoSend = 0;
	}

	if (mm->fg.flags.capsuleOrientationUseRotation != mm->fg.netSend.flags.capsuleOrientationUseRotation)
	{
		mm->fg.netSend.flags.capsuleOrientationUseRotation = mm->fg.flags.capsuleOrientationUseRotation;
		mm->fg.netSend.flags.capsuleOrientationDoSend = 1;
	}else{
		mm->fg.netSend.flags.capsuleOrientationDoSend = 0;
	}
	
	mm->fg.netSend.flags.hasPosUpdate = 0;
	mm->fg.netSend.flags.hasRotFaceUpdate = 0;
	mm->fg.netSend.flags.hasAnimUpdate = 0;

	mm->fg.net.prev = mm->fg.net.cur;

	// Find the first unsent output.

	for(o = td->toFG.outputList.tail;
		o;
		o = (o == td->toFG.outputList.head ? NULL : o->prev))
	{
		if(!FALSE_THEN_SET(o->flagsMutable.sentToClients)){
			break;
		}
		
		oFirst = o;
		outputsToCreate++;
	}
	
	mm->fg.netSend.flags.hasNotInterpedOutput = 0;

	if(outputsToCreate){
		U32	curProcessCount =	mgState.fg.netSendToClient.cur.pc -
								(outputsToCreate - 1) * MM_PROCESS_COUNTS_PER_STEP;

		nprintf("Creating net outputs.\n");

		ANALYSIS_ASSUME(oFirst);
		for(o = oFirst;
			o;
			o = (o == td->toFG.outputList.tail ? NULL : o->next))
		{
			PERFINFO_AUTO_START("createNetOutput", 1);
			{
				MovementNetOutputEncoded*	noe;
				MovementNetOutput*			no;

				// Create net output.
				
				if(mm->fg.netSend.outputCount >= eaUSize(&mm->fg.netSend.outputsEncoded)){
					mmNetOutputEncodedCreate(&noe);
					eaPush(&mm->fg.netSend.outputsEncodedMutable, noe);
				}else{
					noe = mm->fg.netSend.outputsEncoded[mm->fg.netSend.outputCount];
					ZeroStruct(noe);
				}
				
				mm->fg.netSend.outputCount++;
				
				mmNetOutputCreateAndAddTail(mm, td, &no);
				
				noe->no = no;
				noe->o = o;

				// Check if this output isn't interped.

				if(o->flags.notInterped){
					mm->fg.netSend.flags.hasNotInterpedOutput = 1;
					noe->pos.flags.notInterped = 1;
				}
				
				// Set the PCs.

				no->pc.server = curProcessCount;
				
				curProcessCount += 2;

				// Create the pos update.
				
				if(	mm->fg.flags.posNeedsForcedSetAck &&
					o == td->toFG.outputList.tail)
				{
					mmCreateNetPosUpdate(mm, noe, mm->fg.pos);
				}else{
					mmCreateNetPosUpdate(mm, noe, o->data.pos);
				}
				
				// Create the rot and face updates.

				if(	mm->fg.flags.rotNeedsForcedSetAck &&
					o == td->toFG.outputList.tail)
				{
					mmCreateNetRotUpdate(mm, noe, mm->fg.rot);
					mmCreateNetFaceUpdate(mm, noe, mm->fg.pyFace);
				}else{
					mmCreateNetRotUpdate(mm, noe, o->data.rot);
					mmCreateNetFaceUpdate(mm, noe, o->data.pyFace);
				}
				
				// Create the anim bit update.
				
				mmCreateNetAnimUpdate(mm, td, no, o);
			}
			PERFINFO_AUTO_STOP();
		}

		nprintf("Done creating net outputs.\n");

		// Send the outputs to the BG.

		if(!td->toBG.net.outputList.head){
			td->toBG.net.outputListMutable.head = mm->fg.net.outputList.head;
		}

		mmNetOutputListSetTail(	&td->toBG.net.outputListMutable,
								mm->fg.net.outputList.tail);
	}
	else if(!mm->fg.net.outputList.head){
		// No net outputs have been sent yet, so initialize to the current position.

		MM_CHECK_DYNPOS_DEVONLY(mm->fg.pos);

		mmConvertVec3ToIVec3(	mm->fg.pos,
								mm->fg.net.prev.encoded.pos);
								
		copyVec3(	mm->fg.net.prev.encoded.pos,
					mm->fg.net.cur.encoded.pos);
	}
		
	mm->fg.netSend.flags.prepared = 1;
	writeUnlockU32(&mm->fg.netSend.prepareLock);

	PERFINFO_AUTO_STOP();
}

void mmAllBeforeSendingToClients(const FrameLockedTimer* flt){
	SET_FP_CONTROL_WORD_DEFAULT;

	PERFINFO_AUTO_START_FUNC();
	
	#define nstc mgState.fg.netSendToClient
	{
		// Copy the prev values.
		
		nstc.prev = nstc.cur;

		nstc.cur.pc = mgState.fg.frame.prev.pcNetSend;
		nstc.cur.pcDelta = nstc.cur.pc - nstc.prev.pc;
		nstc.cur.normalOutputCount = nstc.cur.pcDelta / MM_PROCESS_COUNTS_PER_STEP;
	}	
	#undef nstc

	// Create the empty combo.

	if(!gConf.bNewAnimationSystem){
		ATOMIC_INIT_BEGIN;
		{
			mmRegisteredAnimBitComboFind(	&mgState.animBitRegistry,
											NULL,
											NULL,
											NULL);
		}
		ATOMIC_INIT_END;
	}

	PERFINFO_AUTO_STOP();
}

static void mmRequestersAfterSendingToClients(	MovementManager* mm,
												S32 sentStateBG)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->fg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_FG(mr);
		
		// Flag for createdness.
		
		mr->fg.flagsMutable.sentCreate = 1;
		mr->fg.flagsMutable.sentCreateToOwner = 1;
		mr->fg.flagsMutable.hasSyncToSend = 0;

		// Check if mr destroy was sent.

		if(	mr->fg.flags.destroyed &&
			!mr->fg.flags.sentDestroy)
		{
			mr->fg.flagsMutable.sentDestroy = 1;
			continue;
		}
		
		// Update sync flags.
		
		if(sentStateBG){
			// Save the last sent BG struct.
		
			if(SAFE_MEMBER(mrtd->toFG.predict, userStruct.bg)){
				PERFINFO_AUTO_START("copy client bg", 1);
				MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_COPY_LATEST_FROM_BG);
				{				
					mr->fg.net.prev.handledMsgs = mrtd->toFG.predict->handledMsgs;
					mr->fg.net.prev.ownedDataClassBits = mrtd->toFG.predict->ownedDataClassBits;

					mmStructAllocAndCopy(	mr->mrc->pti.bg,
											mr->fg.net.prev.userStruct.bg,
											mrtd->toFG.predict->userStruct.bg,
											mm);
				}
				MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_COPY_LATEST_FROM_BG);
				PERFINFO_AUTO_STOP();
			}
		}
			
		if(mr->fg.flags.hasSyncToQueue){
			mr->fg.flagsMutable.hasSyncToSend = 0;
			
			PERFINFO_AUTO_START("copy client sync", 1);
			{
				if(mr->userStruct.sync.fgToQueue){
					mmStructAllocAndCopy(	mr->mrc->pti.sync,
											mr->fg.net.prev.userStruct.sync,
											mr->userStruct.sync.fgToQueue,
											mm);
				}
				
				if(mr->userStruct.syncPublic.fgToQueue){
					assert(mr->mrc->pti.syncPublic);
					
					mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
											mr->fg.net.prev.userStruct.syncPublic,
											mr->userStruct.syncPublic.fgToQueue,
											mm);
				}
			}
			PERFINFO_AUTO_STOP();
		}
		else if(TRUE_THEN_RESET(mr->fg.flagsMutable.hasSyncToSend) &&
				mr->mrc->pti.syncPublic)
		{
			PERFINFO_AUTO_START("copy non-client sync", 1);
			{
				assert(mr->userStruct.syncPublic.fg);

				mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
										mr->fg.net.prev.userStruct.syncPublic,
										mr->userStruct.syncPublic.fg,
										mm);
			}
			PERFINFO_AUTO_STOP();
		}
	}
	EARRAY_FOREACH_END;
}

S32 mmAfterSendingToClientsInThread(void* unused,
									S32 mmIndex,
									void* userPointerUnused)
{
	MovementManager*	mm;
	S32					sentStateBG = 0;
	
	if(mmIndex >= eaSize(&mgState.fg.managers)){
		return 0;
	}
	
	ANALYSIS_ASSUME(mgState.fg.managers);
	
	mm = mgState.fg.managers[mmIndex];
	
	if(mm->fg.flags.destroyed){
		return 1;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if(mm->fg.flags.isAttachedToClient){
		sentStateBG = mm->fg.mcma->flags.sentStateBG;
	}
	
	mm->fg.netSend.flags.prepared = 0;
	
	mm->fg.flagsMutable.mrIsNewToSend = 0;
	mm->fg.flagsMutable.mrHasSyncToSend = 0;
	mm->fg.flagsMutable.mrHasDestroyToSend = 0;

	if(	TRUE_THEN_RESET(mm->fg.flagsMutable.mrNeedsAfterSend) ||
		sentStateBG)
	{
		mmRequestersAfterSendingToClients(mm, sentStateBG);
	}

	if(TRUE_THEN_RESET(mm->fg.flagsMutable.mrNeedsAfterSync)){
		mmHandleAfterSimWakesDecFG(mm, "mrNeedsAfterSync");
		mmSendMsgsAfterSyncFG(mm);
	}

	mm->fg.netSend.outputCount = 0;

	if(TRUE_THEN_RESET(mm->fg.flagsMutable.mmrHasUnsentStates)){
		mmResourcesAfterSendingToClients(mm);
	}
	
	PERFINFO_AUTO_STOP();
	
	return 1;
}

void mmAllAfterSendingToClients(const FrameLockedTimer* flt){
	PERFINFO_AUTO_START_FUNC();
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.clients, i, isize);
	{
		MovementClient* mc = mgState.fg.clients[i];
		
		mc->netSend.flags.sendStateBG = 0;
		
		EARRAY_CONST_FOREACH_BEGIN(mc->mcmas, j, jsize);
		{
			mc->mcmas[j]->flags.sentStateBG = 0;
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

