#include "wlModelBinning.h"
#include "wlModelBinningPrivate.h"

#include "GenericMesh.h"
#include "ScratchStack.h"
#include "timing.h"
#include "file.h"
#include "utils.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc);); // Should be 0 after startup


typedef struct OptVertVertex {
	S32 cachePos;
	int score;
	S32 numTris;
	S32 numTrisLeft;
	S32 *triIndexes;
} OptVertVertex;

typedef struct OptVertTriangle {
	int score;
	bool isAdded;
	bool isUpToDate;
	struct OptVertTriangle* next; //< used to create a chain of not up-to-date triangles
} OptVertTriangle;

#define VertexScore_CacheDecayPower   1.5
#define VertexScore_LastTriScore      0.75
#define VertexScore_ValenceBoostPower 0.5
#define VertexScore_ValenceBoostScale 2.0
#define VertexScore_MaxVertexCache    32

static int VertexScore( OptVertVertex* vert, int it )
{
	if( vert->numTrisLeft == 0 ) {
		return -1000000;
	} else {
		float accum = 0;

		if( vert->cachePos < 0 ) {
			// Vertex is not in FIFO cache - no score.
		} else if( vert->cachePos < 3 ) {
			// This vertex was used in the last triangle, so it has a
			// fixed score, whichever of the tree it's in.  Otherwise
			// you can get very different answers depending on whether
			// you add the triangle 1,2,3 or 3,1,2 - which is silly.
			accum = VertexScore_LastTriScore;
		} else {
			const float scaler = 1.0f / (VertexScore_MaxVertexCache - 3);

			assert( vert->cachePos < VertexScore_MaxVertexCache );
			// Points for being high in the cache.
			accum = powf( 1.0f - (vert->cachePos - 3) * scaler, VertexScore_CacheDecayPower );
		}

		// Bonus points for having a low number of tris still to use
		// the vert, so we get rid of lone verts quickly.
		{
			float valenceBoost = powf( vert->numTrisLeft, -VertexScore_ValenceBoostPower );
			accum += valenceBoost * VertexScore_ValenceBoostScale;
		}

		return (int)(accum * 1000);
	}
}

/// Calculate the number of vertex cache misses when rendering INDEXES
/// with a cache CACHE-SIZE big.
static int countVertexCacheMisses(S32 *indexes, int numIndexes, int cacheSize)
{
	int accum = 0;
	S32 *cache = alloca( sizeof( S32 ) * cacheSize );
	int it;
	int cacheIt;

	memset( cache, -1, sizeof( S32 ) * cacheSize );

	for( it = 0; it != numIndexes; ++it ) {
		for( cacheIt = 0; cacheIt != cacheSize; ++cacheIt ) {
			if( cache[ cacheIt ] == indexes[ it ]) {
				break;
			}
		}

		if( cacheIt == cacheSize ) {
			++accum;
			memmove( &cache[ 1 ], &cache[ 0 ], sizeof( S32 ) * (cacheSize - 1) );
			cache[ 0 ] = indexes[ it ];
		} else {
			memmove( &cache[ 1 ], &cache[ 0 ], sizeof( S32 ) * cacheIt );
			cache[ 0 ] = indexes[ it ];
		}
	}

	return accum;
}

