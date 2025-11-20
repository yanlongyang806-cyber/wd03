#include "sndAnim.h"

#ifndef STUB_SOUNDLIB

// Cryptic Structs
#include "earray.h"
#include "estring.h"
#include "mathutil.h"
#include "timing.h"
#include "wininclude.h"

// DynFx
//#include "dynFxParticle.h" // For getting pos and vel of event/effect
//#include "dynFxInfo.h" // For verification of soundevents
//#include "../dynFx.h" 
//#include "dynFxInterface.h"

// Sound structs
#include "sndLibPrivate.h"
#include "event_sys.h"
#include "sndSource.h"


// Memory Budget
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

SoundAnim* g_SoundAnim = NULL;

SoundAnimSource* sndAnimFindBySource(SoundAnim *pSoundAnim, SoundSource *pSoundSource, int *iIndex);
void sndAnimCleanupSource(SoundAnimSource *pSoundAnimSource);


SoundAnim* sndAnimCreate()
{
	SoundAnim *pSoundAnim = calloc(1, sizeof(SoundAnim));

	return pSoundAnim;
}

void sndAnimAddSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource)
{
	SoundAnimSource *pSoundAnimSource = calloc(1, sizeof(SoundAnimSource));
	pSoundAnimSource->pSoundSource = pSoundSource;
	
	pSoundAnimSource->uiLastTime = timeGetTime();

	eaPush(&pSoundAnim->ppSources, pSoundAnimSource);
}

SoundAnimSource* sndAnimFindBySource(SoundAnim *pSoundAnim, SoundSource *pSoundSource, int *iIndex)
{
	SoundAnimSource *pResult = NULL;

	FOR_EACH_IN_EARRAY(pSoundAnim->ppSources, SoundAnimSource, pSoundAnimSource)
	{
		if(pSoundAnimSource->pSoundSource == pSoundSource)
		{
			pResult = pSoundAnimSource;
			if(iIndex)
			{
				*iIndex = FOR_EACH_IDX(pSoundAnim->ppSources, pSoundAnimSource);
			}
			break;
		}
	}
	FOR_EACH_END

	return pResult;
}

void sndAnimCleanupSource(SoundAnimSource *pSoundAnimSource)
{
	// turn off talk bit
	if(g_audio_state.ent_talking_func)
	{
		// request to turn bit off
		pSoundAnimSource->bTalkBit = false;

		g_audio_state.ent_talking_func(pSoundAnimSource->pSoundSource->point.entRef, false);
	}
}

void sndAnimRemoveSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource)
{
	int iIndex;
	SoundAnimSource *pSoundAnimSource;

	if( pSoundAnimSource = sndAnimFindBySource(pSoundAnim, pSoundSource, &iIndex) )
	{
		sndAnimCleanupSource(pSoundAnimSource);

		eaRemove(&pSoundAnim->ppSources, iIndex);
		SAFE_FREE(pSoundAnimSource);
	}
}

#define ATTACK_THRESHOLD (0.01)
#define RELEASE_THRESHOLD (ATTACK_THRESHOLD * 0.05) // 1/20th
#define MIN_RELEASE_DUR (50)

void sndAnimTick(SoundAnim *pSoundAnim)
{
	PerfInfoGuard *guard;
	PERFINFO_AUTO_START_FUNC_GUARD(&guard);
	if(g_audio_state.ent_talking_func)
	{
		// step through each and set/unset bits
		FOR_EACH_IN_EARRAY(pSoundAnim->ppSources, SoundAnimSource, pSoundAnimSource)
		{
			if(pSoundAnimSource->bTalkBit)
			{
				if(pSoundAnimSource->pSoundSource->currentAmp <= RELEASE_THRESHOLD)
				{
					U32 timeDelta = timeGetTime() - pSoundAnimSource->uiLastTime;
					if(timeDelta > MIN_RELEASE_DUR)
					{
						// save this state on our struct
						pSoundAnimSource->bTalkBit = false;
						
						// mark the time
						pSoundAnimSource->uiLastTime = timeGetTime(); 

						// request to turn bit off
						g_audio_state.ent_talking_func(pSoundAnimSource->pSoundSource->point.entRef, false);
					}
					//printf("off timeDelta: %d\n", timeDelta);
				}
			}
			else
			{
				if(pSoundAnimSource->pSoundSource->currentAmp > ATTACK_THRESHOLD)
				{
					// save this on our struct
					pSoundAnimSource->bTalkBit = true;

					// mark the time
					pSoundAnimSource->uiLastTime = timeGetTime(); 

					// turn the bit on
					g_audio_state.ent_talking_func(pSoundAnimSource->pSoundSource->point.entRef, true);

					//printf("on  timeDelta: %d\n", timeDelta);
				}
			}
		}
		FOR_EACH_END
	}
	PERFINFO_AUTO_STOP_GUARD(&guard);
}

typedef struct SndAnimContactDialogSource
{
	SoundSource *pSource;
	U32 uiLastTime;
	bool bTalkingBit;
} SndContactDialogSource;
static SndContactDialogSource s_SndContactDialogSourceData = {0};

void sndAnimAddContactDialogSource(SoundSource *pSoundSource)
{
	s_SndContactDialogSourceData.pSource = pSoundSource;
	s_SndContactDialogSourceData.bTalkingBit = false;
	if (pSoundSource) {
		pSoundSource->drivesContactDialogAnim = 1;
	}
}

void sndAnimRemoveContactDialogSource(SoundSource *pSoundSource)
{
	if (s_SndContactDialogSourceData.pSource == pSoundSource)
	{
		s_SndContactDialogSourceData.pSource = NULL;
		s_SndContactDialogSourceData.bTalkingBit = false;
	}
}

bool sndAnimIsContactDialogSourceAudible(void)
{
	if (s_SndContactDialogSourceData.pSource)
	{
		if(s_SndContactDialogSourceData.pSource->currentAmp <= RELEASE_THRESHOLD)
		{
			U32 timeDelta = timeGetTime() - s_SndContactDialogSourceData.uiLastTime;
			if(timeDelta > MIN_RELEASE_DUR)
			{
				s_SndContactDialogSourceData.uiLastTime = timeGetTime(); 
				s_SndContactDialogSourceData.bTalkingBit = false;
			}
		}
		else if(s_SndContactDialogSourceData.pSource->currentAmp > ATTACK_THRESHOLD)
		{
			s_SndContactDialogSourceData.uiLastTime = timeGetTime(); 
			s_SndContactDialogSourceData.bTalkingBit = true;
		}
		return s_SndContactDialogSourceData.bTalkingBit;
	}

	return false;
}

#else

SoundAnim* sndAnimCreate() { return NULL; }

void sndAnimAddSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource) { }
void sndAnimRemoveSource(SoundAnim *pSoundAnim, SoundSource *pSoundSource) { }

void sndAnimAddContactDialogSource(SoundSource *pSoundSource) {}
void sndAnimRemoveContactDialogSource(SoundSource *pSoundSource) {}
bool sndAnimIsContactDialogSourceAudible(void) {return false;}

void sndAnimTick(SoundAnim *pSoundAnim) { }

#endif