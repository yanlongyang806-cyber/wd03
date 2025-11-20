#ifndef NO_EDITORS

#include"MaterialEditor2EMNode.h"

#include"GfxMaterials.h"
#include"GfxLightOptions.h"
#include"MaterialEditor2EM.h"
#include"MaterialEditor2EMInput.h"
#include"Materials.h"
#include"ResourceInfo.h"
#include"StringCache.h"
#include"UICheckButton.h"
#include"UIComboBox.h"
#include"UIFlowchart.h"
#include"UILabel.h"
#include"UIPane.h"
#include"UIRectangularSelection.h"
#include"UISkin.h"
#include"UITextEntry.h"
#include"error.h"
#include"Color.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#endif
AUTO_STRUCT;
typedef struct Mated2NodeOption {
	char* name;
	char* nodeName;
} Mated2NodeOption;
#ifndef NO_EDITORS

/// A material editor node.	 This holds all the widgets that a
/// material editor node *could* display, including ones that are
/// hidden.
typedef struct Mated2Node {
	Mated2EditorDoc* doc;

	/// The node's name
	const char* name;

	/// If preview is active for this node
	bool isPreview;
	
	/// The node widget
	UIFlowchartNode* uiNode;

	/// The node's name widget
	UITextEntry* nameEntry;

	/// The node's group name widget
	UITextEntry* groupEntry;

	/// The node's notes widget
	UITextEntry* notesEntry;

	/// The node's instancing widget
	UICheckButton* instancedCheckButton;

	/// The inherit buttons
	UICheckButton* nodeViewInheritButton;
	UICheckButton* inputListInheritButton;

	/// The inheritting name.
	UITextEntry* nodeViewInheritNameText;
	UITextEntry* inputListInheritNameText;

	/// A list of all names to potentially use for INHERIT-NAME-TEXT.
	const char** inheritNameModel;
	
	/// If true, the widget's display state may have changed.
	bool needsReflow;

	/// Contains an input widget for the node view and an input widget
	/// for the input list.
	///
	/// The node view widgets may end up getting hidden.
	Mated2Input** inputs;

	/// Widgets that should always be visible. 
	UIWidget** otherWidgets;

	/// all the option nodes that could be here.
	Mated2NodeOption** options;
} Mated2Node;

/// A material editor guide.  This is a box that appears onscreen for
/// organizational purposes.
typedef struct Mated2Guide {
	Mated2EditorDoc* doc;
	ShaderGuide* shaderGuide;
	UIRectangularSelection* uiGuide;
} Mated2Guide;

typedef struct Mated2NodeSetNameAction {
	char* oldNodeName;
	char* newNodeName;
} Mated2NodeSetNameAction;
static void mated2NodeSetNameActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNameAction* action );
static void mated2NodeSetNameActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNameAction* action );
static void mated2NodeSetNameActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNameAction* action );

typedef struct Mated2NodeSetGroupAction {
	char* nodeName;
	char* oldGroupName;
	char* newGroupName;
} Mated2NodeSetGroupAction;
static void mated2NodeSetGroupActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetGroupAction* action );
static void mated2NodeSetGroupActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetGroupAction* action );
static void mated2NodeSetGroupActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetGroupAction* action );

typedef struct Mated2NodeSetNotesAction {
	char* nodeName;
	char* oldNotes;
	char* newNotes;
} Mated2NodeSetNotesAction;
static void mated2NodeSetNotesActionUndo(
	SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNotesAction* action );
static void mated2NodeSetNotesActionRedo(
	SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNotesAction* action );
static void mated2NodeSetNotesActionFree(
	SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetNotesAction* action );

typedef struct Mated2NodeSetInheritOpNameAction {
	char* nodeName;
	char* oldInheritName;
	char* newInheritName;
} Mated2NodeSetInheritOpNameAction;
static void mated2NodeSetInheritOpNameActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritOpNameAction* action );
static void mated2NodeSetInheritOpNameActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritOpNameAction* action );
static void mated2NodeSetInheritOpNameActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritOpNameAction* action );

typedef struct Mated2NodeSetInheritValuesAction {
	char* nodeName;
	bool inheritValues;
	char* inheritName;
} Mated2NodeSetInheritValuesAction;
static void mated2NodeSetInheritValuesActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritValuesAction* action );
static void mated2NodeSetInheritValuesActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritValuesAction* action );
static void mated2NodeSetInheritValuesActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetInheritValuesAction* action );

typedef struct Mated2NodeSetShadedAction {
	char* nodeName;
	bool isShaded;
} Mated2NodeSetShadedAction;
static void mated2NodeSetShadedActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetShadedAction* action );
static void mated2NodeSetShadedActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetShadedAction* action );
static void mated2NodeSetShadedActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2NodeSetShadedAction* action );

typedef struct Mated2FlowchartLinkAction {
	char* sourceNodeName;
	char* sourceName;
	char* destNodeName;
	char* destName;
	U8 swizzle[4];	
} Mated2FlowchartLinkAction;
static void mated2FlowchartLinkActionLink(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2FlowchartLinkAction* action );
static void mated2FlowchartLinkActionUnlink(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2FlowchartLinkAction* action );
static void mated2FlowchartLinkActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2FlowchartLinkAction* action );

typedef struct Mated2GuideCreateAction {
	ShaderGuide* guide;
	bool isCreated;
} Mated2GuideCreateAction;
static void mated2GuideCreateActionCreate(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideCreateAction* action );
static void mated2GuideCreateActionRemove(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideCreateAction* action );
static void mated2GuideCreateActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideCreateAction* action );

typedef struct Mated2GuideResizeAction {
	ShaderGuide* guide;
	Vec4 oldExtents;
	Vec4 newExtents;
} Mated2GuideResizeAction;
static void mated2GuideResizeActionUndo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideResizeAction* action );
static void mated2GuideResizeActionRedo(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideResizeAction* action );
static void mated2GuideResizeActionFree(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2GuideResizeAction* action );