/// Implements the vertex ordering optimization as described by Tom
/// Forsyth.
///
/// <http://home.comcast.net/~tom_forsyth/papers/fast_vert_cache_opt.html>
void geo2OptimizeVertexOrder( ModelSource *model )
{
	// reordering the triangles optimization
	if (!(model->process_time_flags & (MODEL_PROCESSED_ALPHA_TRI_SORT | MODEL_PROCESSED_VERT_COLOR_SORT)))
	{
		int subobjectIt;
		int triBase=0;

		for( subobjectIt = 0; subobjectIt != model->tex_count; ++subobjectIt )
		{
			U32 *subobjectTris = &model->unpack.tris[triBase * 3];
			int subobjectTriCount = model->tex_idx[subobjectIt].count;

#define TRI_INDEXES( tri )          (&subobjectTris[ (tri) * 3 ])

			OptVertVertex* vertData = ScratchAlloc( model->vert_count * sizeof( *vertData ));
			OptVertTriangle* triData = ScratchAlloc( subobjectTriCount * sizeof( *triData ));
			S32 cachedVerts[ VertexScore_MaxVertexCache ];

			triBase += subobjectTriCount;

			// initialize data
			memset( cachedVerts, -1, sizeof( cachedVerts ));
			{
				int it;
				for( it = 0; it != subobjectTriCount * 3; ++it ) {
					++vertData[ subobjectTris[ it ]].numTris;
				}
				for( it = 0; it != model->vert_count; ++it ) {
					vertData[ it ].cachePos = -1;
					vertData[ it ].triIndexes = ScratchAlloc( sizeof( S32 ) * vertData[ it ].numTris );
				}
				for( it = 0; it != subobjectTriCount * 3; ++it ) {
					int triIt = it / 3;
					OptVertVertex* vert = &vertData[ subobjectTris[ it ]];

					vert->triIndexes[ vert->numTrisLeft ] = triIt;
					++vert->numTrisLeft;
				}
				for( it = 0; it != model->vert_count; ++it ) {
					vertData[ it ].score = VertexScore( &vertData[ it ], it );
				}
			}
			
			// now we have everything set up and are ready to iterate
			// through the triangles, adding the one with the highest
			// score.
			{
				int it;
				S32 *optimizedTrisAccum = ScratchAlloc( subobjectTriCount * 3 * sizeof( S32 ));
				bool needFullRepass = true;
				OptVertTriangle* needUpdateTri = NULL;

				for( it = 0; it != subobjectTriCount; ++it ) {
					int bestTriAccum = 0;
					float bestTriScoreAccum = 0;

					#define UPDATE_TRI_SCORE( triIt ) {					\
							if( !triData[ triIt ].isAdded ) {			\
								float triScore = (vertData[ TRI_INDEXES( triIt )[ 0 ]].score \
												  + vertData[ TRI_INDEXES( triIt )[ 1 ]].score \
												  + vertData[ TRI_INDEXES( triIt )[ 2 ]].score ); \
								assert( triScore >= 0 );				\
								triData[ triIt ].score = triScore;		\
								triData[ triIt ].isUpToDate = true;		\
								triData[ triIt ].next = NULL;			\
																		\
								if( triScore > bestTriScoreAccum ) {	\
									bestTriAccum = triIt;				\
									bestTriScoreAccum = triScore;		\
								}										\
							}											\
						}
					
					
					if( needUpdateTri ) {
						while( needUpdateTri ) {
							OptVertTriangle* nextNeedUpdateTri = needUpdateTri->next;
							int triIt = needUpdateTri - triData;

							UPDATE_TRI_SCORE( triIt );
							
							needUpdateTri = nextNeedUpdateTri;
						}
					}
					if( bestTriScoreAccum == 0 ) {
						int triIt;
						for( triIt = 0; triIt != subobjectTriCount; ++triIt ) {
							UPDATE_TRI_SCORE( triIt );
						}
					}

					#undef UPDATE_TRI_SCORE

					// add the best tri
					{
						S32 *bestTriIndexes = TRI_INDEXES( bestTriAccum );

						{
							int vertIt;
							for( vertIt = 0; vertIt != 3; ++vertIt ) {
								int vertIndex = bestTriIndexes[ vertIt ];
								optimizedTrisAccum[ it * 3 + vertIt ] = vertIndex;
								--vertData[ vertIndex ].numTrisLeft;

								// remove the vertex from the MRU cache,
								// and add it in the front, updating its
								// score.
								{
									int cachePos = -1;
									{
										int cacheIt;
										for( cacheIt = 0; cacheIt != VertexScore_MaxVertexCache; ++cacheIt ) {
											if( cachedVerts[ cacheIt ] == vertIndex ) {
												cachePos = cacheIt;
												break;
											}
										}
									}

									if( cachePos == -1 ) {
										int vertRemovedFromCache = cachedVerts[ VertexScore_MaxVertexCache - 1 ];

										memmove( &cachedVerts[ 1 ], &cachedVerts[ 0 ],
											sizeof( *cachedVerts ) * (VertexScore_MaxVertexCache - 1) );
										if (vertRemovedFromCache != -1)
										{
											vertData[ vertRemovedFromCache ].cachePos = -1;
											vertData[ vertRemovedFromCache ].score = VertexScore( &vertData[ vertRemovedFromCache ], vertRemovedFromCache );
											{
												int triIt;
												for( triIt = 0; triIt != vertData[ vertRemovedFromCache ].numTris; ++triIt ) {
													int triIndex = vertData[ vertRemovedFromCache ].triIndexes[ triIt ];

													if( triData[ triIndex ].isUpToDate ) {
														triData[ triIndex ].next = needUpdateTri;
														needUpdateTri = &triData[ triIndex ];
														triData[ triIndex ].isUpToDate = false;
													}
												}
											}
										}
									} else {
										memmove( &cachedVerts[ 1 ], &cachedVerts[ 0 ],
											sizeof( *cachedVerts ) * cachePos );
									}
									cachedVerts[ 0 ] = vertIndex;

									{
										int cacheIt;
										for( cacheIt = 0; cacheIt != ARRAY_SIZE( cachedVerts ); ++cacheIt ) {
											if( cachedVerts[ cacheIt ] != -1 ) {
												OptVertVertex* vert = &vertData[ cachedVerts[ cacheIt ]];
												vert->cachePos = cacheIt;

												vert->score = VertexScore( vert, cachedVerts[ cacheIt ]);
												{
													int triIt;
													for( triIt = 0; triIt != vert->numTris; ++triIt ) {
														int triIndex = vert->triIndexes[ triIt ];

														if( triData[ triIndex ].isUpToDate ) {
															triData[ triIndex ].next = needUpdateTri;
															needUpdateTri = &triData[ triIndex ];
															triData[ triIndex ].isUpToDate = false;
														}
													}
												}
											}
										}
									}
								}
							}
						}

						triData[ bestTriAccum ].isAdded = true;
					}
				}

				if( 0 ) {
					int i8 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 8 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 8 );
					int i12 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 12 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 12 );
					int i16 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 16 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 16 );
					int i20 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 20 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 20 );
					int i24 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 24 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 24 );
					int i32 = countVertexCacheMisses( subobjectTris, subobjectTriCount * 3, 32 ) - countVertexCacheMisses( optimizedTrisAccum, subobjectTriCount * 3, 32 );

					printf( "Cache Improvement:  8=%-6d  12=%-6d  16=%-6d  20=%-6d  24=%-6d  32=%-6d\n",
						i8, i12, i16, i20, i24, i32 );
				}

				memcpy( subobjectTris, optimizedTrisAccum, subobjectTriCount * 3 * sizeof( S32 ));
				ScratchFree( optimizedTrisAccum );
			}


			{
				int it;
				for( it = model->vert_count - 1; it >= 0; --it ) {
					ScratchFree( vertData[ it ].triIndexes );
				}
			}

			ScratchFree( vertData );
			ScratchFree( triData );

