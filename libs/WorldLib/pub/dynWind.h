#pragma once
GCC_SYSTEM

typedef struct WorldCell WorldCell;
typedef struct WorldWindSourceEntry WorldWindSourceEntry;

typedef struct DynWindSettings
{
	bool bDisabled;
	Vec3 vDir;
	F32 fMag;

	Vec3 vDirRange;
	F32 fMagRange;

	F32 fChangeRate;
	F32 fSpatialScale;
} DynWindSettings;

const DynWindSettings* dynWindGetCurrentSettings(void);
void dynWindSetCurrentSettings(DynWindSettings* settings);
bool dynWindGetEnabled(void);
void dynWindSetEnabled(bool enabled);

//should we apply wind effect (takes into account wind being disabled and current sky settings)
bool dynWindIsWindOn(void);

void dynWindUpdate(F32 fDeltaTime);
void dynWindWaitUpdateComplete(void);
void dynWindStartup(void);
F32 dynWindGetAtPositionPastEdge( const Vec3 vPosition, Vec3 vWindDir, bool isSmallObject);
F32 dynWindGetAtPosition( const Vec3 vPosition, Vec3 vWindDir, bool isSmallObject);
F32 dynWindGetMaxWind( void );
void dynWindUpdateCurrentWindParamsForWorldCell(WorldCell* cell, const F32 *camera_positions, int camera_position_count);
void dynWindStartWindSource(WorldWindSourceEntry* wind_entry);
void dynWindStopWindSource(WorldWindSourceEntry* wind_entry);

//this is used to queue moving character's grass disturbance for the next update
void dynWindQueueMovingObjectForce(const Vec3 vPosition, const Vec3 vVelocity, F32 fRadius, bool onlyAffectSmallObjects);

F32 dynWindGetSampleGridExtents();
F32 dynWindGetSampleGridExtentsSqrd();
F32 dynWindGetSampleGridDivSize();


AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct WorldRegionWindRules
{
	bool disabled;
	//this is currently the only per-region setting, but there might be more later
} WorldRegionWindRules;