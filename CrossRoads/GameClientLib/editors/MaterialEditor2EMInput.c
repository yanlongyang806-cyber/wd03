#ifndef NO_EDITORS

#include"MaterialEditor2EMInput.h"

#include"EditorManager.h"
#include"GfxSprite.h"
#include"GfxTexAtlas.h"
#include"GraphicsLib.h"
#include"MaterialEditor2EM.h"
#include"MaterialEditor2EMNode.h"
#include"Materials.h"
#include"StringCache.h"
#include"inputMouse.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef void (*Mated2InputSetValuesFn)(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PARAM_OP_VALID const F32* values,
		SA_PARAM_OP_VALID const char** svalues, bool isRecursive );
typedef void (*Mated2InputGetValuesFn)(
		SA_PARAM_NN_VALID const Mated2Input* input, SA_PARAM_NN_VALID F32** outValues,
		SA_PARAM_NN_VALID char*** outSValues);
typedef void (*Mated2InputHiddenFn)( SA_PARAM_NN_VALID Mated2Input* input );

/// An input that can be edited 
typedef struct Mated2Input {
	const char* inputName;
	Mated2Node* node;
	Mated2InputSetValuesFn setValuesFn;
	Mated2InputGetValuesFn getValuesFn;
	Mated2InputHiddenFn hiddenFn;

	UICheckButton* nodeViewLockButton;
	UIPane* nodeViewPane;
	UIPane* inputListPane;

	// this is ugly polymorphism
	union {
		struct {
			UIColorButton* nodeViewColorButton;
			UIColorButton* inputListColorButton;
		} colorPicker;

		struct {
			UITextureEntry* nodeViewTextureEntry;
			UITextureEntry* inputListTextureEntry;
		} texturePicker;

		struct {
			UISliderTextEntry* nodeViewSliders[ 4 ];
			UISliderTextEntry* inputListSliders[ 4 ];
		} floatPicker;
	};
} Mated2Input;

typedef struct Mated2InputSetValuesAction {
	char* nodeName;
	char* inputName;
	
	F32* oldFValues;
	char** oldSValues;

	F32* newFValues;
	char** newSValues;
} Mated2InputSetValuesAction;

typedef struct Mated2InputLockAction {
	char* nodeName;
	char* inputName;
	bool isLocked;
} Mated2InputLockAction;

static void mated2InputSetValuesActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputSetValuesAction* action );
static void mated2InputSetValuesActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputSetValuesAction* action );
static void mated2InputSetValuesActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputSetValuesAction* action );

static void mated2InputLockActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputLockAction* action );
static void mated2InputLockActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputLockAction* action );
static void mated2InputLockActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2InputLockAction* action );

static void mated2ColorPickerSetValues(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PRE_NN_RELEMS(4) const F32* values,
		SA_PRE_OP_RELEMS(0) const char** svalues, bool isRecursive );
static void mated2ColorPickerGetValues(
		SA_PARAM_NN_VALID const Mated2Input* input, SA_PARAM_NN_VALID F32** outValues,
		SA_PARAM_NN_VALID char*** outSValues );
