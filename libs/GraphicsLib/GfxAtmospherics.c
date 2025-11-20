/***************************************************************************



***************************************************************************/

#include "ScratchStack.h"
#include "GraphicsLibPrivate.h"
#include "tiff.h"
#include "mathutil.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define MAX_BRIGHTNESS 5.f

static const Vec3 WAVELENGTHS = { 650e-3f, 570e-3f, 475e-3f };
#define INTEGRATION_STEP_COUNT 10
#define OPTICAL_LOOKUP_SIZE 128
#define OPTICAL_DEPTH_SAMPLES 100
#define FOURPI (4 * PI)

AUTO_STRUCT AST_STRIP_UNDERSCORES AST_IGNORE("SourceFiles");
typedef struct AtmosphereProperties
{
	F32 planet_radius;									AST( NAME("PlanetRadius") )
	F32 atmosphere_thickness;							AST( NAME("AtmosphereThickness") )
	F32 aerosol_atmosphere_thickness;					AST( NAME("AerosolAtmosphereThickness") )
	F32 rayleigh_scattering;							AST( NAME("RayleighScattering") )
	F32 mie_scattering;									AST( NAME("MieScattering") )
	F32 mie_phase_asymmetry;							AST( NAME("MiePhaseAsymmetry") )
	F32 intensity_multiplier;							AST( NAME("IntensityMultiplier") )
	Vec3 particle_color_hsv;							AST( NAME("ParticleHSV") FORMAT_HSV )
	Vec3 aerosol_particle_color_hsv;					AST( NAME("AerosolParticleHSV") FORMAT_HSV )
	F32 normalized_view_height;							AST( NAME("NormalizedViewHeight") )

	U32 tex_size;										AST( NAME("TextureSize") )

//  	const char *tex_name;								NO_AST
//  	const char *filename;								AST( POOL_STRING CURRENTFILE )
//  	const char **src_filenames;							AST( NAME("SourceFiles") )

} AtmosphereProperties;

typedef struct AtmosphericScatteringParams
{
	F32 planet_radius;
	F32 atmosphere_radius;

	F32 rayleigh_scattering;
	F32 inv_rayleigh_scale_depth;

	F32 mie_scattering;
	F32 mie_phase_asymmetry;
	F32 inv_mie_scale_depth;

	F32 intensity_multiplier;
	F32 atmosphere_scale;

	Vec3 particle_color;
	Vec3 aerosol_particle_color;

	F32 height;

} AtmosphericScatteringParams;

extern ParseTable parse_AtmosphereProperties[];
#define TYPE_parse_AtmosphereProperties AtmosphereProperties

typedef struct OpticalDepthValue
{
	F32 rayleigh_depth;
	F32 mie_depth;
	bool hit_planet;
} OpticalDepthValue;

typedef struct OpticalDepthLookup
{
	OpticalDepthValue *values;
} OpticalDepthLookup;

// checks ray intersection with sphere centered at the origin
__forceinline static bool checkPlanetIntersection(const Vec3 src, const Vec3 ray, F32 planet_radius, F32 atmosphere_radius, F32 *far_dist)
{
	bool hit_planet = false;
	F32 b = 2.0f * dotVec3(src, ray);
	F32 c = lengthVec3Squared(src) - SQR(atmosphere_radius);
	F32 det = SQR(b) - 4.0f * c;
	F32 dist;

	// find intersection with the atmosphere
	det = sqrtf(det);
	*far_dist = 0.5f * (-b + det);

	// now intersect with planet_radius
	c = lengthVec3Squared(src) - SQR(planet_radius);
	det = SQR(b) - 4.0f * c;
	if (det < 0)
	{
		hit_planet = false;
	}
	else
	{
		det = sqrtf(det);
		
		dist = 0.5f * (-b - det);
		if (dist > 0 && dist < *far_dist)
		{
			hit_planet = true;
			*far_dist = dist;
		}

		dist = 0.5f * (-b + det);
		if (dist > 0 && dist < *far_dist)
		{
			hit_planet = true;
			*far_dist = dist;
		}
	}

	return hit_planet;
}

