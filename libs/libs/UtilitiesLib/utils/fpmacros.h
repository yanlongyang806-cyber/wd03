#ifndef _FPMACROS_H
#define _FPMACROS_H

#include <float.h>

C_DECLARATIONS_BEGIN

#if _PS3

//YVS
#define SET_FP_CONTROL_WORD_DEFAULT

#else

/* MK - switching to _controlfp instrinsic in float.h
#ifdef _WIN32

	#define FP_ROUND_NEAREST    0x0000
	#define FP_ROUND_DOWN       0x0400
	#define FP_ROUND_UP         0x0800
	#define FP_ROUND_ZERO       0x0c00
	#define FP_ROUND_MASK       0x0c00

	#define FP_PRECISION_24     0x0000
	#define FP_PRECISION_53     0x0200
	#define FP_PRECISION_64     0x0300
	#define FP_PRECISION_MASK   0x0300

	#define FP_EXCEPTIONS_ALL   0x003f
	#define FP_EXCEPTIONS_MASK  0x003f

	#define SAVE_FP_CONTROL_WORD(_fpCW_) {                                    \
			__asm fstcw _fpCW_                                                \
	}

	#define SET_FP_CONTROL_WORD(_fpCW_)  {                                    \
			int lVal = _fpCW_;                                                \
			__asm fldcw lVal                                                  \
	}

	#define RESTORE_FP_CONTROL_WORD(_fpCW_) {                                 \
			__asm fldcw _fpCW_                                                \
	}

#else

	#define SAVE_FP_CONTROL_WORD(_fpCW_) { }

	#define SET_FP_CONTROL_WORD(_fpCW_)  { }

	#define RESTORE_FP_CONTROL_WORD(_fpCW_) { }

#endif

#define SET_FP_CONTROL_WORD_DEFAULT 	SET_FP_CONTROL_WORD(FP_ROUND_NEAREST | FP_PRECISION_24 | FP_EXCEPTIONS_ALL)
*/
extern U32 oldControlState;
extern U32 fp_default_exception_mask;

#define FP_DEFAULT_ROUND_MODE _RC_NEAR

#ifdef _WIN64
	#define SET_FP_CONTROL_WORD_DEFAULT 	\
			_controlfp_s(&oldControlState, FP_DEFAULT_ROUND_MODE | fp_default_exception_mask, _MCW_RC | _MCW_EM);
#else
	#define SET_FP_CONTROL_WORD_DEFAULT 	\
			_controlfp_s(&oldControlState, FP_DEFAULT_ROUND_MODE | _PC_64 | fp_default_exception_mask, _MCW_RC | _MCW_EM | _MCW_PC);
#endif

#endif

C_DECLARATIONS_END
#endif