static void mated2ColorPickerValueChanged(
		SA_PARAM_NN_VALID UIColorButton* colorButton, bool finished, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2ColorPickerHidden( SA_PARAM_NN_VALID Mated2Input* input );
static void mated2TexturePickerSetValues(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PRE_OP_RELEMS(0) const F32* values,
		SA_PARAM_NN_VALID const char** svalues, bool isRecursive );
static void mated2TexturePickerGetValues(
		SA_PARAM_NN_VALID const Mated2Input* input, SA_PARAM_NN_VALID F32** outValues,
		SA_PARAM_NN_VALID char*** outSValues );
static void mated2TexturePickerValueChanged(
		SA_PARAM_NN_VALID UITextureEntry* textureEntry, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2TexturePickerValueChangedPartial(
		SA_PARAM_NN_VALID UITextureEntry* textureEntry, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2TexturePickerHidden( SA_PARAM_NN_VALID Mated2Input* input );
static void mated2TexturePickerCustomChoose(
		SA_PARAM_NN_VALID UITextureEntry* textureEntry, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2FloatPickerSetValues(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PRE_NN_RELEMS(4) const F32* values,
		SA_PRE_OP_RELEMS(0) const char** svalues, bool isRecursive );
static void mated2FloatPickerGetValues(
		SA_PARAM_NN_VALID const Mated2Input* input, SA_PARAM_NN_VALID F32** outValues,
		SA_PARAM_NN_VALID char*** outSValues );
static void mated2FloatPickerValueChanged(
		SA_PARAM_NN_VALID UISliderTextEntry* slider, bool bFinished, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2FloatPickerHidden( SA_PARAM_NN_VALID Mated2Input* input );
static void mated2UnsupportedInputSetValues(
		SA_PARAM_NN_VALID Mated2Input* input, SA_PRE_OP_RELEMS(0) const F32* values,
		SA_PRE_OP_RELEMS(0) const char** svalues, bool isRecursive );
static void mated2UnsupportedInputGetValues(
		SA_PARAM_NN_VALID const Mated2Input* input, SA_PARAM_NN_VALID F32** outValues,
		SA_PARAM_NN_VALID char*** outSValues );
static void mated2UnsupportedInputHidden( Mated2Input* input );
static void mated2InputLockChanged( SA_PARAM_NN_VALID UICheckButton* button, SA_PARAM_NN_VALID Mated2Input* input );
static void mated2InputPaneDrawLocked( SA_PARAM_NN_VALID UIPane* pane, UI_PARENT_ARGS );
static void mated2InputPaneDrawUnlocked( SA_PARAM_NN_VALID UIPane* pane, UI_PARENT_ARGS );

/// Create a new color picker.
Mated2Input* mated2ColorPickerCreate(
		Mated2Node* node, const char* op_name, const char* name, const char* tooltip, bool allowLock,
		bool allowAlpha, Vec2 floatRange )
{
	Mated2Input* accum = calloc( 1, sizeof( *accum ));
	char nodeTitle[255];

	// create the node view... set of widgets
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int yIt = 0;
		int xIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->nodeViewPane = root;

		if( allowLock ) {
			UICheckButton* checkButton = ui_CheckButtonCreate( xIt, yIt, "", false );
			ui_WidgetSetTooltipString( UI_WIDGET( checkButton ), "Check to lock this value per-material template" );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( checkButton ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( checkButton )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( checkButton )) + 2;
			accum->nodeViewLockButton = checkButton;
			ui_CheckButtonSetToggledCallback( checkButton, mated2InputLockChanged, accum );
		} else {
			xIt += ui_CheckButtonWidthNoText() + 2;
		}

		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );
		
			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			UIColorButton* colorButton = ui_ColorButtonCreate( xIt, yIt, zerovec4 );
			if (op_name) {
				strcpy(nodeTitle,op_name);
				Strcat(nodeTitle," > ");
				Strcat(nodeTitle,name);
				ui_ColorButtonSetTitle(colorButton,nodeTitle);
			}
			colorButton->liveUpdate = true;
			colorButton->noAlpha = !allowAlpha;
			if( floatRange[ 0 ] != floatRange[ 1 ]) {
				colorButton->min = floatRange[ 0 ];
				colorButton->max = floatRange[ 1 ];
			} else {
				colorButton->min = -1;
				colorButton->max = 10;
			}
			ui_WidgetSetDimensions( UI_WIDGET( colorButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( colorButton ), "Click to show the color picker" );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( colorButton ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( colorButton )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( colorButton )) + 2;

			ui_ColorButtonSetChangedCallback( colorButton, mated2ColorPickerValueChanged, accum );
			accum->colorPicker.nodeViewColorButton = colorButton;
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}

	// create the input list set of widgets...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int yIt = 0;
		int xIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->inputListPane = root;

		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			UIColorButton* colorButton = ui_ColorButtonCreate( xIt, yIt, zerovec4 );
			if (op_name) {
				strcpy(nodeTitle,op_name);
				Strcat(nodeTitle," > ");
				Strcat(nodeTitle,name);
				ui_ColorButtonSetTitle(colorButton,nodeTitle);
			}
			colorButton->liveUpdate = true;
			colorButton->noAlpha = !allowAlpha;
			if( floatRange[ 0 ] != floatRange[ 1 ]) {
				colorButton->min = floatRange[ 0 ];
				colorButton->max = floatRange[ 1 ];
			} else {
				colorButton->min = -1;
				colorButton->max = 10;
			}
			ui_WidgetSetDimensions( UI_WIDGET( colorButton ), 32, 32 );
			ui_WidgetSetTooltipString( UI_WIDGET( colorButton ), "Click to show the color picker" );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( colorButton ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( colorButton )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( colorButton )) + 2;

			ui_ColorButtonSetChangedCallback( colorButton, mated2ColorPickerValueChanged, accum );
			accum->colorPicker.inputListColorButton = colorButton;
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}
	
	accum->inputName = allocAddString( name );
	accum->node = node;
	accum->setValuesFn = mated2ColorPickerSetValues;
	accum->getValuesFn = mated2ColorPickerGetValues;
	accum->hiddenFn = mated2ColorPickerHidden;
	return accum;
}

/// Set the internal values for a color picker.
static void mated2ColorPickerSetValues(
		Mated2Input* input, const F32* values, const char** svalues, bool isRecursive )
{
	if( !isRecursive ) {
		ui_ColorButtonSetColorAndCallback( input->colorPicker.nodeViewColorButton, values );
		ui_ColorButtonSetColorAndCallback( input->colorPicker.inputListColorButton, values );
	} else {
		ui_ColorButtonSetColor( input->colorPicker.nodeViewColorButton, values );
		ui_ColorButtonSetColor( input->colorPicker.inputListColorButton, values );
	}
}

/// Get the internal values for INPUT and store them in OUT-VALUES and OUT-SVALUES
static void mated2ColorPickerGetValues(
		const Mated2Input* input, F32** outValues, char*** outSValues )
{
	eafSetSize( outValues, 4 );
	ui_ColorButtonGetColor( input->colorPicker.nodeViewColorButton, *outValues );

	eaSetSize( outSValues, 0 );
}

/// Callback for when the color picker's value changes.
static void mated2ColorPickerValueChanged( UIColorButton* colorButton, bool finished, Mated2Input* input )
{
	Mated2Node* node = input->node;
	Mated2EditorDoc* doc = mated2InputDoc( input );
	
	if( !mated2IsLoading( doc )) {
		Vec4 oldValues;
		Vec4 values;
		ui_ColorButtonGetColor( colorButton, values );
				
		if( input->nodeViewLockButton && ui_CheckButtonGetState( input->nodeViewLockButton )) {
			ShaderFixedInput* fixedInput = materialFindFixedInputByName( mated2NodeShaderOp( node ), input->inputName );
			int numValues;

			// It's possible that the fixed input has gone away in any
			// number of ways by now. (Made into a specific value, an
			// inherting value, etc. (COR-3104)
			if( !fixedInput ) {
				return;
			}
			
			numValues = eafSize( &fixedInput->fvalues );
			
			if( numValues == 3 ) {
				copyVec3( fixedInput->fvalues, oldValues );
				oldValues[ 0 ] = 1;
				copyVec3( values, fixedInput->fvalues );
			} else if( numValues == 4 ) {
				copyVec4( fixedInput->fvalues, oldValues );
				copyVec4( values, fixedInput->fvalues );
			} else {
				assert( false );
			}
		} else {
			ShaderOperationSpecificValue* specificInput = materialFindOperationSpecificValue2( mated2NodeShaderOpValues( node ), input->inputName );
			int numValues;

			// It's possible that the specific input has gone away in
			// any number of ways by now. (Made into a fixed value, an
			// inherting value, etc. (COR-3104)
			if( !specificInput ) {
				return;
			}

			numValues = eafSize( &specificInput->fvalues );

			assert( eaSize( &specificInput->svalues ) == 0 );
			if( numValues == 3 ) {
				copyVec3( specificInput->fvalues, oldValues );
				oldValues[ 0 ] = 1;
				copyVec3( values, specificInput->fvalues );
			} else if( numValues == 4 ) {
				copyVec4( specificInput->fvalues, oldValues );
				copyVec4( values, specificInput->fvalues );
			} else {
				assert( false );
			}
		}

		// Record undo state
		if(!mouseIsDown(MS_LEFT))
		{
			Mated2InputSetValuesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeName( node ));
			accum->inputName = strdup( mated2InputName( input ));
			eafSetSize( &accum->oldFValues, 4 );
			copyVec4( oldValues, accum->oldFValues );
			eafSetSize( &accum->newFValues, 4 );
			copyVec4( values, accum->newFValues );

			mated2UndoRecord( doc, mated2InputSetValuesActionUndo, mated2InputSetValuesActionRedo,
							  mated2InputSetValuesActionFree, accum );
		}
		input->setValuesFn( input, values, NULL, true );

		mated2SetDirty( doc );
	}
}

static void mated2ColorPickerHidden( SA_PARAM_NN_VALID Mated2Input* input )
{
	ui_ColorButtonCancelWindow( input->colorPicker.nodeViewColorButton );
	ui_ColorButtonCancelWindow( input->colorPicker.inputListColorButton );
}

/// Create a new texture picker
Mated2Input* mated2TexturePickerCreate(
		Mated2Node* node, const char* name, const char* tooltip, bool allowLock, const char *default_string )
{
	Mated2Input* accum = calloc( 1, sizeof( *accum ));

	if (!default_string)
		default_string = "default";

	// create the node view....
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int xIt = 0;
		int yIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->nodeViewPane = root;

		if( allowLock ) {
			UICheckButton* checkButton = ui_CheckButtonCreate( xIt, yIt, "", false );
			ui_WidgetSetTooltipString( UI_WIDGET( checkButton ), "Check to lock this value per-material template" );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( checkButton ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( checkButton )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( checkButton )) + 2;
			accum->nodeViewLockButton = checkButton;
			ui_CheckButtonSetToggledCallback( checkButton, mated2InputLockChanged, accum );
		} else {
			xIt += ui_CheckButtonWidthNoText() + 2;
		}
	
		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );
		
			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			UITextureEntry* textureEntry = ui_TextureEntryCreate( default_string, NULL, false);
			ui_TextureEntrySetCustomChooseCallback( textureEntry, mated2TexturePickerCustomChoose, accum );
			ui_TextureEntrySetSelectOnFocus( textureEntry, true );
			ui_WidgetSetPosition( UI_WIDGET( textureEntry ), xIt, yIt );
			{
				BasicTexture* bind = texFind( default_string, false );
				if( bind ) {
					char buffer[ MAX_PATH ];
					texFindDirName( SAFESTR(buffer), bind );
			
					ui_TextureEntrySetFileStartDir( textureEntry, buffer );
				}
			}

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( textureEntry ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( textureEntry )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( textureEntry )) + 2;

			accum->texturePicker.nodeViewTextureEntry = textureEntry;
			ui_TextureEntrySetChangedCallback( textureEntry, mated2TexturePickerValueChangedPartial, accum );
			ui_TextureEntrySetFinishedCallback( textureEntry, mated2TexturePickerValueChanged, accum );
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}

	// create the input list view...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int xIt = 0;
		int yIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->inputListPane = root;

		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );
		
			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			UITextureEntry* textureEntry = ui_TextureEntryCreate( default_string, NULL, false );
			ui_TextureEntrySetCustomChooseCallback( textureEntry, mated2TexturePickerCustomChoose, accum );
			ui_TextureEntrySetSelectOnFocus( textureEntry, true );
			ui_WidgetSetPosition( UI_WIDGET( textureEntry ), xIt, yIt );
			{
				BasicTexture* bind = texFind( default_string, false );
				if( bind ) {
					char buffer[ MAX_PATH ];
					texFindDirName( SAFESTR(buffer), bind );
			
					ui_TextureEntrySetFileStartDir( textureEntry, buffer );
				}
			}

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( textureEntry ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( textureEntry )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( textureEntry )) + 2;

			accum->texturePicker.inputListTextureEntry = textureEntry;
			ui_TextureEntrySetChangedCallback( textureEntry, mated2TexturePickerValueChangedPartial, accum );
			ui_TextureEntrySetFinishedCallback( textureEntry, mated2TexturePickerValueChanged, accum );
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}
	
	accum->inputName = allocAddString( name );
	accum->node = node;
	accum->setValuesFn = mated2TexturePickerSetValues;
	accum->getValuesFn = mated2TexturePickerGetValues;
	accum->hiddenFn = mated2TexturePickerHidden;
	return accum;
}

