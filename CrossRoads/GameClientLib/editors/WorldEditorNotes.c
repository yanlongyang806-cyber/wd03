#include "WorldEditorNotes.h"

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "GfxSprite.h"
#include "inputLib.h"
#include "GfxTexAtlas.h"
#include "WorldEditorClientMain.h"
#include "EditorPrefs.h"
#include "WorldEditorOptions.h"
#include "gimmeDLLWrapper.h"
#include "jpeg.h" 
#include "EditLibUIUtil.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define INDENT 60				//Indent to text box when there is a lable to the left
#define ZONE_STR_LEN 5			//strlen(".zone")
#define MIN_NOTE_SIZE 0.35
#define MAX_NOTE_SIZE 10.0
#define MIN_NOTE_FADE 0.25
#define MAX_NOTE_FADE 2.0

#define MAX_STICKY_NOTE_LEN 255

//Global data and UI elements for Notes System
static WleNotesUI WleGlobalNotesUI;

//For handeling strings
void stringCopySafeAllocation(char **dst, const char *src)
{
	if (*dst)
		StructFreeString(*dst);

	if (src)
		*dst = StructAllocString(src);
	else
		*dst = NULL;
}

void convertToSMFText(char *dest,const char *src, int max)
{
	int i;
	const char *srcPos;

	for(i = 0, srcPos = src; *srcPos != '\0' && i < max-4; i++, srcPos++)
	{
		if(*srcPos == '\n')
		{
			dest[i] = '\0';
			strcat_s(dest, max, "<br>");
			i+=3;
		}
		else
			dest[i] = *srcPos;
	}
	if(dest[i-1] == '>')
		i-=4;
	dest[i] = '\0';
	if(i == MAX_STICKY_NOTE_LEN-4)
	{
		strcat_s(dest, max, "...");
	}
}	

//Create a Note/Bookmark
wleNotesNote *wleNotes_NoteCreate(const char *name, const char *content, wleNoteType type)
{
	wleNotesNote *newNote = StructCreate(parse_wleNotesNote);
	wleNotes_NoteInitialize(newNote, name, content, type);
	return newNote;
}

//Init a Note/Bookmark
void wleNotes_NoteInitialize(wleNotesNote *note, const char *name, const char *content, wleNoteType type) 
{
	Vec4 vColor = {1.0,1.0,1.0,1.0};
	if(!note)
		return;

	stringCopySafeAllocation(&note->note_name,name);
	stringCopySafeAllocation(&note->note_content,content);
	if(type == STICKY_NOTE)
		stringCopySafeAllocation(&note->note_type,"Sticky Note");	
	else
		stringCopySafeAllocation(&note->note_type,"Bookmark");

	copyVec4(vColor,note->note_color);
	note->note_tex_id = 0;
	note->note_cam_dist = 0;
	copyVec3(zerovec3, note->note_cam_center);
	copyVec3(zerovec3, note->note_cam_pyr);
	note->tracker_handle=NULL;
}

//Destroy a note
void wleNotes_NoteDestory(wleNotesNote *note)
{
	//StructFreeString(note->note_name);
	//StructFreeString(note->note_content);
	//StructFreeString(note->note_type);
	StructDestroy(parse_wleNotesNote,note);
}

//Create an image for a bookmark
wleNoteGraphic *wleNotes_NoteGraphicCreate(const char *noteSetName, U32 tex_id)
{
	wleNoteGraphic *graphic = (wleNoteGraphic *)calloc(1, sizeof(wleNoteGraphic));
	wleNotes_NoteGraphicInitialize(graphic,noteSetName,tex_id);
	return graphic;
}

//Init a graphic
void wleNotes_NoteGraphicInitialize(wleNoteGraphic *graphic, const char *noteSetName, U32 tex_id)
{
	char fileName[MAX_PATH];
	if(!graphic)
		return;
	sprintf(fileName,"%s.%d.jpeg",noteSetName,tex_id);
	graphic->file_base.fullname = allocAddFilename(fileName);
	graphic->file_base.client_owned = true;
	graphic->texture = NULL;
	graphic->buffer = NULL;
	graphic->tex_id = tex_id;	
	graphic->height = NOTES_DRAW_PORT_HEIGHT_DEFAULT;
	graphic->width = NOTES_DRAW_PORT_WIDTH_DEFAULT;
	graphic->deleted = false;
}

//Destroy a graphic
void wleNotes_NoteGraphicDestory(wleNoteGraphic *graphic)
{	
	if(graphic->buffer)
		free(graphic->buffer);
	if(graphic->texture)
		texGenFreeNextFrame(graphic->texture);
	free(graphic);
}

//Search list for a new ID number
U32 wleNotes_NoteGraphicFindNewID(wleNoteGraphic **graphicList)
{
	int i;
	U32 highID = 0;

	for(i = 0; i < eaSize(&graphicList); i++)
		if(graphicList[i]->tex_id > highID)
			highID = graphicList[i]->tex_id;
	
	return ++highID;
}