static bool computeOpticalDepth(const AtmosphericScatteringParams *params, const Vec3 src, const Vec3 ray, F32 *rayleigh_depth, F32 *mie_depth)
{
	F32 far_dist, sample_length, scaled_length;
	Vec3 pos, sample_ray;
	int i;

	if (checkPlanetIntersection(src, ray, params->planet_radius, params->atmosphere_radius, &far_dist))
		return true;

	sample_length = far_dist / OPTICAL_DEPTH_SAMPLES;
	scaled_length = sample_length * params->atmosphere_scale;
	scaleVec3(ray, sample_length, sample_ray);
	scaleAddVec3(sample_ray, 0.5f, src, pos);

	// iterate through the samples to sum up the optical depth for the distance the ray travels through the atmosphere
	*rayleigh_depth = 0;
	*mie_depth = 0;
	for (i = 0; i < OPTICAL_DEPTH_SAMPLES; ++i)
	{
		F32 height = lengthVec3(pos);
		F32 alt = (height - params->planet_radius) * params->atmosphere_scale;
		*rayleigh_depth += expf(-alt * params->inv_rayleigh_scale_depth);
		*mie_depth += expf(-alt * params->inv_mie_scale_depth);

		addVec3(pos, sample_ray, pos);
	}

	// Multiply the sums by the length the ray traveled
	*rayleigh_depth *= scaled_length;
	*mie_depth *= scaled_length;

	if (!_finite(*rayleigh_depth) || *rayleigh_depth > 1.0e25f)
		*rayleigh_depth = 0;
	if (!_finite(*mie_depth) || *mie_depth > 1.0e25f)
		*mie_depth = 0;

	return false;
}

static OpticalDepthLookup *computeOpticalDepthLookup(const AtmosphericScatteringParams *params)
{
	OpticalDepthLookup *lookup;
	int height_iter, declination_iter;

	PERFINFO_AUTO_START_FUNC();

	lookup = calloc(1, sizeof(*lookup));
	lookup->values = calloc(OPTICAL_LOOKUP_SIZE * OPTICAL_LOOKUP_SIZE, sizeof(*lookup->values));

	for (declination_iter = 0; declination_iter < OPTICAL_LOOKUP_SIZE; ++declination_iter)
	{
		F32 cos_declination = CLAMPF32(2.f * (((F32)declination_iter) / (OPTICAL_LOOKUP_SIZE-1)) - 1.f, -1, 1);
		F32 declination_angle = acosf(cos_declination);
		F32 sin_declination = sinf(declination_angle);
		Vec3 ray;

		setVec3(ray, sin_declination, cos_declination, 0);

		for (height_iter = 0; height_iter < OPTICAL_LOOKUP_SIZE; ++height_iter)
		{
			OpticalDepthValue *depth_value = &lookup->values[declination_iter * OPTICAL_LOOKUP_SIZE + height_iter];
			Vec3 src;
			setVec3(src, 0, params->planet_radius + (params->atmosphere_radius - params->planet_radius) * saturate(((F32)height_iter) / (OPTICAL_LOOKUP_SIZE-1)), 0);
			depth_value->hit_planet = computeOpticalDepth(params, src, ray, &depth_value->rayleigh_depth, &depth_value->mie_depth);
		}
	}

	PERFINFO_AUTO_STOP();

	return lookup;
}

static void freeOpticalDepthLookup(OpticalDepthLookup *depth_lookup)
{
	free(depth_lookup->values);
	free(depth_lookup);
}