/// Set the internal values for a texture picker.
static void mated2TexturePickerSetValues(
		Mated2Input* input, const F32* values, const char** svalues, bool isRecursive )
{
	ui_TextureEntrySetTextureName( input->texturePicker.nodeViewTextureEntry, svalues[ 0 ]);
	ui_TextureEntrySetTextureName( input->texturePicker.inputListTextureEntry, svalues[ 0 ]);
	{
		BasicTexture* bind = texFind( svalues[ 0 ], false );
		if( bind ) {
			char buffer[ MAX_PATH ];
			texFindDirName( SAFESTR(buffer), bind );
			
			ui_TextureEntrySetFileStartDir( input->texturePicker.nodeViewTextureEntry, buffer );
			ui_TextureEntrySetFileStartDir( input->texturePicker.inputListTextureEntry, buffer );
		}
	}

	if( !isRecursive ) {
		if( input->texturePicker.nodeViewTextureEntry->cbFinished ) {
			input->texturePicker.nodeViewTextureEntry->cbFinished(
					input->texturePicker.nodeViewTextureEntry,
					input->texturePicker.nodeViewTextureEntry->pFinishedData );
		}
		if( input->texturePicker.inputListTextureEntry->cbFinished ) {
			input->texturePicker.inputListTextureEntry->cbFinished(
					input->texturePicker.inputListTextureEntry,
					input->texturePicker.inputListTextureEntry->pFinishedData );
		}
	}
}

