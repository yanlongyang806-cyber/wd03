
#include "dynFxManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

AUTO_CMD_INT(dynDebugState.cloth.bDrawNormals, dclothDrawNormals);
AUTO_CMD_INT(dynDebugState.cloth.bDrawTanSpace, dclothDrawTangentSpace);
AUTO_CMD_INT(dynDebugState.cloth.bDrawCollision, dclothDrawCollision);
AUTO_CMD_INT(dynDebugState.cloth.bTessellateAttachments, dclothTessellateAttachments);
AUTO_CMD_INT(dynDebugState.cloth.bDisableCloth, dclothDisable) ACMD_COMMANDLINE;

AUTO_CMD_INT(dynDebugState.cloth.iMaxLOD, dclothMaxLOD) ACMD_COMMANDLINE;

AUTO_CMD_INT(dynDebugState.cloth.bDisablePartialCollision, dclothDisablePartialCollision);
AUTO_CMD_INT(dynDebugState.cloth.bDisablePartialControl, dclothDisablePartialControl);
AUTO_CMD_INT(dynDebugState.cloth.bAllBonesAreFullySkinned, dclothAllBonesAreFullySkinned);
AUTO_CMD_INT(dynDebugState.cloth.bLerpVisualsWithBones, dclothLerpVisualsWithBones);
AUTO_CMD_INT(dynDebugState.cloth.bJustDrawWithSkinnedPositions, dclothJustDrawWithSkinnedPositions);
AUTO_CMD_INT(dynDebugState.cloth.bDrawClothAsNormalGeo, dclothDrawClothAsNormalGeo);
AUTO_CMD_INT(dynDebugState.cloth.bForceCollideToSkinnedPosition, dclothForceCollideToSkinnedPosition);
AUTO_CMD_INT(dynDebugState.cloth.bForceNoCollideToSkinnedPosition, dclothForceNoCollideToSkinnedPosition);
AUTO_CMD_INT(dynDebugState.cloth.bDebugStiffBoneSelection, dclothDebugStiffBoneSelection);

AUTO_CMD_FLOAT(dynDebugState.cloth.fMaxMovementToSkinnedPos, dclothMaxMovementToSkinnedPos);