static bool mated2NodeValuesCanInherit( SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeInheritOpNameChanged(
		SA_PARAM_NN_VALID UITextEntry* textEntry, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeInheritValuesChanged(
		SA_PARAM_NN_VALID UICheckButton* checkButton, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2ChangeOptionBySelection(
		SA_PARAM_NN_VALID UIComboBox* comboBox, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeNameChanged(
		SA_PARAM_NN_VALID UITextEntry* textEntry, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeGroupChanged(
		SA_PARAM_NN_VALID UITextEntry* textEntry, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeNotesChanged(
	SA_PARAM_NN_VALID UITextEntry* textEntry, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2NodeInstancedChanged(
		SA_PARAM_NN_VALID UICheckButton* checkButton, SA_PARAM_NN_VALID Mated2Node* node );
static void mated2FlowchartNodeTick( SA_PARAM_NN_VALID UIFlowchartNode* pNode, UI_PARENT_ARGS );
static void mated2FlowchartNodeFree( SA_PARAM_NN_VALID UIFlowchartNode* pNode );
static void mated2SetEdgeFlowchartLink(
		SA_PRE_NN_FREE SA_POST_NN_VALID ShaderInputEdge* edge, SA_PARAM_NN_VALID UIFlowchartButton* source,
		SA_PARAM_NN_VALID UIFlowchartButton* dest );
static void mated2FlowchartNodeClicked(
		SA_PARAM_NN_VALID UIWindow* uiNode, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2FlowchartNodeShaded(
		SA_PARAM_NN_VALID UIWindow* uiNode, SA_PARAM_NN_VALID Mated2EditorDoc* doc );
static void mated2NodeSwapButtons(
		SA_PARAM_NN_VALID UIFlowchartButton** buttons1, SA_PARAM_NN_VALID UIFlowchartButton** buttons2 );
static void mated2GuideResized(
		SA_PARAM_NN_VALID UIRectangularSelection* rect, SA_PARAM_NN_VALID Mated2Guide* guide );
static void mated2GuideRemove(
		SA_PARAM_NN_VALID Mated2EditorDoc* doc, SA_PARAM_NN_VALID Mated2Guide* guide );
static void mated2GuideRemoved(
		SA_PARAM_NN_VALID UIRectangularSelection* rect, SA_PARAM_NN_VALID Mated2Guide* guide );

/// Create a new Mated2Node that represents the ShaderOpDef named by
/// NAME, and insert it into the flowchart.
Mated2Node* mated2NewNodeByOpName( Mated2EditorDoc* doc, const char* name )
{
	char buffer[ 128 ];

	ShaderOperationDef* opDef = RefSystem_ReferentFromString( g_hOpDefsDict, name );
	if( !opDef ) {
		return NULL;
	}
	
	return mated2NewNodeByOpAndName( doc, opDef,
									 mated2UniqueNodeName( doc, name, buffer, sizeof( buffer )));
}

/// Create a new Mated2Node that represents the ShaderOp, and insert
/// that into the flowchart.
Mated2Node* mated2NewNodeByOpAndName( Mated2EditorDoc* doc, const ShaderOperationDef* op, const char* name )
{
	const char* optionRootName = op ? (op->op_parent_type_name ? op->op_parent_type_name : op->op_type_name) : NULL;
	Mated2Node* accum = calloc( 1, sizeof( *accum ));

	UIFlowchartButton** inAccum = NULL;
	UIFlowchartButton** outAccum = NULL;
	int inAccumSize;
	char title[ 256 ];
	
	accum->name = allocAddString( name );
	accum->doc = doc;

	if( !op ) {
		free( accum );
		return NULL;
	}

	sprintf( title, "%s - %s", name, mated2TranslateOpName( optionRootName ));

	{
		int it;
		for( it = 0; it != eaSize( &op->op_inputs ); ++it ) {
			ShaderInput* input = op->op_inputs[ it ];

			if(	  input->num_floats == 0 || input->input_hidden
				  || input->input_not_for_assembler
				  || input->input_no_manual_connect ) {
				continue;
			}
			assertmsg( eaSize( &inAccum ) == it, "Programmer data error: All inputs useable by the flowchart must be the first inputs defined in the .op file." );

			{
				UIFlowchartButton* inputButton = ui_FlowchartButtonCreate(
						mated2Flowchart( doc ), UIFlowchartNormal,
						ui_LabelCreate( input->input_name, 0, 0 ),
						(char*)input->input_name );
				ui_WidgetSetTooltipString( UI_WIDGET( inputButton ), input->input_description );

				ui_FlowchartButtonSetSingleConnection( inputButton, true );
				eaPush( &inAccum, inputButton );
			}
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &op->op_outputs ); ++it ) {
			ShaderOutput* output = op->op_outputs[ it ];

			UIFlowchartButton* outputButton = ui_FlowchartButtonCreate(
					mated2Flowchart( doc ), UIFlowchartNormal,
					ui_LabelCreate( output->output_name, 0, 0 ),
					(char*)output->output_name );
			ui_WidgetSetTooltipString( UI_WIDGET( outputButton ), output->output_description );
			eaPush( &outAccum, outputButton );

			if( output->num_floats > 1 ) {
				int floatIt;
				for( floatIt = 0; floatIt != output->num_floats; ++floatIt ) {
					char nameBuffer[ 128 ];
					UIFlowchartButton* button;
					const char* floatName;

					switch( output->data_type ) {
						case SDT_TEXCOORD:
							floatName = "UVST";

						xdefault:
							floatName = "RGBA";
					}

					sprintf( nameBuffer, "%s:%c", output->output_name, floatName[ floatIt ]);
					button = ui_FlowchartButtonCreate( mated2Flowchart( doc ), UIFlowchartIsChild,
													   ui_LabelCreate( nameBuffer, 0, 0 ),
													   (char*)output->output_name );
					eaPush( &outAccum, button );
				}
			}
		}
	}

	inAccumSize = eaSize( &inAccum );
	accum->uiNode = ui_FlowchartNodeCreate( title, 0, 0, 300, 100, &inAccum, &outAccum, accum );
	accum->uiNode->widget.tickF = mated2FlowchartNodeTick;
	ui_WindowSetClickedCallback( UI_WINDOW( accum->uiNode ), (UIActivationFunc)mated2FlowchartNodeClicked, doc );
	ui_WindowSetShadedCallback( UI_WINDOW( accum->uiNode ), mated2FlowchartNodeShaded, doc );
	ui_WidgetSkin( UI_WIDGET( accum->uiNode ), mated2NodeSkin( doc, op->op_type, false, false ));
	ui_WidgetSetFreeCallback( UI_WIDGET( accum->uiNode ), mated2FlowchartNodeFree );
	ui_WindowSetResizable( UI_WINDOW( accum->uiNode ), false );
	ui_FlowchartNodeSetAutoResize( accum->uiNode, true );

	{
		int xIt = 0;
		int yIt = 0;
		{
			UITextEntry* textEntry = ui_TextEntryCreate( name, xIt, yIt );
			ui_TextEntrySetSelectOnFocus( textEntry, true );
			ui_WidgetSetWidthEx( UI_WIDGET( textEntry ), 1, UIUnitPercentage );
			ui_TextEntrySetFinishedCallback( textEntry, mated2NodeNameChanged, accum );
		
			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( textEntry ), false );
			xIt = 0;
			yIt += ui_WidgetGetHeight( UI_WIDGET( textEntry ));

			accum->nameEntry = textEntry;
		}

		{
			UILabel* groupLabel = ui_LabelCreate( "Group:", xIt, yIt );
			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( groupLabel ), false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( groupLabel )) + 2;
		}

		{
			UITextEntry* textEntry = ui_TextEntryCreate( "", xIt, yIt );
			ui_TextEntrySetSelectOnFocus( textEntry, true );
			ui_WidgetSetWidthEx( UI_WIDGET( textEntry ), 1, UIUnitPercentage );
			ui_TextEntrySetFinishedCallback( textEntry, mated2NodeGroupChanged, accum );

			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( textEntry ), false );
			xIt = 0;
			yIt += ui_WidgetGetHeight( UI_WIDGET( textEntry ));

			accum->groupEntry = textEntry;
		}

		{
			UILabel* notesLabel = ui_LabelCreate( "Notes:", xIt, yIt );
			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( notesLabel ), false );
			xIt += ui_WidgetGetWidth( UI_WIDGET( notesLabel )) + 2;
		}

		{
			UITextEntry* textEntry = ui_TextEntryCreate( "", xIt, yIt );
			ui_TextEntrySetSelectOnFocus( textEntry, true );
			ui_WidgetSetWidthEx( UI_WIDGET( textEntry ), 1, UIUnitPercentage );
			ui_TextEntrySetFinishedCallback( textEntry, mated2NodeNotesChanged, accum );

			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( textEntry ), false );
			xIt = 0;
			yIt += ui_WidgetGetHeight( UI_WIDGET( textEntry ));

			accum->notesEntry = textEntry;
		}

		{
			UICheckButton* checkButton = ui_CheckButtonCreate( xIt, yIt, "Instanced", false );
			ui_WidgetSetWidthEx( UI_WIDGET( checkButton ), 1, UIUnitPercentage );
			ui_CheckButtonSetToggledCallback( checkButton, mated2NodeInstancedChanged, accum );

			ui_FlowchartNodeAddChild( accum->uiNode, UI_WIDGET( checkButton ), false );
			xIt = 0;
			yIt += ui_WidgetGetHeight( UI_WIDGET( checkButton ));

			accum->instancedCheckButton = checkButton;
		}
		
		ui_WidgetSetHeight( UI_WIDGET( accum->uiNode->beforePane ), yIt );
	}
	
	if( mated2DocActiveFallback( doc ) != &mated2DocGfxProperties( doc )->default_fallback ) {
		accum->inheritNameModel = mated2NodeNamesUsingOpDefInDefault( doc, op );

		// node view inherit button
		accum->nodeViewInheritButton = ui_CheckButtonCreate( 0, 0, "Inherit Values", false );
		ui_CheckButtonSetToggledCallback( accum->nodeViewInheritButton, mated2NodeInheritValuesChanged, accum );
		
		accum->nodeViewInheritNameText = ui_TextEntryCreate( "", 0, 0 );
		ui_TextEntrySetFinishedCallback( accum->nodeViewInheritNameText, mated2NodeInheritOpNameChanged, accum );
		ui_TextEntrySetEnterCallback( accum->nodeViewInheritNameText, mated2NodeInheritOpNameChanged, accum );
		accum->nodeViewInheritNameText->comboFinishOnSelect = true;
		accum->nodeViewInheritNameText->autoComplete = true;
		
		ui_TextEntrySetComboBox( accum->nodeViewInheritNameText, ui_ComboBoxCreate( 0, 0, 0, NULL, &accum->inheritNameModel, NULL ));
		ui_WidgetSetWidthEx( UI_WIDGET( accum->nodeViewInheritNameText ), 1, UIUnitPercentage );

		// input list inherit button
		accum->inputListInheritButton = ui_CheckButtonCreate( 0, 0, "Inherit Values", false );
		ui_CheckButtonSetToggledCallback( accum->inputListInheritButton, mated2NodeInheritValuesChanged, accum );

		accum->inputListInheritNameText = ui_TextEntryCreate( "", 0, 0 );
		ui_TextEntrySetFinishedCallback( accum->inputListInheritNameText, mated2NodeInheritOpNameChanged, accum );
		ui_TextEntrySetEnterCallback( accum->inputListInheritNameText, mated2NodeInheritOpNameChanged, accum );
		accum->inputListInheritNameText->comboFinishOnSelect = true;
		accum->inputListInheritNameText->autoComplete = true;

		ui_TextEntrySetComboBox( accum->inputListInheritNameText, ui_ComboBoxCreate( 0, 0, 0, NULL, &accum->inheritNameModel, NULL ));
		ui_WidgetSetWidthEx( UI_WIDGET( accum->inputListInheritNameText ), 1, UIUnitPercentage );
	}

	{
		int it;
		for( it = 0; it != eaSize( &op->op_inputs ); ++it ) {
			ShaderInput* input = op->op_inputs[ it ];
			Mated2Input* mated2InputAccum;

			assertmsg( (input->num_texnames == 0) != (input->num_floats == 0), "Programmer data error: An input can not have both a texture and floats." );

			if(   (input->input_default.default_type == SIDT_NODEFAULT
				   || input->input_not_for_assembler)
				  && !input->input_hidden
				  && !(input->data_type == SDT_TEXTURE_DIFFUSEWARP && !gfx_lighting_options.enableDiffuseWarpTex)
				  && !(input->data_type == SDT_TEXTURE_AMBIENT_CUBE))
			{
				if( input->num_floats ) {
					bool allowLock = !input->input_not_for_assembler && !shaderDataTypeNeedsMapping( input->data_type );
					switch( input->num_floats ) {
						xcase 3:
							mated2InputAccum = mated2ColorPickerCreate( accum, name, input->input_name, input->input_description,
																		allowLock, false, input->float_range );
						xcase 4:
							mated2InputAccum = mated2ColorPickerCreate( accum, name, input->input_name, input->input_description,
																		allowLock, true, input->float_range );
						xdefault:
							mated2InputAccum = mated2FloatPickerCreate( accum, input->input_name, input->input_description,
																		allowLock, input->num_floats, input->float_range );
					}
				} else {
					switch( input->num_texnames ) {
						case 1:
							mated2InputAccum = mated2TexturePickerCreate( accum, input->input_name, input->input_description, false, eaGet(&input->input_default.default_strings,0) );
						xdefault:
							mated2InputAccum = mated2UnsupportedInputCreate( accum, input->input_name );
					}
				}

				if( input->input_description && input->input_description[ 0 ]) {
					ui_WidgetSetTooltipString( mated2InputNodeViewWidget( mated2InputAccum ),
										 input->input_description );
					ui_WidgetSetTooltipString( mated2InputInputListWidget( mated2InputAccum ),
										 input->input_description );
				}
			} else {
				mated2InputAccum = NULL;
			}

			eaPush( &accum->inputs, mated2InputAccum );
		}

		{
			const ShaderOperationDef** opDefs = (const ShaderOperationDef**)resDictGetEArrayStruct( g_hOpDefsDict )->ppReferents;

			int defaultOption = -1;
			int opIt;
			for( opIt = 0; opIt != eaSize( &opDefs ); ++opIt ) {
				const ShaderOperationDef* opDef = opDefs[ opIt ];
				const char* optionRootNameIt = (opDef->op_parent_type_name ? opDef->op_parent_type_name : opDef->op_type_name);

				if( stricmp( optionRootName, optionRootNameIt ) == 0 ) {
					Mated2NodeOption* optionAccum = StructCreate( parse_Mated2NodeOption );
					optionAccum->name = strdup( opDef->op_option_name );
					optionAccum->nodeName = strdup( opDef->op_type_name );
					eaPush( &accum->options, optionAccum );

					if( opDef->op_type_name == op->op_type_name ) {
						defaultOption = eaSize( &accum->options ) - 1;
					}
				}
			}

			if( eaSize( &accum->options ) > 1 ) {
				UIComboBox* comboBox = ui_ComboBoxCreate( 0, 0, 100, parse_Mated2NodeOption, &accum->options, "name" );
				ui_WidgetSetWidthEx( UI_WIDGET( comboBox ), 1, UIUnitPercentage );
				ui_ComboBoxSetSelected( comboBox, defaultOption );
				ui_ComboBoxSetSelectedCallback( comboBox, mated2ChangeOptionBySelection, accum );

				eaPush( &accum->otherWidgets, UI_WIDGET( comboBox ));
			}
		}
	}

	accum->needsReflow = true;
	ui_FlowchartAddNode( mated2Flowchart( doc ), accum->uiNode );

	// If the document is in the process of being loaded, then this
	// data already exists, and will be linked up to it.
	if( !mated2IsLoading( doc )) {
		ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
		MaterialFallback* fallback = mated2DocActiveFallback( doc );
		MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
		ShaderOperation* opAccum = StructCreate( parse_ShaderOperation );
		ShaderOperationValues* valueAccum = StructCreate( parse_ShaderOperationValues );
		

		// Add the op
		opAccum->op_name = allocAddString( name );
		SET_HANDLE_FROM_STRING( g_hOpDefsDict, op->op_type_name, opAccum->h_op_definition );
		opAccum->inputs = NULL;
		opAccum->fixed_inputs = NULL;
		setVec2( opAccum->op_pos,
				 ui_WidgetGetX( UI_WIDGET( accum->uiNode )),
				 ui_WidgetGetY( UI_WIDGET( accum->uiNode )));
		opAccum->op_collapsed = false;
		
		opAccum->op_editor_data = accum;
		eaPush( &graph->operations, opAccum );

		// Add the values
		valueAccum->op_name = allocAddString( name );
		valueAccum->values = NULL;
		{
			int it;
			for( it = 0; it != eaSize( &accum->inputs ); ++it ) {
				ShaderInput* input = op->op_inputs[ it ];
				Mated2Input* mated2Input = accum->inputs[ it ];
				ShaderOperationSpecificValue* inputValueAccum;

				if( !mated2Input ) {
					continue;
				}

				inputValueAccum = StructCreate( parse_ShaderOperationSpecificValue );
				inputValueAccum->input_name = input->input_name;
				eafSetSize( &inputValueAccum->fvalues, input->num_floats );
				{
					int sIt;
					for( sIt = 0; sIt != input->num_texnames; ++sIt ) {
						eaPush( &inputValueAccum->svalues, "default" );
					}
				}

				eaPush( &valueAccum->values, inputValueAccum );
			}
		}
		eaPush( &fallback->shader_values, valueAccum );

		// Now we have a fully created, brand new op!

		{
			int it;

			assert( (accum->inputs != NULL) == (op->op_inputs != NULL) );
			
			for( it = 0; it != eaSize( &accum->inputs ); ++it ) {
				ShaderInput* input = op->op_inputs[ it ];
				Mated2Input* mated2Input = accum->inputs[ it ];

				if( !mated2Input ) {
					continue;
				}

				mated2InputSetValues( mated2Input,
									  input->input_default.default_floats,
									  input->input_default.default_strings,
									  false );
			}
		}

		mated2SetDirty( doc );
	}
 
	return accum;
}

/// Create a new node from OP.
Mated2Node* mated2NewNodeByOp( Mated2EditorDoc* doc, ShaderOperation* op )
{
	Mated2Node* node = mated2NewNodeByOpAndName( doc, GET_REF( op->h_op_definition ), op->op_name );
	UIFlowchartNode* nodeUI = mated2NodeUI( node );
	op->op_editor_data = node;

	ui_TextEntrySetTextAndCallback( node->groupEntry, op->group_name );
	ui_TextEntrySetTextAndCallback( node->notesEntry, op->notes );

	ui_WidgetSetPosition( UI_WIDGET( nodeUI ), op->op_pos[ 0 ], op->op_pos[ 1 ]);
	ui_CheckButtonSetStateAndCallback( node->instancedCheckButton, op->instance_param );
	if( op->op_collapsed ) {
		ui_WindowToggleShadedAndCallback( UI_WINDOW( nodeUI ));
	}

	return node;
}

/// Free NODE and remove it from the doc.
void mated2NodeFree( Mated2Node* node )
{
	Mated2EditorDoc* doc = node->doc;

	MaterialFallback* fallback = mated2DocActiveFallback( doc );
	ShaderGraph* graph = mated2DocActiveShaderGraph( doc );
	MaterialGraphicPropertiesLoadTime* gfxProps = mated2DocGfxProperties( doc );
		
	// Remove the op
	{
		ShaderOperation* nodeOp = mated2NodeShaderOp( node );
		eaFindAndRemove( &graph->operations, nodeOp );
		StructDestroy( parse_ShaderOperation, nodeOp );
	}

	// Remove the values
	{
		ShaderOperationValues* nodeValues = mated2NodeShaderOpValues( node );
		eaFindAndRemove( &fallback->shader_values, nodeValues );
		StructDestroy( parse_ShaderOperationValues, nodeValues );
	}

	node->name = NULL;
	ui_WidgetForceQueueFree( UI_WIDGET( node->uiNode ));

	if( !mated2IsLoading( doc )) {
		mated2SetDirty( doc );
	}
}

static void mated2NodeSetNameActionUndo(
		Mated2EditorDoc* doc, Mated2NodeSetNameAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->newNodeName );

	ui_TextEntrySetText( node->nameEntry, action->oldNodeName );
	if( node->nameEntry->finishedF ) {
		node->nameEntry->finishedF( node->nameEntry, node->nameEntry->finishedData );
	}
}

static void mated2NodeSetNameActionRedo(
		Mated2EditorDoc* doc, Mated2NodeSetNameAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->oldNodeName );

	ui_TextEntrySetText( node->nameEntry, action->newNodeName );
	if( node->nameEntry->finishedF ) {
		node->nameEntry->finishedF( node->nameEntry, node->nameEntry->finishedData );
	}
}
static void mated2NodeSetNameActionFree(
		Mated2EditorDoc* doc, Mated2NodeSetNameAction* action )
{
	free( action->oldNodeName );
	free( action->newNodeName );
	free( action );
}