//Load Notes
static int worldResetCount = 0;
void wleNotesLoad()
{
	wleNotesNoteSet *noteSet;
	wleNoteGraphic *graphic;
	void *fp;
	int fileSize,graphicSize;
	U8 *tempBuf;

	const char *layerName;
	int i,j,k;

	PERFINFO_AUTO_START_FUNC();

	worldResetCount = worldGetResetCount(false);

	WleGlobalNotesUI.data.doingSaveAs = false;
	WleGlobalNotesUI.data.selectedLayersNotes = NULL;
	wleNotesReloadLayers();

	//Remove old data
	if(eaSize(&WleGlobalNotesUI.data.noteSets))
	{
		eaDestroyEx(&WleGlobalNotesUI.data.noteSets,wleNotes_NoteSetDestory);
		WleGlobalNotesUI.data.noteSets = NULL;
		WleGlobalNotesUI.data.selectedLayersNotes = NULL;
	}
	if(eaSize(&WleGlobalNotesUI.data.graphics))
	{
		eaDestroyEx(&WleGlobalNotesUI.data.graphics,wleNotes_NoteGraphicDestory);
		WleGlobalNotesUI.data.graphics = NULL;
		WleGlobalNotesUI.data.viewingGraphic = NULL;
		WleGlobalNotesUI.data.isViewingGraphic = false;
	}


	//For each layer load the corasponding notes file
	for(i=0; i<eaSize(&WleGlobalNotesUI.data.layerNameListPath); i++)
	{
		layerName = eaGet(&WleGlobalNotesUI.data.layerNameListPath,i);

		if(layerName)
		{
			noteSet = wleNotes_NoteSetCreate(layerName,NULL);

			if(!worldFileLoad(&noteSet->file_base,parse_wleNotesNoteSet,noteSet,NULL,false,false))
				wleNotes_NoteSetInitialize(noteSet,layerName,NULL);
			eaPush(&WleGlobalNotesUI.data.noteSets,noteSet);

			//For each note check for a Graphic and load as needed
			for(j=0;j<eaSize(&noteSet->notes);j++)
			{
				//If an orphaned sticky note, put up a reminder
				if(noteSet->notes[j]->tracker_handle && strcmp(noteSet->notes[j]->tracker_handle,"Orphan") == 0)
					emStatusPrintf("Sticky Note with the name \"%s\" has been Orphaned.  Please re-attach.",noteSet->notes[j]->note_name);

				if(noteSet->notes[j]->note_tex_id)
				{
					graphic = wleNotes_NoteGraphicCreate(noteSet->file_base.fullname,noteSet->notes[j]->note_tex_id);
					
					fp = fileAlloc(graphic->file_base.fullname,&fileSize);
					if(fp)
					{
						jpegLoad(fp,fileSize,&tempBuf,&graphic->width,&graphic->height,&graphicSize);

						graphic->texture = texGenNew(graphic->width,graphic->height,"wleNoteImage",TEXGEN_NORMAL,WL_FOR_UI);					
						graphic->buffer = (U8*)calloc(graphic->width*graphic->height*4, sizeof(U8));

						for(k=0; k<graphic->width*graphic->height; k++)
						{
							*(graphic->buffer + k*4) = 255;
							*(graphic->buffer + k*4 + 1) = 255;
							*(graphic->buffer + k*4 + 2) = 255;
							*(graphic->buffer + k*4 + 3) = *(tempBuf + k*3) < 100 ? 0 : 255;
						}

						free(tempBuf);

						texGenUpdate(graphic->texture,graphic->buffer,RTEX_2D,RTEX_BGRA_U8,1,true,false,false,false);	
						eaPush(&WleGlobalNotesUI.data.graphics,graphic);

						worldFileSetSaved(&graphic->file_base);
					}
					else
					{
						Alertf("File not found: \"%s\"", graphic->file_base.fullname);
					}
				}
			}
		}
	}

	//If we loaded notes then set everything to the first layer in the list 
	if(WleGlobalNotesUI.data.noteSets)
	{
		WleGlobalNotesUI.data.selectedLayersNotes = *WleGlobalNotesUI.data.noteSets;
		WleGlobalNotesUI.noteList->peaModel = NULL;
		ui_ListSetModel(WleGlobalNotesUI.noteList,parse_wleNotesNote, &WleGlobalNotesUI.data.selectedLayersNotes->notes);
		ui_ComboBoxSetSelected(WleGlobalNotesUI.layerCombo,0);
		wleNotesSwitchLayers(WleGlobalNotesUI.layerCombo,NULL);		
	}
	else
	{
		WleGlobalNotesUI.noteList->peaModel = NULL;
	}

	PERFINFO_AUTO_STOP();
}

//Save Notes
void wleNotesSave()
{
	wleNotesSaveAs(NULL,NULL);
}

//Save Notes As
void wleNotesSaveAs(const char *dir, const char *fileName)
{
	char newFileName[MAX_PATH];
	char fullFileName[MAX_PATH];
	int i,j;
	wleNotesNoteSet *noteSet;
	wleNoteGraphic *graphic;
	TrackerHandle *stickNoteHandle=NULL;

	for(i=0;i<eaSize(&WleGlobalNotesUI.data.noteSets);i++)
		if(noteSet = eaGet(&WleGlobalNotesUI.data.noteSets,i))
		{
			if(dir&&fileName)
			{
				WleGlobalNotesUI.data.doingSaveAs = true;
				for(j=0;j<eaSize(&noteSet->notes);j++)
				{
					if(noteSet->notes[j]->tracker_handle)
					{
						stickNoteHandle = trackerHandleFromString(noteSet->notes[j]->tracker_handle);
						if(stickNoteHandle)
						{
							sprintf(newFileName,"%s/%s",dir,getFileNameConst(stickNoteHandle->layer_name));
							stringCopySafeAllocation(&stickNoteHandle->layer_name,newFileName);
							sprintf(newFileName,"%s/%s",dir,getFileNameConst(stickNoteHandle->zmap_name));
							stringCopySafeAllocation(&stickNoteHandle->zmap_name,newFileName);
							noteSet->notes[j]->tracker_handle = stringFromTrackerHandle(stickNoteHandle);
						}
					}
				}	
				sprintf(newFileName,"%s/%s",dir,getFileNameConst(noteSet->file_base.fullname));
				worldFileSaveAs(&noteSet->file_base,parse_wleNotesNoteSet,noteSet,newFileName,true);
			}
			else
				worldFileSave(&noteSet->file_base,parse_wleNotesNoteSet,noteSet);
		}

	if(dir&&fileName)
		wleNotesReloadLayers();

	for(i=0;i<eaSize(&WleGlobalNotesUI.data.graphics);i++)
	{
		if(WleGlobalNotesUI.data.graphics[i]->deleted)
		{
			graphic = eaRemove(&WleGlobalNotesUI.data.graphics,i);
			if(!(dir&&fileName))
			{
				worldFileModify(&graphic->file_base,false,true);
				fileForceRemove(graphic->file_base.fullname);
			}
			wleNotes_NoteGraphicDestory(graphic);
			i--;
			continue;
		}

		if(dir&&fileName)
		{
			sprintf(newFileName,"%s/%s",dir,getFileNameConst(WleGlobalNotesUI.data.graphics[i]->file_base.fullname));
			WleGlobalNotesUI.data.graphics[i]->file_base.fullname = allocAddFilename(newFileName);
			WleGlobalNotesUI.data.graphics[i]->file_base.unsaved = 1;
		}

		fileLocateWrite(WleGlobalNotesUI.data.graphics[i]->file_base.fullname,fullFileName);

		if(	WleGlobalNotesUI.data.graphics[i]->file_base.unsaved &&
			worldFileCanSave(&WleGlobalNotesUI.data.graphics[i]->file_base, NULL, !(dir&&fileName)))
		{
			U8 *tempBuff = (U8*)calloc(WleGlobalNotesUI.data.graphics[i]->width*WleGlobalNotesUI.data.graphics[i]->height*3, sizeof(U8));
			
			for(j=0; j<WleGlobalNotesUI.data.graphics[i]->width*WleGlobalNotesUI.data.graphics[i]->height; j++)
			{
				if(*(WleGlobalNotesUI.data.graphics[i]->buffer + j*4 + 3) > 125)
				{
					*(tempBuff + j*3) = *(tempBuff + j*3 + 1) = *(tempBuff + j*3 + 2) = 255;	
				}
				else
				{
					*(tempBuff + j*3) = *(tempBuff + j*3 + 1) = *(tempBuff + j*3 + 2) = 0;	
				}
			}
			
			jpgSave(fullFileName, tempBuff, 3, WleGlobalNotesUI.data.graphics[i]->width, WleGlobalNotesUI.data.graphics[i]->height, 95);
			worldFileSetSaved(&WleGlobalNotesUI.data.graphics[i]->file_base);
			free(tempBuff);
		}
	}

}

