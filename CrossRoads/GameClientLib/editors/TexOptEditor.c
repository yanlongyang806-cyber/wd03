#include "TexOptEditor.h"
#include "EditorManager.h"
#include "UIAutoWidget.h"
#include "GraphicsLib.h"
#include "GfxTexOpts.h"
#include "GfxTextureTools.h"
#include "GfxSprite.h"
#include "StringCache.h"

#include "file.h"
#include "Color.h"

#include "GfxTexOpts_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

#define addGroupEx ui_RebuildableTreeAddGroup
#define addGroup(parent, name, defaultOpen, tooltip) addGroupEx(parent, name, name, defaultOpen, tooltip)
#define addButton(node, name, func, tooltip) ui_AutoWidgetAddButton(node, name, func, toDoc, false, tooltip, NULL)

#define addLabelNewline(parent, labelText) ui_RebuildableTreeAddLabel(parent, labelText, NULL, true)

#define addAutoWidget ui_AutoWidgetAdd

#define PARAMS(s) ui_AutoWidgetParamsFromString(s)
#define USE_SMF USE_SMF_TAG


static EMEditor texopt_editor;

typedef struct TexOptEditDoc
{
	EMEditorDoc base_doc;

	bool needToRebuildUI;
	bool switchedOnce1;
	bool switchedOnce2;
	bool switchedOnce3;
	bool stretch;
	bool noRepeat;
	bool fromFolder;

	UIRebuildableTree *ui_tree;
	UIWindow *mainWindow;
	BasicTexture *bind;
	TexOpt texopt;

	struct {
		F32 x0, y0, xs, ys;
		F32 u0, v0, u1, v1;
	} display;
} TexOptEditDoc;

static void toeSaveDocFunc(UIAnyWidget *widget_UNUSED, TexOptEditDoc *toDoc);

static void toeOnDataChanged(UIRTNode *node_UNUSED, TexOptEditDoc *toDoc)
{
	emSetDocUnsaved(&toDoc->base_doc, true);
	toDoc->needToRebuildUI = true;

	// Data verification and patch-up
	if (toDoc->texopt.flags & TEXOPT_JPEG) {
		if (!(toDoc->texopt.flags & TEXOPT_NOMIP)) {
			toDoc->texopt.flags |= TEXOPT_NOMIP;
		}
	}
	if (toDoc->texopt.flags & TEXOPT_COLORBORDER)
	{
		if (!toDoc->switchedOnce3)
		{
			if (toDoc->texopt.border_color.integer_for_equality_only == 0)
			{
				setVec4(toDoc->texopt.border_color.rgba, 127, 127, 255, 255);
			}
			toDoc->switchedOnce3 = true;
		}
		if (!(toDoc->texopt.flags & TEXOPT_COLORBORDER_LEGACY))
		{
			Color c565;
			c565.r = round(toDoc->texopt.border_color.r*31.f/255.f);
			c565.g = round(toDoc->texopt.border_color.g*63.f/255.f);
			c565.b = round(toDoc->texopt.border_color.b*31.f/255.f);
			toDoc->texopt.border_color.r = round(c565.r*255.f/31.f);
			toDoc->texopt.border_color.g = round(c565.g*255.f/63.f);
			toDoc->texopt.border_color.b = round(c565.b*255.f/31.f);
		}
	}
	if (toDoc->texopt.flags & (TEXOPT_NOMIP))
	{
		toDoc->texopt.mip_filter = MIP_KAISER;
		toDoc->texopt.mip_sharpening = SHARPEN_NONE;
	} else {
		// Has mipmapping
		if (toDoc->texopt.flags & (TEXOPT_CLAMPS|TEXOPT_CLAMPT))
		{
			// Clamping needs cubic (or box?)
			toDoc->texopt.mip_filter = MIP_CUBIC;
		}
	}
	if (toDoc->texopt.compression_type != COMPRESSION_TRUECOLOR && (toDoc->texopt.flags & TEXOPT_FIX_ALPHA_MIPS))
	{
		if (toDoc->texopt.compression_type != COMPRESSION_DXT1 && !toDoc->switchedOnce1) {
			toDoc->texopt.compression_type = COMPRESSION_DXT1;
			toDoc->switchedOnce1 = true;
		}
		if (toDoc->texopt.alpha_mip_threshold == 0 && !toDoc->switchedOnce2) {
			toDoc->texopt.alpha_mip_threshold = 0.95f;
			toDoc->switchedOnce2 = true;
		}
	}

	if (texoptShouldCrunch(&toDoc->texopt, toDoc->texopt.flags)) {
		toDoc->texopt.flags |= TEXOPT_CRUNCH | TEXOPT_REVERSED_MIPS;
	} else {
		toDoc->texopt.flags &= ~TEXOPT_CRUNCH;
	}

	toDoc->fromFolder = false;
}