#undef TRI_INDEXES
		}
	}

	// reordering the indices optimization
	if( 1 )
	{
		// Subobjects may share vertices (it's unlikely), but that
		// prevents this optimization from being done.
		//JE: Should be fine even with multiple sub-objects. if( model->tex_count == 1 )
		{
			U16* vertNewIndex = ScratchAlloc( sizeof( U16 ) * model->vert_count );
			memset( vertNewIndex, -1, sizeof( U16 ) * model->vert_count );

			// Reorder vertices based on what order they are accessed in the
			//  triangle list- simply first accessed is first in the list.
			{
				bool* vertIsUsed = ScratchAlloc( sizeof( bool ) * model->vert_count );
				int it;
				int numVertUsedIt = 0;

				memset( vertIsUsed, 0, sizeof( bool ) * model->vert_count );
				for( it = 0; it != model->tri_count * 3; ++it ) {
					U16 vert = model->unpack.tris[ it ];

					if( !vertIsUsed[ vert ]) {
						vertNewIndex[ vert ] = numVertUsedIt++;					
						vertIsUsed[ vert ] = true;
					}
				}


				ScratchFree( vertIsUsed );
			}

			// do the reordering -- I shouldn't need a scratch space for
			// this, but it's the easiest way to code it up.
			// Apply new indices
			{
				int it;
				for( it = 0; it != model->tri_count * 3; ++it ) {
					int oldIndex = model->unpack.tris[ it ];
					int newIndex = vertNewIndex[ oldIndex ];

					model->unpack.tris[ it ] = newIndex;
				}
			}

			// Reorder vertex data
			{
				struct {
					U8 *field;
					int stride;
				} fields[] = {
					{(void*)model->unpack.verts, sizeof(Vec3)},
					{(void*)model->unpack.norms, sizeof(Vec3)},
					{(void*)model->unpack.binorms, sizeof(Vec3)},
					{(void*)model->unpack.tangents, sizeof(Vec3)},
					{(void*)model->unpack.sts, sizeof(Vec2)},
					{(void*)model->unpack.sts3, sizeof(Vec2)},
					{(void*)model->unpack.colors, sizeof(U8)*4},
					{(void*)model->unpack.matidxs, sizeof(U8)*4},
					{(void*)model->unpack.weights, sizeof(U8)*4},
					{(void*)model->unpack.verts2, sizeof(Vec3)},
					{(void*)model->unpack.norms2, sizeof(Vec3)},
				};
				int i;
				U8 *dataFixed;
				dataFixed = ScratchAlloc(model->vert_count * sizeof(Vec4)); // Allocate largest data type
				for (i=0; i<ARRAY_SIZE(fields); i++)
				{
					int it;
					if (!fields[i].field)
						continue;
					for( it = 0; it != model->vert_count; ++it ) {
						int oldIndex = it;
						int newIndex = vertNewIndex[ oldIndex ];

						memcpy( &dataFixed[ newIndex * fields[i].stride ],
							&fields[i].field[ oldIndex * fields[i].stride ],
							fields[i].stride );
					}
					memcpy( fields[i].field, dataFixed,
						model->vert_count * fields[i].stride );
				}
				ScratchFree( dataFixed );
			}
			ScratchFree( vertNewIndex );
		}
	}
}