//Refresh Layer List Data
void wleNotesReloadLayers()
{
	ZoneMapLayer *layer;
	int i;

	//Deselect note
	ui_ListSetSelectedRow(WleGlobalNotesUI.noteList,-1);

	//Remove old data
	if(WleGlobalNotesUI.data.layerNameList)
	{
		eaClear(&WleGlobalNotesUI.data.layerNameList);
		WleGlobalNotesUI.data.layerNameList = NULL;
	}
	if(WleGlobalNotesUI.data.layerNameListPath)
	{
		eaClear(&WleGlobalNotesUI.data.layerNameListPath);
		WleGlobalNotesUI.data.layerNameListPath = NULL;
	}

	//Add Layers to list for combo box
	WleGlobalNotesUI.data.layerCount = zmapGetLayerCount(NULL);
	for(i = 0; i < WleGlobalNotesUI.data.layerCount; i++)
	{
		layer = zmapGetLayer(NULL,i);
		if(layer)
		{
			eaPush(&WleGlobalNotesUI.data.layerNameList,getFileNameConst(layerGetFilename(layer)));
			eaPush(&WleGlobalNotesUI.data.layerNameListPath,layerGetFilename(layer));	
		}
	}
}

//Called when map is changed
static void wleNotesMapChange(void *unused, bool bUnused)
{
	int newResetCount = worldGetResetCount(false);
	if(newResetCount != worldResetCount)
		wleNotesLoad();
}

//Called when a new layer is added to the map
void wleNotesCheckAddLayer()
{
	int i, j;
	int zmapCount = zmapGetLayerCount(NULL);
	bool update_needed = false;

	for(i = 0; i < zmapCount; i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL,i);
		if(layer)
		{
			bool found = false;
			for( j=0; j < eaSize(&WleGlobalNotesUI.data.layerNameListPath); j++ )
			{
				const char *layerName = WleGlobalNotesUI.data.layerNameListPath[j];
				if(layerName == layerGetFilename(layer))
				{
					found = true;
					break;
				}
			}
			if(!found)
			{
				update_needed = true;
				break;
			}
		}
	}

	if(!update_needed)
		return;

	wleNotesReloadLayers();

	for( i=0; i < eaSize(&WleGlobalNotesUI.data.layerNameListPath) ; i++ )
	{
		bool found = false;
		const char *layerName = WleGlobalNotesUI.data.layerNameListPath[i];
		for( j=0; j < eaSize(&WleGlobalNotesUI.data.noteSets) ; j++ )
		{
			char noteName[MAX_PATH];
			wleNotesNoteSet *noteSet = WleGlobalNotesUI.data.noteSets[j];
			strcpy_s(noteName,MAX_PATH,layerName);
			strcat_s(noteName,MAX_PATH,".notes");
			if(stricmp(noteSet->file_base.fullname, noteName) == 0)
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			wleNotesNoteSet *noteSet = wleNotes_NoteSetCreate(layerName,NULL);
			eaPush(&WleGlobalNotesUI.data.noteSets,noteSet);
		}
	}
}

//Create a set of Notes/Bookmarks
wleNotesNoteSet *wleNotes_NoteSetCreate(const char *layerName, wleNotesNote **notes)
{
	wleNotesNoteSet *newNoteSet = StructCreate(parse_wleNotesNoteSet);
	assert(newNoteSet);
	wleNotes_NoteSetInitialize(newNoteSet,layerName,notes);
	return newNoteSet;
}

//Init a set of Notes/Bookmarks
void wleNotes_NoteSetInitialize(wleNotesNoteSet *noteSet, const char *layerName, wleNotesNote **notes)
{
	char fileName[MAX_PATH];
	if(!noteSet)
		return;
	strcpy_s(fileName,MAX_PATH,layerName);
	strcat_s(fileName,MAX_PATH,".notes");
	noteSet->file_base.fullname = allocAddFilename(fileName);
	noteSet->notes = notes;
	noteSet->file_base.client_owned = true;
}

void wleNotes_NoteSetDestory(wleNotesNoteSet *noteSet)
{
	//eaDestroyEx(&noteSet->notes,wleNotes_NoteDestory);
	StructDestroy(parse_wleNotesNoteSet,noteSet);
}

int wleNotesScreenXtoGraphicX(wleNoteGraphic *graphic, int x, int windowWidth, int windowHeight)
{
	return (int)((float)(x-(((float)windowWidth - (graphic->width*((float)windowHeight)/graphic->height))/2))*graphic->height/((float)windowHeight));
}
int wleNotesScreenYtoGraphicY(wleNoteGraphic *graphic, int y, int windowHeight)
{
	return (int)((float)y*graphic->height/((float)windowHeight));
}

void wleNotesDrawDot(wleNoteGraphic *graphic, int x, int y)
{
	int i,j;

	if(x<0 || y<0 || x >= graphic->width || y >= graphic->height)
		return;

	if(WleGlobalNotesUI.data.erasing)
	{
		for(i = (x<(ERASER_SIZE/2)?0:x-(ERASER_SIZE/2)) ; i < (x>(graphic->width - (ERASER_SIZE/2))?(graphic->width - (ERASER_SIZE/2)):x+(ERASER_SIZE/2))  ; i++)		
			for(j = (y<(ERASER_SIZE/2)?0:y-(ERASER_SIZE/2)) ; j < (y>(graphic->height - (ERASER_SIZE/2))?(graphic->height - (ERASER_SIZE/2)):y+(ERASER_SIZE/2))  ; j++)
			*(graphic->buffer+(j*graphic->width+i)*4+3) = 0;
	}
	else
		*(graphic->buffer+(y*graphic->width+x)*4+3) = 255;
}

void wleNotesDrawLine(wleNoteGraphic *graphic, int x0, int y0, int x1, int y1)
{
	int deltax,deltay;
	int error;
	int ystep;
	int x,y;
	bool steep = abs(y1 - y0) > abs(x1 - x0); 
	if(steep)
	{
         swap(&x0, &y0,sizeof(int));
         swap(&x1, &y1,sizeof(int));
	}
	if(x0 > x1)
	{
         swap(&x0, &x1,sizeof(int));
         swap(&y0, &y1,sizeof(int));	
	}
	deltax = x1 - x0;
	deltay = abs(y1 - y0);
	error = -deltax / 2;
	y = y0;
	if (y0 < y1)
		ystep = 1;
	else 
		ystep = -1;

	for(x = x0; x <= x1; x++)
	{
		if(steep)
			wleNotesDrawDot(graphic,y,x);
		else 
			wleNotesDrawDot(graphic,x,y);
		error += deltay;
		if (error > 0)
		{	
			y += ystep;
			error -= deltax;
		}
	}
}

