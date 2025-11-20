
static void dynBoneTrackPosUpdate(const DynBoneTrack* pBoneTrack, F32 fFrameTime, U32 uiFrameFloor, DynTransform* pTransform, const DynTransform* pxBaseTransform, const char* pcDebugAnimName, bool bNoInterp )
{
	DynPosKeyFrame* frames = pBoneTrack->posKeyFrames;
	DynPosKey* keys = pBoneTrack->posKeys;

	if ( pBoneTrack->uiPosKeyCount > 1)
	{
		// Find first before
		U32 uiFirstBefore=0;
		U32 uiKeyCount = pBoneTrack->uiPosKeyCount;
		U32 uiFirstAfter=uiKeyCount-1;
		U32 uiKeyIndex;
		U32 lo = 0;
		U32 hi = uiKeyCount - 1;
		DynPosKey* pKey;
		DynPosKeyFrame* pFrame;
		PERFINFO_AUTO_START_L3("search", 1);
			while(hi != lo)
			{
				U32 mid = (hi + lo) / 2;
				if(frames[mid].uiFrame <= uiFrameFloor){
					if(lo == mid){
						break;
					}
					lo = mid;
				}else{
					hi = mid;
				}
			}
			for(uiKeyIndex=lo; uiKeyIndex<uiKeyCount; ++uiKeyIndex)
			{
				if ( frames[uiKeyIndex].uiFrame <= uiFrameFloor )
				{
					uiFirstBefore = uiKeyIndex;
				}
				else
				{
					uiFirstAfter = uiKeyIndex;
					break;
				}
			}
		PERFINFO_AUTO_STOP_L3();
		// We now have the before and after,process

		pKey = &keys[uiFirstBefore];
		pFrame = &frames[uiFirstBefore];

		// There must be two frames
		if ( uiFirstBefore > uiFirstAfter )
		{
			FatalErrorf("Frame error in animation %s, on bone %s", pcDebugAnimName, pBoneTrack->pcBoneName);
			return;
		}
		else if ( uiFirstBefore == uiFirstAfter || bNoInterp )
		{
			copyVec3(pKey->vPos, pTransform->vPos);
		}
		else
		{
			PERFINFO_AUTO_START_L3("interp", 1);
			{
				// Do the interp
				DynPosKey* pNextKey = &keys[uiFirstAfter];
				DynPosKeyFrame* pNextKeyFrame = &frames[uiFirstAfter];
				F32 fT = (fFrameTime - (F32)pFrame->uiFrame) / (pNextKeyFrame->uiFrame - pFrame->uiFrame); //* pKey->fInvFrameDelta;
				Vec3 vBonePos;
				PERFINFO_AUTO_START_L3("interp", 1);
					interpVec3(fT, pKey->vPos, pNextKey->vPos, vBonePos);
					//scaleAddVec3(pKey->vPosDelta, fT, pKey->vPos, vBonePos);
				PERFINFO_AUTO_STOP_L3();
				copyVec3(vBonePos, pTransform->vPos);
			}
			PERFINFO_AUTO_STOP_L3();
		}
	}
	else if ( pBoneTrack->uiPosKeyCount == 1 )
	{
		// One frame
		copyVec3(keys[0].vPos, pTransform->vPos);
	}
	else 
	{
		// no frames, get from base
		copyVec3(zerovec3, pTransform->vPos);
	}
	if (pxBaseTransform)
	{
		addVec3(pTransform->vPos, pxBaseTransform->vPos, pTransform->vPos);
	}
}