//////////////////////////////////////////////////////////////////////////

#define DELTA_BIT_START 2
#define DELTA_BIT_BYTES 1
#define DELTA_DATA_START (DELTA_BIT_START + DELTA_BIT_BYTES)
#define DELTA_DATA_SIZE (3 * sizeof(U32))
#define DELTA_SIZE (DELTA_DATA_START + DELTA_DATA_SIZE)

const int byte_count_code[] = {0,1,2,4};

static F32 calcScore(const int bytecounts[256], U8 delta[DELTA_SIZE])
{
	F32 score;
	int i;

	score = DELTA_DATA_SIZE - delta[0];
	for (i = 0; i < delta[0] + DELTA_BIT_BYTES; ++i)
		score += bytecounts[delta[i+DELTA_BIT_START]];
	return score / (delta[0] + DELTA_BIT_BYTES);
}

static void updateDelta(int val8, int val16, int val32, U8 delta[DELTA_SIZE])
{
	int i, code, val = 0, cur_bit_byte;
	if (val8 == 0x7f)
	{
		code	= 0;
	}
	else if ((val8 & ~0xff) == 0)
	{
		code	= 1;
		val		= val8;
	}
	else if ((val16 & ~0xffff) == 0)
	{
		code	= 2;
		val		= val16;
	}
	else
	{
		code	= 3;
		val		= val32;
	}

	cur_bit_byte = delta[1] >> 3;
	assert(cur_bit_byte < DELTA_BIT_BYTES);
	delta[DELTA_BIT_START + cur_bit_byte] |= code << (delta[1] & 7);
	delta[1] += 2;

	for (i = 0; i < byte_count_code[code]; ++i)
	{
		assert(delta[0] < DELTA_SIZE);
		delta[DELTA_DATA_START + delta[0]] = (val >> i*8) & 255;
		delta[0]++;
	}
}

