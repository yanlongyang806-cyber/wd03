#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

typedef struct WorldAtmosphereProperties WorldAtmosphereProperties;

void gfxCreateAtmosphereLookupTexture(const char *atmosphere_filename);

/*
void gfxAtmosphereLoadAtmospheres(void);
const char *gfxAtmospherePoolAtmosphere(WorldAtmosphereProperties *atmosphere, bool is_sky, const char *source_file);
void gfxAtmosphereWritePooled(void);
*/