static void dynBoneTrackRotUpdate(const DynBoneTrack* pBoneTrack, F32 fFrameTime, U32 uiFrameFloor, DynTransform* pTransform, const DynTransform* pxBaseTransform, const char* pcDebugAnimName, bool bNoInterp )
{
	DynRotKeyFrame* frames = pBoneTrack->rotKeyFrames;
	DynRotKey* keys = pBoneTrack->rotKeys;

	if ( pBoneTrack->uiRotKeyCount > 1)
	{
		// Find first before
		U32 uiFirstBefore=0;
		U32 uiKeyCount = pBoneTrack->uiRotKeyCount;
		U32 uiFirstAfter=uiKeyCount-1;
		U32 uiKeyIndex;
		U32 lo = 0;
		U32 hi = uiKeyCount - 1;
		DynRotKey* pKey;
		DynRotKeyFrame* pFrame;
		PERFINFO_AUTO_START_L3("search", 1);
			while(hi != lo)
			{
				U32 mid = (hi + lo) / 2;
				if(frames[mid].uiFrame <= uiFrameFloor){
					if(lo == mid){
						break;
					}
					lo = mid;
				}else{
					hi = mid;
				}
			}
			for(uiKeyIndex=lo; uiKeyIndex<uiKeyCount; ++uiKeyIndex)
			{
				if ( frames[uiKeyIndex].uiFrame <= uiFrameFloor )
				{
					uiFirstBefore = uiKeyIndex;
				}
				else
				{
					uiFirstAfter = uiKeyIndex;
					break;
				}
			}
		PERFINFO_AUTO_STOP_L3();
		// We now have the before and after,process

		pKey = &keys[uiFirstBefore];
		pFrame = &frames[uiFirstBefore];

		// There must be two frames
		if ( uiFirstBefore > uiFirstAfter )
		{
			FatalErrorf("Frame error in animation %s, on bone %s", pcDebugAnimName, pBoneTrack->pcBoneName);
			return;
		}
		else if ( uiFirstBefore == uiFirstAfter || bNoInterp)
		{
			copyQuat(pKey->qRot, pTransform->qRot);
		}
		else
		{
			PERFINFO_AUTO_START_L3("interp", 1);
			{
				// Do the interp
				DynRotKey* pNextKey = &keys[uiFirstAfter];
				DynRotKeyFrame* pNextKeyFrame = &frames[uiFirstAfter];
				F32 fT = (fFrameTime - (F32)pFrame->uiFrame) / (pNextKeyFrame->uiFrame - pFrame->uiFrame); //* pKey->fInvFrameDelta;
				Quat qBoneRot;
				PERFINFO_AUTO_START_L3("fastQuatInterp", 1);
					/*
					quatDiff(pKey->qRot, pNextKey->qRot, qDelta);
					scaleAddVec4(pKey->qRotDelta, fT, pKey->qRot, qBoneRot);
					quatNormalize(qBoneRot);
					*/
					quatInterp(fT, pKey->qRot, pNextKey->qRot, qBoneRot);
				PERFINFO_AUTO_STOP_L3();
				copyQuat(qBoneRot, pTransform->qRot);
			}
			PERFINFO_AUTO_STOP_L3();
		}
	}
	else if ( pBoneTrack->uiRotKeyCount == 1 )
	{
		// One frame
		copyQuat(keys[0].qRot, pTransform->qRot);
	}
	else 
	{
		// no frames, get from base
		copyQuat(unitquat, pTransform->qRot);
	}

	if (pxBaseTransform)
	{
		Quat qTemp;
		quatMultiply(pTransform->qRot, pxBaseTransform->qRot, qTemp);
		copyQuat(qTemp, pTransform->qRot);
	}
}

