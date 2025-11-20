/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "globaltypes.h"
#include "sysutil.h"
#include "serverlib.h"
#include "ClientController.h"
#include "estring.h"
#include "UtilitiesLib.h"
#include "utils.h"
#include "mathutil.h"
#include "TestClientLib.h"
#include "TextParser.h"
#include "objtransactions.h"
#include "ClientController_h_ast.h"
#include "../../core/common/autogen/controller_autogen_remotefuncs.h"



#include "../../libs/AILib/Autogen/AILib_commands_autogen_CommandFuncs.c"
#include "../../CrossRoads/AppServerLib/Autogen/AppServerLib_commands_autogen_CommandFuncs.c"
#include "../../CrossRoads/GameClientLib/Autogen/GameClientLib_commands_autogen_CommandFuncs.c"
#include "../../CrossRoads/GameServerLib/Autogen/GameServerLib_commands_autogen_CommandFuncs.c"
#include "../../libs/InputLib/Autogen/InputLib_commands_autogen_CommandFuncs.c"
#include "../../libs/ServerLib/Autogen/ServerLib_commands_autogen_CommandFuncs.c"
#include "../../libs/SoundLib/Autogen/SoundLib_commands_autogen_CommandFuncs.c"
#include "../../libs/UI2Lib/Autogen/UI2Lib_commands_autogen_CommandFuncs.c"
#include "../../libs/UtilitiesLib/Autogen/UtilitiesLib_commands_autogen_CommandFuncs.c"
#include "../../libs/WorldLib/Autogen/WorldLib_commands_autogen_CommandFuncs.c"
#include "../../libs/XRenderLib/Autogen/XrenderLib_commands_autogen_CommandFuncs.c"


// Times in seconds
#define RESPAWN_DELAY    8
#define AUTO_MOVE_DELAY  2

// Times in number of AUTO_MOVE_DELAY steps
#define AUTO_MOVE_MAX    20
#define AUTO_MOVE_FORWARD 5
#define AUTO_MOVE_TURN    4
#define AUTO_MOVE_JUMP    3

// ----- Global state ---------------------------------------------------------

// The main step list read from the script file
static ControllerScriptingStepList mainStepList;
static int currentStep;

// The repeat step list
static ControllerScriptingStepList repeatStepList;

// The time information
static int controllerTimer = 0;
static F32 controllerElapsed;

// The captured player ID used for sending certain commands
static U32 playerId;

// The auto-move control values
static bool autoMoveRandom = 0;
static F32 autoMoveStart;
static int moveCount;
static int stuckCount = 0;
static Vec3 previousLoc = { 0,0,0 };

// The auto-attack control values
static bool autoAttack = 0;

// The auto-respawn control value
static bool autoRespawn = 0;
static bool respawned = 0;
static int respawnAtDefeatPercent = 0;
static F32 respawnStart;
static bool hasRespawnLoc = 0;
static bool ignoreDefeatLoc = 0;
static Vec3 afterRespawnLoc;
static Vec3 respawnDefeatLoc;


// ----- Utility Functions ----------------------------------------------------

// Returns a random number between -(range/2) and +(range/2)
int ccRandomAngle(int range)
{
	return (int)(((rand()*range)/RAND_MAX) - (range/2));
}

// Captures the player ID for future use
void ccSetPlayerId(void)
{
	ContainerRef *ref = cmd_GetPlayerContainer();
	playerId = ref->containerID;
}

// Determines if the player is dead
bool ccIsPlayerDead(void)
{
	char *pSystemString = NULL;
	char *st;

	estrPrintf(&pSystemString, "EntityPlayer[%d].myEntityFlags",playerId);
	st = cmd_ReadObjectPath(pSystemString);
	estrDestroy(&pSystemString);

	if (st && strstr(st,"ENTITYFLAG_DEAD")) {
		free(st);
		return 1;
	} else {
		free(st);
		return 0;
	}
}


// ----- Main Work Functions --------------------------------------------------