//Init Window and Attach UI Elements
void wleNotesCreate(EMEditorDoc *doc)
{
	//Pointers used tempararily for creation
	UIListColumn *column;	
	UIButton *button;
	UILabel *label;
	UICheckButton *check;
	Vec4 vStart = {1.0f, 1.0f, 1.0f, 1.0f};
	int temp;

	PERFINFO_AUTO_START_FUNC();

	//Init Global Data
	WleGlobalNotesUI.data.layerNameListPath = NULL;
	WleGlobalNotesUI.data.layerNameList = NULL;
	WleGlobalNotesUI.data.noteSets = NULL;
	WleGlobalNotesUI.data.selectedLayersNotes = NULL;
	WleGlobalNotesUI.data.isViewingGraphic = false;
	WleGlobalNotesUI.data.drawing = false;
	WleGlobalNotesUI.data.doingSaveAs = false;
	WleGlobalNotesUI.data.isViewingSticky = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ViewStickyNotes", 0);
	WleGlobalNotesUI.stickNoteImage = atlasLoadTexture("eui_note");

	//Set save and refesh function for map changes
	emAddMapChangeCallback(wleNotesMapChange,NULL);

	//Create window and add to the list.
	WleGlobalNotesUI.panel = emPanelCreate("Map", "Bookmarks & Notes", 400);
	eaPush(&doc->em_panels, WleGlobalNotesUI.panel);
	emPanelSetOpened(WleGlobalNotesUI.panel, true);

	//Add combo box for selecting the correct layer.
	WleGlobalNotesUI.layerCombo = ui_ComboBoxCreate(0, 0, 1, NULL, &WleGlobalNotesUI.data.layerNameList, NULL);
	WleGlobalNotesUI.layerCombo->widget.offsetFrom = UITopLeft;
	WleGlobalNotesUI.layerCombo->widget.widthUnit = UIUnitPercentage;
	WleGlobalNotesUI.layerCombo->selectedF = wleNotesSwitchLayers;
	emPanelAddChild(WleGlobalNotesUI.panel, WleGlobalNotesUI.layerCombo, false);

	//Add check box to select wether or not to view Sticky Notes
	check = ui_CheckButtonCreate(0, elUINextY(WleGlobalNotesUI.layerCombo),"View Sticky Notes",false);
	check->statePtr = &WleGlobalNotesUI.data.isViewingSticky;
	ui_CheckButtonSetToggledCallback(check, wleNotesSwitchStickyDisplayStatus, NULL);
	emPanelAddChild(WleGlobalNotesUI.panel, check, false);

	//Add editable entries for selected Bookmark or Note	
	label = ui_LabelCreate("Name:", 0, elUINextY(check));
	emPanelAddChild(WleGlobalNotesUI.panel, label, false);
	temp = elUINextX(label) + 5;
	WleGlobalNotesUI.nameEntry = ui_TextEntryCreate("", temp, label->widget.y);
	ui_TextEntrySetFinishedCallback(WleGlobalNotesUI.nameEntry, wleNotesNameChanged, NULL);
	WleGlobalNotesUI.nameEntry->widget.width = 125;
	ui_SetActive((UIWidget*) WleGlobalNotesUI.nameEntry, false);
	emPanelAddChild(WleGlobalNotesUI.panel, WleGlobalNotesUI.nameEntry, false);

	label = ui_LabelCreate("Color:", elUINextX(WleGlobalNotesUI.nameEntry) + 10, label->widget.y);
	emPanelAddChild(WleGlobalNotesUI.panel, label, false);
	WleGlobalNotesUI.graphicColor = ui_ColorButtonCreateEx(0, label->widget.y, 0, 15, vStart);
	WleGlobalNotesUI.graphicColor->noRGB = 1;
	WleGlobalNotesUI.graphicColor->widget.width = 1;
	WleGlobalNotesUI.graphicColor->widget.widthUnit = UIUnitPercentage;
	WleGlobalNotesUI.graphicColor->widget.leftPad = elUINextX(label) + 5;
	ui_ColorButtonSetChangedCallback(WleGlobalNotesUI.graphicColor, wleNotesColorChanged, NULL);
	emPanelAddChild(WleGlobalNotesUI.panel, WleGlobalNotesUI.graphicColor, false);
	ui_SetActive((UIWidget*) WleGlobalNotesUI.graphicColor, false);

	label = ui_LabelCreate("Note:", 0, elUINextY(WleGlobalNotesUI.nameEntry) + 5);
	WleGlobalNotesUI.noteLabel = label;
	emPanelAddChild(WleGlobalNotesUI.panel, label, false);
	WleGlobalNotesUI.noteTextArea = ui_TextAreaCreate("");
	WleGlobalNotesUI.noteTextArea->widget.unfocusF = wleNotesNoteTextChanged;
	//ui_TextAreaSetChangedCallback(WleGlobalNotesUI.noteTextArea, wleNotesNoteTextChanged, NULL);
	ui_WidgetSetPosition(UI_WIDGET(WleGlobalNotesUI.noteTextArea), 0, label->widget.y);
	ui_WidgetSetDimensionsEx((UIWidget*) WleGlobalNotesUI.noteTextArea, 1, 100, UIUnitPercentage, UIUnitFixed);
	WleGlobalNotesUI.noteTextArea->widget.leftPad = temp;
	ui_SetActive((UIWidget*) WleGlobalNotesUI.noteTextArea, false);
	emPanelAddChild(WleGlobalNotesUI.panel, WleGlobalNotesUI.noteTextArea, false);

	//Add buttons for adding new items
	button = ui_ButtonCreate("New Bookmark", 0, 0, wleNotesNewBookmark, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.rightPad = 3;
	button->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(WleGlobalNotesUI.panel, button, false);

	button = ui_ButtonCreate("New Sticky Note", 0, 0, wleNotesNewStickyNote, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.5;
	button->widget.leftPad = 3;
	button->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(WleGlobalNotesUI.panel, button, false);

	//Add a list for displaying all notes
	WleGlobalNotesUI.noteList = ui_ListCreate(parse_wleNotesNote, &WleGlobalNotesUI.data.noteSets, 15);//kinda wierd code: WleGlobalNotesUI.data.noteSets is a NULL double pointer
	column = ui_ListColumnCreateCallback("", wleNotesDisplayHasGraphic, NULL);
	column->fWidth = 20;
	ui_ListAppendColumn(WleGlobalNotesUI.noteList, column);
	column = ui_ListColumnCreate(UIListPTName, "Name", (intptr_t) "note_name", NULL);
	column->fWidth = 175;
	ui_ListAppendColumn(WleGlobalNotesUI.noteList, column);
	column = ui_ListColumnCreate(UIListPTName, "User", (intptr_t) "user_name", NULL);
	column->fWidth = 75;
	ui_ListAppendColumn(WleGlobalNotesUI.noteList, column);
	ui_ListSetSelectedCallback(WleGlobalNotesUI.noteList, wleNotesSelectNote, NULL);
	ui_ListSetActivatedCallback(WleGlobalNotesUI.noteList, wleNotesGoToNote, NULL);
	ui_ListSetContextCallback(WleGlobalNotesUI.noteList, wleNotesRightClickNote, NULL);
	ui_WidgetSetDimensionsEx((UIWidget*) WleGlobalNotesUI.noteList, 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx((UIWidget*) WleGlobalNotesUI.noteList, 0, 0, elUINextY(WleGlobalNotesUI.noteTextArea) + 5, elUINextY(button) + 5);
	emPanelAddChild(WleGlobalNotesUI.panel, WleGlobalNotesUI.noteList, false);

	//Load Note Data
	wleNotesLoad();

	PERFINFO_AUTO_STOP();
}

//Called every frame 
void wleNotesTick(void)
{
	wleNoteGraphic *graphic = WleGlobalNotesUI.data.viewingGraphic;
	GfxCameraController *camera = gfxGetActiveCameraController();
	GfxCameraView *cameraView = gfxGetActiveCameraView();
	GroupTracker *stickNoteTracker = NULL;
	static int xLast,yLast;
	static bool toolTipDisplayed;
	int x, y, z, i;
	U32 colorVal=0;
	int windowWidth,windowHeight;
	Vec3 position, camPosition;
	Vec2 screenPosition;
	Vec3 cameraSpacePosition;
	F32 scale, camDistance;	
	Mat4 avg;
	char SMFText[MAX_STICKY_NOTE_LEN];

	//Check to see if the layers have changed
	wleNotesCheckAddLayer();

	wleUpdateGraphicWait(NULL,false);

	if(	WleGlobalNotesUI.data.doingSaveAs == true )
		return;

	if(	!sameVec3(WleGlobalNotesUI.data.viewingGraphic_cam_center,camera->camcenter) ||
		!sameVec3(WleGlobalNotesUI.data.viewingGraphic_cam_pyr,camera->campyr) ||
		WleGlobalNotesUI.data.viewingGraphic_cam_dist != camera->camdist)
	{
		WleGlobalNotesUI.data.isViewingGraphic = false;
	}

	toolTipDisplayed = false;
	if(WleGlobalNotesUI.data.isViewingGraphic == false &&WleGlobalNotesUI.data.isViewingSticky == true && WleGlobalNotesUI.data.selectedLayersNotes)
		for(i=0;i<eaSize(&WleGlobalNotesUI.data.selectedLayersNotes->notes);i++)
		{
			if(	strcmp(WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_type,"Sticky Note")==0 &&
				WleGlobalNotesUI.stickNoteImage)
			{
				stickNoteTracker = trackerFromHandleString(WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->tracker_handle);
		
				if(stickNoteTracker)
				{
					trackerGetMat(stickNoteTracker,avg);
					
					mulVecMat4(stickNoteTracker->def->bounds.mid, avg, position);
					camDistance = stickNoteTracker->def->bounds.radius*1.5f;
					if(	!sameVec3(WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_center,position) || 
						WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_dist != camDistance)
					{
						copyVec3(position,WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_center);
						WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_dist = camDistance;					
						worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
						wleNotesSetWorldUnsaved();
					}
				}
				else
				{
					if(	!WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->tracker_handle || 
						strcmp(WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->tracker_handle,"Orphan") != 0)
					{
						stringCopySafeAllocation(&WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->tracker_handle, "Orphan");
						emStatusPrintf("Sticky Note with the name \"%s\" has been Orphaned.  Please re-attach.",WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_name);
						worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
						wleNotesSetWorldUnsaved();
					}
					continue;
				}


				x = WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_center[0];
				y = WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_center[1] + (WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_dist / 5);
				z = WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_cam_center[2];
				
				setVec3(position,x,y,z);
				mulVecMat4(position, cameraView->frustum.viewmat, cameraSpacePosition);

				gfxGetActiveCameraPos(camPosition);

				camDistance = distance3(position,camPosition);
				camDistance = camDistance != 0 ? camDistance : 0.1;
				scale = CLAMPF32(200/camDistance,MIN_NOTE_SIZE,MAX_NOTE_SIZE);

				if(scale > MIN_NOTE_SIZE && scale < MAX_NOTE_SIZE && frustumCheckSphere(&cameraView->frustum,cameraSpacePosition,0.1))
				{
					editLibGetScreenPos(position,screenPosition);
				
					mousePos(&x,&y);

					if(	!toolTipDisplayed		&&
						x > screenPosition[0]	&& 
						x < screenPosition[0] + 1.25*scale*WleGlobalNotesUI.stickNoteImage->width && 
						y > screenPosition[1]	&& 
						y < screenPosition[1] + scale*WleGlobalNotesUI.stickNoteImage->height )
					{
						toolTipDisplayed = true;
						convertToSMFText(SMFText,WleGlobalNotesUI.data.selectedLayersNotes->notes[i]->note_content,MAX_STICKY_NOTE_LEN);
						ui_WidgetSetTooltipString(UI_WIDGET(WleGlobalNotesUI.noteLabel),SMFText);
						ui_TooltipsSetActive(UI_WIDGET(WleGlobalNotesUI.noteLabel),screenPosition[0],screenPosition[1]);				
					}
		
					colorVal = 0xFFFFFF00 | ((scale > MIN_NOTE_SIZE+MIN_NOTE_FADE && scale < MAX_NOTE_SIZE-MAX_NOTE_FADE) ? 0x000000FF :  ((scale <= MIN_NOTE_SIZE+MIN_NOTE_FADE) ?  (unsigned int)((scale-MIN_NOTE_SIZE)*(250/MIN_NOTE_FADE)) : (unsigned int)(-(scale-MAX_NOTE_SIZE)*(250/MAX_NOTE_FADE)))  );
					display_sprite(WleGlobalNotesUI.stickNoteImage, screenPosition[0], screenPosition[1], -2, scale, scale, colorVal);				
				}
			}
		}

	if(!WleGlobalNotesUI.data.isViewingGraphic || !WleGlobalNotesUI.data.viewingGraphic)
	{		
		wleFinishedDrawing();
		return;
	}

	gfxGetActiveDeviceSize(&windowWidth,&windowHeight);
	graphic = WleGlobalNotesUI.data.viewingGraphic;
	WleGlobalNotesUI.data.erasing = inpLevelPeek(INP_CONTROL);
	if (mouseIsDown(MS_LEFT) && !inpCheckHandled())
	{
		mousePos(&x,&y);
		if(!WleGlobalNotesUI.data.drawing)
		{	
			WleGlobalNotesUI.data.drawing = true;
			xLast = wleNotesScreenXtoGraphicX(graphic,x,windowWidth,windowHeight);
			yLast = wleNotesScreenYtoGraphicY(graphic,y,windowHeight);
		}

		x = wleNotesScreenXtoGraphicX(graphic,x,windowWidth,windowHeight);
		y = wleNotesScreenYtoGraphicY(graphic,y,windowHeight);
		wleNotesDrawLine(graphic,xLast,yLast,x,y);
		texGenUpdate(graphic->texture,graphic->buffer,RTEX_2D,RTEX_BGRA_U8,1,true,false,false,false);
		xLast = x;
		yLast = y;
		inpHandled();
	}
	else
		wleFinishedDrawing();

	if (mouseClick(MS_LEFT))
		inpHandled();//Stop selections from occuring

	colorVal = (U32)(WleGlobalNotesUI.data.viewingColor[0]*255); 
	colorVal = (colorVal<<8) + ((U32)(WleGlobalNotesUI.data.viewingColor[1]*255));
	colorVal = (colorVal<<8) + ((U32)(WleGlobalNotesUI.data.viewingColor[2]*255));
	colorVal = (colorVal<<8) + ((U32)(WleGlobalNotesUI.data.viewingColor[3]*255));
	display_sprite_tex(graphic->texture, ((float)windowWidth - (graphic->width*((float)windowHeight)/graphic->height))/2, 0, -2, ((float)windowHeight)/graphic->height, ((float)windowHeight)/graphic->height,colorVal); // 0x0000ffff);
}

void wleUpdateGraphicWait(WorldFile *file_base, bool force)
{
	static U32 lastUpdate = 0;
	static WorldFile *file = NULL;
	
	if(!file && !file_base)
		return;
	
	if(file_base && !file)
		file = file_base;

	if(timerCpuMs() - lastUpdate > 1000 || force)
	{
		lastUpdate = timerCpuMs();
		if(file)
		{	
			worldFileModify(file,false,true);
			wleNotesSetWorldUnsaved();
			file = NULL;
		}
	}
}

//Called when a change is made that should be saved
void wleNotesSetWorldUnsaved()
{
	EMEditorDoc *worldDoc = wleGetWorldEditorDoc();
	if (worldDoc)
		worldDoc->saved = 0;
}

void wleFinishedDrawing()
{
	if(WleGlobalNotesUI.data.viewingGraphic && WleGlobalNotesUI.data.drawing == true)
	{
		wleUpdateGraphicWait(&WleGlobalNotesUI.data.viewingGraphic->file_base, false);
	}
	WleGlobalNotesUI.data.drawing = false;
}

//Called when selected layer is changed using the layer combo 
static void wleNotesSwitchLayers(UIComboBox *combo, void *unused2)
{
	wleNotesNoteSet *noteSet;
	int comboSelected;

	comboSelected = ui_ComboBoxGetSelected(combo);
	if(noteSet = eaGet(&WleGlobalNotesUI.data.noteSets,comboSelected))
	{
		WleGlobalNotesUI.data.selectedLayersNotes = noteSet;
		ui_ListSetModel(WleGlobalNotesUI.noteList,parse_wleNotesNote, &WleGlobalNotesUI.data.selectedLayersNotes->notes);
	}

	ui_ListClearSelected(WleGlobalNotesUI.noteList);
	wleNotesNoSelection();
}

//Called when Display Sticky Notes check box is clicked
static void wleNotesSwitchStickyDisplayStatus(UICheckButton *check, void *unused)
{
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_UI, "ViewStickyNotes", WleGlobalNotesUI.data.isViewingSticky);
}

//Called when a "Name" text box is changed
static void wleNotesNameChanged(UITextEntry *entry, void *unused)
{
	if(WleGlobalNotesUI.data.selectedLayersNotes) {
		wleNotesNote *note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,ui_ListGetSelectedRow(WleGlobalNotesUI.noteList));
		if(note) {
			stringCopySafeAllocation(&note->note_name,ui_TextEntryGetText(entry));
			worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
			wleNotesSetWorldUnsaved();
		}
	}
}


//Called when a color slider is changed
static void wleNotesColorChanged(UIColorButton *colorButton, bool finished, void *unused)
{
	wleNotesNote *note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,ui_ListGetSelectedRow(WleGlobalNotesUI.noteList));
	
	if(note)
	{
		ui_ColorButtonGetColor(colorButton,note->note_color);
		worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
		wleNotesSetWorldUnsaved();
		wleNotesGoToNote(WleGlobalNotesUI.noteList,NULL);
	}
}

