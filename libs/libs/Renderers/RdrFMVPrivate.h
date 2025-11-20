typedef struct BINK* HBINK;
typedef struct RdrDevice RdrDevice;

// Other than during creation, all of these members are only accessible from the thread
typedef struct RdrFMV {
	RdrDevice *device;
	HBINK bink;
	void *handleToClose; // Our own file handle that needs to be closed

	F32 x;
	F32 y;
	F32 x_scale;
	F32 y_scale;
	F32 alpha_level;

	// Feedback from playing thread
	bool bDone;
} RdrFMV;

typedef struct RdrFMVParams
{
	RdrFMV *fmv;
	F32 x;
	F32 y;
	F32 x_scale;
	F32 y_scale;
	F32 alpha_level;
} RdrFMVParams;