__forceinline static void bilinearInterpDepthValue(const OpticalDepthLookup *lookup, F32 height_coord, F32 declination_coord, OpticalDepthValue *depth_value)
{
	OpticalDepthValue lerpa, lerpb;
	int height_coord0;
	int height_coord1;
	int decl_coord0;
	int decl_coord1;
	F32 alpha;
	F32 beta;

	PERFINFO_AUTO_START_FUNC_L2();

	height_coord0 = round(floorf(height_coord));
	height_coord1 = round(ceilf(height_coord));
	decl_coord0 = round(floorf(declination_coord));
	decl_coord1 = round(ceilf(declination_coord));
	alpha = height_coord - height_coord0;
	beta = declination_coord - decl_coord0;

	lerpa.rayleigh_depth = lerp(lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord0].rayleigh_depth, lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord1].rayleigh_depth, alpha);
	lerpa.mie_depth = lerp(lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord0].mie_depth, lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord1].mie_depth, alpha);
	lerpa.hit_planet = alpha > 0.5f ? lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord1].hit_planet : lookup->values[decl_coord0 * OPTICAL_LOOKUP_SIZE + height_coord0].hit_planet;

	lerpb.rayleigh_depth = lerp(lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord0].rayleigh_depth, lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord1].rayleigh_depth, alpha);
	lerpb.mie_depth = lerp(lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord0].mie_depth, lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord1].mie_depth, alpha);
	lerpb.hit_planet = alpha > 0.5f ? lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord1].hit_planet : lookup->values[decl_coord1 * OPTICAL_LOOKUP_SIZE + height_coord0].hit_planet;

	depth_value->rayleigh_depth = lerp(lerpa.rayleigh_depth, lerpb.rayleigh_depth, beta);
	depth_value->mie_depth = lerp(lerpa.mie_depth, lerpb.mie_depth, beta);
	depth_value->hit_planet = beta > 0.5f ? lerpb.hit_planet : lerpa.hit_planet;

	PERFINFO_AUTO_STOP_L2();
}

static bool lookupOpticalDepth(const AtmosphericScatteringParams *params, const OpticalDepthLookup *lookup, const Vec3 src, const Vec3 ray, F32 *rayleigh_depth, F32 *mie_depth)
{
	OpticalDepthValue depth_value;
	F32 height;
	F32 declination;
	Vec3 src_vec;

	PERFINFO_AUTO_START_FUNC_L2();

	height = (lengthVec3(src) - params->planet_radius) * params->atmosphere_scale * (OPTICAL_LOOKUP_SIZE-1);
	height = CLAMP(height, 0, OPTICAL_LOOKUP_SIZE-1);

	copyVec3(src, src_vec);
	normalVec3(src_vec);
	declination = (0.5f + 0.5f * dotVec3(src_vec, ray)) * (OPTICAL_LOOKUP_SIZE-1);
	declination = CLAMP(declination, 0, OPTICAL_LOOKUP_SIZE-1);

	bilinearInterpDepthValue(lookup, height, declination, &depth_value);
	*rayleigh_depth = depth_value.rayleigh_depth;
	*mie_depth = depth_value.mie_depth;

	PERFINFO_AUTO_STOP_L2();

	return depth_value.hit_planet;
}

