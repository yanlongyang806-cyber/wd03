#ifndef __WORLDEDITORNOTES_H__
#define __WORLDEDITORNOTES_H__
GCC_SYSTEM

#include "WorldGrid.h"

typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMPanel EMPanel;
typedef struct UIWindow UIWindow;
typedef struct UIComboBox UIComboBox;
typedef struct UIList UIList;
typedef struct UITextEntry UITextEntry;
typedef struct UITextArea UITextArea;
typedef void UIAnyWidget;
typedef struct WorldFile WorldFile;
typedef struct UIMenu UIMenu;
typedef struct UIButton UIButton;
typedef struct UIColorButton UIColorButton;
typedef struct UILabel UILabel;
typedef struct UICheckButton UICheckButton;
typedef struct AtlasTex AtlasTex;

#ifndef NO_EDITORS

#define NOTES_DRAW_PORT_WIDTH_DEFAULT 395
#define NOTES_DRAW_PORT_HEIGHT_DEFAULT 300
#define ERASER_SIZE 5							//only tested with odd numbers 
#define NOTES_PIE 3.14159						//Is there a defined pie somewhere else?

#define UI_PARENT_ARGS F32 pX, F32 pY, F32 pW, F32 pH, F32 pScale
#define UI_PARENT_VALUES pX, pY, pW, pH, pScale
#define UI_MY_VALUES x, y, w, h, scale
#define UI_MY_ARGS F32 x, F32 y, F32 w, F32 h, F32 scale

//For handeling strings
void stringCopySafeAllocation(char **dst, const char *src);
void convertToSMFText(char dest[],const char *src, int max);

//Two types of notes:  Sticky Notes and Bookmarks
#define STICKY_NOTE true
#define BOOKMARK false
#define wleNoteType bool

#endif
//A note or a bookmark
AUTO_STRUCT;
typedef struct wleNotesNote
{
	char *note_name;
	char *note_content;
	char *note_type;
	F32	note_cam_dist;
	Vec3 note_cam_center;
	Vec3 note_cam_pyr;		AST(NAME("note_cam_pyr", "note_cam_target")) 
	U32 note_tex_id;
	Vec4 note_color;
	char *tracker_handle;
	char *user_name;

} wleNotesNote;
#ifndef NO_EDITORS

extern ParseTable parse_wleNotesNote[];
#define TYPE_parse_wleNotesNote wleNotesNote

wleNotesNote *wleNotes_NoteCreate(const char *name, const char *content, wleNoteType type);//Create a Note/Bookmark
void wleNotes_NoteInitialize(wleNotesNote *note, const char *name, const char *content, wleNoteType type);//Init a Note/Bookmark
void wleNotes_NoteDestory(wleNotesNote *note);

typedef struct wleNoteGraphic
{
	WorldFile file_base;	
	U32 tex_id;
	BasicTexture *texture;
	U8 *buffer;
	int width;
	int height;
	bool deleted;	//Set true so that it will be deleted next time you save

} wleNoteGraphic;

wleNoteGraphic *wleNotes_NoteGraphicCreate(const char *noteSetName, U32 tex_id);//Create an image for a bookmark
void wleNotes_NoteGraphicInitialize(wleNoteGraphic *graphic, const char *noteSetName, U32 tex_id);//Init the image
void wleNotes_NoteGraphicDestory(wleNoteGraphic *graphic);
U32 wleNotes_NoteGraphicFindNewID(wleNoteGraphic **graphicList);		//Search list for a new ID number

#endif
//A set of Notes bound to a layer
AUTO_STRUCT;
typedef struct wleNotesNoteSet
{
	WorldFile file_base;	NO_AST
	wleNotesNote **notes;	//list of notes and bookmarks
} wleNotesNoteSet;
#ifndef NO_EDITORS

extern ParseTable parse_wleNotesNoteSet[];
#define TYPE_parse_wleNotesNoteSet wleNotesNoteSet

wleNotesNoteSet *wleNotes_NoteSetCreate(const char *layerName, wleNotesNote **notes);//Create a set of Notes/Bookmarks
void wleNotes_NoteSetInitialize(wleNotesNoteSet *noteSet, const char *layerName, wleNotesNote **notes);//Init a set of Notes/Bookmarks
void wleNotes_NoteSetDestory(wleNotesNoteSet *noteSet);