//Called when "Note" text area is changed
static void wleNotesNoteTextChanged(UITextArea *textArea, UIAnyWidget *focusitem)
{
	wleNotesNote *note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,ui_ListGetSelectedRow(WleGlobalNotesUI.noteList));
	
	if(note)
	{
		stringCopySafeAllocation(&note->note_content,ui_TextAreaGetText(textArea));
		worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
		wleNotesSetWorldUnsaved();
	}
}

//Called when a note is selected from the list
static void wleNotesSelectNote(UIList *list, void *unused)
{
	wleNotesNote *note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,ui_ListGetSelectedRow(list));

	if(note)
	{
		wleCheckActiveButtons();

		ui_SetActive((UIWidget*) WleGlobalNotesUI.nameEntry, true);
		ui_SetActive((UIWidget*) WleGlobalNotesUI.noteTextArea, true);
		ui_TextEntrySetText(WleGlobalNotesUI.nameEntry,note->note_name);
		ui_TextAreaSetText(WleGlobalNotesUI.noteTextArea,note->note_content);
		ui_ColorButtonSetColor(WleGlobalNotesUI.graphicColor, note->note_color);	
	}
	else
	{
		wleNotesNoSelection();
	}
}

//Called when a note is double clicked form the list
static void wleNotesGoToNote(UIList *list, void *unused)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	int selectedRow = ui_ListGetSelectedRow(list);
	wleNotesNote *note;
	int i;

	if(selectedRow < 0)
		return;

	note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,selectedRow);

	wleUpdateGraphicWait(NULL,true);
	wleCheckActiveButtons();

	WleGlobalNotesUI.data.isViewingGraphic = false;
	if(note && camera)
	{
		if(strcmp(note->note_type,"Bookmark")==0)
		{
			if(note->note_tex_id)
			{
				for(i = 0; i < eaSize(&WleGlobalNotesUI.data.graphics);	i++)
				{
					if(WleGlobalNotesUI.data.graphics[i]->tex_id == note->note_tex_id)
					{
						WleGlobalNotesUI.data.viewingGraphic = WleGlobalNotesUI.data.graphics[i];
						WleGlobalNotesUI.data.isViewingGraphic = true;
						break;				
					}
				}
			}
			copyVec4(note->note_color,WleGlobalNotesUI.data.viewingColor);

			//Set PYR only if bookmark	
			copyVec3(note->note_cam_pyr,camera->campyr);
		}

		//Set the camera
		copyVec3(note->note_cam_center,camera->camcenter);
		camera->camdist = note->note_cam_dist;
		//Save this postion
		copyVec3(note->note_cam_center,WleGlobalNotesUI.data.viewingGraphic_cam_center);
		copyVec3(note->note_cam_pyr,WleGlobalNotesUI.data.viewingGraphic_cam_pyr);
		WleGlobalNotesUI.data.viewingGraphic_cam_dist = note->note_cam_dist;
	}
}