void performStep(ControllerScriptingStep *step)
{
	if (stricmp(step->pcAction,CC_ACTION_AFTER_RESPAWN_LOC) == 0) {
		char *loc = strdup(step->pcValue);
		char *ctx = NULL;
		char *st = loc;

		st = strtok_s(st," ",&ctx);
		afterRespawnLoc[0] = atof(st);
		st = strtok_s(NULL," ",&ctx);
		afterRespawnLoc[1] = atof(st);
		st = strtok_s(NULL," ",&ctx);
		afterRespawnLoc[2] = atof(st);
		free(loc);

		hasRespawnLoc = 1;
	} else if (stricmp(step->pcAction,CC_ACTION_AUTO_ATTACK) == 0) {
		autoAttack = (atoi(step->pcValue) != 0);

	} else if (stricmp(step->pcAction,CC_ACTION_AUTO_MOVE_RANDOM) == 0) {
		autoMoveRandom = (atoi(step->pcValue) != 0);

	} else if (stricmp(step->pcAction,CC_ACTION_AUTO_RESPAWN) == 0) {
		autoRespawn = (atoi(step->pcValue) != 0);

	} else if (stricmp(step->pcAction,CC_ACTION_COMMAND) == 0) {
		TestClientExecute(step->pcValue);

	} else if (stricmp(step->pcAction,CC_ACTION_MOVE_FORWARD) == 0) {
		TestClientExecute("forward 1");

	} else if (stricmp(step->pcAction,CC_ACTION_RESPAWN_AT_DEFEAT_LOC_CHANCE) == 0) {
		respawnAtDefeatPercent = atoi(step->pcValue);

	} else if (stricmp(step->pcAction,CC_ACTION_TURN_RANDOM) == 0) {
		cmd_AdjustCamYaw(ccRandomAngle(360));

	} else if (stricmp(step->pcAction,CC_ACTION_QUIT) == 0) {
		TestClientExecute("quit");

	} else {
		printf("Unknown action '%s' ignored\n",step->pcAction);
	}
}


