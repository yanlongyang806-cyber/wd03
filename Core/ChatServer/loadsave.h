#ifndef _LOADSAVE_H
#define _LOADSAVE_H

//#include "textparser.h"

void chatDbPreInit();   // Initializes the stash tables so the replayed logs won't crash
void chatDbInit();		// Loads the database or makes an empty one, starts the timer, etc.
void chatDBTick();		// Calls chatDBSaveNow() periodically, monitors the save progress
void chatDBSave();		// Initiates a save
void chatDBShutDown();	// Waits until the current save is complete, then exit()s
void chatDBRunMerge();	// Attempts to merge diff.chatdb with data.chatdb

void chatGuildsInit();	// Creates and Inits the guild ID to GuildName mappying earray used in player searching

//extern TokenizerParseInfo parse_channel[];
#endif
