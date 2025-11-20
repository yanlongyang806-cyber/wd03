#ifndef EXPERIMENT_UI_H
#define EXPERIMENT_UI_H

typedef struct Entity Entity;
typedef struct Item Item;
typedef struct GameAccountDataExtract GameAccountDataExtract;

bool ExperimentIsItemInListByBag(Item* pItem, int iBagIdx, int iSlotIdx, GameAccountDataExtract *pExtract);

#endif // EXPERIMENT_UI_H