static void dynBoneTrackScaleUpdate(const DynBoneTrack* pBoneTrack, F32 fFrameTime, U32 uiFrameFloor, DynTransform* pTransform, const DynTransform* pxBaseTransform, const char* pcDebugAnimName, bool bNoInterp )
{
	DynScaleKeyFrame* frames = pBoneTrack->scaleKeyFrames;
	DynScaleKey* keys = pBoneTrack->scaleKeys;

	if ( pBoneTrack->uiScaleKeyCount > 1)
	{
		// Find first before
		U32 uiFirstBefore=0;
		U32 uiKeyCount = pBoneTrack->uiScaleKeyCount;
		U32 uiFirstAfter=uiKeyCount-1;
		U32 uiKeyIndex;
		U32 lo = 0;
		U32 hi = uiKeyCount - 1;
		DynScaleKey* pKey;
		DynScaleKeyFrame* pFrame;
		PERFINFO_AUTO_START_L3("search", 1);
			while(hi != lo)
			{
				U32 mid = (hi + lo) / 2;
				if(frames[mid].uiFrame <= uiFrameFloor){
					if(lo == mid){
						break;
					}
					lo = mid;
				}else{
					hi = mid;
				}
			}
			for(uiKeyIndex=lo; uiKeyIndex<uiKeyCount; ++uiKeyIndex)
			{
				if ( frames[uiKeyIndex].uiFrame <= uiFrameFloor )
				{
					uiFirstBefore = uiKeyIndex;
				}
				else
				{
					uiFirstAfter = uiKeyIndex;
					break;
				}
			}
		PERFINFO_AUTO_STOP_L3();
		// We now have the before and after,process

		pKey = &keys[uiFirstBefore];
		pFrame = &frames[uiFirstBefore];

		// There must be two frames
		if ( uiFirstBefore > uiFirstAfter )
		{
			FatalErrorf("Frame error in animation %s, on bone %s", pcDebugAnimName, pBoneTrack->pcBoneName);
			return;
		}
		else if ( uiFirstBefore == uiFirstAfter || bNoInterp )
		{
			copyVec3(pKey->vScale, pTransform->vScale);
		}
		else
		{
			PERFINFO_AUTO_START_L3("interp", 1);
			{
				// Do the interp
				DynScaleKey* pNextKey = &keys[uiFirstAfter];
				DynScaleKeyFrame* pNextKeyFrame = &frames[uiFirstAfter];
				F32 fT = (fFrameTime - (F32)pFrame->uiFrame) / (pNextKeyFrame->uiFrame - pFrame->uiFrame); //* pKey->fInvFrameDelta;
				Vec3 vBoneScale;
				PERFINFO_AUTO_START_L3("interp", 1);
					interpVec3(fT, pKey->vScale, pNextKey->vScale, vBoneScale);
					//scaleAddVec3(pKey->vScaleDelta, fT, pKey->vScale, vBoneScale);
				PERFINFO_AUTO_STOP_L3();
				copyVec3(vBoneScale, pTransform->vScale);
			}
			PERFINFO_AUTO_STOP_L3();
		}
	}
	else if ( pBoneTrack->uiScaleKeyCount == 1 )
	{
		// One frame
		copyVec3(keys[0].vScale, pTransform->vScale);
	}
	else 
	{
		// no frames, get from base
		copyVec3(onevec3, pTransform->vScale);;
	}
	if (pxBaseTransform)
	{
		mulVecVec3(pTransform->vScale, pxBaseTransform->vScale, pTransform->vScale);
	}
}

//uint32_t max_at_uiBoneCount, max_atu_uiBoneCount;
static bool dynBoneTrackUpdateOld(const DynAnimTrack* pAnimTrack, F32 fFrameTime, U32 uiFrameFloor, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, bool bNoInterp )
{
	const DynBoneTrack* pBoneTrack;
    //max_at_uiBoneCount = MAX(max_at_uiBoneCount, pAnimTrack->uiBoneCount);
    
    // boneNames are qw aligned, so it's ok to use SIMD
	if (!stashFindPointerConst(pAnimTrack->boneTable, pcBoneTag, &pBoneTrack))
	{
		// Copy from base track
		if ( pxBaseTransform )
		{
			PERFINFO_AUTO_START_L2("copy from base track", 1);
				dynTransformCopy(pxBaseTransform, pTransform);
			PERFINFO_AUTO_STOP_L2();
		}
		else
			dynTransformClearInline(pTransform);
		return false;
	}
	
	PERFINFO_AUTO_START_L2("dynBoneTrackPosUpdate", 1);
		dynBoneTrackPosUpdate(pBoneTrack, fFrameTime, uiFrameFloor, pTransform, pxBaseTransform, pAnimTrack->pcName, bNoInterp );
	PERFINFO_AUTO_STOP_START_L2("dynBoneTrackRotUpdate", 1);
		dynBoneTrackRotUpdate(pBoneTrack, fFrameTime, uiFrameFloor, pTransform, pxBaseTransform, pAnimTrack->pcName, bNoInterp );
	PERFINFO_AUTO_STOP_START_L2("dynBoneTrackScaleUpdate", 1);
		dynBoneTrackScaleUpdate(pBoneTrack, fFrameTime, uiFrameFloor, pTransform, pxBaseTransform, pAnimTrack->pcName, bNoInterp );
	PERFINFO_AUTO_STOP_L2();
	return true;
}