//Called when a note list is rightclicked
static void wleNotesRightClickNote(UIList *list, void *unused)
{
	int selectedRow = ui_ListGetSelectedRow(list);

	wleCheckActiveButtons();

	if (WleGlobalNotesUI.listRightClick)
		ui_MenuClear(WleGlobalNotesUI.listRightClick);
	else
		WleGlobalNotesUI.listRightClick = ui_MenuCreate("");

	if(selectedRow > -1)
	{
		if(	WleGlobalNotesUI.data.isViewingGraphic &&
			WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_tex_id == WleGlobalNotesUI.data.viewingGraphic->tex_id)
		{
			ui_MenuAppendItem(WleGlobalNotesUI.listRightClick, ui_MenuItemCreate("Erase All", UIMenuCallback, wleNotesClearGraphicBits, NULL, NULL));
		}
		else if(strcmp(WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_type,"Bookmark")==0 &&
				WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_tex_id == 0)
		{
			ui_MenuAppendItem(WleGlobalNotesUI.listRightClick, ui_MenuItemCreate("Add Graphic", UIMenuCallback, wleNotesAddGraphic, NULL, NULL));
		}

		if(strcmp(WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_type,"Sticky Note")==0)
		{	
			ui_MenuAppendItem(WleGlobalNotesUI.listRightClick, ui_MenuItemCreate("Attach To", UIMenuCallback, wleNotesAttachStickyNote, NULL, NULL));
		}

		ui_MenuAppendItem(WleGlobalNotesUI.listRightClick, ui_MenuItemCreate("Delete", UIMenuCallback, wleNotesDeleteNote, NULL, NULL));

		if (eaSize(&WleGlobalNotesUI.listRightClick->items))
		{
			WleGlobalNotesUI.listRightClick->widget.scale = emGetSidebarScale() / g_ui_State.scale;
			ui_MenuPopupAtCursor(WleGlobalNotesUI.listRightClick);
		}
	}
}

