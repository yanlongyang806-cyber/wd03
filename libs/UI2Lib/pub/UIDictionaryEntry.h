#ifndef UI_DICTIONARY_ENTRY_H
#define UI_DICTIONARY_ENTRY_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UITextEntry UITextEntry;
typedef struct UIButton UIButton;

typedef struct UIDictionaryEntry
{
	UIWidget widget;

	UITextEntry *pEntry;
	UIButton *pButton;

	UIActivationFunc cbChanged;
	UserData pChangedData;
	UIActivationFunc cbFinished;
	UserData pFinishedData;
	UIActivationFunc cbEnter;
	UserData pEnterData;
	UIActivationFunc cbOpen;
	UserData pOpenData;

	const char *pchDictHandleOrName;
} UIDictionaryEntry;

SA_RET_NN_VALID UIDictionaryEntry *ui_DictionaryEntryCreate(SA_PARAM_NN_STR const char *pchDictText, SA_PARAM_NN_STR const char *pchDictHandleOrName, bool bFiltered, bool bOpenButton);
void ui_DictionaryEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIDictionaryEntry *pDictEntry);
void ui_DictionaryEntryTick(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UI_PARENT_ARGS);
void ui_DictionaryEntryDraw(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UI_PARENT_ARGS);

const char *ui_DictionaryEntryGetText(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry);
void ui_DictionaryEntrySetText(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, const char *pchDictText);

const char *ui_DictionaryEntryGetDictionaryNameOrHandle(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry);

void ui_DictionaryEntrySetChangedCallback(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_DictionaryEntrySetFinishedCallback(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UIActivationFunc cbFinished, UserData pFinishedData);
void ui_DictionaryEntrySetEnterCallback(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UIActivationFunc cbEnter, UserData pEnterData);
void ui_DictionaryEntrySetOpenCallback(SA_PARAM_NN_VALID UIDictionaryEntry *pDictEntry, UIActivationFunc cbOpen, UserData pOpenData);

#endif