static void mated2NodeSetGroupActionUndo(
		Mated2EditorDoc* doc, Mated2NodeSetGroupAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_TextEntrySetText( node->groupEntry, action->oldGroupName );
	if( node->groupEntry->finishedF ) {
		node->groupEntry->finishedF( node->groupEntry, node->groupEntry->finishedData );
	}
}

static void mated2NodeSetGroupActionRedo(
		Mated2EditorDoc* doc, Mated2NodeSetGroupAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_TextEntrySetText( node->groupEntry, action->newGroupName );
	if( node->groupEntry->finishedF ) {
		node->groupEntry->finishedF( node->groupEntry, node->groupEntry->finishedData );
	}
}
static void mated2NodeSetGroupActionFree(
		Mated2EditorDoc* doc, Mated2NodeSetGroupAction* action )
{
	free( action->nodeName );
	free( action->oldGroupName );
	free( action->newGroupName );
	free( action );
}

static void mated2NodeSetNotesActionUndo(
	Mated2EditorDoc* doc, Mated2NodeSetNotesAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_TextEntrySetText( node->notesEntry, action->oldNotes );
	if( node->notesEntry->finishedF ) {
		node->notesEntry->finishedF( node->notesEntry, node->notesEntry->finishedData );
	}
}

static void mated2NodeSetNotesActionRedo(
	Mated2EditorDoc* doc, Mated2NodeSetNotesAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_TextEntrySetText( node->notesEntry, action->newNotes );
	if( node->notesEntry->finishedF ) {
		node->notesEntry->finishedF( node->notesEntry, node->notesEntry->finishedData );
	}
}
static void mated2NodeSetNotesActionFree(
	Mated2EditorDoc* doc, Mated2NodeSetNotesAction* action )
{
	free( action->nodeName );
	free( action->oldNotes );
	free( action->newNotes );
	free( action );
}

