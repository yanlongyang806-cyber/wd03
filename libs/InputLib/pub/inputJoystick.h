#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#include "inputLib.h"

// Physical names for the Joystick inputs
AUTO_ENUM;
typedef enum InputJoystickPhysical {
	kInputJoystickPhysical_None,

	// Labeled Axis
	kInputJoystickPhysical_X,
	kInputJoystickPhysical_Y,
	kInputJoystickPhysical_Z,
	kInputJoystickPhysical_Rx,
	kInputJoystickPhysical_Ry,
	kInputJoystickPhysical_Rz,
	// There's also velocity, acceleration, and force variants

	// 2 Sliders
	kInputJoystickPhysical_Slider0,
	kInputJoystickPhysical_Slider1,
	kInputJoystickPhysical_SliderLast = kInputJoystickPhysical_Slider0 + 1, EIGNORE
	// There's also velocity, acceleration, and force variants

	// 4 POV
	kInputJoystickPhysical_PovUp0,
	kInputJoystickPhysical_PovUp1,
	kInputJoystickPhysical_PovUp2,
	kInputJoystickPhysical_PovUp3,
	kInputJoystickPhysical_PovUpLast = kInputJoystickPhysical_PovUp0 + 3, EIGNORE
	kInputJoystickPhysical_PovDown0,
	kInputJoystickPhysical_PovDown1,
	kInputJoystickPhysical_PovDown2,
	kInputJoystickPhysical_PovDown3,
	kInputJoystickPhysical_PovDownLast = kInputJoystickPhysical_PovDown0 + 3, EIGNORE
	kInputJoystickPhysical_PovLeft0,
	kInputJoystickPhysical_PovLeft1,
	kInputJoystickPhysical_PovLeft2,
	kInputJoystickPhysical_PovLeft3,
	kInputJoystickPhysical_PovLeftLast = kInputJoystickPhysical_PovLeft0 + 3, EIGNORE
	kInputJoystickPhysical_PovRight0,
	kInputJoystickPhysical_PovRight1,
	kInputJoystickPhysical_PovRight2,
	kInputJoystickPhysical_PovRight3,
	kInputJoystickPhysical_PovRightLast = kInputJoystickPhysical_PovRight0 + 3, EIGNORE

	// 25 Buttons
	kInputJoystickPhysical_Button0,
	kInputJoystickPhysical_Button1,
	kInputJoystickPhysical_Button2,
	kInputJoystickPhysical_Button3,
	kInputJoystickPhysical_Button4,
	kInputJoystickPhysical_Button5,
	kInputJoystickPhysical_Button6,
	kInputJoystickPhysical_Button7,
	kInputJoystickPhysical_Button8,
	kInputJoystickPhysical_Button9,
	kInputJoystickPhysical_Button10,
	kInputJoystickPhysical_Button11,
	kInputJoystickPhysical_Button12,
	kInputJoystickPhysical_Button13,
	kInputJoystickPhysical_Button14,
	kInputJoystickPhysical_Button15,
	kInputJoystickPhysical_Button16,
	kInputJoystickPhysical_Button17,
	kInputJoystickPhysical_Button18,
	kInputJoystickPhysical_Button19,
	kInputJoystickPhysical_Button20,
	kInputJoystickPhysical_Button21,
	kInputJoystickPhysical_Button22,
	kInputJoystickPhysical_Button23,
	kInputJoystickPhysical_Button24,
	kInputJoystickPhysical_ButtonLast = kInputJoystickPhysical_Button0 + 24, EIGNORE
} InputJoystickPhysical;