/// Get the internal values from INPUT.
static void mated2TexturePickerGetValues(
		const Mated2Input* input, F32** outValues, char*** outSValues )
{
	eafSetSize( outValues, 0 );
	
	eaSetSize( outSValues, 1 );
	(*outSValues)[ 0 ] = strdup( ui_TextureEntryGetTextureName( input->texturePicker.nodeViewTextureEntry ));
}

/// Callback for when the color picker's value changes.
static void mated2TexturePickerValueChanged(
		UITextureEntry* textureEntry, Mated2Input* input )
{
	Mated2Node* node = input->node;
	Mated2EditorDoc* doc = mated2InputDoc( input );
	
	if( !mated2IsLoading( doc )) {
		ShaderOperationSpecificValue* specificInput = materialFindOperationSpecificValue2( mated2NodeShaderOpValues( node ), input->inputName );
		const char* oldSValue;
		const char* newSValue;
		bool isChanged;

		if (!specificInput)
		{
			specificInput = mated2NodeAddOperationSpecificValue(node, input->inputName );
			if (!eaSize(&specificInput->svalues))
				eaPush(&specificInput->svalues, NULL);
		}

		oldSValue = specificInput->svalues[ 0 ];
		newSValue = allocAddString( ui_TextureEntryGetTextureName( textureEntry ));
		isChanged = (specificInput->svalues[ 0 ] != newSValue);

		assert( eafSize( &specificInput->fvalues ) == 0 );
		assert( eaSize( &specificInput->svalues ) == 1 );

		specificInput->svalues[ 0 ] = newSValue;

		// record undo action...
		if( isChanged ) {
			Mated2InputSetValuesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeName( node ));
			accum->inputName = strdup( mated2InputName( input ));
			eaPush( &accum->oldSValues, strdup( oldSValue ));
			eaPush( &accum->newSValues, strdup( newSValue ));

			mated2UndoRecord( doc, mated2InputSetValuesActionUndo,
							  mated2InputSetValuesActionRedo, mated2InputSetValuesActionFree,
							  accum );
		}
		input->setValuesFn( input, NULL, specificInput->svalues, true );

		mated2SetDirty( doc );
	}
}

/// Callback for when the color picker's value changes, but it should not be commited.
static void mated2TexturePickerValueChangedPartial(
		UITextureEntry* textureEntry, Mated2Input* input )
{
	Mated2Node* node = input->node;
	Mated2EditorDoc* doc = mated2InputDoc( input );
	
	if( !mated2IsLoading( doc )) {
		const char** svalues = NULL;
		eaPush( &svalues, ui_TextureEntryGetTextureName( textureEntry ));
		input->setValuesFn( input, NULL, svalues, true );
		eaDestroy( &svalues );

		mated2SetDirty( doc );
	}
}

static void mated2TexturePickerHidden( SA_PARAM_NN_VALID Mated2Input* input )
{
}

static bool mated2TexturePickerPicked(
		EMPicker* picker, EMPickerSelection** selections,
		UITextureEntry* textureEntry)
{
	char buffer[ 256 ];
	
	if( !eaSize( &selections )) {
		return false;
	}

	ui_TextureEntrySetTextureNameAndCallback( textureEntry, getFileNameNoExt( buffer, selections[ 0 ]->doc_name ));
	if( textureEntry->cbFinished ) {
		textureEntry->cbFinished( textureEntry, textureEntry->pFinishedData );
	}

	return true;
}