//Called when a "New Bookmark" is clicked
static void wleNotesNewBookmark(void *unused1, void *unused2)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	wleNotesNote *note;

	if(WleGlobalNotesUI.data.selectedLayersNotes)
	{
		note = wleNotes_NoteCreate("New Note","Insert note here.",BOOKMARK);

		copyVec3(camera->camcenter,note->note_cam_center);
		copyVec3(camera->campyr,note->note_cam_pyr);
		note->note_cam_dist = camera->camdist;
		stringCopySafeAllocation(&note->user_name, gimmeDLLQueryUserName());

		eaPush(&WleGlobalNotesUI.data.selectedLayersNotes->notes,note);
		worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);		
		wleNotesSetWorldUnsaved();
	}
}

//Called when a "New Sticky Note" is clicked
static void wleNotesNewStickyNote(void *unused1, void *unused2)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	wleNotesNote *note;
	TrackerHandle **selection = NULL;
	GroupTracker *selectedTracker;
	Mat4 avg;

	if(WleGlobalNotesUI.data.selectedLayersNotes)
	{
		if (wleSelectionGetCount() == 1)
		{
			wleSelectionGetTrackerHandles(&selection);
			selectedTracker = trackerFromTrackerHandle(selection[0]);
			if(selectedTracker  && strncmp(selection[0]->layer_name,WleGlobalNotesUI.data.selectedLayersNotes->file_base.fullname,strlen(selection[0]->layer_name))==0)
			{
				note = wleNotes_NoteCreate("New Note","Insert note here.",STICKY_NOTE);

				trackerGetMat(selectedTracker,avg);
				mulVecMat4(selectedTracker->def->bounds.mid, avg, note->note_cam_center);
				//setVec3(note->note_cam_pyr,0.5,-0.5,0);
				note->note_cam_dist = selectedTracker->def->bounds.radius*1.5f;
				note->tracker_handle=stringFromTrackerHandle(selection[0]);
				eaDestroy(&selection);
				stringCopySafeAllocation(&note->user_name, gimmeDLLQueryUserName());

				eaPush(&WleGlobalNotesUI.data.selectedLayersNotes->notes,note);
				worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
				wleNotesSetWorldUnsaved();
			}
			else
				emStatusPrintf("Object on a different layer or selectedTracker failed");
		}
		else
		{
			if(wleSelectionGetCount() > 1)
			{
				emStatusPrintf("Too Many Objects Selected");
			}
			else 
			{
				emStatusPrintf("No Object Selected"); 
			}
		}
	}
}


//Called when a rightclick attach is called 
static void wleNotesAttachStickyNote(void *unused1, void *unused2)
{
	GfxCameraController *camera = gfxGetActiveCameraController();
	int selectedRow = ui_ListGetSelectedRow(WleGlobalNotesUI.noteList);
	wleNotesNote *note;
	TrackerHandle **selection = NULL;
	GroupTracker *selectedTracker;
	Mat4 avg;

	if(selectedRow > -1 && WleGlobalNotesUI.data.selectedLayersNotes)
	{
		if (wleSelectionGetCount() == 1)
		{
			wleSelectionGetTrackerHandles(&selection);
			selectedTracker = trackerFromTrackerHandle(selection[0]);
			if(selectedTracker  && strncmp(selection[0]->layer_name,WleGlobalNotesUI.data.selectedLayersNotes->file_base.fullname,strlen(selection[0]->layer_name))==0)
			{
				note = WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow];//wleNotes_NoteCreate("New Note","Insert note here.",STICKY_NOTE);

				trackerGetMat(selectedTracker,avg);
				mulVecMat4(selectedTracker->def->bounds.mid, avg, note->note_cam_center);
				//setVec3(note->note_cam_pyr,0.5,-0.5,0);
				note->note_cam_dist = selectedTracker->def->bounds.radius*1.5f;
				note->tracker_handle=stringFromTrackerHandle(selection[0]);
				eaDestroy(&selection);

				//eaPush(&WleGlobalNotesUI.data.selectedLayersNotes->notes,note);
				worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
				wleNotesSetWorldUnsaved();
			}
			else
				emStatusPrintf("Object on a different layer or selectedTracker failed");
		}
		else
		{
			if(wleSelectionGetCount() > 1)
			{
				emStatusPrintf("Too Many Objects Selected");
			}
			else 
			{
				emStatusPrintf("No Object Selected"); 
			}
		}
	}
}

