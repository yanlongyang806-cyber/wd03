#ifndef GSLBEACONINTERFACE_H
#define GSLBEACONINTERFACE_H

typedef struct UrlArgumentList UrlArgumentList;
typedef struct ParseTable ParseTable;

void gslBeaconServerRun(void);
void gslBeaconGetInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct);

#endif