static void mated2TexturePickerCustomChoose(
		UITextureEntry* textureEntry, Mated2Input* input )
{
	EMPicker* texturePicker = emPickerGetByName( "Texture Picker" );
	if( !texturePicker ) {
		return;
	}
	
	emPickerShow( texturePicker, "Select", false, (EMPickerCallback)mated2TexturePickerPicked, textureEntry );
}

/// Create a single float picker
Mated2Input* mated2FloatPickerCreate(
		Mated2Node* node, const char* name, const char* tooltip, bool allowLock,
		int numFloats, Vec2 floatRange )
{
	Mated2Input* accum = calloc( 1, sizeof( *accum ));

	// create the node view...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int xIt = 0;
		int yIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->nodeViewPane = root;

		if( allowLock ) {
			UICheckButton* checkButton = ui_CheckButtonCreate( xIt, yIt, "", false );
			ui_WidgetSetTooltipString( UI_WIDGET( checkButton ), "Check to lock this value per-material template" );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( checkButton ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( checkButton )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( checkButton )) + 2;
			accum->nodeViewLockButton = checkButton;
			ui_CheckButtonSetToggledCallback( checkButton, mated2InputLockChanged, accum );
		} else {
			xIt += ui_CheckButtonWidthNoText() + 2;
		}
	
		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );
		
			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			int it;
			for( it = 0; it != numFloats; ++it ) {
				UISliderTextEntry* slider = ui_SliderTextEntryCreate( "0", floatRange[ 0 ], floatRange[ 1 ], xIt, yIt, 230 );
				ui_SliderTextEntrySetSelectOnFocus( slider, true );
				ui_SliderTextEntrySetIsOutOfRangeAllowed( slider, true );
				ui_SliderTextEntrySetPolicy( slider, UISliderContinuous );
				if( xIt > 75 ) {
					xIt = 75;
					yIt = yMaxAccum + 2;
				} else {
					xIt = 75;
				}
				if( floatRange[ 0 ] != floatRange[ 1 ]) {
					slider->pSlider->min = floatRange[ 0 ];
					slider->pSlider->max = floatRange[ 1 ];
				} else {
					slider->pSlider->min = 0;
					slider->pSlider->max = 1;
				}
				ui_WidgetSetPosition( UI_WIDGET( slider ), xIt, yIt );

				ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( slider ));
				MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( slider )));
				xIt += ui_WidgetGetWidth( UI_WIDGET( slider )) + 2;

				accum->floatPicker.nodeViewSliders[ it ] = slider;
				ui_SliderTextEntrySetChangedCallback( slider, mated2FloatPickerValueChanged, accum );
			}
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}

	// create the input list widgets...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int xIt = 0;
		int yIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->inputListPane = root;

		{
			UILabel* label = ui_LabelCreate( name, xIt, yIt );
			ui_WidgetSetTooltipString( UI_WIDGET( label ), tooltip );
		
			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		if( xIt > 75 ) {
			xIt = 75;
			yIt = yMaxAccum + 2;
		} else {
			xIt = 75;
		}

		{
			int it;
			for( it = 0; it != numFloats; ++it ) {
				UISliderTextEntry* slider = ui_SliderTextEntryCreate( "0", floatRange[ 0 ], floatRange[ 1 ], xIt, yIt, 230 );
				ui_SliderTextEntrySetSelectOnFocus( slider, true );
				ui_SliderTextEntrySetIsOutOfRangeAllowed( slider, true );
				ui_SliderTextEntrySetPolicy( slider, UISliderContinuous );
				if( xIt > 75 ) {
					xIt = 75;
					yIt = yMaxAccum + 2;
				} else {
					xIt = 75;
				}
				if( floatRange[ 0 ] != floatRange[ 1 ]) {
					slider->pSlider->min = floatRange[ 0 ];
					slider->pSlider->max = floatRange[ 1 ];
				} else {
					slider->pSlider->min = 0;
					slider->pSlider->max = 1;
				}
				ui_WidgetSetPosition( UI_WIDGET( slider ), xIt, yIt );

				ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( slider ));
				MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( slider )));
				xIt += ui_WidgetGetWidth( UI_WIDGET( slider )) + 2;

				accum->floatPicker.inputListSliders[ it ] = slider;
				ui_SliderTextEntrySetChangedCallback( slider, mated2FloatPickerValueChanged, accum );
			}
		}
		
		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
	}
	
	accum->inputName = allocAddString( name );
	accum->node = node;
	accum->setValuesFn = mated2FloatPickerSetValues;
	accum->getValuesFn = mated2FloatPickerGetValues;
	accum->hiddenFn = mated2FloatPickerHidden;
	return accum;
}

/// Set the internal values for a float picker
static void mated2FloatPickerSetValues(
		Mated2Input* input, const F32* values, const char** svalues, bool isRecursive )
{
	int it;
	for( it = 0; it != ARRAY_SIZE( input->floatPicker.nodeViewSliders ); ++it ) {
		UISliderTextEntry* nodeViewTextEntry = input->floatPicker.nodeViewSliders[ it ];
		UISliderTextEntry* inputListTextEntry = input->floatPicker.inputListSliders[ it ];

		assert( (nodeViewTextEntry == NULL) == (inputListTextEntry == NULL) );
		if( nodeViewTextEntry ) {
			if( !isRecursive ) {
				ui_SliderTextEntrySetValueAndCallback( nodeViewTextEntry, values[ it ]);
				ui_SliderTextEntrySetValueAndCallback( inputListTextEntry, values[ it ]);
			} else {
				ui_SliderTextEntrySetValue( nodeViewTextEntry, values[ it ]);
				ui_SliderTextEntrySetValue( inputListTextEntry, values[ it ]);
			}
		}
	}
}