static bool dynBoneTrackUpdateUncompressed(const DynAnimTrackUncompressed* pUncompressed, F32 fFrameTime, U32 uiFrameFloor, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, bool bNoInterp )
{
	const DynBoneTrackUncompressed* pBoneTrack;
    //max_atu_uiBoneCount = MAX(max_atu_uiBoneCount, pUncompressed->uiBoneCount);

    // boneNames are qw aligned, so it's ok to use SIMD
	if (!pUncompressed || !stashFindPointerConst(pUncompressed->boneTable, pcBoneTag, &pBoneTrack))
        pBoneTrack = 0;

	if(pBoneTrack) 
    {
		U32 uiNextFrame = uiFrameFloor+1;
		const F32 fInterp = fFrameTime - (F32)uiFrameFloor;

		assert(	uiNextFrame <= pUncompressed->uiTotalFrames ||
				(	uiFixDynMoveOffByOneError &&
					uiNextFrame == pUncompressed->uiTotalFrames+1
				));

        // Rotation
		if (pBoneTrack->pqRot)
		{
            Quat* const pqRot = pBoneTrack->pqRot;
			if ( uiNextFrame >= pUncompressed->uiTotalFrames || bNoInterp )
				copyQuat(pqRot[uiFrameFloor], pTransform->qRot);
			else
				quatInterp(fInterp, pqRot[uiFrameFloor], pqRot[uiNextFrame], pTransform->qRot);
		}
		else
			copyQuat(pBoneTrack->qStaticRot, pTransform->qRot);

		if (pxBaseTransform)
		{
			Quat qTemp;
			quatMultiplyInline(pTransform->qRot, pxBaseTransform->qRot, qTemp);
			copyQuat(qTemp, pTransform->qRot);
		}

		// Position
		if (pBoneTrack->pvPos)
		{
            Vec3* const pvPos = pBoneTrack->pvPos;
			if ( uiNextFrame >= pUncompressed->uiTotalFrames || bNoInterp )
				copyVec3(pvPos[uiFrameFloor], pTransform->vPos);
			else
				interpVec3(fInterp, pvPos[uiFrameFloor], pvPos[uiNextFrame], pTransform->vPos);
		}
		else
			copyVec3(pBoneTrack->vStaticPos, pTransform->vPos);

		if (pxBaseTransform)
		{
			addVec3(pTransform->vPos, pxBaseTransform->vPos, pTransform->vPos);
		}

		// Scale
		if (pBoneTrack->pvScale)
		{
            Vec3* const pvScale = pBoneTrack->pvScale;
			if ( uiNextFrame >= pUncompressed->uiTotalFrames || bNoInterp )
				copyVec3(pvScale[uiFrameFloor], pTransform->vScale);
			else
				interpVec3(fInterp, pvScale[uiFrameFloor], pvScale[uiNextFrame], pTransform->vScale);
		}
		else
			copyVec3(pBoneTrack->vStaticScale, pTransform->vScale);

		if (pxBaseTransform)
		{
			mulVecVec3(pTransform->vScale, pxBaseTransform->vScale, pTransform->vScale);
		}

        return true;
	}
    else 
    {
		// Copy from base track
		if ( pxBaseTransform )
			dynTransformCopy(pxBaseTransform, pTransform);
		else
			dynTransformClearInline(pTransform);
	}

    return false;
}

bool dynBoneTrackUpdate(const DynAnimTrack* pAnimTrack, F32 fFrameTime, DynTransform* pTransform, const char* pcBoneTag, const DynTransform* pxBaseTransform, bool bNoInterp)
{
	U32 uiFrameFloor = qtrunc(fFrameTime);
	switch (pAnimTrack->eType)
	{
		xcase eDynAnimTrackType_Uncompressed:
			return dynBoneTrackUpdateOld(pAnimTrack, fFrameTime, uiFrameFloor, pTransform, pcBoneTag, pxBaseTransform, bNoInterp);
		xcase eDynAnimTrackType_Compressed:
            {
                return dynBoneTrackUpdateUncompressed(pAnimTrack->pUncompressedTrack, fFrameTime, uiFrameFloor, pTransform, pcBoneTag, pxBaseTransform, bNoInterp);
            }
        xdefault:
        assert(0);
	}
	return false;
}