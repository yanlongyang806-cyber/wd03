#if !PLATFORM_CONSOLE

#include "ListView.h"
#include "StringUtil.h"
#include <CommCtrl.h>

#include "textparser.h"
#include "file.h"
#include "tokenstore.h"

#pragma comment (lib, "comctl32.lib")

#define SNDMSGW SendMessageW

// #define ListView_DeleteAllItemsW(hwnd) \
//     (BOOL)SNDMSGW((hwnd), LVM_DELETEALLITEMS, 0, 0L)

// #define ListView_DeleteItemW(hwnd, i) \
//     (BOOL)SNDMSGW((hwnd), LVM_DELETEITEM, (WPARAM)(int)(i), 0L)

// #define ListView_EnsureVisibleW(hwndLV, i, fPartialOK) \
//     (BOOL)SNDMSGW((hwndLV), LVM_ENSUREVISIBLE, (WPARAM)(int)(i), MAKELPARAM((fPartialOK), 0))

// #define ListView_FindItemW(hwnd, iStart, plvfi) \
//     (int)SNDMSGW((hwnd), LVM_FINDITEMW, (WPARAM)(int)(iStart), (LPARAM)(const LV_FINDINFOW *)(plvfi))

#define ListView_GetItemW(hwnd, pitem) \
    (BOOL)SNDMSGW((hwnd), LVM_GETITEMW, 0, (LPARAM)(LV_ITEMW *)(pitem))

// #define ListView_GetItemCount W(hwnd) \
//     (int)SNDMSGW((hwnd), LVM_GETITEMCOUNT, 0, 0L)

// #define ListView_GetNextItemW(hwnd, i, flags) \
//     (int)SNDMSGW((hwnd), LVM_GETNEXTITEM, (WPARAM)(int)(i), MAKELPARAM((flags), 0))

// #define ListView_GetSelectionMarkW(hwnd) \
//     (int)SNDMSGW((hwnd), LVM_GETSELECTIONMARK, 0, 0)

// #define ListView_HitTestW(hwndLV, pinfo) \
//     (int)SNDMSGW((hwndLV), LVM_HITTEST, 0, (LPARAM)(LV_HITTESTINFO *)(pinfo))

#define ListView_InsertColumnW(hwnd, iCol, pcol) \
     (int)SNDMSGW((hwnd), LVM_INSERTCOLUMNW, (WPARAM)(int)(iCol), (LPARAM)(const LV_COLUMNW *)(pcol))

#define ListView_InsertItemW(hwnd, pitem)   \
    (int)SNDMSGW((hwnd), LVM_INSERTITEMW, 0, (LPARAM)(const LV_ITEMW *)(pitem))

// #define ListView_SetExtendedListViewStyleW(hwndLV, dw)\
//         (DWORD)SNDMSGW((hwndLV), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, dw)

 #define ListView_SetItemW(hwnd, pitem) \
     (BOOL)SNDMSGW((hwnd), LVM_SETITEMW, 0, (LPARAM)(const LV_ITEMW *)(pitem))

// #define ListView_SetItemStateW(hwndLV, i, data, mask) \
// { LV_ITEM _ms_lvi;\
//   _ms_lvi.stateMask = mask;\
//   _ms_lvi.state = data;\
//   SNDMSGW((hwndLV), LVM_SETITEMSTATE, (WPARAM)(i), (LPARAM)(LV_ITEM *)&_ms_lvi);\
// }

// #define ListView_SetSelectionMarkW(hwnd, i) \
//     (int)SNDMSGW((hwnd), LVM_SETSELECTIONMARK, 0, (LPARAM)(i))

// #define ListView_SortItems W(hwndLV, _pfnCompare, _lPrm) \
//   (BOOL)SNDMSGW((hwndLV), LVM_SORTITEMS, (WPARAM)(LPARAM)(_lPrm), \
//   (LPARAM)(PFNLVCOMPARE)(_pfnCompare))

// #define ListView_SubItemHitTestW(hwnd, plvhti) \
//         (int)SNDMSGW((hwnd), LVM_SUBITEMHITTEST, 0, (LPARAM)(LPLVHITTESTINFO)(plvhti))


typedef struct
{
	int iColumn;
	ListViewColumnCompareFunc compare_func;
} ListViewCompareFuncHolder;

typedef struct ListView {
	const ParseTable *tpi;
	HWND hDlgParent;
	HWND hDlgListView;
	int iNumItems; // Size of EArray, not necessarily the number of items visible in the list
	int iNumColumns;
	EArrayHandle eaElements_data;
	EArrayHandle *eaElements;
	EArrayIntHandle eaForegroundColors;
	EArrayIntHandle eaBackgroundColors;

	ListViewItemColorFunc item_color_func;
	ListViewSubItemColorFunc sub_item_color_func;

	ListViewCompareFuncHolder **ppCompareFuncs;



	int iSubItem; // When used in a callback, the column to be sorted on
	intptr_t *subItemMap; // Map of what subitems go to what elements in the TPI..., includes subelements that point to more arrays
	bool sortable;
	bool colored;
	bool check_boxes;
	bool noScrollToEndOnAdd;
	bool doingInitialBuild;
} ListView;

