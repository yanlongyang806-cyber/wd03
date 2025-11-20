/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslLogSettings.h"

//AUTO_SETTINGs for turning on and off the various log groups

bool gbEnableCombatDamageEventLogging = false;
bool gbEnableCombatDeathEventLogging = true;
bool gbEnableCombatHealingEventLogging = false;
bool gbEnableCombatKillEventLogging = false;
bool gbEnableContentEventLogging = false;
bool gbEnableEconomyLogging = true;
bool gbEnableGamePlayDataLogging = false;
bool gbEnableGamePlayEventLogging = false;
bool gbEnableItemAssignmentLogging = true;
bool gbEnableItemEventLogging = true;
bool gbEnablePetAndPuppetLogging = false;
bool gbEnablePowersDataLogging = false;
bool gbEnablePvPLogging = false;
bool gbEnableRewardDataLogging = false;
bool gbEnableUgcDataLogging = false;
bool gbEnableUserExperienceLogging = true;


//__CATEGORY Enabling or disabling various categories of GS logging
//Enable combat damage logs (damage, healing)
AUTO_CMD_INT(gbEnableCombatDamageEventLogging, EnableCombatDamageEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable combat death logs (death)
AUTO_CMD_INT(gbEnableCombatDeathEventLogging, EnableCombatDeathEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable combat death logs (death)
AUTO_CMD_INT(gbEnableCombatHealingEventLogging, EnableCombatHealingEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable combat kill logs (kill, assist, nearDeath, PvP death)
AUTO_CMD_INT(gbEnableCombatKillEventLogging, EnableCombatKillEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable content event logs (interaction, cut scene, encounters, FSM States, contacts, missions, volumes, zone events)
AUTO_CMD_INT(gbEnableContentEventLogging, EnableContentEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable economy logs (numerics, items)
AUTO_CMD_INT(gbEnableEconomyLogging, EnableEconomyLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable game play data logs (interact internal, training, mail, options, afk)
AUTO_CMD_INT(gbEnableGamePlayDataLogging, EnableGamePlayDataLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable game play event logs (duels, group projects, item assignments, level up, nemesis, pvp)
AUTO_CMD_INT(gbEnableGamePlayEventLogging, EnableGamePlayEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable item assignment logging (duty officer and professions)
AUTO_CMD_INT(gbEnableItemAssignmentLogging, EnableItemAssignmentLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable item event logging (item gained/purchased/lost/used, gem slotted)
AUTO_CMD_INT(gbEnableItemEventLogging, EnableItemEventLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable pet and puppet logs (saved pets, puppets, nemesis)
AUTO_CMD_INT(gbEnablePetAndPuppetLogging, EnablePetAndPuppetLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable powers logs (power trees, powers)
AUTO_CMD_INT(gbEnablePowersDataLogging, EnablePowersDataLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable PvP logs (deaths, match results)
AUTO_CMD_INT(gbEnablePvPLogging, EnablePvPLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable reward logs (minigame events, loot drops)
AUTO_CMD_INT(gbEnableRewardDataLogging, EnableRewardDataLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable extra ugc logs
AUTO_CMD_INT(gbEnableUgcDataLogging, EnableUgcDataLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);

//Enable extra ugc logs
AUTO_CMD_INT(gbEnableUserExperienceLogging, EnableUserExperienceLogging) ACMD_AUTO_SETTING(GS_Logging, GAMESERVER);