// Logical names that a Joystick input is capable of being mapped
AUTO_ENUM;
typedef enum InputJoystickLogical {
	kInputJoystickLogical_None,			EIGNORE

	kInputJoystickLogical_MovementX,
	kInputJoystickLogical_MovementY,
	kInputJoystickLogical_CameraX,
	kInputJoystickLogical_CameraY,
	kInputJoystickLogical_LeftTrigger,
	kInputJoystickLogical_RightTrigger,

	// Other possible axis:
	// kInputJoystickLogical_Throttle,
	// kInputJoystickLogical_Rudder,
	// kInputJoystickLogical_Wheel,
	// kInputJoystickLogical_Accelerator,
	// kInputJoystickLogical_Brake,
	// ...

	// XBox Buttons
 	kInputJoystickLogical_Start,
	kInputJoystickLogical_Select,
	kInputJoystickLogical_LB,
	kInputJoystickLogical_RB,
	kInputJoystickLogical_LStick,
	kInputJoystickLogical_RStick,
	kInputJoystickLogical_JoypadUp,
	kInputJoystickLogical_JoypadDown,
	kInputJoystickLogical_JoypadLeft,
	kInputJoystickLogical_JoypadRight,
	kInputJoystickLogical_AB,
	kInputJoystickLogical_BB,
	kInputJoystickLogical_XB,
	kInputJoystickLogical_YB,

	kInputJoystickLogical_MAX,			EIGNORE
} InputJoystickLogical;

AUTO_STRUCT;
typedef struct InputJoystickMapping {
	InputJoystickLogical eLogicalOutput;	AST(STRUCTPARAM REQUIRED)
	InputJoystickPhysical ePhysicalInput;	AST(STRUCTPARAM REQUIRED)
	const char *pchGuidDevice;				AST(STRUCTPARAM POOL_STRING)
} InputJoystickMapping;

AUTO_STRUCT;
typedef struct InputJoystickProfile {
	const char *pchName;				AST(STRUCTPARAM REQUIRED KEY)
	const char *pchFilename;			AST(CURRENTFILE)
	const char *pchDeviceName;			AST(NAME(Device))
	U16	uUsagePage;						AST(NAME(HIDUsagePage))
	U16	uUsageCollection;				AST(NAME(HIDCollection))
	U16	uUsage;							AST(NAME(HIDUsage))
	InputJoystickMapping **eaMapping;	AST(NAME(Map))
} InputJoystickProfile;

extern StaticDefineInt InputJoystickPhysicalEnum[];
extern StaticDefineInt InputJoystickLogicalEnum[];
extern ParseTable parse_InputJoystickMapping[];
#define TYPE_parse_InputJoystickMapping InputJoystickMapping
extern ParseTable parse_InputJoystickProfile[];
#define TYPE_parse_InputJoystickProfile InputJoystickProfile

// This crash handler is to trap exceptions inside IDirectInput8->EnumDevices.
// Returning true will cause the Joystick code to attempt recovery. False will
// cause the exception to propagate up.
typedef bool (*joystickCrashHandlerCB)(void);

typedef struct InputJoystick {
	U32		uButtons;
	float	fAxis[8];
	SHORT	sPOV;

	bool	bActive;	// If the controller is use by the player

	// Records which buttons were pressed this frame.
	// These are only set on the first frame that the button is pressed
	U32		uPressedButtons;
	SHORT	sPressedPOV;

	// Last state of the buttons
	U32		uLastButtons;
	SHORT	sLastPOV;

	U32		buttonTimeStamps[25];
	U32		povTimeStamps[16];

	const char	*pchGuidDevice; // Identifier of the Joystick, safe to copy
	const char	*pchName; // Name of the Joystick, safe to copy
} InputJoystick;

// Check to see if a physical Joystick input is an analog input
#define inpJoystickIsInputAnalog(ePhysical) (kInputJoystickPhysical_X <= (ePhysical) && (ePhysical) <= kInputJoystickPhysical_SliderLast)

// Toggle availability of the Joystick
void joystickSetEnabled(bool bEnabled);
bool joystickGetEnabled(void);

// Toggle processing of the Joystick
void joystickSetProcessingEnabled(bool bEnabled);
bool joystickGetProcessingEnabled(void);

// Read the logical input states as either a binary value or an analog value
bool joystickLogicalState(InputJoystickLogical eLogicalInput);
float joystickLogicalValue(InputJoystickLogical eLogicalInput);
bool joystickLogicalIsAnalog(InputJoystickLogical eLogicalInput);

// Set the current joystick profile
void joystickSetProfile(InputJoystickProfile *pProfile);

// Get the name of a joystick
const char *joystickGetName(const char *pchIdentifier);

// Find a joystick that has an used input
bool joystickGetActiveInput(const char **ppchIdentifier, InputJoystickPhysical *peInput);

// Set the handler for when IDirectInput8->EnumDevices crashes.
void joystickSetCrashHandler(joystickCrashHandlerCB cbCrashHandler);