ListViewCompareFuncHolder *FindCompareFuncHolder(ListView *lv, int iColumn);

void listViewDoingInitialBuild(ListView *lv, bool bDoingInitialBuild)
{
	lv->doingInitialBuild = bDoingInitialBuild;
	if (!lv->doingInitialBuild)
		UpdateWindow(lv->hDlgListView);
}

void listViewSetColorFuncs(ListView *lv, ListViewItemColorFunc item_color_func, ListViewSubItemColorFunc sub_item_color_func)
{
	if (item_color_func || sub_item_color_func)
		lv->colored = true;
	lv->item_color_func = item_color_func;
	lv->sub_item_color_func = sub_item_color_func;
}

void listViewDefaultItemColorFunc(ListView *lv, void *structptr, int row, COLORREF *clrText, COLORREF *clrTextBk);

ListView *listViewCreate(void)
{
	ListView *lv = calloc(sizeof(ListView), 1);
	lv->eaElements = &lv->eaElements_data;
	lv->sortable = true;
	lv->colored = true;	// this is set to true when any item's color is set with listViewSetItemColor()
	lv->item_color_func = listViewDefaultItemColorFunc;
	return lv;
}

void listViewDestroy(ListView *lv)
{
	eaDestroy(lv->eaElements);
	eaiDestroy(&lv->eaForegroundColors);
	eaiDestroy(&lv->eaBackgroundColors);
	eaDestroyEx(&lv->ppCompareFuncs, NULL);
	free(lv);
}

void listViewSetSortable(ListView *lv, bool sortable)
{
	lv->sortable = sortable;
}

void listViewSetNoScrollToEndOnAdd(ListView *lv, bool noScrollToEndOnAdd)
{
	lv->noScrollToEndOnAdd = noScrollToEndOnAdd;
}


void listViewSetColumnWidth(ListView *lv, int iColumnNum, int width)
{
	ListView_SetColumnWidth(lv->hDlgListView, iColumnNum, width);
}


static int listViewInitInternal(const ParseTable tpi[], HWND hDlgListView, int count, intptr_t **subItemMap)
{
	LV_COLUMNW lvC;
	int i;
	wchar_t buf[1024];

	// Add the columns
	lvC.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvC.fmt = LVCFMT_LEFT;
	lvC.cx = 75;
	lvC.pszText = buf;
	if (*subItemMap==NULL) {
		*subItemMap = calloc(ParserGetTableNumColumns((ParseTable*)tpi), sizeof(intptr_t));
	}

	// Load the column list from the tpi
	FORALL_PARSETABLE(tpi, i)
	{
		if (GetBoolFromTPIFormatString((ParseTable*)(&tpi[i]), "NO_LIST_VIEW"))
		{
			continue;
		}

		lvC.cx = TOK_FORMAT_GET_LVWIDTH_DEF(tpi[i].format, 75);

		switch (TOK_GET_TYPE(tpi[i].type))
		{
		case TOK_BOOLFLAG_X:
		case TOK_BOOL_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_INT_X:
		case TOK_STRING_X:
		case TOK_U8_X:
		case TOK_F32_X:
			(*subItemMap)[i] = count;
			lvC.iSubItem = count;
			UTF8ToWideStrConvert(tpi[i].name,buf,ARRAY_SIZE(buf));
			if(ListView_InsertColumnW(hDlgListView, count, &lvC) == -1) {
				assert(0);
			}
			count++;
			break;
		case TOK_STRUCT_X:
			count=listViewInitInternal((ParseTable*)tpi[i].subtable, hDlgListView, count, (intptr_t**)&(*subItemMap)[i]);
			break;
		case TOK_START:
		case TOK_END:
		case TOK_IGNORE:
			break;
		default:
			assert(!"This type not supported by ListView");
			break;
		}
	}

	return count;
}



void listViewInit(ListView *lv, const ParseTable tpi[], HWND hDlg, HWND hDlgListView)
{

	lv->tpi = tpi;
	lv->hDlgParent = hDlg;
	lv->hDlgListView = hDlgListView;
	lv->iNumItems = 0;
	lv->subItemMap = NULL;
	lv->iNumColumns = listViewInitInternal(tpi, hDlgListView, 0, &lv->subItemMap);

	// entire row will be highlighted on selection
	ListView_SetExtendedListViewStyle(hDlgListView, LVS_EX_FULLROWSELECT);
}