static F32 calcFloatDeltaAndScore(const Vec3 v0, const Vec3 v1, const int bytecounts[256], U8 delta[DELTA_SIZE])
{
	static const F32 float_scale = 32768.f;
	int i;

	memset(delta, 0, DELTA_SIZE * sizeof(U8));

	for (i = 0; i < 3; ++i)
	{
		F32 fDelta = v1[i] - v0[i];
		updateDelta(fDelta * float_scale + 0x7f, fDelta * float_scale + 0x7fff, *((int *)&fDelta), delta);
	}

	return calcScore(bytecounts, delta);
}

static F32 calcIntDeltaAndScore(const IVec3 v0, const IVec3 v1, const int bytecounts[256], U8 delta[DELTA_SIZE])
{
	int i;

	memset(delta, 0, DELTA_SIZE * sizeof(U8));

	for (i = 0; i < 3; ++i)
	{
		int iDelta = v1[i] - v0[i] - 1;
		updateDelta(iDelta + 0x7f, iDelta + 0x7fff, iDelta, delta);
	}

	return calcScore(bytecounts, delta);
}

static void updateByteCounts(int bytecounts[256], const U8 delta[DELTA_SIZE])
{
	int i;
	assert(delta[0] < DELTA_SIZE);
	for (i = 0; i < delta[0] + DELTA_BIT_BYTES; ++i)
		bytecounts[delta[i + DELTA_BIT_START]]++;
}

void optimizeForDeltaCompression(Vec3 *verts, int vert_count, IVec3 *tris, int tri_count)
{
	int i, j, k, *vertremap, *invvertremap;
	int bytecounts[256];
	U8 delta[DELTA_SIZE];

	//////////////////////////////////////////////////////////////////////////
	// reorder verts for optimal delta compression
	invvertremap = ScratchAlloc(vert_count * sizeof(int));
	for (i = 0; i < vert_count; ++i)
		invvertremap[i] = i;

	ZeroArray(bytecounts);

	if (vert_count)
	{
		calcFloatDeltaAndScore(zerovec3, verts[0], bytecounts, delta);
		updateByteCounts(bytecounts, delta);
	}
	for (i = 0; i < vert_count - 1; ++i)
	{
		F32 highest_score = -1;
		int highest_idx = -1;
		U8 highest_delta[DELTA_SIZE];

		for (j = i+1; j < vert_count; ++j)
		{
			F32 score = calcFloatDeltaAndScore(verts[i], verts[j], bytecounts, delta);

			if (score > highest_score)
			{
				highest_score = score;
				highest_idx = j;
				memcpy(highest_delta, delta, DELTA_SIZE * sizeof(U8));
			}
		}

		assert(highest_idx > i);
		if (highest_idx != i+1)
		{
			SWAPF32(verts[i+1][0], verts[highest_idx][0]);
			SWAPF32(verts[i+1][1], verts[highest_idx][1]);
			SWAPF32(verts[i+1][2], verts[highest_idx][2]);
			SWAP32(invvertremap[i+1], invvertremap[highest_idx]);
		}

		updateByteCounts(bytecounts, highest_delta);
	}

	vertremap = ScratchAlloc(vert_count * sizeof(int));
	for (i = 0; i < vert_count; ++i)
		vertremap[invvertremap[i]] = i;

	for (i = 0; i < tri_count; ++i)
	{
		int *tri = &tris[i][0];

		assert(tri[0] < vert_count);
		assert(tri[1] < vert_count);
		assert(tri[2] < vert_count);

		tri[0] = vertremap[tri[0]];
		tri[1] = vertremap[tri[1]];
		tri[2] = vertremap[tri[2]];
	}

	ScratchFree(vertremap);
	ScratchFree(invvertremap);


	//////////////////////////////////////////////////////////////////////////
	// reorder tri indices for optimal delta compression

	if (tri_count)
	{
		F32 highest_score = -1;
		int highest_elem = 0;
		U8 highest_delta[DELTA_SIZE];
		IVec3 zeroivec3 = {0,0,0};
		IVec3 dsttri;

		copyVec3(tris[0], dsttri);
		for (k = 0; k < 3; ++k)
		{
			F32 score = calcIntDeltaAndScore(zeroivec3, dsttri, bytecounts, delta);

			if (score > highest_score)
			{
				highest_score = score;
				highest_elem = k;
				memcpy(highest_delta, delta, DELTA_SIZE * sizeof(U8));
			}

			leftShiftIVec3(dsttri);
		}

		for (k = 0; k < highest_elem; ++k)
			leftShiftIVec3(tris[0]);
		updateByteCounts(bytecounts, highest_delta);
	}
	for (i = 0; i < tri_count - 1; ++i)
	{
		int *srctri = &tris[i][0];

		F32 highest_score = -1;
		int highest_idx = -1;
		int highest_elem = 0;
		U8 highest_delta[DELTA_SIZE];

		for (j = i+1; j < tri_count; ++j)
		{
			IVec3 dsttri;
			copyVec3(tris[j], dsttri);
			for (k = 0; k < 3; ++k)
			{
				F32 score = calcIntDeltaAndScore(srctri, dsttri, bytecounts, delta);

				if (score > highest_score)
				{
					highest_score = score;
					highest_idx = j;
					highest_elem = k;
					memcpy(highest_delta, delta, DELTA_SIZE * sizeof(U8));
				}

				leftShiftIVec3(dsttri);
			}
		}


		assert(highest_idx > i);

		for (k = 0; k < highest_elem; ++k)
			leftShiftIVec3(tris[highest_idx]);

		if (highest_idx != i+1)
		{
			SWAP32(tris[i+1][0], tris[highest_idx][0]);
			SWAP32(tris[i+1][1], tris[highest_idx][1]);
			SWAP32(tris[i+1][2], tris[highest_idx][2]);
		}

		updateByteCounts(bytecounts, highest_delta);
	}
}

