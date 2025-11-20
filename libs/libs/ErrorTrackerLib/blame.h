#ifndef ERRORTRACKER_BLAME_H
#define ERRORTRACKER_BLAME_H

typedef struct ErrorEntry ErrorEntry;

void updateRequestedBlameInfo(void);

// For the web interface ... does async blame lookups
U32 startBlameCache(ErrorEntry *pEntry);
bool getBlameCacheProgress(U32 id, U32 *current, U32 *total, bool *complete); // Returns false on unknown ID
ErrorEntry *getBlameCache(U32 uID); // Returns NULL if not found or not complete
int SVNGetRevisionForPath (const char *svnPath);

void blameCacheWait(void); // Waits for all caching operations to complete
void BlameCache_OncePerFrame(void);

// Setting the SVN username and password, as well as the root path for SVN (eg. "svn://code" or "http://code/svn")
void setSVNUsername(const char *username);
void setSVNPassword(const char *password);
void setSVNRoot (const char *rootPath);

#endif