void listViewEnableCheckBoxes(ListView * lv, bool enable)
{
	DWORD settings = LVS_EX_FULLROWSELECT;

	lv->check_boxes = enable;
	if(enable)
		settings |= LVS_EX_CHECKBOXES;

	ListView_SetExtendedListViewStyle(lv->hDlgListView, settings);
}

static bool getElement(const ParseTable tpi[], void *structptr, int index, intptr_t *subItemMap,
					  OUT ParseTable const **ptpi, OUT void **element)
{
	int i;
	FORALL_PARSETABLE(tpi, i)
	{
		if (GetBoolFromTPIFormatString((ParseTable*)(&tpi[i]), "NO_LIST_VIEW"))
		{
			continue;
		}

		switch (TOK_GET_TYPE(tpi[i].type))
		{
		case TOK_BOOLFLAG_X:
		case TOK_BOOL_X:
		case TOK_INT64_X:
		case TOK_INT16_X:
		case TOK_INT_X:
		case TOK_STRING_X:
		case TOK_U8_X:
		case TOK_F32_X:
			if (subItemMap[i] == index) {
				*ptpi = &tpi[i];
				*element = structptr;
				return true;
			}
			break;
		case TOK_STRUCT_X:
			{
				void* substruct = TokenStoreGetPointer((ParseTable*)tpi, i, structptr, 0, NULL);
				if (!substruct) break;
				if (getElement((ParseTable*)tpi[i].subtable, substruct, index, (intptr_t*)(subItemMap[i]), ptpi, element))
					return true;
			}
			break;
		case TOK_START:
		case TOK_END:
		case TOK_IGNORE:
			break;
		default:
			assert(!"This type not supported by ListView");
			break;
		}
	}
	*ptpi = NULL;
	*element = NULL;
	return false;
}

int InnerWriteTextToken(FILE* out, ParseTable tpi[], int column, void* structptr, int level, bool showname, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);
int InnerWriteTextFile(FILE* out, ParseTable pti[], void* structptr, int level, bool ignoreInherited, WriteTextFlags iWriteTextFlags, StructTypeField iOptionFlagsToMatch, StructTypeField iOptionFlagsToExclude);

static void listViewUpdateItem(ListView *lv, bool bNewItem, int index)
{
	LV_ITEMW lvI;
	int i;
	int iColumnNum=0;
	wchar_t oldbuf[1024]={0};
	void *structptr = eaGet(lv->eaElements, index);
	int item_num; // index into windows list view control
	static StuffBuff sbBuff;
	static FILE *fpBuff=NULL;

	if (!sbBuff.buff)
		initStuffBuff(&sbBuff, 128);
	if (!fpBuff) {
		fpBuff = fileOpenStuffBuff(&sbBuff);
	}

	if (!bNewItem) {
		LVFINDINFO lvfi;
		// Find the correct index for this item
		lvfi.flags = LVFI_PARAM;
		lvfi.lParam = index;
		item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
		assert(item_num!=-1);

	} else {
		item_num = ListView_GetItemCount(lv->hDlgListView);
	}

	// Insert item
	lvI.state = 0;
	lvI.stateMask = 0;
	lvI.iItem = item_num;
	lvI.cchTextMax = ARRAY_SIZE(oldbuf)-1;
	i=0;

	// Loop over each column
	for (iColumnNum = 0; iColumnNum < lv->iNumColumns; iColumnNum++) {
		ParseTable *tpi=NULL;
		void *element;
		wchar_t *buf = NULL;
		
		if (iColumnNum==0 && bNewItem) {
			lvI.mask = LVIF_TEXT | LVIF_STATE | LVIF_PARAM;
			lvI.lParam = index;
		} else {
			lvI.mask = LVIF_TEXT | LVIF_STATE;
		}
		lvI.iSubItem = iColumnNum;

		// Get the old value
		lvI.pszText = oldbuf;
		if (!bNewItem && !lv->doingInitialBuild)
			ListView_GetItemW(lv->hDlgListView, &lvI);

		if (structptr == LISTVIEW_EMPTY_ITEM) {
			element = NULL;
		} else {
			getElement(lv->tpi, structptr, iColumnNum, lv->subItemMap, &tpi, &element);
		}
			

		if (element == NULL) {
			buf = L"";
		} else {
			int len;
			static int zero=0;
			static wchar_t *static_buf;
			static int static_buf_max=0;

			sbBuff.idx = 0;
			InnerWriteTextToken(fpBuff, tpi, 0, element, 0, 0, WRITETEXTFLAG_NO_QUOTING_OR_ESCAPING_STRINGS, 0, 0);
			addBinaryDataToStuffBuff(&sbBuff, (char*)&zero, 1); // Null terminate
			len = sbBuff.idx-1;
			dynArrayFit(&static_buf, sizeof(*static_buf), &static_buf_max, len+1);
			buf = static_buf;
			buf[0] = 0;
			assert(sbBuff.buff);

			UTF8ToWideStrConvert(sbBuff.buff,buf,len+1);
			
			while (*buf==' ') buf++;

			// chop off an quotes
			if (element && sbBuff.buff[sbBuff.idx-2]=='\"' && *buf=='\"') {
				buf[wcslen(buf)-1] = 0;
				buf = buf+1;
			}
		}
		lvI.pszText = buf;

		// set the item
		if (bNewItem && iColumnNum==0) {
			if(ListView_InsertItemW(lv->hDlgListView, &lvI) == -1) {
				assert(0);
			}
		} else {
			if (wcscmp(oldbuf, lvI.pszText)!=0) {
				if(ListView_SetItemW(lv->hDlgListView, &lvI) == -1) {
					assert(0);
				}
			}
		}
	}
/* Tooltips (didn't seem to work)
	fpBuff = fileOpenStuffBuff(&sbBuff);
	InnerWriteTextFile(fpBuff, lv->tpi, structptr, 0);
	fclose(fpBuff);
	buf = sbBuff.buff;
	while (*buf==' ') buf++;
	{
		WCHAR buf2[16384];
		LVSETINFOTIP lvsit;
		lvsit.cbSize = sizeof(lvsit);
		lvsit.dwFlags = 0;
		lvsit.iItem = item_num;
		lvsit.iSubItem = 0;
		mbstowcs(buf2, buf, ARRAY_SIZE(buf2));
		lvsit.pszText = buf2;
		ListView_SetInfoTip(lv->hDlgListView, &lvsit);
	}
	*/

	if (!lv->doingInitialBuild)
		UpdateWindow(lv->hDlgListView);

}