static void toeOnDataChangedAlphaBorder(UIRTNode *node, TexOptEditDoc *toDoc)
{
	toeOnDataChanged(node, toDoc);
}

static void toeOnDataChangedAlphaBorderLRTB(UIRTNode *node, TexOptEditDoc *toDoc)
{
	toeOnDataChanged(node, toDoc);
}

static void toeToggleStretch(UIAnyWidget *widget_UNUSED, TexOptEditDoc *toDoc)
{
	toDoc->stretch = !toDoc->stretch;
	toDoc->needToRebuildUI = true;
}

static void toeToggleRepeat(UIAnyWidget *widget_UNUSED, TexOptEditDoc *toDoc)
{
	toDoc->noRepeat = !toDoc->noRepeat;
	toDoc->needToRebuildUI = true;
}

static void toeSetupUI(TexOptEditDoc *toDoc)
{
	UIWindow *window;
#define	DEFAULT_ALIGN "AlignTo 140\n"
#define	FLAGS_ALIGN "AlignTo 40\n"
#define COMBO_WIDTH "OverrideWidth 200\n"
#define RGBA_WIDTH "OverrideWidth 50\n"
#define DISABLED "Disabled\n"
	UIAutoWidgetParams params_align = *PARAMS(DEFAULT_ALIGN);
	UIAutoWidgetParams params_align_disabled = *PARAMS(DEFAULT_ALIGN DISABLED);
	UIAutoWidgetParams params_align_combo = *PARAMS(DEFAULT_ALIGN COMBO_WIDTH);
	UIAutoWidgetParams params_align_combo_disabled = *PARAMS(DEFAULT_ALIGN COMBO_WIDTH DISABLED);
	UIAutoWidgetParams params_align_flags = *PARAMS(FLAGS_ALIGN);
	UIAutoWidgetParams params_align_flags_disabled = *PARAMS(FLAGS_ALIGN DISABLED);
	UIAutoWidgetParams params_align_RGBA = *PARAMS(DEFAULT_ALIGN RGBA_WIDTH);
	UIAutoWidgetParams params_align_RGBA_disabled = *PARAMS(DEFAULT_ALIGN RGBA_WIDTH DISABLED);

	if (toDoc->mainWindow) {
		window = toDoc->mainWindow;
	} else {
		window = ui_WindowCreate("Edit TexOpt", 400, 100, 400, 490);
		window->resizable = 1;
		ui_WindowSetClosable(window, false);
		toDoc->mainWindow = window;
		eaPush(&toDoc->base_doc.ui_windows, window);
	}

 	ui_RebuildableTreeInit(toDoc->ui_tree, &UI_WIDGET(window)->children, 0, 0, UIRTOptions_YScroll);
	{
		UIRTNode *group;
		group = addGroup(toDoc->ui_tree->root, "General Options", true, NULL);
		addButton(group, "Save/Apply", toeSaveDocFunc, "Save the current TexOpt, causing the preview to be updated, and the texture to be reprocessed if appropriate.");
		addButton(group, toDoc->stretch?"Preview: Stretched":"Preview: Actual Size",
			toeToggleStretch, "Toggle between previewing the actual size and a fully stretched version of the textre");
		addButton(group, toDoc->noRepeat?"Preview: NOT tiled":"Preview: Tiled",
			toeToggleRepeat, "Toggle between previewing the a repeated copy of the texture, or just a single copy of it");

	}
	{
		UIRTNode *group;
		group = addGroup(toDoc->ui_tree->root, "TexOpt", true, NULL);
		//ui_AutoWidgetAddAllFlags(group, parse_tex_opt, "Flags", &toDoc->texopt, true, toeOnDataChanged, toDoc, PARAMS("AlignTo 40\n"), "Specifies various process-time and run-time flags");
		addLabelNewline(group, "Run-time Flags:");
		addAutoWidget(group, parse_tex_opt, "Flags|MirrorS", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_CLAMPS)?&params_align_flags_disabled:&params_align_flags, "Run-time option.  Enables texture mirroring.  Mutually exclusive with ClampS.");
		addAutoWidget(group, parse_tex_opt, "Flags|MirrorT", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_CLAMPT)?&params_align_flags_disabled:&params_align_flags, "Run-time option.  Enables texture mirroring.  Mutually exclusive with ClampT.");
		addAutoWidget(group, parse_tex_opt, "Flags|MagnificationFilterPoint", &toDoc->texopt, true, toeOnDataChanged, toDoc, &params_align_flags, "Run-time option.  Causes the rendering engine to use point sampling when magnifying a texture. This is useful if you have a very low-res mask texture and you want it stretch while maintaining sharp edges. The disadvantage of this is aliasing artifacts on pixel borders.");
		addAutoWidget(group, parse_tex_opt, "Flags|ForFallback", &toDoc->texopt, true, toeOnDataChanged, toDoc, &params_align_flags, "Run-time option.  This flag causes this texture to ignore global texture reduction settings.");
		addAutoWidget(group, parse_tex_opt, "Flags|NoAniso", &toDoc->texopt, true, toeOnDataChanged, toDoc, &params_align_flags, "Run-time option.  This flag disables anisotropic filtering on the texture, reducing artifacts on high-frequency textures..");
		addLabelNewline(group, "Process-time Flags:");
		addLabelNewline(group, "  (Need to save and have TextureLibrary");
		addLabelNewline(group, "  processing running in the MCP)");
		addAutoWidget(group, parse_tex_opt, "Flags|ClampS", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_MIRRORS)?&params_align_flags_disabled:&params_align_flags, "Process and Run-time option.  Disables texture tiling.  Forces Cubic mipmap generation.  Mutually exclusive with MirrorS.");
		addAutoWidget(group, parse_tex_opt, "Flags|ClampT", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_MIRRORT)?&params_align_flags_disabled:&params_align_flags, "Process and Run-time option.  Disables texture tiling.  Forces Cubic mipmap generation.  Mutually exclusive with MirrorT.");
		addAutoWidget(group, parse_tex_opt, "Flags|NoMip", &toDoc->texopt, true, toeOnDataChanged, toDoc, &params_align_flags, "Process-time option.  Disables mipmap generation.");
		addAutoWidget(group, parse_tex_opt, "Flags|Jpeg", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & (TEXOPT_COLORBORDER))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  Compress image on disk as JPEG for disk space savings.  Note: loaded in memory as Truecolor, and without mipmaps, so this is only suitable for loading screens.");
		addAutoWidget(group, parse_tex_opt, "Flags|FixAlphaMIPs", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.compression_type == COMPRESSION_TRUECOLOR)?&params_align_flags_disabled:&params_align_flags, "Process-time option.  This flag fixes issues with high-frequency, highly transparent textures becoming completely invisible in the distance (e.g. a chain link fence) by ensuring the MIPs being generated have at least some opaque pixels (tries to fade the opacity of the MIPs to around 50%). This should almost always be used in conjunction with DXT1 compression, because it treats the alpha channel as binary (either opaque or transparent). The exception to this is that there is currently a bug with NVDXT (the texture compressor we use) in which sometimes DXT1 compression running in this mode causes very jagged edges (loses most transparency information), in which case DXT5 compression can be used (but we'll convert them back to DXT1 once the NVDXT bug is fixed). \nMutually exclusive with TrueColor");
		addAutoWidget(group, parse_tex_opt, "AlphaMIPThreshold", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_FIX_ALPHA_MIPS)?&params_align:&params_align_disabled, "Process-time option.  This is a scaling value that is applied to the filtering when FixAlphaMIPs is enabled. Useful values probably range between 0.90 and 1.10, with 0.95-0.99 being the most useful (0.95 is the default). A lower value will cause the MIPs to be more opaque (e.g. a value of 0.1 will make the MIPs very quickly get completely opaque, which will cause aliasing issues with outlining effects), and higher more transparent (values over 1.0 quickly cause the MIPs to become completely transparent). <br><br>\
Note: When previewing a texture processed with this, the easiest way to see it's \"worst case\" is to turn of Anisotropic filtering (run the command texaniso 1 to turn it off, texaniso 4 to set it back to defaults) and then look at the texture from an angle.");
		addAutoWidget(group, parse_tex_opt, "Flags|ColorBorder", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & (TEXOPT_JPEG|TEXOPT_ALPHABORDER))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  This flag borders the texture and it's mip levels with a fixed color texel, specified below.");
		addAutoWidget(group, parse_tex_opt, "Flags|ColorBorderLegacy", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_COLORBORDER)?&params_align_flags:&params_align_flags_disabled, "Process-time option.  This option allows less-perfect bordering which in some cases allows usage of colors outside of the DXT 16bpp (5:6:5) color space.");
		addAutoWidget(group, parse_tex_opt, "BorderColor", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_COLORBORDER)?&params_align_RGBA:&params_align_RGBA_disabled, "Process-time option.  Sets the color used for color bordering.  For Normal maps, the RGB values should be near 127, 127, 255.  These are automatically adjusted to match the 565 color space used by DXT compression.");
		addAutoWidget(group, parse_tex_opt, "Flags|AlphaBorder", &toDoc->texopt, true, toeOnDataChangedAlphaBorder, toDoc, (toDoc->texopt.flags & (TEXOPT_NOMIP|TEXOPT_COLORBORDER))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  This flag borders the texture and it's mip levels with a alpha=0 texel.  If your texture does not already have a transparent border, the border pixels will be overwritten.  Expected usage is with a texture with mip levels and clamped texture coordinates.");
		addAutoWidget(group, parse_tex_opt, "Flags|AlphaBorderLR", &toDoc->texopt, true, toeOnDataChangedAlphaBorderLRTB, toDoc, (toDoc->texopt.flags & (TEXOPT_NOMIP|TEXOPT_COLORBORDER))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  This flag borders the left and right edges of the texture and it's mip levels with a alpha=0 texel.  If your texture does not already have a transparent border, the border pixels will be overwritten.  Expected usage is with a texture with mip levels and clamped texture coordinates.");
		addAutoWidget(group, parse_tex_opt, "Flags|AlphaBorderTB", &toDoc->texopt, true, toeOnDataChangedAlphaBorderLRTB, toDoc, (toDoc->texopt.flags & (TEXOPT_NOMIP|TEXOPT_COLORBORDER))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  This flag borders the top and bottom edges of the texture and it's mip levels with a alpha=0 texel.  If your texture does not already have a transparent border, the border pixels will be overwritten.  Expected usage is with a texture with mip levels and clamped texture coordinates.");
		addAutoWidget(group, parse_tex_opt, "Flags|ReversedMips", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & (TEXOPT_NOMIP | TEXOPT_CRUNCH))?&params_align_flags_disabled:&params_align_flags, "Process-time option.  Stores mipmaps in reverse order (smallest to largest) on disk to speed texture loading on low-end systems.");

		addAutoWidget(group, parse_tex_opt, "MipFilter", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & (TEXOPT_NOMIP|TEXOPT_CLAMPS|TEXOPT_CLAMPT))?&params_align_disabled:&params_align, "Process-time option.  what filtering method is used to generate mipmaps. In general the default is fine, but you can adjust this for specific effects or if something doesn't look quite right. <br><br>\
			<b>Kaiser</b> - Default <br>\
			<b>Mitchell</b> - NVIDIA recommended, might look slightly better, but in general the difference is unnoticeable <br>\
			<b>Box</b> - Causes more aliased mipmaps, but the mipmap generation is done rigidly on power of 2 boundaries, so if you have a texture sheet you can use this to ensure neighboring tiles don't blur into each other on the mipmap. <br>\
			<b>Cubic</b> - Best (and auto-set) for any textures which have texture coordinate clamping (ClampS/ClampT) or mirroring. Prevents opposite edges from blurring into each other. <br>\
");
		addAutoWidget(group, parse_tex_opt, "MipSharpening", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_NOMIP)?&params_align_disabled:&params_align, "Process-time option. What form of sharpening to apply to generated mip maps.");
		addAutoWidget(group, parse_tex_opt, "HighLevelSize", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_NOMIP)?&params_align_disabled:&params_align, "Process-time option. The size at which levels are moved into the htex file.");
		addAutoWidget(group, parse_tex_opt, "MinLevelSplit", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.flags & TEXOPT_NOMIP)?&params_align_disabled:&params_align, "Process-time option. Minimum number of levels to move into the htex file regardless of the texture dimensions.");
		addAutoWidget(group, parse_tex_opt, "Quality", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.compression_type == COMPRESSION_TRUECOLOR)?&params_align_disabled:&params_align, "Process-time option of what quality level of DXT-compression and filtering to run");
		addAutoWidget(group, parse_tex_opt, "Compression", &toDoc->texopt, true, toeOnDataChanged, toDoc, (toDoc->texopt.compression_type == COMPRESSION_TRUECOLOR)?&params_align_combo_disabled:&params_align_combo, "Process-time option of how to compress. <br><br>\
				<b>Auto</b> - uses DXT5 for textures with an alpha channel, DXT1 if they do not <br>\
				<b>DXT1</b> - provides only 1-bit alpha (binary on or off), at half the size of a DXT5, and is suitable for alpha channels on textures which will be used for any Material with AlphaCutout. <br>\
				<b>HalfResTrueColor</b> - See docs on Normal Map Compression <br>\
				<b>Uncompressed16bpp</b> - Uncompressed 16bpp format, with 5 bits for R, G, and B, and 1 bit for alpha. <br>\
				<b>DXT5nm</b> - DXT5 Normal Map compression.  No alpha channel.  This requires a special Material Template to be used. <br>\
				<b>TrueColor</b> - Uncompressed.<br>\
");
		if (toDoc->fromFolder) {
			addLabelNewline(group, "Note: inherited values from a Folder TexOpt.");
		}
	}
	ui_RebuildableTreeDoneBuilding(toDoc->ui_tree);

	toDoc->needToRebuildUI = false;
	ui_WindowShow(window);

}