static void computeScattering(const AtmosphericScatteringParams *params, const OpticalDepthLookup *depth_lookup, const Vec3 src, const Vec3 ray, const Vec3 neg_sun_direction, Vec3 scattering)
{
	F32 far_dist, sample_length, scaled_length, rayleigh_depth_to_view, mie_depth_to_view;
	Vec3 pos, sample_ray, rayleigh_scattering, mie_scattering;
	Vec3 rayleigh_k, mie_k;
	F32 g_sqrd, cos_sun_angle, rayleigh_phase, mie_phase;
	int i, c;

	PERFINFO_AUTO_START_FUNC();

	setVec3(rayleigh_k,
		params->rayleigh_scattering / (WAVELENGTHS[0] * WAVELENGTHS[0] * WAVELENGTHS[0] * WAVELENGTHS[0]),
		params->rayleigh_scattering / (WAVELENGTHS[1] * WAVELENGTHS[1] * WAVELENGTHS[1] * WAVELENGTHS[1]),
		params->rayleigh_scattering / (WAVELENGTHS[2] * WAVELENGTHS[2] * WAVELENGTHS[2] * WAVELENGTHS[2]));

	setVec3(mie_k, params->mie_scattering, params->mie_scattering, params->mie_scattering);

	checkPlanetIntersection(src, ray, params->planet_radius, params->atmosphere_radius, &far_dist);

	if (far_dist <= 0)
	{
		setVec3same(scattering, 0);
		PERFINFO_AUTO_STOP();
		return;
	}

	g_sqrd = SQR(params->mie_phase_asymmetry);
	cos_sun_angle = -dotVec3(neg_sun_direction, ray);
	rayleigh_phase = 0.75f * (1 + SQR(MIN(cos_sun_angle, 0))); // CD: not sure about this MIN, but it seems to look better
	mie_phase = 1.5f * (1 - g_sqrd) + (1 + SQR(cos_sun_angle)) / ((2 + g_sqrd) * (1 + g_sqrd - 2 * params->mie_phase_asymmetry * cos_sun_angle));

	sample_length = far_dist / INTEGRATION_STEP_COUNT;
	scaled_length = sample_length * params->atmosphere_scale;
	scaleVec3(ray, sample_length, sample_ray);
	scaleAddVec3(sample_ray, 0.5f, src, pos);

	setVec3same(rayleigh_scattering, 0);
	setVec3same(mie_scattering, 0);
	rayleigh_depth_to_view = 0;
	mie_depth_to_view = 0;

	for (i = 0; i < INTEGRATION_STEP_COUNT; ++i)
	{
		F32 height = lengthVec3(pos);
		F32 rayleigh_depth_to_sun, mie_depth_to_sun;
		F32 alt = (height - params->planet_radius) * params->atmosphere_scale;
		F32 rayleigh_density = expf(-alt * params->inv_rayleigh_scale_depth);
		F32 mie_density = expf(-alt * params->inv_mie_scale_depth);

		// accumulate
		rayleigh_depth_to_view += rayleigh_density * sample_length;
		mie_depth_to_view += mie_density * sample_length;

		if (!lookupOpticalDepth(params, depth_lookup, pos, neg_sun_direction, &rayleigh_depth_to_sun, &mie_depth_to_sun))
		{
			// sun visible
			for (c = 0; c < 3; ++c)
			{
				F32 temp1 = FOURPI * rayleigh_k[c] * (-rayleigh_depth_to_sun - rayleigh_depth_to_view);
				rayleigh_scattering[c] += rayleigh_density * expf(temp1);

				temp1 = FOURPI * mie_k[c] * (-mie_depth_to_sun - mie_depth_to_view);
				mie_scattering[c] += mie_density * expf(temp1);
			}
		}

		addVec3(pos, sample_ray, pos);
	}

	scaleVec3(rayleigh_scattering, sample_length, rayleigh_scattering);
	scaleVec3(mie_scattering, sample_length, mie_scattering);

	for (c = 0; c < 3; ++c)
	{
		scattering[c] = rayleigh_phase * rayleigh_k[c] * rayleigh_scattering[c] * params->particle_color[c];
		scattering[c] += mie_phase * mie_k[c] * mie_scattering[c] * params->aerosol_particle_color[c];
	}

	PERFINFO_AUTO_STOP();
}

static void propertiesToParams(const AtmosphereProperties *atmosphere_properties, AtmosphericScatteringParams *params)
{
	F32 atmosphere_thickness = atmosphere_properties->atmosphere_thickness;
	F32 aerosol_atmosphere_thickness = atmosphere_properties->aerosol_atmosphere_thickness;
	F32 mie_phase_asymmetry = atmosphere_properties->mie_phase_asymmetry;

	ZeroStructForce(params);

	MAX1(atmosphere_thickness, 0);
	MAX1(aerosol_atmosphere_thickness, 0);
	if (mie_phase_asymmetry == 1)
		mie_phase_asymmetry = 0.9999999f;
	if (mie_phase_asymmetry == -1)
		mie_phase_asymmetry = -0.9999999f;

	params->planet_radius = atmosphere_properties->planet_radius;
	params->atmosphere_radius = atmosphere_properties->planet_radius + MAX(atmosphere_thickness, aerosol_atmosphere_thickness);

	params->rayleigh_scattering = atmosphere_properties->rayleigh_scattering;
	params->inv_rayleigh_scale_depth = 1.f / atmosphere_thickness;

	params->mie_scattering = atmosphere_properties->mie_scattering;
	params->mie_phase_asymmetry = mie_phase_asymmetry;
	params->inv_mie_scale_depth = 1.f / aerosol_atmosphere_thickness;

	params->atmosphere_scale = 1.0f / (params->atmosphere_radius - params->planet_radius);
	params->intensity_multiplier = atmosphere_properties->intensity_multiplier;

	gfxHsvToRgb(atmosphere_properties->particle_color_hsv, params->particle_color);
	gfxHsvToRgb(atmosphere_properties->aerosol_particle_color_hsv, params->aerosol_particle_color);

	params->height = CLAMP(atmosphere_properties->normalized_view_height, 0.05f, 1.f);
}

