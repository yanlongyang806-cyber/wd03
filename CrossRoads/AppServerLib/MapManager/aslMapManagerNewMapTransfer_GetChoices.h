#pragma once

typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct MapSearchInfo MapSearchInfo;


PossibleMapChoices *NewMapTransfer_GetPossibleMapChoices(MapSearchInfo *pSearchInfo, MapSearchInfo *pBackupSearchInfo, char *pReason);