static void mated2NodeSetInheritOpNameActionUndo(
		Mated2EditorDoc* doc, Mated2NodeSetInheritOpNameAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );
	ui_TextEntrySetText( node->nodeViewInheritNameText, action->oldInheritName );
	if( node->nodeViewInheritNameText->finishedF ) {
		node->nodeViewInheritNameText->finishedF( node->nodeViewInheritNameText, node->nodeViewInheritNameText->finishedData );
	}
}
static void mated2NodeSetInheritOpNameActionRedo(
		Mated2EditorDoc* doc, Mated2NodeSetInheritOpNameAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );
	ui_TextEntrySetText( node->nodeViewInheritNameText, action->newInheritName );
	if( node->nodeViewInheritNameText->finishedF ) {
		node->nodeViewInheritNameText->finishedF( node->nodeViewInheritNameText, node->nodeViewInheritNameText->finishedData );
	}
}
static void mated2NodeSetInheritOpNameActionFree(
		Mated2EditorDoc* doc, Mated2NodeSetInheritOpNameAction* action )
{
	free( action->nodeName );
	free( action->oldInheritName );
	free( action->newInheritName );
	free( action );
}

static void mated2NodeSetInheritValuesActionUndo(
		Mated2EditorDoc* doc, Mated2NodeSetInheritValuesAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	if( action->inheritValues ) {
		ui_CheckButtonSetStateAndCallback( node->nodeViewInheritButton, false );
	} else {
		ui_CheckButtonSetStateAndCallback( node->nodeViewInheritButton, true );
		ui_TextEntrySetText( node->nodeViewInheritNameText, action->inheritName );
		if( node->nodeViewInheritNameText->finishedF ) {
			node->nodeViewInheritNameText->finishedF( node->nodeViewInheritNameText, node->nodeViewInheritNameText->finishedData );
		}
	}
}
static void mated2NodeSetInheritValuesActionRedo(
		Mated2EditorDoc* doc, Mated2NodeSetInheritValuesAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	if( action->inheritValues ) {
		ui_CheckButtonSetStateAndCallback( node->nodeViewInheritButton, true );
		ui_TextEntrySetText( node->nodeViewInheritNameText, action->inheritName );
		if( node->nodeViewInheritNameText->finishedF ) {
			node->nodeViewInheritNameText->finishedF( node->nodeViewInheritNameText, node->nodeViewInheritNameText->finishedData );
		}
	} else {
		ui_CheckButtonSetStateAndCallback( node->nodeViewInheritButton, false );
	}
}
static void mated2NodeSetInheritValuesActionFree(
		Mated2EditorDoc* doc, Mated2NodeSetInheritValuesAction* action )
{
	free( action->nodeName );
	free( action->inheritName );
	free( action );
}

static void mated2NodeSetShadedActionUndo(
		Mated2EditorDoc* doc, Mated2NodeSetShadedAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_WindowToggleShadedAndCallback( UI_WINDOW( node->uiNode ));
	assert( node->uiNode->window.shaded != action->isShaded );
}

static void mated2NodeSetShadedActionRedo(
		Mated2EditorDoc* doc, Mated2NodeSetShadedAction* action )
{
	Mated2Node* node = mated2FindNodeByName( doc, action->nodeName );

	ui_WindowToggleShadedAndCallback( UI_WINDOW( node->uiNode ));
	assert( node->uiNode->window.shaded == action->isShaded );
}

static void mated2NodeSetShadedActionFree(
		Mated2EditorDoc* doc, Mated2NodeSetShadedAction* action )
{
	free( action->nodeName );
	free( action );
}

static void mated2FlowchartLinkActionLink(
		Mated2EditorDoc* doc, Mated2FlowchartLinkAction* action )
{
	mated2NodeLink( mated2FindNodeByName( doc, action->sourceNodeName ), action->sourceName,
					mated2FindNodeByName( doc, action->destNodeName ), action->destName,
					action->swizzle,
					true );
}

static void mated2FlowchartLinkActionUnlink(
		Mated2EditorDoc* doc, Mated2FlowchartLinkAction* action )
{
	mated2NodeLink( mated2FindNodeByName( doc, action->sourceNodeName ), action->sourceName,
					mated2FindNodeByName( doc, action->destNodeName ), action->destName,
					action->swizzle,
					false );
}

static void mated2FlowchartLinkActionFree(
		Mated2EditorDoc* doc, Mated2FlowchartLinkAction* action )
{
	free( action->sourceNodeName );
	free( action->sourceName );
	free( action->destNodeName );
	free( action->destName );
	free( action );
}

static void mated2GuideCreateActionCreate(
		Mated2EditorDoc* doc, Mated2GuideCreateAction* action )
{
	assert( !action->isCreated );
	
	mated2NewGuide( doc, action->guide );
	eaPush( mated2DocActiveShaderGuide( doc ), action->guide );

	action->isCreated = true;
}

static void mated2GuideCreateActionRemove(
		Mated2EditorDoc* doc, Mated2GuideCreateAction* action )
{
	assert( action->isCreated );
	
	mated2GuideRemove( doc, (Mated2Guide*)action->guide->op_editor_data );

	action->isCreated = false;
}

static void mated2GuideCreateActionFree(
		Mated2EditorDoc* doc, Mated2GuideCreateAction* action )
{
	if( !action->isCreated ) {
		StructDestroy( parse_ShaderGuide, action->guide );
	}
	free( action );
}

static void mated2GuideResizeActionUndo(
		Mated2EditorDoc* doc, Mated2GuideResizeAction* action )
{
	Mated2Guide* guide = (Mated2Guide*)action->guide->op_editor_data;

	ui_WidgetSetPosition( UI_WIDGET( guide->uiGuide ), action->oldExtents[ 0 ], action->oldExtents[ 1 ]);
	ui_WidgetSetDimensions( UI_WIDGET( guide->uiGuide ), action->oldExtents[ 2 ], action->oldExtents[ 3 ]);
	
	if( guide->uiGuide->resizeFinishedF ) {
		guide->uiGuide->resizeFinishedF( guide->uiGuide, guide->uiGuide->resizeFinishedData );
	}
}

static void mated2GuideResizeActionRedo(
		Mated2EditorDoc* doc, Mated2GuideResizeAction* action )
{
	Mated2Guide* guide = (Mated2Guide*)action->guide->op_editor_data;

	ui_WidgetSetPosition( UI_WIDGET( guide->uiGuide ), action->newExtents[ 0 ], action->newExtents[ 1 ]);
	ui_WidgetSetDimensions( UI_WIDGET( guide->uiGuide ), action->newExtents[ 2 ], action->newExtents[ 3 ]);
	
	if( guide->uiGuide->resizeFinishedF ) {
		guide->uiGuide->resizeFinishedF( guide->uiGuide, guide->uiGuide->resizeFinishedData );
	}
}

static void mated2GuideResizeActionFree(
		Mated2EditorDoc* doc, Mated2GuideResizeAction* action )
{
	free( action );
}

/// Return if NODE can inherit values.
///
/// Values can be inherited if using a fallback other than the
/// default, and there are values to inherit.
static bool mated2NodeValuesCanInherit( Mated2Node* node )
{
	Mated2EditorDoc* doc = mated2NodeDoc( node );
	
	if( mated2DocActiveFallback( doc ) == &mated2DocGfxProperties( doc )->default_fallback ) {
		return false;
	} else {
		int it;

		for( it = 0; it != eaSize( &node->inputs ); ++it ) {
			if( node->inputs[ it ]) {
				Mated2Input* input = node->inputs[ it ];

				if(   it < eaSize( &node->uiNode->inputButtons )
					  && eaSize( &node->uiNode->inputButtons[ it ]->connected )) {
					continue;
				}

				return true;
			}
		}

		return false;
	}
}