static void toeUpdateScale(TexOptEditDoc *toDoc)
{
	F32 x0;
	F32 y0;
	F32 w, h;
	F32 xs, ys;
	if (!toDoc->noRepeat) {
		xs = 2;
		ys = 2;
		toDoc->display.u0 = -0.5;
		toDoc->display.v0 = -0.5;
		toDoc->display.u1 = 1.5;
		toDoc->display.v1 = 1.5;
	} else {
		xs = 1;
		ys = 1;
		toDoc->display.u0 = 0;
		toDoc->display.v0 = 0;
		toDoc->display.u1 = 1;
		toDoc->display.v1 = 1;
	}
	emGetCanvasSize(&x0, &y0, &w, &h);
	if (toDoc->bind) {
		if (toDoc->stretch) {
			xs = w / (F32)texWidth(toDoc->bind);
			ys = h / (F32)texHeight(toDoc->bind);
		}
	}
	if (!toDoc->stretch) {
		y0+=28;
	}
	toDoc->display.x0 = x0;
	toDoc->display.y0 = y0;
	toDoc->display.xs = xs;
	toDoc->display.ys = ys;
}

static void toeDrawDoc(EMEditorDoc *doc_in)
{
	TexOptEditDoc* toDoc = (TexOptEditDoc*)doc_in;
	if (toDoc->needToRebuildUI) {
		toeSetupUI(toDoc);
	}

	if (texopt_editor.camera)
		setVec4(texopt_editor.camera->clear_color, 0.5, 0.5, 0.5, 1);

	toeUpdateScale(toDoc);
	if (toDoc->bind)
	{
		display_sprite_ex(NULL, toDoc->bind, NULL, NULL, 
			toDoc->display.x0, toDoc->display.y0, 0, 
			toDoc->display.xs, toDoc->display.ys, 
			0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
			toDoc->display.u0, toDoc->display.v0, toDoc->display.u1, toDoc->display.v1,
			0, 0, 1, 1, 
			0, 0, clipperGetCurrent());
	}
}