int wlUncompressDeltas(void *dst, const U8 *src, int stride, int count, PackType pack_type)
{
	int		i,j,code,iDelta,cur_bit=0;
	Vec4	fLast = {0,0,0,0};
	int		iLast[4] = {0,0,0,0};
	F32		inv_float_scale = 1.f;
	F32		*fPtr = dst;
	int		*iPtr = dst;
	U16		*siPtr = dst;
	U8		*biPtr = dst;
	const U8 *bits = src;
	const U8 *bytes = src + (2 * count * stride + 7)/8;
	F32		float_scale;

	PERFINFO_AUTO_START_FUNC();
	
	assert(stride >= 0 && stride <= ARRAY_SIZE(iLast) && stride <= ARRAY_SIZE(fLast));

	float_scale = (F32)(1 << *bytes++);
	if (float_scale)
		inv_float_scale = 1.f / float_scale;
	for(i=0;i<count;i++)
	{
		for(j=0;j<stride;j++)
		{
			code = (bits[cur_bit >> 3] >> (cur_bit & 7)) & 3;
			cur_bit+=2;
			switch(code)
			{
				xcase 0:
				default:
					iDelta = 0;
				xcase 1:
					iDelta = *bytes++ - 0x7f;
				xcase 2:
					iDelta = (bytes[0] | (bytes[1] << 8)) - 0x7fff;
					bytes += 2;
				xcase 3:
					iDelta = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
					bytes += 4;
			}
			switch(pack_type)
			{
				xcase PACK_F32:
				{
					F32		fDelta;

					if (code != 3)
						fDelta = iDelta * inv_float_scale;
					else
						fDelta = *((F32 *)&iDelta);
					*fPtr++ = fLast[j] = fLast[j] + fDelta;
				}
				xcase PACK_U32:
					*iPtr++ = iLast[j] = iLast[j] + iDelta + 1;
				xcase PACK_U16:
					*siPtr++ = iLast[j] = iLast[j] + iDelta + 1;
				xcase PACK_U8:
					*biPtr++ = iLast[j] = iLast[j] + iDelta + 1;
			}
		}
	}

	switch(pack_type)
	{
		xcase PACK_F32:
			assert((U8*)fPtr - (U8*)dst == count * stride * sizeof(F32));
		xcase PACK_U32:
			assert((U8*)iPtr - (U8*)dst == count * stride * sizeof(U32));
		xcase PACK_U16:
			assert((U8*)siPtr - (U8*)dst == count * stride * sizeof(U16));
		xcase PACK_U8:
			assert((U8*)biPtr - (U8*)dst == count * stride * sizeof(U8));
	}

	PERFINFO_AUTO_STOP();

	return bytes - src;
}