int listViewAddItem(ListView *lv, void *structptr)
{
	int item_num = lv->iNumItems++;
	int iSelected=-1;

	eaPush(lv->eaElements, structptr);
	eaiPush(&lv->eaForegroundColors, LISTVIEW_DEFAULT_COLOR);
	eaiPush(&lv->eaBackgroundColors, LISTVIEW_DEFAULT_COLOR);

	assert(lv->iNumItems == eaSize(lv->eaElements));

	// Determine old cursor location
	if (!lv->noScrollToEndOnAdd)
		iSelected = ListView_GetSelectionMark(lv->hDlgListView);

	listViewUpdateItem(lv, true, item_num);
	if (!lv->noScrollToEndOnAdd && (iSelected==item_num-1)) {
		// We previously had the last item selected
		ListView_SetSelectionMark(lv->hDlgListView, iSelected + 1);
		ListView_EnsureVisible(lv->hDlgListView, iSelected + 1, FALSE);
	}
	return item_num;
}


void listViewSetItemDefaultColor(ListView * lv, void * structptr)
{
	listViewSetItemColor(lv, 
						 structptr, 
						 LISTVIEW_DEFAULT_COLOR, 
						 LISTVIEW_DEFAULT_COLOR);
}

void listViewSetItemColor(ListView * lv, void * structptr, int foreground, int background)
{
	int index = listViewFindItem(lv, structptr);
	bool changed=false;
	if(index != -1)
	{
		assert(index >= 0 && index < eaiSize(&lv->eaForegroundColors));
		assert(index >= 0 && index < eaiSize(&lv->eaBackgroundColors));

		lv->colored = true;

		if (lv->eaForegroundColors[index] != foreground) {
			lv->eaForegroundColors[index] = foreground;
			changed = true;
		}
		if (lv->eaBackgroundColors[index] != background) {
			lv->eaBackgroundColors[index] = background;
			changed = true;
		}
		// Invalidate the control if a whole row's color changes
		if (changed) {
			InvalidateRect(lv->hDlgListView, NULL, FALSE);
		}
	}
}

// Removes an item from the list, returns the pointer to the struct removed (to be freed by caller)
void *listViewDelItem(ListView *lv, int index)
{
	LVFINDINFO lvfi;
	int item_num;
	void *ptr;

	if (index<0) {
		assert(!"Invalid item number being removed from listView");
		return NULL;
	}
	// Find the correct index for this item
	lvfi.flags = LVFI_PARAM;
	lvfi.lParam = index;
	item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
	assert(item_num!=-1);
	if (item_num>=0) {
		ListView_DeleteItem(lv->hDlgListView, item_num);
	}

	// Remove from EArray
	ptr = eaGet(lv->eaElements, index);
	eaSet(lv->eaElements, NULL, index);
	// Prune EArray
	while (eaSize(lv->eaElements)>0 && eaGet(lv->eaElements, eaSize(lv->eaElements)-1)==NULL) {
		eaRemove(lv->eaElements, eaSize(lv->eaElements)-1);
		eaiRemove(&lv->eaBackgroundColors, eaiSize(&lv->eaBackgroundColors)-1);
		eaiRemove(&lv->eaForegroundColors, eaiSize(&lv->eaForegroundColors)-1);
		lv->iNumItems = eaSize(lv->eaElements);
	}
	return ptr;
}