/// Get the internal values from INPUT.
static void mated2FloatPickerGetValues(
		const Mated2Input* input, F32** outValues, char*** outSValues )
{
	eafSetSize( outValues, 0 );
	{
		int it;
		for( it = 0; it != ARRAY_SIZE( input->floatPicker.nodeViewSliders ); ++it ) {
			UISliderTextEntry* textEntry = input->floatPicker.nodeViewSliders[ it ];

			if( textEntry ) {
				eafPush( outValues, ui_SliderTextEntryGetValue( textEntry ));
			}
		}
	}

	eaSetSize( outSValues, 0 );
}

/// Callback for when the color picker's value changes.
static void mated2FloatPickerValueChanged(
		UISliderTextEntry* slider, bool bFinished, Mated2Input* input )
{
	Mated2Node* node = input->node;
	Mated2EditorDoc* doc = mated2InputDoc( input );
	bool isReleaseEvent = !mouseIsDown( MS_LEFT );
	
	if( !mated2IsLoading( doc )) {
		F32* oldValues = NULL;
		F32* values;
		
		if( input->nodeViewLockButton && ui_CheckButtonGetState( input->nodeViewLockButton )) {
			ShaderFixedInput* fixedInput = materialFindFixedInputByName( mated2NodeShaderOp( node ), input->inputName );
			int it;

			for( it = 0; it != ARRAY_SIZE( input->floatPicker.nodeViewSliders ); ++it ) {
				if(   input->floatPicker.nodeViewSliders[ it ] == slider
					  || input->floatPicker.inputListSliders[ it ] == slider ) {
					break;
				}
			}
			assert( it < ARRAY_SIZE( input->floatPicker.nodeViewSliders ));
			eafCopy( &oldValues, &fixedInput->fvalues );
			fixedInput->fvalues[ it ] = ui_SliderTextEntryGetValue( slider );
			values = fixedInput->fvalues;
		} else {
			ShaderOperationSpecificValue* specificInput = materialFindOperationSpecificValue2( mated2NodeShaderOpValues( node ), input->inputName );
			int it;

			for( it = 0; it != ARRAY_SIZE( input->floatPicker.nodeViewSliders ); ++it ) {
				if(   input->floatPicker.nodeViewSliders[ it ] == slider
					  || input->floatPicker.inputListSliders[ it ] == slider ) {
					break;
				}
			}
			assert( it < ARRAY_SIZE( input->floatPicker.nodeViewSliders ));

			assert( eafSize( &specificInput->fvalues ) > it );
			assert( eaSize( &specificInput->svalues ) == 0 );
			eafCopy( &oldValues, &specificInput->fvalues );
			specificInput->fvalues[ it ] = ui_SliderTextEntryGetValue( slider );
			values = specificInput->fvalues;
		}

		if( isReleaseEvent ) {
			Mated2InputSetValuesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeName( node ));
			accum->inputName = strdup( mated2InputName( input ));
			eafCopy( &accum->oldFValues, &oldValues );
			eafCopy( &accum->newFValues, &values );

			mated2UndoRecord( doc, mated2InputSetValuesActionUndo,
							  mated2InputSetValuesActionRedo, mated2InputSetValuesActionFree,
							  accum );
		}
		input->setValuesFn( input, values, NULL, true );

		mated2SetDirty( doc );
	}
}

static void mated2FloatPickerHidden( SA_PARAM_NN_VALID Mated2Input* input )
{
}

/// Create a picker for an option that is not yet supported
Mated2Input* mated2UnsupportedInputCreate( Mated2Node* node, const char* name )
{
	Mated2Input* accum = calloc( 1, sizeof( *accum ));
	char buffer[ 128 ];
						
	sprintf( buffer, "Unsupported Input: %s", name );

	// create the node view widgets...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int yIt = 0;
		int xIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->nodeViewPane = root;

		{
			UILabel* label = ui_LabelCreate( buffer, xIt, yIt );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
		accum->nodeViewLockButton = NULL;
	}

	// create the input list widgets...
	{
		UIPane* root = ui_PaneCreate( 0, 0, 1, 1, UIUnitPercentage, UIUnitFixed, 0 );
		int yIt = 0;
		int xIt = 0;
		int yMaxAccum = 0;

		root->invisible = true;
		accum->inputListPane = root;

		{
			UILabel* label = ui_LabelCreate( buffer, xIt, yIt );

			ui_WidgetAddChild( UI_WIDGET( root ), UI_WIDGET( label ));
			MAX1( yMaxAccum, yIt + ui_WidgetGetHeight( UI_WIDGET( label )));
			xIt += ui_WidgetGetWidth( UI_WIDGET( label )) + 2;
		}

		ui_WidgetSetHeight( UI_WIDGET( root ), yMaxAccum );
		accum->inputListPane = root;
	}
	
	accum->inputName = allocAddString( name );
	accum->node = node;
	accum->setValuesFn = mated2UnsupportedInputSetValues;
	accum->getValuesFn = mated2UnsupportedInputGetValues;
	accum->hiddenFn = mated2UnsupportedInputHidden;
	return accum;
}

