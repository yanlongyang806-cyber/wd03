
void Item_InnatePowerAutoDesc(Entity *pEnt, Item *pItem, char **ppchDesc, S32 eActiveGemType);
void Item_GetInnatePowerDifferencesAutoDesc(Entity *pEnt, Item *pItem, Item *pOtherItem, char **ppchDesc);
void Item_PowerAutoDescCustom(Entity *pEnt,
	Item *pItem,
	char **ppchDesc,
	const char *pchPowerMessageKey,
	const char *pchAttribMessageKey,
	S32 eActiveGemSlotType);

void Item_GetItemCategoriesString(SA_PARAM_OP_VALID ItemDef * pItemDef, 
	SA_PARAM_OP_VALID const char* pchFilterPrefix,
	SA_PARAM_OP_VALID char ** estrItemCategories);

const char* item_GetItemPowerUsagePrompt(Language lang, Item* pItem);

void GetItemDescriptionFromItemSetDef(char **pestrItemSetDesc, 
	SA_PARAM_OP_VALID ItemDef *pItemDefSet, 
	SA_PARAM_OP_VALID Entity *pEntity, 
	const char* pchPowerMessageKey, 
	const char* pchAttribModsMessageKey,
	const char* pchItemSetFormatKey,
	GameAccountDataExtract *pExtract);

void GetItemDescriptionItemSet(char **pestrItemSetDesc, 
	SA_PARAM_NN_VALID ItemDef *pItemDef, 
	SA_PARAM_OP_VALID Entity *pEntity, 
	const char* pchPowerKey, 
	const char* pchModKey,
	const char* pchItemSetFormatKey,
	GameAccountDataExtract *pExtract);

// End of File