// Removes all items from the list
void listViewDelAllItems(ListView *lv, Destructor destructor)
{
	if (destructor) {
		eaClearEx(lv->eaElements, destructor);
	} else {
		eaDestroy(lv->eaElements);
	}
	eaiDestroy(&lv->eaBackgroundColors);
	eaiDestroy(&lv->eaForegroundColors);
	ListView_DeleteAllItems(lv->hDlgListView);
	lv->iNumItems=0;
}



// Comparator
static int CALLBACK listViewCompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	ListView *lv = (ListView*)lParamSort;
	ParseTable *tpi;
	void *structptr1, *structptr2;
	int ret;
	int mult = 1;
	int iSubItem = lv->iSubItem;
	ListViewCompareFuncHolder *pCompareFunc;
	if (iSubItem & 0x80000000) {
		iSubItem &= ~0x80000000;
		mult = -1;
	}
	structptr1 = eaGet(lv->eaElements, lParam1);
	structptr2 = eaGet(lv->eaElements, lParam2);

	pCompareFunc = FindCompareFuncHolder(lv, iSubItem);
	if (pCompareFunc && pCompareFunc->compare_func)
	{
		return pCompareFunc->compare_func(structptr1, structptr2) * mult;
	}

	getElement(lv->tpi, structptr1, iSubItem, lv->subItemMap, &tpi, &structptr1);
	getElement(lv->tpi, structptr2, iSubItem, lv->subItemMap, &tpi, &structptr2);
	ret = TokenCompare(tpi, 0, structptr1, structptr2, 0, 0); // HACK - should be pointing to base table here, but TokenCompare won't actually care
	return ret * mult;
}

void listViewSort(ListView *lv, int column)
{
	lv->iSubItem = column;
	ListView_SortItems(lv->hDlgListView, listViewCompare, lv);
}

void listViewReverseSort(ListView *lv, int column)
{
	lv->iSubItem = column;
	lv->iSubItem |= 0x80000000;
	ListView_SortItems(lv->hDlgListView, listViewCompare, lv);
}

static void doCallback(ListView *lv, ListViewCallbackFunc callback, int index)
{
	if (callback) {
		LVITEM lvi = {0};
		lvi.mask = LVIF_PARAM;
		lvi.iItem = index;
		if (ListView_GetItem(lv->hDlgListView, &lvi)) {
			callback(lv, lv->eaElements_data[lvi.lParam], NULL);
		}
	}
}

COLORREF darkenColor(COLORREF c0)
{
	COLORREF c = (c0==0xff000000)?GetSysColor(COLOR_WINDOW):c0;
	int r = GetRValue(c);
	int g = GetGValue(c);
	int b = GetBValue(c);
	r = MAX(r-16, 0);
	g = MAX(g-16, 0);
	b = MAX(b-6, 0);
	return RGB(r, g, b);
}

COLORREF lightenColor(COLORREF c0)
{
	COLORREF c = (c0==0xff000000)?GetSysColor(COLOR_WINDOWTEXT):c0;
	int r = GetRValue(c);
	int g = GetGValue(c);
	int b = GetBValue(c);
	r = MIN(r+128, 0xff);
	g = MIN(g+128, 0xff);
	b = MIN(b+128, 0xff);
	//b = 0xff;
	return RGB(r, g, b);
}

// Rainbow or gradient
COLORREF filterColor(COLORREF c0, int index)
{
	COLORREF c = (c0==0xff000000)?GetSysColor(COLOR_WINDOW):c0;
	int r = GetRValue(c);
	int g = GetGValue(c);
	int b = GetBValue(c);
	int d;
	d = ABS((index % 6) - 2)*48/3;
	r = MAX(r-d, 0);

	//d = ABS(((index+1) % 6) - 2)*48/3;
	g = MAX(g-d, 0);

	//d = ABS(((index+2) % 6) - 2)*48/3;
	b = MAX(b-d, 0);
	return RGB(r, g, b);
}


void listViewDefaultItemColorFunc(ListView *lv, void *structptr, int row, COLORREF *clrText, COLORREF *clrTextBk)
{
	int foreground;
	int background;

	assert(row >= 0 && row <= eaiSize(&lv->eaBackgroundColors));
	assert(eaiSize(&lv->eaForegroundColors) == eaiSize(&lv->eaBackgroundColors));

	assert(lv->eaForegroundColors && lv->eaBackgroundColors);
	foreground = lv->eaForegroundColors[row];
	background = lv->eaBackgroundColors[row];			

	// if default is specified, don't modify the color value

	if(foreground != LISTVIEW_DEFAULT_COLOR)
		*clrText = foreground;

	if(background != LISTVIEW_DEFAULT_COLOR)
		*clrTextBk = background;

	if (row %6>=3 && background == LISTVIEW_DEFAULT_COLOR) {
		*clrTextBk = darkenColor(*clrTextBk);
	}
}