//Data for the Notes system
typedef struct WleNotesData
{
	const char **layerNameListPath;			//List of layers for the notes
	const char **layerNameList;				//List of layers for the combo box
	wleNotesNoteSet **noteSets;				//All the sets of notes
	wleNotesNoteSet *selectedLayersNotes;	//Currently Selected Layer's Set of Notes
	int layerCount;							//Number of Layers
	wleNoteGraphic **graphics;				//List of note graphics
	bool isViewingGraphic;					//Weather of not the overlay should be displayed.
	wleNoteGraphic *viewingGraphic;			//Graphic being viewed
	F32	viewingGraphic_cam_dist;			//Next 3 are: Where the camera should be if we are viewing 
	Vec3 viewingGraphic_cam_center;
	Vec3 viewingGraphic_cam_pyr;			
	bool drawing;							//Is currently drawing or erasing on the screen
	bool erasing;							//Is earsing;
	Vec4 viewingColor;						//Color to draw graphic with
	bool doingSaveAs;						//Are we in that bad spot between having Saved but not having loadeded yet. 
	bool isViewingSticky;					//Is the View Sticky check box checked

} WleNotesData;

//Data and UI elements for Notes System
typedef struct WleNotesUI
{
	EMPanel *panel;					//Notes panel
	UIComboBox *layerCombo;			//Combo box for selecting layer
	UITextEntry *nameEntry;			//Text Entry for editing a Notes Name
	UITextArea *noteTextArea;		//Text Area for editing the actual Note
	UIList *noteList;				//List box for displaying and editing notes and bookmarks
	UIColorButton *graphicColor;	//Color of the overlay graphic
	UIMenu *listRightClick;			//Menu for list box right click
	AtlasTex *stickNoteImage;		//Image for sticky note
	UILabel *noteLabel;				//Saved only as a convient object to use its tooltip data

	WleNotesData data;				//Data for Notes system

} WleNotesUI;

//Note Sysyem Calls
void wleNotesCreate(EMEditorDoc *doc);
void wleNotesTick(void);
void wleNotesLoad();
void wleNotesSave();
void wleNotesSaveAs(const char *dir,const char *fileName);
void wleNotesReloadLayers();
void wleNotesAddLayer();
void wleNotesNoSelection();//Call when note selection is lost
void wleCheckActiveButtons();//Call when button active status might be different
void wleFinishedDrawing();//Call when no longer drawing to the screen
bool wleIsViewingGraphic();//Returns true if a graphic is being veiwed.
void wleUpdateGraphicWait(WorldFile *file_base, bool force);//send to this so that updates to graphic happen no more than a second between
void wleNotesSetWorldUnsaved();//Call when a change is made that should be saved

//BookmarkGraphic Helpers
int wleNotesScreenXtoGraphicX(wleNoteGraphic *graphic, int x, int windowWidth, int windowHeight);
int wleNotesScreenYtoGraphicY(wleNoteGraphic *graphic, int y, int windowHeight);
void wleNotesDrawDot(wleNoteGraphic *graphic, int x, int y);
void wleNotesDrawLine(wleNoteGraphic *graphic, int xStart, int yStart, int xEnd, int yEnd);

//Called when selected layer is changed using the layer combo 
static void wleNotesSwitchLayers(UIComboBox *combo, void *unused2);

//Called when Display Sticky Notes check box is clicked
static void wleNotesSwitchStickyDisplayStatus(UICheckButton *check, void *unused);

//Called when a "Name" text box is changed
static void wleNotesNameChanged(UITextEntry *entry, void *unused);

//Called when a color button is changed
static void wleNotesColorChanged(UIColorButton *colorButton, bool finished, void *unused);

//Called when "Note" text area is changed
static void wleNotesNoteTextChanged(UITextArea *textArea, UIAnyWidget *focusitem);

//Called when a note is selected from the list
static void wleNotesSelectNote(UIList *list, void *unused);

//Called when a note is double clicked form the list
static void wleNotesGoToNote(UIList *list, void *unused);

//Called when a note list is rightclicked
static void wleNotesRightClickNote(UIList *list, void *unused);

//Called when a "New Bookmark" is clicked
static void wleNotesNewBookmark(void *unused1, void *unused2);

//Called when a "New Sticky Note" is clicked
static void wleNotesNewStickyNote(void *unused1, void *unused2);

//Called when a rightclick attach is called
static void wleNotesAttachStickyNote(void *unused1, void *unused2);

//Called when a rightclick delete is called
static void wleNotesDeleteNote(void *unused1, void *unused2);

//Erase everything on the graphic
static void wleNotesClearGraphicBits(void *unused1, void *unused2);

//Called when a "Add Graphic" is clicked
static void wleNotesAddGraphic(void *unused1, void *unused2);

//Draw an image if the bookmark has a graphic
static void wleNotesDisplayHasGraphic(struct UIList *list, struct UIListColumn *col, UI_MY_ARGS, F32 z, struct CBox *pBox, int index, void *data);

#endif // NO_EDITORS

#endif // __WORLDEDITORNOTES_H__