static void mated2NodeInheritOpNameChanged( UITextEntry* textEntry, Mated2Node* node )
{
	Mated2EditorDoc* doc = mated2NodeDoc( node );
	ShaderInputMapping* shaderInputMapping = mated2NodeShaderInputMapping( node );
	const char* oldInheritName;
	const char* newInheritName;

	assert( shaderInputMapping );
	oldInheritName = shaderInputMapping->op_name;
	newInheritName = allocAddString( ui_TextEntryGetText( textEntry ));

	
	shaderInputMapping->op_name = newInheritName;

	ui_TextEntrySetText( node->nodeViewInheritNameText, newInheritName );
	ui_TextEntrySetText( node->inputListInheritNameText, newInheritName );

	// add the undo information
	if( oldInheritName != newInheritName ) {
		Mated2NodeSetInheritOpNameAction* accum = calloc( 1, sizeof( *accum ));

		accum->nodeName = strdup( mated2NodeShaderOp( node )->op_name );
		accum->oldInheritName = strdup( oldInheritName );
		accum->newInheritName = strdup( newInheritName );

		mated2UndoRecord( node->doc, mated2NodeSetInheritOpNameActionUndo,
						  mated2NodeSetInheritOpNameActionRedo, mated2NodeSetInheritOpNameActionFree,
						  accum );
	}
	
	mated2SetDirty( doc );
}

static void mated2NodeInheritValuesChanged( UICheckButton* checkButton, Mated2Node* node )
{
	Mated2EditorDoc* doc = mated2NodeDoc( node );
	MaterialFallback* fallback = mated2DocActiveFallback( doc );
	bool inheritValues = ui_CheckButtonGetState( checkButton );
	ShaderOperationValues* shaderOpValues = mated2NodeShaderOpValues( node );
	ShaderInputMapping* shaderInputMapping = mated2NodeShaderInputMapping( node );

	ui_CheckButtonSetState( node->nodeViewInheritButton, inheritValues );
	ui_CheckButtonSetState( node->inputListInheritButton, inheritValues );

	if( inheritValues ) {
		ShaderInputMapping* newInputMapping = StructCreate( parse_ShaderInputMapping );
		assert( shaderOpValues && !shaderInputMapping );

		newInputMapping->mapped_op_name = shaderOpValues->op_name;
		eaRemove( &fallback->shader_values, eaFind( &fallback->shader_values, shaderOpValues ));

		if( eaSize( &node->inheritNameModel ) != 0 ) {
			if( eaFindString( &node->inheritNameModel, newInputMapping->mapped_op_name ) > -1 ) {
				newInputMapping->op_name = newInputMapping->mapped_op_name;
			} else {
				newInputMapping->op_name = node->inheritNameModel[ 0 ];
			}
		} else {
			newInputMapping->op_name = "<NULL>";
		}
		eaPush( &fallback->input_mappings, newInputMapping );
		
		// add the undo information
		{
			Mated2NodeSetInheritValuesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeShaderOp(node)->op_name );
			accum->inheritValues = inheritValues;
			accum->inheritName = strdup( newInputMapping->op_name );

			mated2UndoRecord( node->doc, mated2NodeSetInheritValuesActionUndo,
							  mated2NodeSetInheritValuesActionRedo, mated2NodeSetInheritValuesActionFree,
							  accum );
		}
	} else {
		ShaderOperationValues* newOpValues = StructCreate( parse_ShaderOperationValues );
		assert( !shaderOpValues && shaderInputMapping );

		newOpValues->op_name = shaderInputMapping->mapped_op_name;
		eaRemove( &fallback->input_mappings, eaFind( &fallback->input_mappings, shaderInputMapping ));
		
		eaPush( &fallback->shader_values, newOpValues );
		
		// add the undo information
		{
			Mated2NodeSetInheritValuesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeShaderOp(node)->op_name );
			accum->inheritValues = inheritValues;
			accum->inheritName = strdup( shaderInputMapping->op_name );

			mated2UndoRecord( node->doc, mated2NodeSetInheritValuesActionUndo,
							  mated2NodeSetInheritValuesActionRedo, mated2NodeSetInheritValuesActionFree,
							  accum );
		}
	}

	mated2SetDirty( doc );
	node->needsReflow = true;
	mated2SetInputListNeedsReflow( doc, true );

	StructDestroy( parse_ShaderOperationValues, shaderOpValues );
	StructDestroy( parse_ShaderInputMapping, shaderInputMapping );
}

/// Replace NODE's option with the selected value in COMBO-BOX.
static void mated2ChangeOptionBySelection( UIComboBox* comboBox, Mated2Node* node )
{
	Mated2NodeOption* newOption = ui_ComboBoxGetSelectedObject( comboBox );

	if( !newOption ) {
		return;
	} else {
		mated2UndoBeginGroup( node->doc );
		{
			Mated2Node* newNode = mated2NewNodeByOpName( node->doc, newOption->nodeName );
			UIFlowchartNode* newNodeUI;
			if( newNode == NULL ) {
				mated2UndoCancelUnfisihedGroups( node->doc );
				return;
			}
			newNodeUI = mated2NodeUI( newNode );
		
			ui_WidgetSetPosition( UI_WIDGET( newNode->uiNode ),
								  ui_WidgetGetX( UI_WIDGET( node->uiNode )),
								  ui_WidgetGetY( UI_WIDGET( node->uiNode )));

			{
				Mated2NodeCreateAction* accum = calloc( 1, sizeof( *accum ));
				accum->nodeName = strdup( mated2NodeName( newNode ));
				setVec2( accum->nodePos, ui_WidgetGetX( UI_WIDGET( newNodeUI )), ui_WidgetGetY( UI_WIDGET( newNodeUI )));
				COPY_HANDLE( accum->opDef, mated2NodeShaderOp( newNode )->h_op_definition );
				
				mated2UndoRecord( node->doc, mated2NodeCreateActionRemove,
								  mated2NodeCreateActionCreate, mated2NodeCreateActionFree,
								  accum );
			}
		
			// For this scope, both the new node and the old node exist.
			// Schrödinger would be pleased.
			{
				mated2NodeSwapButtons( node->uiNode->inputButtons, newNode->uiNode->inputButtons );
				mated2NodeSwapButtons( node->uiNode->outputButtons, newNode->uiNode->outputButtons );
			}
		
			ui_FlowchartRemoveNode( mated2Flowchart( node->doc ), node->uiNode );
		}
		mated2UndoEndGroup( node->doc );
	}
}

/// When the widget changes its name, we need to update the
/// corresponding material node.
static void mated2NodeNameChanged( UITextEntry* textEntry, Mated2Node* node )
{
	char newNodeNameBuffer[ 128 ];
  	const char* newNodeName;

	if( stricmp( node->name, ui_TextEntryGetText( textEntry )) == 0 ) {
		return;
	}

	newNodeName = allocAddString( mated2UniqueNodeName( node->doc, ui_TextEntryGetText( textEntry ), SAFESTR( newNodeNameBuffer )));
	ui_TextEntrySetText( textEntry, newNodeName );
		
	if( !mated2IsLoading( node->doc )) {
		ShaderOperation* op = mated2NodeShaderOp( node );
		ShaderOperationValues* opValues = mated2NodeShaderOpValues( node );
		
		const char* oldNodeName = op->op_name;

		node->name = newNodeName;
		op->op_name = newNodeName;
		if( opValues ) {
			opValues->op_name = newNodeName;
		}
		{
			int buttonIt;
			int connectedIt;
			int inputIt;
			
			for( buttonIt = 0; buttonIt != eaSize( &node->uiNode->outputButtons ); ++buttonIt ) {
				UIFlowchartButton* button = node->uiNode->outputButtons[ buttonIt ];
				for( connectedIt = 0; connectedIt != eaSize( &button->connected ); ++connectedIt ) {
					UIFlowchartButton* connected = button->connected[ connectedIt ];
					Mated2Node* connectedNode = connected->node->userData;
					ShaderOperation* connectedOp = mated2NodeShaderOp( connectedNode );

					for( inputIt = 0; inputIt != eaSize( &connectedOp->inputs ); ++inputIt ) {
						ShaderInputEdge* input = connectedOp->inputs[ inputIt ];

						if( input->input_source_name == oldNodeName ) {
							input->input_source_name = newNodeName;
						}
					}
				}
			}
		}

		// add the undo information
		{
			Mated2NodeSetNameAction* accum = calloc( 1, sizeof( *accum ));

			accum->oldNodeName = strdup( oldNodeName );
			accum->newNodeName = strdup( newNodeName );

			mated2UndoRecord( node->doc, mated2NodeSetNameActionUndo,
							  mated2NodeSetNameActionRedo, mated2NodeSetNameActionFree,
							  accum );
		}

		mated2DocNodeRename( node->doc, oldNodeName, newNodeName );

		mated2SetDirty( node->doc );
	}

	{
		char buffer[ 256 ];
		ui_WindowSetTitle( UI_WINDOW( node->uiNode ), mated2NodeNameWithType( node, SAFESTR( buffer )));
	}
}

/// When the widget changes its group, we need to update the
/// corresponding material node.
static void mated2NodeGroupChanged( UITextEntry* textEntry, Mated2Node* node )
{
	const char* newGroupName = allocAddString( ui_TextEntryGetText( textEntry ));

	if( !mated2IsLoading( node->doc )) {
		const char* oldGroupName = mated2NodeShaderOp(node)->group_name;
		mated2NodeShaderOp(node)->group_name = newGroupName;
		
		// add the undo information
		{
			Mated2NodeSetGroupAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeShaderOp(node)->op_name );
			accum->oldGroupName = strdup( oldGroupName );
			accum->newGroupName = strdup( newGroupName );

			mated2UndoRecord( node->doc, mated2NodeSetGroupActionUndo,
							  mated2NodeSetGroupActionRedo, mated2NodeSetGroupActionFree,
							  accum );
		}

		mated2SetDirty( node->doc );
	}
}