LRESULT ProcessCustomDraw (ListView * lv, LPARAM lParam)
{
	LRESULT ret = 0;
	LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
	
	switch(lplvcd->nmcd.dwDrawStage) 
	{
	case CDDS_PREPAINT : //Before the paint cycle begins
		//request notifications for individual listview items
		return CDRF_NOTIFYITEMDRAW;

	case CDDS_ITEMPREPAINT: //Before an item is drawn
		{
			int row = lplvcd->nmcd.lItemlParam;

			if (lv->item_color_func)
			{
				lv->item_color_func(lv, eaGet(lv->eaElements, row), row, &lplvcd->clrText, &lplvcd->clrTextBk);
				ret |= CDRF_NEWFONT;
			}

			if (lv->sub_item_color_func)
			{
				ret |= CDRF_NOTIFYSUBITEMDRAW;
			}
		}
		break;

		//MJP: THIS CODE MAY BE USEFUL FOR COLORING COLUMNS OR SINGLE CELLS
		//Before a subitem is drawn
		case CDDS_SUBITEM | CDDS_ITEMPREPAINT: 
		{
			int row = lplvcd->nmcd.lItemlParam;
			int col = lplvcd->iSubItem;
			assert(lv->sub_item_color_func);
			//Note: setting for column i 
			//carries over to columnn i+1 unless
			//      it is explicitly reset
			lv->sub_item_color_func(lv, eaGet(lv->eaElements, row), row, col, &lplvcd->clrText, &lplvcd->clrTextBk);
			break;
		}
	}
	return ret?ret:CDRF_DODEFAULT;
}

BOOL listViewOnNotify(ListView *lv, WPARAM wParam, LPARAM lParam, ListViewCallbackFunc callback)
{
	static int last_sort_id=-1;
	static ListView *last_sort_listview=NULL;
	int idCtrl = (int)wParam;
	if(!lv) {
		return FALSE;
	}
	if (!lv->hDlgListView || lv->hDlgListView != GetDlgItem(lv->hDlgParent, idCtrl)) {
		// Not us!
		return FALSE;
	}
	switch (((LPNMHDR) lParam)->code)
	{
	case LVN_KEYDOWN:
		{
			if(callback)
			{
				LPNMLVKEYDOWN lpnmkey = (LPNMLVKEYDOWN)lParam;
				int index = ListView_GetSelectionMark(lv->hDlgListView);
				switch(lpnmkey->wVKey)
				{
				case VK_UP:
					{
						doCallback(lv, callback, max((index - 1), 0));
						break;
					}
				case VK_DOWN:
					{	
						int count = ListView_GetItemCount(lv->hDlgListView);
						doCallback(lv, callback, min((index + 1), (count - 1)));
						break;
					}
				}
			}
			break;
		}
	case LVN_COLUMNCLICK:
		if (lv->sortable) {
			NMLISTVIEW *pnmv = (LPNMLISTVIEW)lParam;
			lv->iSubItem = pnmv->iSubItem;
			if (lv == last_sort_listview && pnmv->iSubItem == last_sort_id) {
				lv->iSubItem |= 0x80000000;
				last_sort_id = -1;
			} else {
				last_sort_listview = lv;
				last_sort_id = pnmv->iSubItem;
			}
			ListView_SortItems(lv->hDlgListView, listViewCompare, lv);
			break;
		}
		break;
	case NM_CLICK:
		{
			LPNMITEMACTIVATE lpnmitem = (LPNMITEMACTIVATE) lParam;
			LVHITTESTINFO lvhti;
			LVITEM lvi;
			if (lpnmitem->iItem==-1)
			{
				// The click won't be handled properly
				//  This is sorta a hack so that we get some functionality when you click on other subitems, but not as good as it could be
				lvhti.flags = LVHT_ONITEM;
				lvhti.pt = lpnmitem->ptAction;
				ListView_SubItemHitTest(lv->hDlgListView, &lvhti);
				if (lvhti.iItem == -1) {
					// We don't care about the X-coord, since we're assuming this is in Details/Report view
					lvhti.flags = LVHT_ONITEM;
					lvhti.pt.x = 2;
					lvhti.iItem = ListView_HitTest(lv->hDlgListView, &lvhti);
				}
				lpnmitem->iItem = lvhti.iItem;
				if (lpnmitem->iItem!=-1) {
					lvi.iItem = lpnmitem->iItem;
					lvi.mask = LVIF_STATE;
					lvi.stateMask = LVIS_SELECTED;
					ListView_GetItem(lv->hDlgListView, &lvi);
					lvi.state = ~lvi.state;
					ListView_SetItem(lv->hDlgListView, &lvi);
					if(lvi.state & LVIS_SELECTED)
					{
						doCallback(lv, callback, lpnmitem->iItem);
					}
				}
			}
			else
			{
				if(callback)
				{
					int index = ListView_GetSelectionMark(lv->hDlgListView);
					if(index != -1) {
						doCallback(lv, callback, index);
					}
				}
			}
		}
		break;
	case NM_CUSTOMDRAW:
		{
			if(lv->colored)
			{
				SetWindowLongPtr(lv->hDlgParent, DWLP_MSGRESULT, (LONG_PTR)ProcessCustomDraw(lv, lParam));
				return TRUE;
			}
		}
		break;
	}

	return FALSE;
}