void gfxCreateAtmosphereLookupTexture(const char *atmosphere_filename)
{
	AtmosphereProperties props = {0};
	AtmosphericScatteringParams params;
	int data_size;
	F32 mult, highest_max_scatter = 0;
	OpticalDepthLookup *depth_lookup;
	char filename[MAX_PATH];
	U16 *dataptr, *data;
	Vec3 view_position;
	U32 x, y, z;

	if (!ParserReadTextFile(atmosphere_filename, parse_AtmosphereProperties, &props, 0))
	{
		printf("Unable to read atmosphere file \"%s\"\n", atmosphere_filename);
		return;
	}

	if (!props.tex_size || !isPower2(props.tex_size))
	{
		printf("Invalid texture size specified for atmosphere!\n");
		StructDeInit(parse_AtmosphereProperties, &props);
		return;
	}

	loadstart_printf("Processing atmosphere... ");

	propertiesToParams(&props, &params);

	data_size = props.tex_size * props.tex_size * 3 * sizeof(U16);
	mult = 1.f / (props.tex_size-1);

	data = ScratchAlloc(data_size);
	depth_lookup = computeOpticalDepthLookup(&params);

	setVec3(view_position, 0, lerp(params.planet_radius, params.atmosphere_radius, params.height), 0);

	MAX1(view_position[1], params.planet_radius + 0.001f);

	for (z = 0; z < props.tex_size; ++z)
	{
		F32 cos_sun_angle = CLAMPF32(z * mult * 2 - 1, -1, 1);
		F32 sun_angle = acosf(cos_sun_angle);
		F32 sin_sun_angle = sinf(sun_angle);
		Vec3 neg_sun_direction;
		F32 page_max_scatter = 1, multiplier = USHRT_MAX / MAX_BRIGHTNESS;
		int val;

		dataptr = data;

		setVec3(neg_sun_direction, sin_sun_angle, cos_sun_angle, 0);

		for (y = 0; y < props.tex_size; ++y)
		{
			F32 cos_view_angle = CLAMPF32(y * mult * 2 - 1, -1, 1);
			F32 view_angle = acosf(cos_view_angle);
			F32 sin_view_angle = sinf(view_angle);

			for (x = 0; x < props.tex_size; ++x)
			{
				F32 cos_rotation = CLAMPF32(x * mult * 2 - 1, -1, 1);
				F32 view_rotation_angle, sin_view_rotation_angle, cos_view_rotation_angle, max_scatter;
				Vec3 view_direction, scattering;

				if (sin_sun_angle == 0 || sin_view_angle == 0)
					cos_view_rotation_angle = 1;
				else
					cos_view_rotation_angle = (cos_rotation - cos_sun_angle * cos_view_angle) / (sin_sun_angle * sin_view_angle);
				cos_view_rotation_angle = CLAMPF32(cos_view_rotation_angle, -1, 1);
				view_rotation_angle = acosf(cos_view_rotation_angle);
				sin_view_rotation_angle = sinf(view_rotation_angle);

				setVec3(view_direction, sin_view_angle * cos_view_rotation_angle, cos_view_angle, sin_view_angle * sin_view_rotation_angle);

				computeScattering(&params, depth_lookup, view_position, view_direction, neg_sun_direction, scattering);

				scaleVec3(scattering, params.intensity_multiplier, scattering);

				max_scatter = MAX(scattering[0], scattering[1]);
				MAX1(max_scatter, scattering[2]);

				MAX1(highest_max_scatter, max_scatter);
				MAX1(page_max_scatter, max_scatter);

				val = round(multiplier * scattering[0]);
				*(dataptr++) = CLAMP(val, 0, USHRT_MAX); // R

				val = round(multiplier * scattering[1]);
				*(dataptr++) = CLAMP(val, 0, USHRT_MAX); // G

				val = round(multiplier * scattering[2]);
				*(dataptr++) = CLAMP(val, 0, USHRT_MAX); // B
			}
		}

		// save slice to tiff
		changeFileExt(atmosphere_filename, "_slice", filename);
		strcatf(filename, "%d.tif", z);
		tiffSaveToFilename(filename, data, props.tex_size, props.tex_size, sizeof(U16), 3, false, true, TIFF_DIFF_FLOAT);
	}

//	printf("Max scatter: %f\n", highest_max_scatter);

	freeOpticalDepthLookup(depth_lookup);

	ScratchFree(data);

	StructDeInit(parse_AtmosphereProperties, &props);

	loadend_printf("done.");
}