/// When the widget changes its notes, we need to update the label in the side pane
static void mated2NodeNotesChanged( UITextEntry* textEntry, Mated2Node* node )
{
	if( !mated2IsLoading( node->doc )) {
		char* newNotes = strdup( ui_TextEntryGetText( textEntry ) );

		char* oldNotes = mated2NodeShaderOp(node)->notes;
		
		mated2NodeShaderOp(node)->notes = newNotes;

		// add the undo information
		{
			Mated2NodeSetNotesAction* accum = calloc( 1, sizeof( *accum ));

			accum->nodeName = strdup( mated2NodeShaderOp(node)->op_name );
			accum->oldNotes = oldNotes;
			accum->newNotes = strdup( newNotes );

			mated2UndoRecord( node->doc, mated2NodeSetNotesActionUndo,
				mated2NodeSetNotesActionRedo, mated2NodeSetNotesActionFree,
				accum );
		}

		mated2SetDirty( node->doc );
	}
}

/// When the instanced widget changes, we need to update the rest of
/// the material.
void mated2NodeInstancedChanged( UICheckButton* checkButton, Mated2Node* node )
{
	Mated2EditorDoc* doc = node->doc;
	bool newState = ui_CheckButtonGetState( checkButton );

	if( newState ) {
		mated2DocSetInstancedNode( doc, node );
	} else {
		mated2DocSetInstancedNode( doc, NULL );
	}
}

/// Update NODE's instance param to IS-INSTANCED.  
void mated2NodeInstancedUpdate( SA_PARAM_NN_VALID Mated2Node* node, bool isInstanced )
{
	ui_CheckButtonSetState( node->instancedCheckButton, isInstanced );
}

/// Programmatically link or unlink SOURCE-NODE's output SOURCE-NAME
/// to DEST-NODE's input DEST-NAME.
///
/// If IS-CREATE-LINK, then create a link, otherwise destroy it.
void mated2NodeLink( Mated2Node* sourceNode, const char* sourceName,
					 Mated2Node* destNode, const char* destName,
					 const U8 swizzle[ 4 ], bool isCreateLink )
{
	U8 internalSwizzle[ 4 ];
	if( !mated2SupportedSwizzle( swizzle )) {
		Alertf( "Swizzle is not in a supported format for this editor... defaulting to null swizzle." );
		internalSwizzle[ 0 ] = 0;
		internalSwizzle[ 1 ] = 1;
		internalSwizzle[ 2 ] = 2;
		internalSwizzle[ 3 ] = 3;
	} else {
		memcpy( internalSwizzle, swizzle, sizeof( internalSwizzle ));
	}

	assert( sourceNode->doc == destNode->doc );

	// link the UI component... the callback will handle the model
	{
		UIFlowchartButton* source = NULL;
		UIFlowchartButton* dest = NULL;

		{
			int it;
			for( it = 0; it != eaSize( &sourceNode->uiNode->outputButtons ); ++it ) {
				UIFlowchartButton* sourceButton = sourceNode->uiNode->outputButtons[ it ];
				if(   sourceButton->type != UIFlowchartIsChild
					  && sourceButton->label
					  && strcmp( ui_WidgetGetText(UI_WIDGET(sourceButton->label)), sourceName ) == 0 ) {
					int offset = mated2SwizzleOffset( internalSwizzle );
					assert( it + offset < eaSize( &sourceNode->uiNode->outputButtons ));
					
					source = sourceNode->uiNode->outputButtons[ it + offset ];
					break;
				}
			}
			for( it = 0; it != eaSize( &destNode->uiNode->inputButtons ); ++it ) {
				UIFlowchartButton* destButton = destNode->uiNode->inputButtons[ it ];
				const char* destButtonText = ui_WidgetGetText( UI_WIDGET( destButton->label ));
				if(   destButton->label
					  && strcmp( destButtonText, destName ) == 0 ) {
					dest = destButton;
				}
			}
		}
		if ( source && dest ) // otherwise a vestigial link to a node/socket that doesn't exist
		{
			if( isCreateLink ) {
				ui_FlowchartLink( source, dest );
			} else {
				ui_FlowchartUnlink( source, dest, false );
			}
		}
	}
}

void mated2NodeSetIsPreview( Mated2Node* node, bool isPreview )
{
	node->isPreview = isPreview;
}

/// Return the input with name INPUT-NAME, or NULL if there is no such
/// input.
///
/// Accepts NULL for NODE, for ease of chaining.
Mated2Input* mated2FindNodeInputByName( Mated2Node* node, const char* inputName )
{
	if( node == NULL ) {
		return NULL;
	} else {
		int it;
		for( it = 0; it != eaSize( &node->inputs ); ++it ) {
			Mated2Input* input = node->inputs[ it ];
			if( input && stricmp( inputName, mated2InputName( input )) == 0 ) {
				return input;
			}
		}
	}
	
	return NULL;
}

/// Syncronize NODE's position with its ShaderOp's position.
void mated2NodeUpdatePosition( Mated2Node* node )
{
	ShaderOperation* op = mated2NodeShaderOp( node );
	
	op->op_pos[ 0 ] = ui_WidgetGetX( UI_WIDGET( node->uiNode ));
	op->op_pos[ 1 ] = ui_WidgetGetY( UI_WIDGET( node->uiNode ));
}

/// Return the number of material-speciific inputs in NODE.
int mated2NodeCountMaterialSpecificInput( Mated2Node* node )
{
	ShaderOperationValues* materialValue = mated2NodeShaderOpValues( node );
	if( materialValue ) {
		return eaSize( &materialValue->values );
	} else {
		return 0;
	}
}

/// Overrides the flowchart node tick, to reflow data members if
/// necesarry.
static void mated2FlowchartNodeTick( UIFlowchartNode* pNode, UI_PARENT_ARGS )
{
	Mated2Node* node = pNode->userData;
	Mated2EditorDoc* doc = mated2NodeDoc( node );
	bool nodeIsActive = ui_IsActive( UI_WIDGET( pNode ));

	if( mated2NodeShaderOp(node)->op_has_error ) {
		if( !strStartsWith( ui_WidgetGetText( UI_WIDGET( pNode )), "ERROR: " )) {
			char newTitle[ 256 ];
			sprintf( newTitle, "ERROR: %s", ui_WidgetGetText( UI_WIDGET( pNode )));
			ui_WindowSetTitle( UI_WINDOW( pNode ), newTitle );
		}
	} else {
		if( strStartsWith( ui_WidgetGetText( UI_WIDGET( pNode )), "ERROR: " )) {
			char newTitle[ 256 ];
			strcpy( newTitle, ui_WidgetGetText( UI_WIDGET( pNode )) + strlen( "ERROR: " ));
			ui_WindowSetTitle( UI_WINDOW( pNode ), newTitle );
		}
	}

	assert( GET_REF( mated2NodeShaderOp(node)->h_op_definition ));
	{
		ShaderOperation* op = mated2NodeShaderOp( node );
		
		ui_WidgetSkin( UI_WIDGET( pNode ),
					   mated2NodeSkin( node->doc, SAFE_MEMBER( GET_REF( op->h_op_definition ), op_type ),
								   node->isPreview, op->op_has_error ));
	}

	if( node->needsReflow ) {
		ui_FlowchartNodeRemoveAllChildren( node->uiNode, false, true );

		{
			int yIt = 0;
			int it;
			ShaderInputMapping* inputMapping = mated2NodeShaderInputMapping( node );
			const char* inheritName = SAFE_MEMBER( inputMapping, op_name );
			
			if( mated2NodeValuesCanInherit( node )) {
				ui_WidgetSetPosition( UI_WIDGET( node->nodeViewInheritButton ), 0, yIt );
				ui_CheckButtonSetState( node->nodeViewInheritButton, inheritName != NULL );
				ui_CheckButtonSetState( node->inputListInheritButton, inheritName != NULL );
				ui_FlowchartNodeAddChild( node->uiNode, UI_WIDGET( node->nodeViewInheritButton ), true );
				yIt += ui_WidgetGetHeight( UI_WIDGET( node->nodeViewInheritButton )) + 2;
			}

			if( inheritName ) {
				ui_TextEntrySetText( node->nodeViewInheritNameText, inheritName );
				ui_TextEntrySetText( node->inputListInheritNameText, inheritName );
				ui_WidgetSetPosition( UI_WIDGET( node->nodeViewInheritNameText ), 0, yIt );				
				ui_FlowchartNodeAddChild( node->uiNode, UI_WIDGET( node->nodeViewInheritNameText ), true );
				yIt += ui_WidgetGetHeight( UI_WIDGET( node->nodeViewInheritNameText )) + 2; 
			} else {
				for( it = 0; it != eaSize( &node->inputs ); ++it ) {
					if( node->inputs[ it ]) {
						Mated2Input* input = node->inputs[ it ];
						UIWidget* widget = mated2InputNodeViewWidget( input );

						if( it < eaSize( &node->uiNode->inputButtons )
							&& eaSize( &node->uiNode->inputButtons[ it ]->connected )) {
							continue;
						}
					
						ui_WidgetSetPosition( widget, 0, yIt );
						ui_FlowchartNodeAddChild( node->uiNode, widget, true );
						mated2InputUpdateIsActive( input, nodeIsActive );
					
						yIt += ui_WidgetGetHeight( widget ) + 2;
					}
				}
			}
			for( it = 0; it != eaSize( &node->otherWidgets ); ++it ) {
				UIWidget* widget = node->otherWidgets[ it ];

				ui_WidgetSetPosition( widget, 0, yIt );
				ui_FlowchartNodeAddChild( node->uiNode, widget, true );
				ui_SetActive( widget, nodeIsActive );
 
				yIt += ui_WidgetGetHeight( widget ) + 2;
			}
			ui_WidgetSetHeight( UI_WIDGET( node->uiNode->afterPane ), yIt );
		}
		
		node->needsReflow = false;
	}
	
	ui_FlowchartNodeTick( pNode, UI_PARENT_VALUES );
}

