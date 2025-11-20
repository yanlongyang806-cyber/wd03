#pragma once
GCC_SYSTEM

#include "components\referencesystem.h"

AUTO_STRUCT AST_STARTTOK(PhysicalProperties) AST_ENDTOK(EndPhysicalProperties) WIKI("PhysicalProperties");
typedef struct PhysicalProperties
{
	const char	*filename;				AST( NAME(FN) CURRENTFILE ) // Must be first for ParserReloadFile
	const char	*name_key;				AST( KEY NO_TEXT_SAVE POOL_STRING )
	const char  *countAs;				AST( POOL_STRING WIKI("What this physical property is treated as for FX/Civ/Etc."))

	F32 reverb;     AST( NAME(Reverb) WIKI("How much this material occludes reverb reflections") )
	F32 occlude;    AST( NAME(Dampen) WIKI("How much this material occludes sound") )
	F32 restitution;     AST( NAME(Restitution) DEFAULT(-1) WIKI("[0.0 - 1.0] How much this material bounces in a physics simulation") )
	F32 staticFriction;    AST( NAME(SFriction) DEFAULT(-1) WIKI("[0.0 - Inf] Static friction is how hard it is to move an object when it is still. Values above 1.0 mean very sticky objects.") )
	F32 dynamicFriction;    AST( NAME(DFriction) DEFAULT(-1) WIKI("[0.0 - 1.0] Dynamic friction is how hard it is to move an object when it is moving") )
	U32 civilianEnabled : 1;	AST( NAME(Civilian) WIKI("Which types of civilians can walk on this"))
} PhysicalProperties;

typedef struct SoundRef
{
	REF_TO(PhysicalProperties) handle;
} SoundRef;

AUTO_STRUCT;
typedef	struct PhysicalPropertiesList
{
	PhysicalProperties **profiles;  AST( NAME(PhysicalProperties) )
} PhysicalPropertiesList;

// Name: PhysicalPropertiesLoad
// Desc: Called by worldLibStartup (after utils and before materials) to load sound profiles.
//			Reads data/world/PhysicalProperties/*.txt for all sound profiles.  Loads them into
//			physical_properties.profiles.
// Args: None
// Ret : None
void physicalPropertiesLoad(void);  // Load all sound profiles

// Name: physicalPropertiesFindByName
// Desc: Finds a profile by name, e.g. "Metal" or "Default", etc. (see data/Sound/PhysicalProperties/)
bool physicalPropertiesFindByName(const char *profile_name, ReferenceHandle *profile_handle);

// Name: physicalPropertiesGetDefault
// Desc: Gets the default profile
PhysicalProperties *physicalPropertiesGetDefault(void);

const char* physicalPropertiesGetName(PhysicalProperties *properties);
