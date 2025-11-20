

#include "EntDebugMenu.h"
#include "timing.h"

//typedef void (*DebugMenuGroupFillCB)(Entity* playerEnt, DebugMenuItem* groupRoot);
void aiDebugMenu(Entity *playerEnt, DebugMenuItem* groupRoot)
{
	PERFINFO_AUTO_START_FUNC();
	debugmenu_AddNewCommand(groupRoot, "Beacon Debug Window", "bcnDebug");
	debugmenu_AddNewCommand(groupRoot, "Check Beacon File", "bcnCheckFile");
	debugmenu_AddNewCommand(groupRoot, "AnimList Debug", "aiAnimListDebug");
	debugmenu_AddNewCommand(groupRoot, "AI Debug Selected", "aidebugent selected");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Basic Info", "aidebugtoggleflags basicinfo");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Status Table", "aidebugtoggleflags statustable");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Powers Info", "aidebugtoggleflags powers");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Team Info", "aidebugtoggleflags team");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Vars Info", "aidebugtoggleflags vars");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Extern Vars Info", "aidebugtoggleflags xvars");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Message Info", "aidebugtoggleflags msgs");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Movement Info", "aidebugtoggleflags movement");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Ratings Info", "aidebugtoggleflags ratings");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Config Mod Info", "aidebugtoggleflags configmods");
	debugmenu_AddNewCommand(groupRoot, "AI Debug - Toggle Logs", "aidebugtoggleflags logs");
	debugmenu_AddNewCommand(groupRoot, "AI Logs - Enable Combat Logs", "aidebuglogenable combat 6");
	debugmenu_AddNewCommand(groupRoot, "AI Logs - Enable Movement Logs", "aidebuglogenable movement 6");
	debugmenu_AddNewCommand(groupRoot, "AI Logs - Toggle Log to File", "++aiDebugLogToFile");
	PERFINFO_AUTO_STOP();
}

AUTO_RUN;
void aiRegisterDebugMenu(void)
{
	debugmenu_RegisterNewGroup("AI", aiDebugMenu);
}