/// Free the additional data that is associated with each flowchart node.
static void mated2FlowchartNodeFree( UIFlowchartNode* pNode )
{
	Mated2Node* node = pNode->userData;

	assert( pNode == node->uiNode );
	ui_FlowchartNodeRemoveAllChildren( node->uiNode, false, true );
	ui_WidgetForceQueueFree( UI_WIDGET( node->nodeViewInheritButton ));
	ui_WidgetForceQueueFree( UI_WIDGET( node->nodeViewInheritNameText ));
	ui_WidgetForceQueueFree( UI_WIDGET( node->inputListInheritButton ));
	ui_WidgetForceQueueFree( UI_WIDGET( node->inputListInheritNameText ));
	eaDestroyEx( &node->inputs, mated2InputFree );
	eaDestroyEx( &node->otherWidgets, ui_WidgetForceQueueFree );
	eaDestroyEx( &node->options, mated2NodeOptionDestroy );
	memset( node, 0, sizeof( *node ));
	free( node );
}

/// Fill out EDGE with data that represents a link from SOURCE to DEST.
static void mated2SetEdgeFlowchartLink(
		ShaderInputEdge* edge, UIFlowchartButton* source, UIFlowchartButton* dest )
{
	Mated2Node* sourceNode = (Mated2Node*)source->node->userData;
	Mated2Node* destNode = (Mated2Node*)dest->node->userData;
	
	edge->input_name = (char*)dest->userData;
	edge->input_source_name = sourceNode->name;
	edge->input_source_output_name = (char*)source->userData;

	if( source->type != UIFlowchartIsChild ) {
		edge->input_swizzle[ 0 ] = 0;
		edge->input_swizzle[ 1 ] = 1;
		edge->input_swizzle[ 2 ] = 2;
		edge->input_swizzle[ 3 ] = 3;
	} else {
		UIFlowchartButton** outputButtons = source->node->outputButtons;
		int index = eaFind( &outputButtons, source );
		int it = index;

		// The child buttons represent specific swizzles, in the
		// order: XXXX, YYYY, ZZZZ, WWWW
		assert( index >= 0 );
		while( outputButtons[ it ]->type == UIFlowchartIsChild ) {
			--it;
		}
		edge->input_swizzle[ 0 ] = index - it - 1;
		edge->input_swizzle[ 1 ] = index - it - 1;
		edge->input_swizzle[ 2 ] = index - it - 1;
		edge->input_swizzle[ 3 ] = index - it - 1;
	}
}

static void mated2FlowchartNodeClicked( UIWindow* uiNode, Mated2EditorDoc* doc )
{
	Mated2Node* node = (Mated2Node*)((UIFlowchartNode*)uiNode)->userData;

	mated2PreviewSetSelectedNode( doc, node );
}

/// A flowchart node had its shaded state changed.
static void mated2FlowchartNodeShaded( UIWindow* uiNode, Mated2EditorDoc* doc )
{
	Mated2Node* node = (Mated2Node*)((UIFlowchartNode*)uiNode)->userData;

	mated2NodeShaderOp(node)->op_collapsed = !!uiNode->shaded;

	if( !mated2IsLoading( doc )) {
		// Materials do not save the shaded/unshaded state.
		if( mated2DocIsTemplate( doc )) {
			Mated2NodeSetShadedAction* accum = calloc( 1, sizeof( *accum ));
			accum->nodeName = strdup( mated2NodeName( node ));
			accum->isShaded = uiNode->shaded;

			mated2UndoRecord( doc, mated2NodeSetShadedActionUndo,
							  mated2NodeSetShadedActionRedo, mated2NodeSetShadedActionFree,
							  accum );

			mated2SetDirty( doc );
		}
	}
}

/// Return the ShaderOp for node.
ShaderOperation* mated2NodeShaderOp( Mated2Node* node )
{
	ShaderGraph* graph = mated2DocActiveShaderGraph( node->doc );
	ShaderOperation* op = materialFindOpByName( graph, node->name );

	assert( op && (op->op_editor_data == NULL || op->op_editor_data == node));
	ANALYSIS_ASSUME(op != NULL);
	return op;
}

/// Get ShaderOp's node.
Mated2Node* mated2ShaderOpNode( ShaderOperation* op )
{
	assert( op->op_editor_data );
	return op->op_editor_data;
}

/// Get the material-specific data for NODE.
ShaderOperationValues* mated2NodeShaderOpValues( Mated2Node* node )
{
	ShaderOperationValues* values = materialFindOperationValues(
			mated2DocMaterialData( node->doc ), mated2DocActiveFallback( node->doc ), node->name );
	if( mated2NodeShaderInputMapping( node )) {
		return NULL;
	}

	return values;
}

ShaderOperationSpecificValue* mated2NodeAddOperationSpecificValue(Mated2Node* node, const char *inputName)
{
	return materialAddOperationSpecificValue(mated2DocMaterialData( node->doc ), mated2DocActiveFallback( node->doc ), node->name, inputName );
}

/// Get the material-specific mapping for NODE.
ShaderInputMapping* mated2NodeShaderInputMapping( Mated2Node* node )
{
	ShaderInputMapping* inputMapping = materialFindInputMapping( mated2DocActiveFallback( node->doc ), node->name );

	return inputMapping;
}

/// Get the doc NODE belongs to.
Mated2EditorDoc* mated2NodeDoc( Mated2Node* node )
{
	return node->doc;
}

/// Get NODE's unique name.
const char* mated2NodeName( Mated2Node* node )
{
	return mated2NodeShaderOp( node )->op_name;
}

/// Get NODE's name, with the node type appended.
const char* mated2NodeNameWithType(
		Mated2Node* node, char* buffer, int buffer_size )
{
	const ShaderOperationDef* opDef = GET_REF( mated2NodeShaderOp( node )->h_op_definition );
	const char* optionRootName = opDef ? (opDef->op_parent_type_name ? opDef->op_parent_type_name : opDef->op_type_name) : NULL;

	assert( opDef != NULL && optionRootName != NULL );

	sprintf_s( SAFESTR2( buffer ), "%s - %s",
			   mated2NodeName( node ), mated2TranslateOpName( optionRootName ));

	return buffer;
}

/// Get NODE's group name.
const char* mated2NodeGroup( const Mated2Node* node )
{
	const char* groupName = mated2NodeShaderOp( (Mated2Node*)node )->group_name;
	if( groupName ) {
		return groupName;
	} else {
		return "";
	}
}

/// Get NODE's notes.
const char* mated2NodeNotes( const Mated2Node* node )
{
	const char* notes = mated2NodeShaderOp( (Mated2Node*)node )->notes;
	if( notes ) {
		return notes;
	} else {
		return "";
	}
}

/// Get the NODE's inputs.
Mated2Input** mated2NodeInputs( Mated2Node* node )
{
	return node->inputs;
}

/// Get NODE's UI model.
UIFlowchartNode* mated2NodeUI( Mated2Node* node )
{
	return node->uiNode;
}

/// Get NODE's input list, check button to enable Inheriting values.
UICheckButton* mated2NodeInputListInheritValuesCheckButton(
		Mated2Node* node )
{
	return node->inputListInheritButton;
}

/// Get Node's input list, text entry to select what value to inherit'
UITextEntry* mated2NodeInputListInheritValuesNameWidget(
		Mated2Node* node )
{
	return node->inputListInheritNameText;
}

/// Return if NODE is inheriting values
bool mated2NodeInheritValues( SA_PARAM_NN_VALID Mated2Node* node )
{
	return mated2NodeShaderInputMapping( node ) != NULL;
}

/// Swap the value of similarly named buttons between two groups,
/// BUTTONS1 and BUTTONS2.
static void mated2NodeSwapButtons(
		UIFlowchartButton** buttons1, UIFlowchartButton** buttons2 )
{
	int it1;
	int it2;
	for( it1 = 0; it1 != eaSize( &buttons1 ); ++it1 ) {
		if( eaSize( &buttons1[ it1 ]->connected )) {
			for( it2 = 0; it2 != eaSize( &buttons2 ); ++it2 ) {
				const char* text1 = ui_WidgetGetText( UI_WIDGET( buttons1[ it1 ]->label ));
				const char* text2 = ui_WidgetGetText( UI_WIDGET( buttons2[ it2 ]->label ));
				if( 0 == strcmp( text1, text2 )) {
					ui_FlowchartSwap( buttons1[ it1 ], buttons2[ it2 ] );
					break;
				}
			}
		}
	}
}