//////////////////////////////////////////////////////////////////////////

/*

#include "WorldGrid.h"
#include "qsortG.h"
#include "rgb_hsv.h"
#include "StringCache.h"

static AtmosphereProperties **planet_atmospheres = NULL;
static AtmosphereProperties **sky_atmospheres = NULL;

static int cmpAtmosphere(const AtmosphereProperties **p_atmo1, const AtmosphereProperties **p_atmo2)
{
	const AtmosphereProperties *atmo1 = *p_atmo1;
	const AtmosphereProperties *atmo2 = *p_atmo2;
	Vec3 rgb1, rgb2;

#define CMP_TOL 0.01f
#define CMP_F32(field) if (!nearSameF32Tol(atmo1->field, atmo2->field, CMP_TOL)) return SIGN(atmo1->field - atmo2->field)
#define CMP_HSV(field) hsvToRgb(atmo1->field, rgb1); hsvToRgb(atmo2->field, rgb2); if (!nearSameF32Tol(rgb1[0], rgb2[0], CMP_TOL)) return SIGN(rgb1[0] - rgb2[0]); if (!nearSameF32Tol(rgb1[1], rgb2[1], CMP_TOL)) return SIGN(rgb1[1] - rgb2[1]); if (!nearSameF32Tol(rgb1[2], rgb2[2], CMP_TOL)) return SIGN(rgb1[2] - rgb2[2])

	CMP_F32(planet_radius);
	CMP_F32(atmosphere_thickness);
	CMP_F32(aerosol_atmosphere_thickness);
	CMP_F32(rayleigh_scattering);
	CMP_F32(mie_scattering);
	CMP_F32(mie_phase_asymmetry);
	CMP_F32(intensity_multiplier);
	CMP_HSV(particle_color_hsv);
	CMP_HSV(aerosol_particle_color_hsv);

	return (int)atmo1->tex_size - (int)atmo2->tex_size;
}

void gfxAtmosphereLoadAtmospheres(void)
{
	char path[MAX_PATH];
	bool failed = false;
	int i;

	for (i = 0; ; ++i)
	{
		AtmosphereProperties *atmo = StructCreate(parse_AtmosphereProperties);
		sprintf(path, "%s/texture_library/Space/Atmosphere/Planet_Atmosphere_%d.atmosphere", fileSrcDir(), i);
		if (!ParserReadTextFile(path, parse_AtmosphereProperties, atmo, 0))
		{
			StructDestroy(parse_AtmosphereProperties, atmo);
			break;
		}

		getFileNameNoExtNoDirs(path, path);
		strcat(path, "_voltex");
		atmo->tex_name = allocAddString(path);

		eaPush(&planet_atmospheres, atmo);
	}

	for (i = 0; ; ++i)
	{
		AtmosphereProperties *atmo = StructCreate(parse_AtmosphereProperties);
		sprintf(path, "%s/texture_library/Sky/Sky_Atmosphere_%d.atmosphere", fileSrcDir(), i);
		if (!ParserReadTextFile(path, parse_AtmosphereProperties, atmo, 0))
		{
			StructDestroy(parse_AtmosphereProperties, atmo);
			break;
		}

		getFileNameNoExtNoDirs(path, path);
		strcat(path, "_voltex");
		atmo->tex_name = allocAddString(path);

		eaPush(&sky_atmospheres, atmo);
	}
}

const char *gfxAtmospherePoolAtmosphere(WorldAtmosphereProperties *atmosphere, bool is_sky, const char *source_file)
{
	AtmosphereProperties *atmo = StructCreate(parse_AtmosphereProperties);
	char path[MAX_PATH];
	char *str = NULL;
	int i;

	ParserWriteText(&str, parse_WorldAtmosphereProperties, atmosphere, 0, 0, 0);
	ParserReadText(str, parse_AtmosphereProperties, atmo, 0);
	estrDestroy(&str);

	atmo->tex_size = 64;
	eaPush(&atmo->src_filenames, StructAllocString(source_file));

	if (is_sky)
	{
		atmo->normalized_view_height = 0.05f;
		for (i = 0; i < eaSize(&sky_atmospheres); ++i)
		{
			if (cmpAtmosphere(&sky_atmospheres[i], &atmo) == 0)
			{
				StructDestroy(parse_AtmosphereProperties, atmo);
				eaPush(&sky_atmospheres[i]->src_filenames, StructAllocString(source_file));
				return sky_atmospheres[i]->tex_name;
			}
		}

		i = eaPush(&sky_atmospheres, atmo);
		sprintf(path, "%s/texture_library/Sky/Sky_Atmosphere_%d.atmosphere", fileSrcDir(), i);
		atmo->filename = allocAddFilename(path);
		sprintf(path, "Sky_Atmosphere_%d_voltex", i);
		atmo->tex_name = allocAddString(path);
	}
	else
	{
		atmo->normalized_view_height = 1;
		for (i = 0; i < eaSize(&planet_atmospheres); ++i)
		{
			if (cmpAtmosphere(&planet_atmospheres[i], &atmo) == 0)
			{
				StructDestroy(parse_AtmosphereProperties, atmo);
				eaPush(&planet_atmospheres[i]->src_filenames, StructAllocString(source_file));
				return planet_atmospheres[i]->tex_name;
			}
		}

		i = eaPush(&planet_atmospheres, atmo);
		sprintf(path, "%s/texture_library/Space/Atmosphere/Planet_Atmosphere_%d.atmosphere", fileSrcDir(), i);
		atmo->filename = allocAddFilename(path);
		sprintf(path, "Planet_Atmosphere_%d_voltex", i);
		atmo->tex_name = allocAddString(path);
	}

	return atmo->tex_name;
}

void gfxAtmosphereWritePooled(void)
{
	int i;

	for (i = 0; i < eaSize(&planet_atmospheres); ++i)
	{
		ParserWriteTextFile(planet_atmospheres[i]->filename, parse_AtmosphereProperties, planet_atmospheres[i], 0, 0);
		gfxCreateAtmosphereLookupTexture(planet_atmospheres[i]->filename);
		StructDestroy(parse_AtmosphereProperties, planet_atmospheres[i]);
	}
	eaDestroy(&planet_atmospheres);

	for (i = 0; i < eaSize(&sky_atmospheres); ++i)
	{
		ParserWriteTextFile(sky_atmospheres[i]->filename, parse_AtmosphereProperties, sky_atmospheres[i], 0, 0);
		gfxCreateAtmosphereLookupTexture(sky_atmospheres[i]->filename);
		StructDestroy(parse_AtmosphereProperties, sky_atmospheres[i]);
	}
	eaDestroy(&sky_atmospheres);
}

*/

#include "AutoGen\GfxAtmospherics_c_ast.c"