int listViewFindItem(ListView *lv, void *structptr)
{
	return eaFind(lv->eaElements, structptr);
}

void listViewItemChanged(ListView *lv, void *structptr)
{
	int index = listViewFindItem(lv, structptr);
	if (index==-1) {
		assert(!"listViewItemChanged called with an item that is not in the list!");
		return;
	}

	listViewUpdateItem(lv, false, index);
}

void *listViewGetItem(ListView *lv, int index)
{
	return eaGet(lv->eaElements, index);
}

int listViewGetNumItems(ListView *lv)
{
	return ListView_GetItemCount(lv->hDlgListView);
}

int listViewGetLargestItemIndex(ListView *lv)
{
	return eaSize(lv->eaElements);
}

HWND listViewGetParentWindow(ListView *lv)
{
	return lv->hDlgParent;
}

HWND listViewGetListViewWindow(ListView *lv)
{
	return lv->hDlgListView;
}

bool listViewIsSelected(ListView *lv, void *structptr)
{
	LVFINDINFO lvfi;
	LVITEM lvi;
	int item_num;
	int i = listViewFindItem(lv, structptr);
	if (i==-1)
		return false;

	lvfi.flags = LVFI_PARAM;
	lvfi.lParam = i;
	item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
	assert(item_num!=-1);
	if (item_num>=0) {
		lvi.iItem = item_num;
		lvi.mask = LVIF_STATE;
		lvi.stateMask = LVIS_SELECTED;
		ListView_GetItem(lv->hDlgListView, &lvi);
		if(lvi.state & LVIS_SELECTED)
			return true;
		else
			return false;
	}
	return false;
}

void listViewSetSelected(ListView *lv, void *structptr, bool bSelect)
{
	LVFINDINFO lvfi;
	int item_num;
	int i = listViewFindItem(lv, structptr);
	if (i==-1)
		return;

	// Find the correct index for this item
	lvfi.flags = LVFI_PARAM;
	lvfi.lParam = i;
	item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
	assert(item_num!=-1);
	if (item_num>=0) {
		ListView_SetItemState(lv->hDlgListView, item_num, (bSelect ? LVIS_SELECTED : 0), LVIS_SELECTED);
	}
}

void listViewSelectAll(ListView *lv, bool bSelect)
{
	int index=-1;
	while (-1!=(index=ListView_GetNextItem(lv->hDlgListView, index, LVNI_ALL))) {
		ListView_SetItemState(lv->hDlgListView, index, (bSelect ? LVIS_SELECTED : 0), LVIS_SELECTED);
	}
}

void listViewForEach(ListView * lv, ListViewCallbackFunc callback, void * data)
{
	int i, size = eaSize(lv->eaElements);

	for(i=0;i<size;i++)
	{
		callback(lv, lv->eaElements_data[i], data);
	}
}

int listViewDoOnSelected(ListView *lv, ListViewCallbackFunc callback, void *data)
{
	int index=-1;
	LVITEM lvi;
	int *eai = NULL;
	int i;

	if ( !lv->eaElements_data )
		return 0;

	while (-1!=(index=ListView_GetNextItem(lv->hDlgListView, index, LVNI_SELECTED))) {
		lvi.mask = LVIF_PARAM;
		lvi.iItem = index;
		if (!ListView_GetItem(lv->hDlgListView, &lvi)) {
			assert(0);
		}
		eaiPush(&eai, lvi.lParam);
		//printf("  %d (%d)\n", index, lvi.lParam);
	}
	//printf("%d selected items\n", eaiSize(&eai));
	for (i=0; i<eaiSize(&eai); i++) {
		if (callback) {
			callback(lv, lv->eaElements_data[eai[i]], data);
		}
	}
	eaiDestroy(&eai);
	return i;
}