//Called when a rightclick delete is called
static void wleNotesDeleteNote(void *unused1, void *unused2)
{
	wleNotesNote *note;
	int selectedRow = ui_ListGetSelectedRow(WleGlobalNotesUI.noteList);
	int i;

	wleUpdateGraphicWait(NULL,true);

	if(selectedRow > -1)
	{
		note = eaRemove(&WleGlobalNotesUI.data.selectedLayersNotes->notes,selectedRow);
		
		if(note->note_tex_id!=0)
		{
			for(i = 0; i < eaSize(&WleGlobalNotesUI.data.graphics);	i++)
			{
				if(WleGlobalNotesUI.data.graphics[i]->tex_id == note->note_tex_id)
				{
					WleGlobalNotesUI.data.graphics[i]->deleted = true;
					WleGlobalNotesUI.data.isViewingGraphic = false;
					break;				
				}
			}						
		}

		wleNotes_NoteDestory(note);
		worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
		wleNotesSetWorldUnsaved();

		ui_ListSetSelectedRow(WleGlobalNotesUI.noteList,-1);
		wleNotesNoSelection();
	}
}


//Called when right click Erase All is clicked
static void wleNotesClearGraphicBits(void *unused1, void *unused2)
{	
	int i;
	if(	WleGlobalNotesUI.data.isViewingGraphic )
	{
		for(i=0; i< WleGlobalNotesUI.data.viewingGraphic->width * WleGlobalNotesUI.data.viewingGraphic->height; i++)
		{
			*(WleGlobalNotesUI.data.viewingGraphic->buffer + i*4) = 255;
			*(WleGlobalNotesUI.data.viewingGraphic->buffer + i*4 + 1) = 255;
			*(WleGlobalNotesUI.data.viewingGraphic->buffer + i*4 + 2) = 255;
			*(WleGlobalNotesUI.data.viewingGraphic->buffer + i*4 + 3) = 0;
		}
		texGenUpdate(WleGlobalNotesUI.data.viewingGraphic->texture,WleGlobalNotesUI.data.viewingGraphic->buffer,RTEX_2D,RTEX_BGRA_U8,1,true,false,false,false);
	}
}

//Called when note selection is lost
void wleNotesNoSelection()
{
	Vec4 vColor = {1.0,1.0,1.0,1.0};
	ui_SetActive((UIWidget*) WleGlobalNotesUI.graphicColor, false);
	ui_SetActive((UIWidget*) WleGlobalNotesUI.nameEntry, false);
	ui_SetActive((UIWidget*) WleGlobalNotesUI.noteTextArea, false);
	ui_TextEntrySetText(WleGlobalNotesUI.nameEntry,"");
	ui_TextAreaSetText(WleGlobalNotesUI.noteTextArea,"");
	ui_ColorButtonSetColor(WleGlobalNotesUI.graphicColor, vColor);	
}

//Called when a "Add Graphic" is clicked
static void wleNotesAddGraphic(void *unused1, void *unused2)
{
	Vec4 vColor = {1.0,1.0,1.0,1.0};
	wleNoteGraphic *graphic;
	int selectedRow = ui_ListGetSelectedRow(WleGlobalNotesUI.noteList);
	int i;

	if(WleGlobalNotesUI.data.selectedLayersNotes && selectedRow > -1)
	{
		graphic = wleNotes_NoteGraphicCreate(WleGlobalNotesUI.data.selectedLayersNotes->file_base.fullname,wleNotes_NoteGraphicFindNewID(WleGlobalNotesUI.data.graphics));
		WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_tex_id = graphic->tex_id;
		copyVec4(vColor,WleGlobalNotesUI.data.selectedLayersNotes->notes[selectedRow]->note_color);
		ui_ColorButtonSetColor(WleGlobalNotesUI.graphicColor, vColor);	
		graphic->texture = texGenNew(graphic->width,graphic->height,"wleNoteImage",TEXGEN_NORMAL,WL_FOR_UI);//getFileNameConst(graphic->file_base.fullname)
		graphic->buffer = (U8*)calloc(graphic->width*graphic->height*4, sizeof(U8));
		
		for(i=0; i<graphic->width*graphic->height; i++)
		{
			*(graphic->buffer + i*4) = 255;
			*(graphic->buffer + i*4 + 1) = 255;
			*(graphic->buffer + i*4 + 2) = 255;
			*(graphic->buffer + i*4 + 3) = 0;
		}
		texGenUpdate(graphic->texture,graphic->buffer,RTEX_2D,RTEX_BGRA_U8,1,true,false,false,false);
		eaPush(&WleGlobalNotesUI.data.graphics,graphic);
		wleNotesGoToNote(WleGlobalNotesUI.noteList,NULL);
		worldFileModify(&WleGlobalNotesUI.data.selectedLayersNotes->file_base,false,true);
		worldFileModify(&graphic->file_base,true,true);
		wleNotesSetWorldUnsaved();
		emStatusPrintf("Added Graphic: Left Click = Draw, CTRL + Left Click = Erase");
	}
}

//Called when button active status might be different
void wleCheckActiveButtons()
{
	int selectedRow = ui_ListGetSelectedRow(WleGlobalNotesUI.noteList);

	if(WleGlobalNotesUI.data.selectedLayersNotes && selectedRow > -1)
	{	
		wleNotesNote *note = eaGet(&WleGlobalNotesUI.data.selectedLayersNotes->notes,ui_ListGetSelectedRow(WleGlobalNotesUI.noteList));
		if(strcmp(note->note_type,"Bookmark")==0)
		{
			if(note->note_tex_id == 0)
			{
				ui_SetActive((UIWidget*) WleGlobalNotesUI.graphicColor, false);
				return;
			}
			else
			{
				ui_SetActive((UIWidget*) WleGlobalNotesUI.graphicColor, true);
				return;
			}
		}
	}
	ui_SetActive((UIWidget*) WleGlobalNotesUI.graphicColor, false);
}

//Returns true if a graphic is being veiwed.
bool wleIsViewingGraphic()
{
	return WleGlobalNotesUI.data.isViewingGraphic;
}

//Draw an image if the bookmark has a graphic
static void wleNotesDisplayHasGraphic(struct UIList *list, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *data)
{
	wleNotesNote *note = (*list->peaModel)[index];
	AtlasTex *tex;

	if(!note)
		return;

	if(note->note_type && strcmp(note->note_type, "Sticky Note") == 0)
	{
		tex = atlasLoadTexture("eui_note");
		display_sprite(tex, x + 3, y, z, scale * 0.70f, scale * 0.70f, 0xffffffff);
	}
	else if (note->note_tex_id)
	{
		tex = atlasLoadTexture("eui_inksplat");
		display_sprite(tex, x + 4, y + 2, z, scale * 1.1f, scale * 1.1f, 0xffffffff);
	}
}

#endif

#include "WorldEditorNotes_h_ast.c"