static void toeCloseDoc(EMEditorDoc *doc_in)
{
	TexOptEditDoc* toDoc = (TexOptEditDoc*)doc_in;
	eaDestroyEx(&toDoc->base_doc.ui_windows, ui_WindowFreeInternal);
	ui_RebuildableTreeDestroy(toDoc->ui_tree);
	free(toDoc);
}

static EMTaskStatus toeSaveDoc(EMEditorDoc *doc_in)
{
	TexOptEditDoc* toDoc = (TexOptEditDoc*)doc_in;
	char fullname[MAX_PATH];
	TexOptList dummy = { NULL };
	int ok;
	TexOptFlags old_flags = toDoc->texopt.flags;
	assert(toDoc->texopt.file_name);
	toDoc->texopt.flags &= ~TEXOPT_DO_NOT_SAVE_OR_READ_FLAGS;
	eaPush(&dummy.ppTexOpts, &toDoc->texopt);
	fileLocateWrite(toDoc->texopt.file_name, fullname);
	if (!fileExists(fullname)) {
		// New file, make sure if the texture is in Core, so is the TexOpt!
		changeFileExt(toDoc->texopt.file_name, ".wtex", fullname);
		fileLocateWrite(fullname, fullname);
		if (!fileExists(fullname)) {
			// Can't find the texture anyway, go back
			fileLocateWrite(toDoc->texopt.file_name, fullname);
		} else {
			changeFileExt(fullname, ".TexOpt", fullname);
		}
	}
	fileRenameToBak(fullname);
	mkdirtree(fullname);
	ok = ParserWriteTextFile(fullname, parse_TexOptList, &dummy, 0, 0);
	gfxStatusPrintf("File saved %s", ok?"successfully":"FAILED");

	toDoc->texopt.flags = old_flags;

	toDoc->base_doc.saved = 1;
	return ok?EM_TASK_SUCCEEDED:EM_TASK_FAILED;
}

