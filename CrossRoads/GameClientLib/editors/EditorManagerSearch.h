#pragma once
GCC_SYSTEM
#ifndef __EDITORMANAGERSEARCH_H__
#define __EDITORMANAGERSEARCH_H__

#ifndef NO_EDITORS

typedef struct EMSearchResultTab EMSearchResultTab;

/******
* The Editor Manager allows the user to perform generic usage searches by accessing
* a special search panel on the "Utilities" tab.  These searches return a list of assets, each
* of which can be double-clicked to open for editing.  Each editor can implement a search
* across its specific data types that will be invoked when a usage search occurs.  Search
* results are displayed in a new panel that appears on the sidebar.
*
* In addition, there is an API that allows editors to display any search results it wishes by
* passing an EMSearchResult object, which then appears as a separate tab.
******/

void emSearchResultTabSaveColWidths(SA_PARAM_NN_VALID const EMSearchResultTab *result_tab);
//void emSearchUsages(SA_PARAM_NN_VALID EMEditor *editor, SA_PARAM_OP_STR const char *search_key);

#endif // NO_EDITORS

#endif // __EDITORMANAGERSEARCH_H__