/// Set the internal values for an unsupported input -- always failed.
static void mated2UnsupportedInputSetValues(
		Mated2Input* input, const F32* values, const char** svalues, bool isRecursive )
{
	assertmsg( false, "Programmer data error: Trying to set the values for an unsupported input type." );
}

/// Get the internal values an unsupported input.
static void mated2UnsupportedInputGetValues(
		const Mated2Input* input, F32** outValues, char*** outSValues )
{
	eafSetSize( outValues, 0 );
	eaSetSize( outSValues, 0 );
}

static void mated2UnsupportedInputHidden( Mated2Input* input )
{
}

/// Free INPUT.
void mated2InputFree( Mated2Input* input )
{
	ui_WidgetForceQueueFree( UI_WIDGET( input->nodeViewPane ));
	ui_WidgetForceQueueFree( UI_WIDGET( input->inputListPane ));
	free( input );
}

/// Return if INPUT's value is locked to the template.  If not locked,
/// then each material instance will have its own unique value for
/// this input.
///
/// Textures can never be locked to the template.
bool mated2InputIsLocked( const Mated2Input* input )
{
	return (input->nodeViewLockButton != NULL
			&& ui_CheckButtonGetState( input->nodeViewLockButton));
}

/// Get INPUT's root widget to be placed in the node view.
UIWidget* mated2InputNodeViewWidget( const Mated2Input* input )
{
	return UI_WIDGET( input->nodeViewPane );
}

/// Get INPUT's root widget to be placed in the input list.
UIWidget* mated2InputInputListWidget( const Mated2Input* input )
{
	return UI_WIDGET( input->inputListPane );
}

/// Return INPUT's name.
const char* mated2InputName( const Mated2Input* input )
{
	return input->inputName;
}

/// Return the doc INPUT belongs to.
Mated2EditorDoc* mated2InputDoc( const Mated2Input* input )
{
	return mated2NodeDoc( input->node );
}

/// Update INPUT's user editable state.
void mated2InputUpdateIsActive( Mated2Input* input, bool isTemplateEditable )
{
	bool isLocked = mated2InputIsLocked( input );
	
	ui_SetActive( UI_WIDGET( input->nodeViewPane ), (isTemplateEditable || !isLocked));
	ui_SetActive( UI_WIDGET( input->inputListPane ), (isTemplateEditable || !isLocked));
	if( input->nodeViewLockButton ) {
		ui_SetActive( UI_WIDGET( input->nodeViewLockButton ), isTemplateEditable );
	}
}

/// Set INPUT's values.
void mated2InputSetValues(
		Mated2Input* input, const F32* values, const char** svalues, bool isLocked )
{
	Mated2EditorDoc* doc = mated2InputDoc( input );
		
	if( input->nodeViewLockButton ) {
		ui_CheckButtonSetStateAndCallback( input->nodeViewLockButton, isLocked );
	}
	if( values != NULL || svalues != NULL ) {
		input->setValuesFn( input, values, svalues, false );
	}
}

/// Set INPUT's values to whatever they would have right after INPUT
/// was created.
void mated2InputSetValuesToDefault( Mated2Input* input )
{
	F32* defaultFValues = NULL;
	char** defaultSValues = NULL;

	eafPush( &defaultFValues, 0 );
	eafPush( &defaultFValues, 0 );
	eafPush( &defaultFValues, 0 );
	eafPush( &defaultFValues, 0 );
	eaPush( &defaultSValues, "Default" );
	eaPush( &defaultSValues, "Default" );
	eaPush( &defaultSValues, "Default" );
	eaPush( &defaultSValues, "Default" );
	
	mated2InputSetValues( input, defaultFValues, defaultSValues, false );

	eaDestroy( &defaultSValues );
	eafDestroy( &defaultFValues );
}

/// INPUT just got hidden in some way... hide anything related to it
/// as well.
void mated2InputHidden( SA_PARAM_NN_VALID Mated2Input* input )
{
	input->hiddenFn( input );
}

static void mated2InputSetValuesActionUndo(
		Mated2EditorDoc* doc, Mated2InputSetValuesAction* action )
{
	Mated2Input* input = mated2FindNodeInputByName(
			mated2FindNodeByName( doc, action->nodeName ), action->inputName );

	if( input == NULL ) {
		Alertf( "Could not find input %s for node %s.  Notify Jared of the problem!",
				action->inputName, action->nodeName );
		return;
	}
	
	input->setValuesFn( input, action->oldFValues, action->oldSValues, false );
}

static void mated2InputSetValuesActionRedo(
		Mated2EditorDoc* doc, Mated2InputSetValuesAction* action )
{
	Mated2Input* input = mated2FindNodeInputByName(
			mated2FindNodeByName( doc, action->nodeName ), action->inputName );
	
	if( input == NULL ) {
		Alertf( "Could not find input %s for node %s.  Notify Jared of the problem!",
				action->inputName, action->nodeName );
		return;
	}
	
	input->setValuesFn( input, action->newFValues, action->newSValues, false );
}

static void mated2InputSetValuesActionFree(
		Mated2EditorDoc* doc, Mated2InputSetValuesAction* action )
{
	free( action->nodeName );
	free( action->inputName );
	eafDestroy( &action->oldFValues );
	eaDestroyEx( &action->oldSValues, NULL );
	eafDestroy( &action->newFValues );
	eaDestroyEx( &action->newSValues, NULL );
	free( action );
}