static void toeSaveDocFunc(UIAnyWidget *widget_UNUSED, TexOptEditDoc *toDoc)
{
	toeSaveDoc(&toDoc->base_doc);
}

static EMEditorDoc* toeNewDoc(const char* name_in, const char* unused)
{
	char name[MAX_PATH];
	TexOptEditDoc* toDoc = calloc(1, sizeof(TexOptEditDoc));

	name[0] = 0;

	// Cannot create new ones, you must edit an existing texture or TexOpt
	if (!name_in)
		return NULL;

	// Get TexOpt filename
	getFileNameNoExt(name, name_in);

	// Setup data on new toDoc
	toDoc->ui_tree = ui_RebuildableTreeCreate();
	toDoc->bind = texLoadBasic(name, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);
	assert(toDoc->bind);
	assert(toDoc->bind != white_tex || strStartsWith(name, "white"));
	// Get full texopt filename
	changeFileExt(texFindFullPath(toDoc->bind), ".TexOpt", name);

	{
		// Set flags/values from any folder texopt
		TexOpt *tex_opt = texoptFromTextureName(name, &toDoc->texopt.flags);
		if (tex_opt) {
			if (tex_opt->is_folder)
				toDoc->fromFolder = 1;
			toDoc->texopt = *tex_opt;
			toDoc->texopt.file_name = NULL;
			toDoc->texopt.texture_name = NULL;
			toDoc->texopt.is_folder = 0;
		} else {
			toDoc->texopt.mip_filter = MIP_KAISER;
			toDoc->texopt.quality = QUALITY_PRODUCTION;
			toDoc->texopt.compression_type = (toDoc->texopt.flags & TEXOPT_COMPRESSION_MASK) >> TEXOPT_COMPRESSION_SHIFT;
		}
	}

	if (!toDoc->texopt.file_name)
		toDoc->texopt.file_name = allocAddString(name);
	emDocAssocFile(&toDoc->base_doc, name);
	//doc_file = lodedGetDocFile(&toDoc->base_doc);
	//if (!fileExists(doc_file->filename))
	//	emSetDocUnsaved(&doc->base_doc, false);

	{
		char shortFileName[MAX_PATH];
		char *s;
		s = strstri(name, "texture_library/");
		if (s) {
			strcpy(shortFileName, s+strlen("texture_library/"));
		} else {
			strcpy(shortFileName, name);
		}
		strcpy(toDoc->base_doc.doc_display_name, shortFileName);
	}

	// Setup the toDoc UI
	toeSetupUI(toDoc);

	return &toDoc->base_doc;
}