void ccScriptingTick(void)
{
	int i, count;
	F32 frametime;

	// Timer Maintenance
	if (!controllerTimer) {
		controllerTimer = timerAlloc();
	}
	frametime = timerElapsedAndStart(controllerTimer);
	controllerElapsed += frametime;

	// Iterate Steps (if any steps left)
	if (currentStep >= 0) {
		ControllerScriptingStep *step = mainStepList.eaSteps[currentStep];
		if (controllerElapsed - step->startDelay > step->delayTime) {
			// Delay has passed so perform step
			printf("Performing step after delay %d: %s %s\n",step->delayTime,step->pcAction,step->pcValue);
			performStep(step);

			// Check if needs to repeat
			if (step->repeatTime > 0) {
				step->startRepeat = controllerElapsed;
				eaPush(&repeatStepList.eaSteps,step);
			}

			// Move to next step
			++currentStep;
			if (currentStep >= eaSize(&mainStepList.eaSteps)) {
				currentStep = -1;
			} else {
				mainStepList.eaSteps[currentStep]->startDelay = controllerElapsed;
			}
		}
	}

	// Perform repeat actions
	count = eaSize(&repeatStepList.eaSteps);
	for(i=0; i<count; ++i) {
		ControllerScriptingStep *step = repeatStepList.eaSteps[i];
		if (controllerElapsed - step->startRepeat > step->repeatTime) {
			// Repeat step as requested
			printf("Performing step after repeat %d: %s %s\n",step->repeatTime,step->pcAction,step->pcValue);
			performStep(step);

			// Set up for next repeat
			step->startRepeat = controllerElapsed;
		}
	}

	// Check for auto-respawn action
	if (autoRespawn && (controllerElapsed - respawnStart > RESPAWN_DELAY)) {
		if (ccIsPlayerDead()) {
			if (respawnAtDefeatPercent > 0) {
				// Get location before respawn in case we need it
				Vec3 *loc = cmd_loc();
				copyVec3(*loc,respawnDefeatLoc);
				printf("Defeat location %f %f %f\n",respawnDefeatLoc[0],respawnDefeatLoc[1],respawnDefeatLoc[2]);
			}
			printf("Respawning player...\n");

			cmd_PlayerRespawn();
			respawned = 1;
		} else if (respawned) {
			if (!ignoreDefeatLoc && (respawnAtDefeatPercent > 0) && (respawnAtDefeatPercent > (int)((rand()*100)/RAND_MAX))){
				printf("Moving after spawn to defeat location %f %f %f\n",respawnDefeatLoc[0],respawnDefeatLoc[1],respawnDefeatLoc[2]);
				cmd_setpos(respawnDefeatLoc);
			} else if (hasRespawnLoc) {
				printf("Moving after spawn to specified location %f %f %f\n",afterRespawnLoc[0],afterRespawnLoc[1],afterRespawnLoc[2]);
				cmd_setpos(afterRespawnLoc);
			}
			ignoreDefeatLoc = 0;
			respawned = 0;
		}
		respawnStart = controllerElapsed;
	}

	// Check for auto-move action
	if (autoMoveRandom && (controllerElapsed - autoMoveStart > AUTO_MOVE_DELAY)) {
		++moveCount;
		autoMoveStart = controllerElapsed;

		if ((moveCount % AUTO_MOVE_FORWARD) == 0) {
			// Start moving forward on every loop in case stopped moving
			TestClientExecute("forward 1");
		}
		if ((moveCount % AUTO_MOVE_JUMP) == 0) {
			// Jump every so many moves
			TestClientExecute("up 1");
		}
		if ((moveCount % AUTO_MOVE_JUMP) == 1) {
			// Have to end the jump or won't jump again
			TestClientExecute("up 0");
		}
		if ((moveCount % AUTO_MOVE_TURN) == 0) {
			cmd_AdjustCamYaw(ccRandomAngle(360));
		}
		if (moveCount == AUTO_MOVE_MAX) {
			// Print location every so often
			Vec3 *loc = cmd_loc();
			printf("Current location %f %f %f\n",(*loc)[0],(*loc)[1],(*loc)[2]);

			if (hasRespawnLoc && (*loc)[1] < -15000) {
				// Apparently fell off the world, so die
				printf("Moving character since apparently fell below the world\n");
				cmd_setpos(afterRespawnLoc);
			} else if ((fabsf((*loc)[0] - previousLoc[0]) < 3.0) && (fabsf((*loc)[2] - previousLoc[2]) < 3.0)) {
				// Haven't moved far in the past time chunk
				++stuckCount;
				if ((stuckCount > 1) && hasRespawnLoc) {
					printf("Moving character since apparently got stuck\n");
					cmd_setpos(afterRespawnLoc);
					stuckCount = 0;
				}
			} else {
				// Save location to compare next time
				copyVec3((*loc),previousLoc);
				stuckCount = 0;
			}
			moveCount = 0;
		}
	}
}


// ----- Initialization -------------------------------------------------------

void ccInitScripting(const char *scriptName)
{
	char *fileName = NULL;
	int i;

	// Initialize for random numbers
	srand((unsigned)time(NULL));

	// Set the player's ID
	ccSetPlayerId();
	printf("Player ID is %d\n",playerId);

	// Read in the command script
	estrPrintf(&fileName, "C:/FightClub/data/%s.txt",scriptName);
	if (!ParserReadTextFile(fileName, parse_ControllerScriptingStepList, &mainStepList, 0)) {
		printf("Unable to read script file %s\n",fileName);
		assertmsg(0, "Unable to read specified script");
	}
	estrDestroy(&fileName);

	// Apply defaults and protect against failures
	for(i=eaSize(&mainStepList.eaSteps)-1; i>=0; --i) {
		if (!mainStepList.eaSteps[i]->pcAction) {
			// Skip steps with no action
			eaRemove(&mainStepList.eaSteps,i);
			--i;
			continue;
		}
		if (mainStepList.eaSteps[i]->delayTime == 0) {
			mainStepList.eaSteps[i]->delayTime = 1;
		}
		if (mainStepList.eaSteps[i]->delayTime == -1) {
			mainStepList.eaSteps[i]->delayTime = 0;
		}
		if (!mainStepList.eaSteps[i]->pcValue) {
			mainStepList.eaSteps[i]->pcValue = "";
		}
	}

	// Start on the first step
	currentStep = 0;
	respawned = 1;
	ignoreDefeatLoc = 1;
	repeatStepList.eaSteps = NULL;
}

#include "ClientController_h_ast.c"