/// Create a new Mated2Guide that represents GUIDE in the editor.
Mated2Guide* mated2NewGuide( Mated2EditorDoc* doc, ShaderGuide* guide )
{
	float l = guide->top_left[ 0 ];
	float t = guide->top_left[ 1 ];
	float r = guide->bottom_right[ 0 ];
	float b = guide->bottom_right[ 1 ];
	
	Mated2Guide* accum = calloc( 1, sizeof( *accum ));

	accum->doc = doc;
	accum->shaderGuide = guide;
	guide->op_editor_data = accum;

	accum->uiGuide = ui_RectangularSelectionCreate( l, t, r - l, b - t, CreateColor( 33, 63, 31, 255 ), ColorRed );
	ui_RectangularSelectionSetResizeFinishedCallback( accum->uiGuide, mated2GuideResized, accum );
	ui_WidgetSetContextCallback( UI_WIDGET( accum->uiGuide ), mated2GuideRemoved, accum );

	ui_WidgetAddChild( UI_WIDGET(mated2Flowchart( doc )), UI_WIDGET( accum->uiGuide ));
	eaPush( mated2ShaderGuides( doc ), accum );

	if( !mated2IsLoading( doc )) {
		mated2SetDirty( doc );

		{
			Mated2GuideCreateAction* actionAccum = calloc( 1, sizeof( *actionAccum ));
			actionAccum->guide = guide;
			actionAccum->isCreated = true;

			mated2UndoRecord( doc, mated2GuideCreateActionRemove,
							  mated2GuideCreateActionCreate, mated2GuideCreateActionFree,
							  actionAccum );
		}
	}

	return accum;
}

/// Callback when RECT gets resized.
static void mated2GuideResized( UIRectangularSelection* rect, Mated2Guide* guide )
{
	Vec4 newExtents = { ui_WidgetGetX( UI_WIDGET( rect )),
						ui_WidgetGetY( UI_WIDGET( rect )),
						ui_WidgetGetWidth( UI_WIDGET( rect )),
						ui_WidgetGetHeight( UI_WIDGET( rect ))};
	Vec4 oldExtents = { guide->shaderGuide->top_left[ 0 ],
						guide->shaderGuide->top_left[ 1 ],
						guide->shaderGuide->bottom_right[ 0 ] - guide->shaderGuide->top_left[ 0 ],
						guide->shaderGuide->bottom_right[ 1 ] - guide->shaderGuide->top_left[ 1 ]};

	if(   newExtents[ 0 ] == oldExtents[ 0 ] && newExtents[ 1 ] == oldExtents[ 1 ]
		  && newExtents[ 2 ] == oldExtents[ 2 ] && newExtents[ 3 ] == oldExtents[ 3 ]) {
		return;
	}
	
	if( !mated2IsLoading( guide->doc )) {
		{
			Mated2GuideResizeAction* accum = calloc( 1, sizeof( *accum ));
			accum->guide = guide->shaderGuide;
			copyVec4( oldExtents, accum->oldExtents );
			copyVec4( newExtents, accum->newExtents );

			mated2UndoRecord( guide->doc, mated2GuideResizeActionUndo,
							  mated2GuideResizeActionRedo, mated2GuideResizeActionFree,
							  accum );
		}
		
		setVec2( guide->shaderGuide->top_left, newExtents[ 0 ], newExtents[ 1 ]);
		setVec2( guide->shaderGuide->bottom_right,
				 newExtents[ 0 ] + newExtents[ 2 ],
				 newExtents[ 1 ] + newExtents[ 3 ]);
		
		mated2SetDirty( guide->doc );
	}
}

/// Remove and free the Mated2Guide and widgets.  Remove the underlying
/// ShaderGuide, but DOES NOT FREE IT!
static void mated2GuideRemove( Mated2EditorDoc* doc, Mated2Guide* guide )
{
	ShaderGuide*** guides = mated2DocActiveShaderGuide( doc );
	
	eaRemove( guides, eaFind( guides, guide->shaderGuide ));
	ui_WidgetForceQueueFree( UI_WIDGET( guide->uiGuide ));
	eaRemove( mated2ShaderGuides( doc ), eaFind( mated2ShaderGuides( doc ), guide ));

	free( guide );
	mated2SetDirty( doc );
}

/// Callback when RECT gets removed.
static void mated2GuideRemoved( UIRectangularSelection* rect, Mated2Guide* guide )
{
	Mated2EditorDoc* doc = guide->doc;
	ShaderGuide* shaderGuide = guide->shaderGuide;
	mated2GuideRemove( doc, guide );

	{
		Mated2GuideCreateAction* accum = calloc( 1, sizeof( *accum ));
		accum->guide = shaderGuide;
		accum->isCreated = false;

		mated2UndoRecord( doc, mated2GuideCreateActionCreate,
						  mated2GuideCreateActionRemove, mated2GuideCreateActionFree,
						  accum );
	}
}

/// Destroy NODE-OPTION.
void mated2NodeOptionDestroy( Mated2NodeOption* nodeOption )
{
	StructDestroy( parse_Mated2NodeOption, nodeOption );
}

/// Called by the flowchart widget every time a link is added --
/// returns if the link is allowed.
bool mated2FlowchartLinkRequest(
		UIFlowchart* flowchart, UIFlowchartButton* source,
		UIFlowchartButton* dest, bool force, Mated2EditorDoc* doc )
{
	Mated2Node* sourceNode = (Mated2Node*)source->node->userData;
	Mated2Node* destNode = (Mated2Node*)dest->node->userData;
		
	if( !force ) {
		if( source->node == dest->node ) {
			return false;
		}
	}

	// Point of NO RETURN!	The link has been accepted!
	destNode->needsReflow = true;
	if( !mated2IsLoading( doc )) {
		ShaderInputEdge* edge = StructCreate( parse_ShaderInputEdge );
		mated2SetEdgeFlowchartLink( edge, source, dest );
		eaPush( &mated2NodeShaderOp(destNode)->inputs, edge );

		{
			Mated2FlowchartLinkAction* accum = calloc( 1, sizeof( *accum ));
			accum->sourceNodeName = strdup( mated2NodeName( sourceNode ));
			accum->sourceName = strdup( edge->input_source_output_name );
			accum->destNodeName = strdup( mated2NodeName( destNode ));
			accum->destName = strdup( edge->input_name );
			memcpy( accum->swizzle, edge->input_swizzle, sizeof( accum->swizzle ));

			mated2UndoRecord( doc, mated2FlowchartLinkActionUnlink,
							  mated2FlowchartLinkActionLink, mated2FlowchartLinkActionFree,
							  accum );
		}

		mated2SetDirty( doc );
	}

	
	return true;
}

/// Allways return TRUE.  Called by the flowchart widget every time a
/// link is removed, to update internal state.
bool mated2FlowchartUnlinkRequest(
		UIFlowchart* flowchart, UIFlowchartButton* source, UIFlowchartButton* dest,
		bool force, Mated2EditorDoc* doc )
{
	Mated2Node* sourceNode = (Mated2Node*)source->node->userData;
	Mated2Node* destNode = (Mated2Node*)dest->node->userData;

	destNode->needsReflow = true;
	
	if( !mated2IsLoading( doc )) {
		ShaderInputEdge edge;
		mated2SetEdgeFlowchartLink( &edge, source, dest );

		{
			ShaderOperation* destOp = mated2NodeShaderOp(destNode);
			int it;
			for( it = 0; it != eaSize( &destOp->inputs ); ++it ) {
				if( StructCompare( parse_ShaderInputEdge, &edge, destOp->inputs[ it ], 0, 0, 0 ) == 0 ) {
					eaRemove( &destOp->inputs, it );
					--it;
				}
			}
		}

		{
			Mated2FlowchartLinkAction* accum = calloc( 1, sizeof( *accum ));
			accum->sourceNodeName = strdup( mated2NodeName( sourceNode ));
			accum->sourceName = strdup( edge.input_source_output_name );
			accum->destNodeName = strdup( mated2NodeName( destNode ));
			accum->destName = strdup( edge.input_name );
			memcpy( accum->swizzle, edge.input_swizzle, sizeof( accum->swizzle ));

			mated2UndoRecord( doc, mated2FlowchartLinkActionLink,
							  mated2FlowchartLinkActionUnlink, mated2FlowchartLinkActionFree,
							  accum );
		}

		mated2SetDirty( doc );
	}
	
	return true;
}

/// Called by the flowchart widget ONCE every time a link add is
/// requested.  This will be the first call.
bool mated2FlowchartLinkBegin(
		UIFlowchart* flow, UIFlowchartButton* source, UIFlowchartButton* dest,
		bool force, Mated2EditorDoc* doc )
{
	mated2UndoBeginGroup( doc );
	return true;
}

/// Called by the flowchart widget ONCE every time a link add is
/// requested.  This will be the last call.
bool mated2FlowchartLinkEnd(
		UIFlowchart* flow, UIFlowchartButton* source, UIFlowchartButton* dest,
		bool linked, Mated2EditorDoc* doc )
{
	if( linked ) {
		mated2UndoEndGroup( doc );
	} else {
		mated2UndoCancelUnfisihedGroups( doc );
	}

	return true;
}

/// A set of tests for debugging.  Feel free to add more validity
/// tests to this function.
void mated2NodeAssertValid( const Mated2Node* node )
{
	int it;

	for( it = 0; it != eaSize( &node->inputs ); ++it ) {
		if( node->inputs[ it ]) {
			mated2InputAssertValid( node->inputs[ it ]);
		}
	}
}

#endif

#include"MaterialEditor2EMNode_c_ast.c"