int listViewCountSelected(ListView *lv)
{
	int index=-1;
	LVITEM lvi;
	int iRetVal = 0;

	if ( !lv->eaElements_data )
		return 0;

	while (-1!=(index=ListView_GetNextItem(lv->hDlgListView, index, LVNI_SELECTED))) {
		lvi.mask = LVIF_PARAM;
		lvi.iItem = index;
		if (!ListView_GetItem(lv->hDlgListView, &lvi)) {
			assert(0);
		}
		iRetVal++;
	}



	return iRetVal;


}

void listViewEnableColor(ListView * lv, bool enable)
{
	lv->colored = enable;
}

int listViewDoOnChecked(ListView * lv, ListViewCallbackFunc callback, void * userData)
{
	int index = -1;
	LVITEM lvi;
	int * eai = NULL;
	int i, count = 0;

	if(!lv->check_boxes || !eaSize(&lv->eaElements_data))
		return 0;

	while(-1 != (index = ListView_GetNextItem(lv->hDlgListView, index, LVNI_ALL)))
	{
		if(ListView_GetCheckState(lv->hDlgListView, index))
		{
			lvi.mask = LVIF_PARAM;
			lvi.iItem = index;
			if (!ListView_GetItem(lv->hDlgListView, &lvi))
			{
				assert(0);
			}
			eaiPush(&eai, lvi.lParam);
		}
	}

	count = eaiSize(&eai);
	if(callback)
	{
		for(i = 0; i < count; i++)
			callback(lv, lv->eaElements_data[eai[i]], userData);
	}
	eaiDestroy(&eai);
	return count;
}

bool listViewIsChecked(ListView * lv, void * structptr)
{
	LVFINDINFO lvfi;
	int item_num;
	int i;

	if(!lv->check_boxes)
		return false;

	i = listViewFindItem(lv, structptr);
	if (i == -1)
		return false;
	lvfi.flags = LVFI_PARAM;
	lvfi.lParam = i;
	item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
	assert(item_num != -1);
	if (item_num >= 0)
	{
		if(ListView_GetCheckState(lv->hDlgListView, item_num))
			return true;
		else
			return false;
	}
	return false;
}

void listViewSetChecked(ListView * lv, void * structptr, bool checked)
{
	int i;
	LVFINDINFO lvfi;
	int item_num;

	if(!lv->check_boxes)
		return;

	i = listViewFindItem(lv, structptr);
	if (i == -1)
		return;
	lvfi.flags = LVFI_PARAM;
	lvfi.lParam = i;
	item_num = ListView_FindItem(lv->hDlgListView, -1, &lvfi);
	assert(item_num != -1);
	if (item_num >= 0)
	{
		ListView_SetCheckState(lv->hDlgListView, item_num, (checked ? TRUE : FALSE));
	}
}

void listViewAutosizeColumns(ListView * lv)
{
	int i, j, width, item_width;
	LVCOLUMN col;
	S16 col_name[MAX_PATH], subitem_text[MAX_PATH];

	for(i = 0; i < lv->iNumColumns; i++)
	{
		memset(&col, 0, sizeof(LVCOLUMN));
		col.mask = LVCF_TEXT | LVCF_SUBITEM;
		col.pszText = col_name;
		col.cchTextMax = MAX_PATH;

		ListView_GetColumn(lv->hDlgListView, i, &col);
		width = ListView_GetStringWidth(lv->hDlgListView, col.pszText) + 12;

		for(j = 0; j < lv->iNumItems; j++)
		{
			ListView_GetItemText(lv->hDlgListView, j, col.iSubItem, subitem_text, MAX_PATH);
			item_width = ListView_GetStringWidth(lv->hDlgListView, subitem_text) + 12;
			if(col.iSubItem == 0 && lv->check_boxes)
				item_width += 16;
			if(item_width > width)
				width = item_width;
		}

		ListView_SetColumnWidth(lv->hDlgListView, i, width);
	}
}

ListViewCompareFuncHolder *FindCompareFuncHolder(ListView *lv, int iColumn)
{
	int i;

	for (i=0; i < eaSize(&lv->ppCompareFuncs); i++)
	{
		if (lv->ppCompareFuncs[i]->iColumn == iColumn)
		{
			return lv->ppCompareFuncs[i];
		}
	}

	return NULL;
}

typedef int (*ListViewColumnCompareFunc)(void *pStruct1, void *pStruct2);
void listViewSetColumnCompareFunc(ListView *lv, int iColumn, ListViewColumnCompareFunc compare_func)
{
	ListViewCompareFuncHolder *pFuncHolder = FindCompareFuncHolder(lv, iColumn);
	if (pFuncHolder)
	{
		pFuncHolder->compare_func = compare_func;
		return;
	}
	
	pFuncHolder = calloc(sizeof(ListViewCompareFuncHolder), 1);
	pFuncHolder->iColumn = iColumn;
	pFuncHolder->compare_func = compare_func;

	eaPush(&lv->ppCompareFuncs, pFuncHolder);
}
	


#endif
