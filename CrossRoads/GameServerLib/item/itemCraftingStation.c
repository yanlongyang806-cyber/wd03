//
//
//
//Door* craft_FindFromTracker(GroupTracker* doorTracker)
//{
//	char* trackerString = handleStringFromTracker(doorTracker);
//	if (trackerString)
//	{
//		ClickableObject* retDoor;
//		stashFindPointer(s_DoorFromTrackerStringHash, trackerString, &retDoor);
//		free(trackerString);
//		return retDoor;
//	}
//	return NULL;
//}
//
//
//
//bool craft_CanInteract(WorldInteractionNode *interactNode, Entity* playerEnt)
//{
//	GroupTracker* tracker = wlInteractionNodeGetGroupTracker(interactNode);
//	ClickableObject* door = doorTracker ? encounter_FindDoorFromTracker(doorTracker) : NULL;
//	InteractTarget interTarg;
//
//	// If this door is "busy" (in cooldown, already being used) it can't be interacted with
//	interTarg.entRef = 0; interTarg.pClickableNode = NULL;
//	interTarg.pDoorNode = interactNode;
//	if(IsInteractTargetBusy(interTarg))
//		return false;
//
//	// If there's a canInteract expression, evaluate it
//	if(door && door->interactProps.interactCond && !door_Evaluate(door, doorTracker, playerEnt, door->interactProps.interactCond))
//		return false;
//
//	return true;
//}
//
//void craft_BeginInteraction(WorldInteractionNode* interactNode, GroupTracker* doorTracker, Entity* playerEnt)
//{
//	ClickableObject* door = encounter_FindDoorFromTracker(doorTracker);
//	InteractTarget target;
//
//	// Only players can interact
//	if(!playerEnt->pPlayer)
//		return;
//
//	target.entRef = 0;
//	target.bLoot = 0;
//	target.pDoorNode = interactNode;
//	target.pClickableNode = NULL;
//
//	player_StartInteracting(playerEnt, target);
//}
//
//char* craft_DisplayString(WorldInteractionNode* interactNode, GroupTracker* doorTracker)
//{
//	ClickableObject* door = encounter_FindDoorFromTracker(doorTracker);
//	if (door)
//	{
//		char doorStr[1024];
//		if (door->interactProps.pchInteractText)
//			sprintf(doorStr, "%s", door->interactProps.pchInteractText);
//		else if (door->spawnTarget && !stricmp("MissionReturn", door->spawnTarget))
//			sprintf(doorStr, "Exit Mission");
//		else
//			sprintf(doorStr, "Open Door");
//		return StructAllocString(doorStr);
//	}
//	else
//	{
//		return StructAllocString("Unreferenced Door");
//	}
//}
//
//char* craft_GetFx(WorldInteractionNode* interactNode, GroupTracker* doorTracker)
//{
//	if (encounter_FindDoorFromTracker(doorTracker))
//		return "TrackerHighlight";
//	return NULL;
//}
