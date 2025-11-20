#ifndef __EDITORMANAGEROPTIONS_H__
#define __EDITORMANAGEROPTIONS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EMOptionsTab EMOptionsTab;
typedef struct UITab UITab;

typedef bool (*EMOptionsLoadFunc)(EMOptionsTab *options_tab, UITab *tab);
typedef void (*EMOptionsTabFunc)(EMOptionsTab *options_tab, void *data);

typedef struct EMOptionsTab
{
	char tab_name[128];
	UITab *tab;
	void *data;
	bool show;

	EMOptionsLoadFunc load_func;
	EMOptionsTabFunc ok_func;
	EMOptionsTabFunc cancel_func;
} EMOptionsTab;

/********************
* MAIN
********************/
void emOptionsInit(void);

#endif // NO_EDITORS

#endif // __EDITORMANAGEROPTIONS_H__