static void mated2InputLockActionUndo(
		Mated2EditorDoc* doc, Mated2InputLockAction* action )
{
	Mated2Input* input = mated2FindNodeInputByName(
			mated2FindNodeByName( doc, action->nodeName ), action->inputName );

	if( input == NULL ) {
		Alertf( "Could not find input %s for node %s.  Notify Jared of the problem!",
				action->inputName, action->nodeName );
		return;
	}

	assert( input->nodeViewLockButton );
	ui_CheckButtonSetStateAndCallback( input->nodeViewLockButton, !action->isLocked );
}

static void mated2InputLockActionRedo(
		Mated2EditorDoc* doc, Mated2InputLockAction* action )
{
	Mated2Input* input = mated2FindNodeInputByName(
			mated2FindNodeByName( doc, action->nodeName ), action->inputName );

	if( input == NULL ) {
		Alertf( "Could not find input %s for node %s.  Notify Jared of the problem!",
				action->inputName, action->nodeName );
		return;
	}

	assert( input->nodeViewLockButton );
	ui_CheckButtonSetStateAndCallback( input->nodeViewLockButton, action->isLocked );
}

static void mated2InputLockActionFree(
		Mated2EditorDoc* doc, Mated2InputLockAction* action )
{
	free( action->nodeName );
	free( action->inputName );
	free( action );
}

/// Callback for when any ol' input's locked-to-template status has
/// changed.
static void mated2InputLockChanged( UICheckButton* button, Mated2Input* input )
{
	Mated2Node* node = input->node;
	Mated2EditorDoc* doc = mated2InputDoc( input );
	ShaderOperation* shaderOp = mated2NodeShaderOp( node );
	ShaderOperationValues* shaderOpValues = mated2NodeShaderOpValues( node );
	bool isLocked = ui_CheckButtonGetState( input->nodeViewLockButton );

	if( isLocked ) {
		if( input->nodeViewPane ) { input->nodeViewPane->widget.drawF = mated2InputPaneDrawLocked; };
	} else {
		if( input->nodeViewPane ) { input->nodeViewPane->widget.drawF = mated2InputPaneDrawUnlocked; }
	}
	
	if( !mated2IsLoading( doc )) {
		if( isLocked ) {
			ShaderFixedInput* fixedInput = materialFindFixedInputByName( shaderOp, input->inputName );
			ShaderOperationSpecificValue* specificInput;

			assert( shaderOpValues );
			specificInput = materialFindOperationSpecificValue2( shaderOpValues, input->inputName );
			assert( !fixedInput && specificInput );
			fixedInput = StructCreate( parse_ShaderFixedInput );
			fixedInput->input_name = specificInput->input_name;
			fixedInput->fvalues = specificInput->fvalues;

			specificInput->input_name = NULL;
			specificInput->fvalues = NULL;
			assert( specificInput->svalues == NULL );
			
			eaPush( &shaderOp->fixed_inputs, fixedInput );
			eaFindAndRemove( &shaderOpValues->values, specificInput );
			StructDestroy( parse_ShaderOperationSpecificValue, specificInput );
		} else {		
			ShaderFixedInput* fixedInput = materialFindFixedInputByName( shaderOp, input->inputName );
			ShaderOperationSpecificValue* specificInput = materialFindOperationSpecificValue2( shaderOpValues, input->inputName );

			assert( fixedInput && !specificInput );
			specificInput = StructCreate( parse_ShaderOperationSpecificValue );
			specificInput->input_name = fixedInput->input_name;
			specificInput->fvalues = fixedInput->fvalues;
			specificInput->svalues = NULL;

			fixedInput->input_name = NULL;
			fixedInput->fvalues = NULL;

			if( !shaderOpValues ) {
				shaderOpValues = StructCreate( parse_ShaderOperationValues );
				shaderOpValues->op_name = allocAddString( mated2NodeName( node ));
				eaPush( &mated2DocActiveFallback( doc )->shader_values, shaderOpValues );
			}

			eaPush( &shaderOpValues->values, specificInput );
			eaRemove( &shaderOp->fixed_inputs, eaFind( &shaderOp->fixed_inputs, fixedInput ));
			StructDestroy( parse_ShaderFixedInput, fixedInput );
		}

		// record the undo action...
		{
			Mated2InputLockAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeName( node ));
			accum->inputName = strdup( mated2InputName( input ));
			accum->isLocked = isLocked;

			mated2UndoRecord( doc, mated2InputLockActionUndo,
							  mated2InputLockActionRedo, mated2InputLockActionFree,
							  accum );
		}

		mated2SetDirty( doc );
	}
}

/// Draw a Mated2Input when its value is locked to the template.
static void mated2InputPaneDrawLocked( UIPane* pane, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( pane );
	
	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0x00000030 );
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

/// Draw a Mated2Input when its value is not locked to the template.
static void mated2InputPaneDrawUnlocked( UIPane* pane, UI_PARENT_ARGS )
{
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

/// A set of tests for debugging.  Feel free to add more validity
/// tests to this function.
void mated2InputAssertValid( const Mated2Input* input )
{
	if( input->inputListPane->widget.group )
		assert( (unsigned)((uintptr_t)*input->inputListPane->widget.group & 0xFFFFFFFF) != 0xFDFDFDFD ); /* I hate you, 64 bits. */
}

#endif