static void toeInitEditor(EMEditor *editor)
{
	assert(editor->camera);
	editor->camera->override_bg_color = 1; 
	editor->camera->override_disable_3D = 1;
}

static Color toePickerColor( const char* path, bool isSelected )
{
	char texName[ MAX_PATH ];

	getFileNameNoExt( texName, path );
	
	if( texoptFromTextureName( texName, NULL ) != NULL ) {
		return CreateColorRGB( 0, 80, 0 );
	} else {
		if( isSelected ) {
			return CreateColorRGB( 255, 255, 255 );
		} else {
			return CreateColorRGB( 0, 0, 0 );
		}
	}
}

#endif

AUTO_RUN_LATE;
int toeRegister(void)
{
#ifndef NO_EDITORS
	EMPicker * picker;
	
	if (!areEditorsAllowed())
		return 0;
	strcpy(texopt_editor.editor_name, "TexOpt Editor");
	texopt_editor.allow_multiple_docs = 1;
	texopt_editor.allow_save = 1;
	texopt_editor.hide_world = 1;
	texopt_editor.allow_outsource = 1;
	texopt_editor.edit_shared_memory = 1;

	texopt_editor.camera_func = gfxNullCamFunc;

	texopt_editor.init_func = toeInitEditor;
	texopt_editor.load_func = toeNewDoc;
	texopt_editor.close_func = toeCloseDoc;
	texopt_editor.save_func = toeSaveDoc;
	texopt_editor.draw_func = toeDrawDoc;
//	texopt_editor.ghost_draw_func = toeDrawGhosts;
//	texopt_editor.lost_focus_func = toeLostFocus;
//	texopt_editor.got_focus_func = toeGotFocus;
// 	texopt_editor.object_dropped_func = toeObjectSelected;

	texopt_editor.use_em_cam_keybinds = true;

	emRegisterEditor(&texopt_editor);

	picker = emTexturePickerCreateForType( "TexOpt" );
	emEasyPickerSetColorFunc( picker, toePickerColor );
	eaPush(&texopt_editor.pickers, picker );

	emRegisterFileTypeEx("TexOpt", "TexOpt Editor", "TexOpt Editor", NULL);
#endif
	return 1;
}

void toeOncePerFrame(void)
{
	CHECK_EM_FUNC(toeNewDoc);
	CHECK_EM_FUNC(toeCloseDoc);
	CHECK_EM_FUNC(toeSaveDoc);
	CHECK_EM_FUNC(toeDrawDoc);
}
