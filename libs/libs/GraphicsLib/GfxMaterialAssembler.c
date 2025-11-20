#include "GfxMaterialAssembler.h"
#include "GfxMaterialProfile.h"
#include "GfxMaterials.h"
#include "Materials.h"
#include "MemoryPool.h"
#include "GraphicsLibPrivate.h"
#include "structInternals.h"
#include "GfxLightOptions.h"

GCC_SYSTEM

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

#define MAX_VARMAPNAME_LENGTH 64

typedef struct ShaderAssemblerTreeNode ShaderAssemblerTreeNode;
typedef struct ShaderAssemblerTreeNodeConnection {
	ShaderAssemblerTreeNode *node;
	ShaderInputEdge *input_edge;
} ShaderAssemblerTreeNodeConnection;

typedef struct ShaderAssemblerTreeNodeOutput {
	int temporary_index; // Which temporary variable gets used here.
	ShaderAssemblerTreeNodeConnection **cons; // List of multiple outputs
} ShaderAssemblerTreeNodeOutput;

typedef struct ShaderAssemblerTreeNodeInput {
	int temporary_index; // Which temporary variable gets used here.
	int tex_index; // If this node has a texture input, what index to use
	ShaderRuntimeInput *param_runtime_input; // the runtime input for this parameter
	ShaderAssemblerTreeNodeConnection con; // Single input
} ShaderAssemblerTreeNodeInput;

typedef struct ShaderAssemblerTreeNode {
	int depth;
	ShaderOperation *op;
	int num_inputs;
	ShaderAssemblerTreeNodeInput *inputs; // parellel to op->h_op_definition->op_inputs
	int num_outputs;
	ShaderAssemblerTreeNodeOutput *outputs;
} ShaderAssemblerTreeNode;

typedef struct ShaderAssemblerState {
	const char *filename; // For blame
	ShaderGraph *shader_graph;
	const MaterialAssemblerProfile *mat_profile;
	const GfxLightingModel *lighting_model;
	ShaderAssemblerTreeNode *root;
	int num_nodes;
	ShaderAssemblerTreeNode **nodes; // parallel to shader_graph->operations.  ** for easy sorting
	int num_textures;
	int num_param_vectors; // Number of Vec4s used by the parameters
	ShaderInput **param_inputs; // the nodes for shader input parameters (SIDT_NODEFAULT)
	ShaderAssemblerTreeNodeInput **param_node_inputs; // parallel to param_inputs, for easy access to input information
	int *temporaries; // EArrayInt
	char *params_block;
	char *temps_block;
	char *code_block;
	char *textures_block;
	char *final_output;
	char *final_output_multilight;
	FileList source_files;

	// for varmapping:
	StashTable stMappings;
	int num_temps_alloced; // total number of temps allocated in the program
	int num_temps_used; // number of temps used by the fragment we're assembling

	U32 has_color_tint:1;
} ShaderAssemblerState;

MP_DEFINE(ShaderAssemblerTreeNodeConnection);
MP_DEFINE(ShaderAssemblerTreeNode);

static void destoryShaderAssemblerTreeNodeConnection(ShaderAssemblerTreeNodeConnection *obj)
{
	MP_FREE(ShaderAssemblerTreeNodeConnection, obj);
}

static void clearShaderAssemblerState(ShaderAssemblerState *sastate)
{
	int i, j;
	for (i=0; i<sastate->num_nodes; i++) {
		ShaderAssemblerTreeNode *node = sastate->nodes[i];
		SAFE_FREE(node->inputs);
		assert(node->outputs || !node->num_outputs);
		for (j=0; j<node->num_outputs; j++) {
			eaDestroyEx(&node->outputs[j].cons, destoryShaderAssemblerTreeNodeConnection);
		}
		SAFE_FREE(node->outputs);
		MP_FREE(ShaderAssemblerTreeNode, node);
	}
	SAFE_FREE(sastate->nodes);

	estrClear(&sastate->code_block);
	estrClear(&sastate->params_block);
	estrClear(&sastate->temps_block);
	estrClear(&sastate->textures_block);
	sastate->filename = NULL;
	sastate->has_color_tint = 0;
	sastate->mat_profile = NULL;
	sastate->num_nodes = 0;
	sastate->num_param_vectors = 0;
	eaClear(&sastate->param_inputs);
	eaClear(&sastate->param_node_inputs);
	sastate->num_temps_alloced = 0;
	sastate->num_temps_used = 0;
	sastate->num_textures = 0;
	sastate->root = NULL;
	sastate->shader_graph = NULL;
	stashTableClear(sastate->stMappings);
	eaiSetSize(&sastate->temporaries, 0);
}

static void destroyShaderAssemblerState(ShaderAssemblerState *sastate)
{
	clearShaderAssemblerState(sastate);
	estrDestroy(&sastate->code_block);
	estrDestroy(&sastate->params_block);
	estrDestroy(&sastate->temps_block);
	estrDestroy(&sastate->textures_block);
	estrDestroy(&sastate->final_output);
	estrDestroy(&sastate->final_output_multilight);
	stashTableDestroy(sastate->stMappings);
	eaiDestroy(&sastate->temporaries);
	eaDestroy(&sastate->param_inputs);
	eaDestroy(&sastate->param_node_inputs);
}

static ShaderAssemblerTreeNode *findNodeByName(ShaderAssemblerState *sastate, const char *op_name)
{
	int i;
	for (i=0; i<sastate->num_nodes; i++) {
		if (stricmp(sastate->nodes[i]->op->op_name, op_name)==0)
			return sastate->nodes[i];
	}
	ErrorFilenamef(sastate->filename, "Shader graph has an input referencing a non-existent Operation (\"%s\")!", op_name);
	return NULL;
}

static void allocShaderTreeNode(ShaderAssemblerState *sastate, ShaderAssemblerTreeNode *node)
{
	int i;
	node->num_inputs = GET_REF(node->op->h_op_definition) ? eaSize(&GET_REF(node->op->h_op_definition)->op_inputs) : 0;
	node->inputs = calloc(sizeof(node->inputs[0]), node->num_inputs);
	for (i=0; i<node->num_inputs; i++)  {
		node->inputs[i].temporary_index = -1;
		node->inputs[i].tex_index = -1;
	}
	node->num_outputs = GET_REF(node->op->h_op_definition) ? eaSize(&GET_REF(node->op->h_op_definition)->op_outputs) : 0;
	node->outputs = calloc(sizeof(node->outputs[0]), node->num_outputs);
	for (i=0; i<node->num_outputs; i++) 
		node->outputs[i].temporary_index = -1;
}

static void makeShaderTreeNode(ShaderAssemblerState *sastate, ShaderAssemblerTreeNode *node)
{
	int i, j;
	bool bDidInstanceParam=false;
	// Go through each input, and find the node it references
	for (i=0; i<node->num_inputs; i++) {
		const ShaderOperationDef *op_def = GET_REF(node->op->h_op_definition);
		ShaderInput *op_input = op_def ? op_def->op_inputs[i] : NULL;
		ShaderInputEdge *input_edge = op_input ? materialFindInputEdgeByName(node->op, op_input->input_name) : NULL;

		if (gfxMaterialsShouldSkipOpInput(op_input, sastate->shader_graph))
			continue;

		if (input_edge) {
			ShaderAssemblerTreeNode *input_node;
			// This links to another node, store the input information
			node->inputs[i].con.input_edge = input_edge;
			input_node = findNodeByName(sastate, input_edge->input_source_name);
			node->inputs[i].con.node = input_node;
			// Find the node and add this as an output
			if (input_node)
				j = materialFindOutputIndexByName(GET_REF(input_node->op->h_op_definition), input_edge->input_source_output_name);
			else
				j = -1;
			if (j!=-1) {
				ShaderAssemblerTreeNodeConnection *new_connection;
				MP_CREATE(ShaderAssemblerTreeNodeConnection, 16);
				new_connection = MP_ALLOC(ShaderAssemblerTreeNodeConnection);
				new_connection->node = node;
				new_connection->input_edge = input_edge;
				eaPush(&input_node->outputs[j].cons, new_connection);
			} else {
				// Bad!
			}
		} else if (op_input) {
			// Must use the default
			//node->inputs[i] = NULL;
			if (op_input->input_default.default_type == SIDT_NODEFAULT && op_input->num_floats && !materialFindFixedInputByName(node->op, op_input->input_name))
			{
				// This node needs a parameter
				node->inputs[i].param_runtime_input = materialFindRuntimeInputByName(node->op, op_input->input_name);
				assert(node->inputs[i].param_runtime_input);
				if (!bDidInstanceParam && node->op->instance_param)
				{
					bDidInstanceParam = true;
					assert(node->inputs[i].param_runtime_input->input_instance_param);
				} else {
					assert(!node->inputs[i].param_runtime_input->input_instance_param);
				}
				
				eaPush(&sastate->param_inputs, op_input);
				eaPush(&sastate->param_node_inputs, &node->inputs[i]);
			}
		}
		if (op_input && op_input->num_texnames) {
			assert(op_input->num_texnames==1);
			assert(node->inputs[i].tex_index == -1);
			node->inputs[i].tex_index = sastate->num_textures++;
		}
	}
}

static void makeShaderTree(ShaderAssemblerState *sastate)
{
	int i;
	MP_CREATE(ShaderAssemblerTreeNode, 16);

	sastate->nodes = calloc(sizeof(sastate->nodes[0]), eaSize(&sastate->shader_graph->operations));
	sastate->num_nodes = 0;
	for (i=0; i<eaSize(&sastate->shader_graph->operations); i++) {
		const ShaderOperationDef *op_def = GET_REF(sastate->shader_graph->operations[i]->h_op_definition);
		if (!op_def) // BAD!
			continue;
		FileListInsertChecksum(&sastate->source_files, op_def->filename, 0); // TODO: Not valid in production mode
		sastate->nodes[sastate->num_nodes] = MP_ALLOC(ShaderAssemblerTreeNode);
		sastate->nodes[sastate->num_nodes]->op = sastate->shader_graph->operations[i];
		if (op_def->op_type == SOT_SINK) {
			assert(!sastate->root); // Can only have one output operation!  If this goes off, we need to validate/fix the lack of an Output node in the verifyGraph step.
			sastate->root = sastate->nodes[sastate->num_nodes];
		}
		allocShaderTreeNode(sastate, sastate->nodes[sastate->num_nodes]);
		sastate->num_nodes++;
	}
	assert(sastate->root); // Needs an output operation!  If this goes off, we need to validate/fix the lack of an Output node in the verifyGraph step.
	for (i=0; i<sastate->num_nodes; i++) {
		makeShaderTreeNode(sastate, sastate->nodes[i]);
	}
}

static void orderNodesRecur(ShaderAssemblerState *sastate, ShaderAssemblerTreeNode *node)
{
	int i;
	int new_depth = node->depth+1;
	if (new_depth > sastate->num_nodes+1) {
		// Verification done earlier:
		//ErrorFilenamef(sastate->filename, "ShaderGraph contains an infinite loop!");
		return;
	}
	for (i=0; i<node->num_inputs; i++) {
		if (node->inputs[i].con.node) {
			if (new_depth > node->inputs[i].con.node->depth) {
				node->inputs[i].con.node->depth = new_depth;
				orderNodesRecur(sastate, node->inputs[i].con.node);
			}
		}
	}
}

static int cmpNodeDepth(const ShaderAssemblerTreeNode **a, const ShaderAssemblerTreeNode **b)
{
	return (*b)->depth - (*a)->depth;
}

static void orderNodes(ShaderAssemblerState *sastate)
{
	int i;
	// Fill in tree depth information
	orderNodesRecur(sastate, sastate->root);
	// Prune orphaned nodes (flag them with depth == -1) (verification/errors done at startup)
	for (i=0; i<sastate->num_nodes; i++) {
		if (sastate->nodes[i]->depth==0 && sastate->nodes[i] != sastate->root) {
			sastate->nodes[i]->depth = -1;
		}
	}
	qsort(sastate->nodes, sastate->num_nodes, sizeof(sastate->nodes[0]), cmpNodeDepth);
}

static int assignTemporary(ShaderAssemblerState *sastate)
{
	int i;
	for (i=0; i<eaiSize(&sastate->temporaries); i++) {
		if (sastate->temporaries[i]==-1)
			return i; // Caller must fill things in
	}
	return eaiPush(&sastate->temporaries, -1);
}


static void assignTemporaries(ShaderAssemblerState *sastate)
{
	// This could be made more efficient by actually keeping track of which outputs have been hit, and clearing the
	//  temps when all outputs are hit instead of when the depth changes (so that a second op at the same depth can use
	//  an input temp from a previous one as one of it's outputs.).  This could be simply implemented by doing a pass
	//  that just sets the depth equal to the index in the nodes[] array.
	int i, j, k, ii;
	int cur_depth=-1;
	// Nodes are in order, simply walk them, and on depth changes, clear temporaries that end at said depth
	for (i=0; i<sastate->num_nodes; i++) {
		ShaderAssemblerTreeNode *node = sastate->nodes[i];
		if (node->depth == -1)
			break;
		if (cur_depth!=node->depth) {
			cur_depth = node->depth;
			// Clear temporaries in use that are not needed at depth cur_depth
			if (sastate->temporaries) {
				for (j=eaiSize(&sastate->temporaries)-1; j>=0; j--) {
					if (sastate->temporaries[j]>cur_depth) {
						sastate->temporaries[j] = -1; // Not in use
					}
				}
			}
		}
		// Assign a temp for each output, and find the matching input and store the value there too.
		for (j=0; j<node->num_outputs; j++) {
			ShaderAssemblerTreeNodeOutput *output = &node->outputs[j];
			output->temporary_index = assignTemporary(sastate);
			if (eaSize(&output->cons)) {
				for (k=eaSize(&output->cons)-1; k>=0; k--) {
					ShaderAssemblerTreeNodeConnection *con = output->cons[k];
					if (con->node->depth == -1)
						continue;
					// Store depth at which this temporary expires
					assert(sastate->temporaries);
					if (sastate->temporaries[output->temporary_index]==-1) {
						sastate->temporaries[output->temporary_index] = con->node->depth;
					} else {
						sastate->temporaries[output->temporary_index] = MIN(sastate->temporaries[output->temporary_index], con->node->depth);
					}
					// Update inputs on the other end to have this temporary index
					// Find the right one
					ii = materialFindInputIndexByName(GET_REF(con->node->op->h_op_definition), con->input_edge->input_name);
					if (ii != -1) {
						con->node->inputs[ii].temporary_index = output->temporary_index;
					} else {
						assert(0); // Internal consistency
					}
				}
			} else {
				sastate->temporaries[output->temporary_index] = cur_depth;
			}
		}
	}
}

static const char *getTempName(int index)
{
	static char **temps;
    // Not quite thread-safe
	if (index < 0) {
		Errorf("Index for temp in shader is not valid!");
		index = 0;
	}
	while (index >= eaSize(&temps)) {
		char temp_name[32];
		sprintf(temp_name, "MATemp%d", eaSize(&temps));
		eaPush(&temps, strdup(temp_name));
	}
	return temps[index];
}

static const char *getLocalTempName(int index)
{
	static char **temps;
	// Not quite thread-safe
	assert(index >= 0);
	while (index >= eaSize(&temps)) {
		char temp_name[32];
		sprintf(temp_name, "MALTemp%d", eaSize(&temps));
		eaPush(&temps, strdup(temp_name));
	}
	return temps[index];
}


static void generateTemps(ShaderAssemblerState *sastate)
{
	int i;
	int num_temps = eaiSize(&sastate->temporaries);
	estrConcatf(&sastate->temps_block, "%s ----- Begin generateTemps ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
	for (i=0; i<num_temps; i++) {
		estrConcatf(&sastate->temps_block, FORMAT_OK(sastate->mat_profile->gen_temp), getTempName(i));
		estrConcatStatic(&sastate->temps_block, "\n");
	}
	estrConcatf(&sastate->temps_block, "%s -----   End generateTemps ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
}

static char **paramTemps;
static const char *getParamName(const ShaderAssemblerTreeNodeInput* input)
{
	int index = input->param_runtime_input->input_register;

	assert(index >= 0);
	
	// Not quite thread-safe
	while (index >= eaSize(&paramTemps)) {
		char temp_name[32];
		sprintf(temp_name, "MAParam%d", eaSize(&paramTemps));
		eaPush(&paramTemps, strdup(temp_name));
	}
	return paramTemps[index];
}

static const int getParamRegister(const ShaderAssemblerTreeNodeInput* input)
{
	return input->param_runtime_input->input_register;
}

static __forceinline RoundFloatCountToVec4Count(int num_floats)
{
	return ( num_floats + 3 ) / 4;
}

static const int getParamRegisterCount(const ShaderAssemblerTreeNodeInput* input)
{
	return RoundFloatCountToVec4Count( input->param_runtime_input->input_swizzle_count );
}

static const char *getParamExpr(const ShaderAssemblerState *sastate, const ShaderAssemblerTreeNodeInput* input)
{
	char buffer[ 256 ];
	int index = input->param_runtime_input->input_register;
	int swizzle = input->param_runtime_input->input_swizzle_start;
	int swizzle_count = input->param_runtime_input->input_swizzle_count;

	assert( index >= 0 && index < eaSize( &paramTemps ));
	assert( swizzle < 0 || swizzle + swizzle_count <= 4 );

	if (input->param_runtime_input->input_instance_param)
	{
		strcpy(buffer, sastate->mat_profile->gen_instance_param);
	} else if( swizzle >= 0 ) {
		// TODO: these should be in the .MaterialProfile if they need to be different on different architectures.
		const char* float4Formats[] = { "float4( %s.%s, 0, 0, 1 ).xxxx",
										"float4( %s.%s, 0, 1 )",
										"float4( %s.%s, 1 )",
										"%s" };
		const char swizzleBuf[] = "xyzwxyzw";
		char buf[5];
	
		memcpy( buf, swizzleBuf + swizzle, swizzle_count );
		buf[swizzle_count] = '\0';

		sprintf( buffer, FORMAT_OK(float4Formats[ swizzle_count - 1 ]), paramTemps[index], buf );
	} else {
		strcpy( buffer, paramTemps[index]);
	}

	return allocAddCaseSensitiveString( buffer );
}

static void generateParams(ShaderAssemblerState *sastate)
{
	int i, vec_param_origin = sastate->mat_profile->gen_param_offset;
	int num_params = eaSize(&sastate->param_inputs);
	ShaderGraphRenderInfo* renderInfo = sastate->shader_graph->graph_render_info;
	assert( renderInfo );
	
	estrConcatf(&sastate->params_block, "%s ---- Begin generateParams ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
	if (sastate->mat_profile->material_buffer_name) {
		assert(sastate->mat_profile->constant_buffer_start);
		estrConcatf(&sastate->params_block, FORMAT_OK(sastate->mat_profile->constant_buffer_start), sastate->mat_profile->material_buffer_name, sastate->mat_profile->material_buffer_base_register);
		estrConcatStatic(&sastate->params_block, "\n");
	}

	// Any operation which does not have an input linkage and which does not
	//   have a default value gets a parameter assigned.
	// Any operation with a default value does *not* get a parameter and
	//   therefore cannot be overridden in a Material definition.
	{
		bool* regIsPrinted = alloca( sizeof( bool ) * num_params * 4 );		
		int max_index = 0;

		memset( regIsPrinted, 0, sizeof( bool ) * num_params * 4 );
		
		for (i=0; i<num_params; i++) {
			const char * paramType;

			if( regIsPrinted[ sastate->param_node_inputs[i]->param_runtime_input->input_register ]) {
				continue;
			}
			
			switch ( sastate->param_inputs[i]->num_floats )
			{
				xcase 8:
					paramType = sastate->mat_profile->gen_paramMatrix42;
				xcase 12:
					paramType = sastate->mat_profile->gen_paramMatrix43;
				xcase 16:
					paramType = sastate->mat_profile->gen_paramMatrix44;
				xdefault:
					paramType = sastate->mat_profile->gen_param;
			}
			estrConcatf(&sastate->params_block, FORMAT_OK(paramType), getParamName(sastate->param_node_inputs[i]), vec_param_origin + getParamRegister(sastate->param_node_inputs[i]));
			estrConcatStatic(&sastate->params_block, "\n");
			
			MAX1( max_index, getParamRegister(sastate->param_node_inputs[i]) + getParamRegisterCount(sastate->param_node_inputs[i]));
			regIsPrinted[sastate->param_node_inputs[i]->param_runtime_input->input_register] = true;
		}

		sastate->num_param_vectors = max_index;
	}
	if (sastate->mat_profile->material_buffer_name) {
		assert(sastate->mat_profile->constant_buffer_end);
		estrConcatf(&sastate->params_block, FORMAT_OK(sastate->mat_profile->constant_buffer_end));
		estrConcatStatic(&sastate->params_block, "\n");
	}

	estrConcatf(&sastate->params_block, "%s ----   End generateParams ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
}

static void generateTextures(ShaderAssemblerState *sastate)
{
	// Add texture sampler of compile target requires it
	if (sastate->mat_profile->gen_texsampler2D)
	{
		int j;
		for (j=0; j<sastate->num_nodes; j++)
		{
			int i;
			ShaderAssemblerTreeNode *node = sastate->nodes[j];
			const ShaderOperation *op = node->op;
			const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
			for (i=0; i<node->num_inputs; i++)
			{
				const ShaderInput *op_input = op_def ? op_def->op_inputs[i] : NULL;
				if (gfxMaterialsShouldSkipOpInput(op_input, sastate->shader_graph))
					continue;
				if (!op_input || op_input->input_not_for_assembler)
					continue;
				if (!node->inputs[i].con.node && op_input->input_default.default_type == SIDT_NODEFAULT && op_input->num_texnames)
				{
					if (node->inputs[i].tex_index >= 0)
					{
						bool bNeedPart2=true;
						assert(!op_input->num_floats);
						if (op_input->data_type == SDT_TEXTURE3D) {
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler3D), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
						} else if (op_input->data_type == SDT_TEXTURECUBE) {
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsamplerCUBE), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
						} else if (op_input->data_type == SDT_TEXTURE_AMBIENT_CUBE) {
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsamplerCUBE), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
						} else if (op_input->data_type == SDT_TEXTURE_REFLECTION) {
							estrConcatf(&sastate->textures_block, "#ifdef ReflectionCubemap\n");
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsamplerCUBE), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
							if (sastate->mat_profile->gen_texsampler_part2)
							{
								estrConcatStatic(&sastate->textures_block, "\n");
								estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler_part2), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
							}
							estrConcatf(&sastate->textures_block, "\n#elifdef ReflectionSimple\n");
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler2D), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
							if (sastate->mat_profile->gen_texsampler_part2)
							{
								estrConcatStatic(&sastate->textures_block, "\n");
								estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler_part2), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
							}
							estrConcatf(&sastate->textures_block, "\n#endif");
							bNeedPart2 = false;
						} else {
							// SDT_TEXTURE, SCREENCOLOR, etc
							estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler2D), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
						}
						estrConcatStatic(&sastate->textures_block, "\n");
						if (bNeedPart2)
						{
							if (sastate->mat_profile->gen_texsampler_part2)
							{
								estrConcatf(&sastate->textures_block, FORMAT_OK(sastate->mat_profile->gen_texsampler_part2), "TexSampler", node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index, node->inputs[i].tex_index);
								estrConcatStatic(&sastate->textures_block, "\n");
							}
						}
					}
				}
			}
		}
	}
}

static void addMapping(ShaderAssemblerState *sastate, const char *from, bool from_alloc, const char *to, bool to_alloc)
{
	if (!stashAddPointer(sastate->stMappings, from_alloc?allocAddCaseSensitiveString(from):from, to_alloc?(char*)allocAddCaseSensitiveString(to):to, false)) {
		assertmsg(0, "Duplicate keys?");
	}
}

static void resetMappings(ShaderAssemblerState *sastate)
{
	if (sastate->stMappings) {
		stashTableClear(sastate->stMappings);
	} else {
		sastate->stMappings = stashTableCreateWithStringKeys(16,StashDefault);
	}
}

static void appendVarmappedCode(ShaderAssemblerState *sastate, char **dest, const char *src, const char *shader_filename)
{
	sastate->num_temps_used = 0;

	while (*src) {
		const char *end = strchr(src, '%');
		if (!end) {
			int len = (int)strlen(src);
			estrConcat(dest, src, len);
			src += len;
		} else if (end[1]=='%') { // %%
			estrConcat(dest, src, end - src + 1);
			src = end + 2;
		} else {
			// Must be a variable!
			const char *varptr = end + 1;
			const char *varendptr = strchr(varptr, '%');
			// Append code preceding the variable
			estrConcat(dest, src, end - src);
			if (!varendptr) {
				ErrorFilenamef(shader_filename, "Mismatched %% around \"%s\" in shader file.", varptr);
				src++;
			} else {
				char key_buf[MAX_VARMAPNAME_LENGTH + 1];
				char *value;
				if (varendptr - varptr >= ARRAY_SIZE(key_buf))
				{
					strncpy(key_buf, varptr, MAX_VARMAPNAME_LENGTH);
					ErrorFilenamef(shader_filename, "Variable map \"%s\" longer-than %d characters. Possible mismatched %%'s while loading \"%s\"", key_buf, MAX_VARMAPNAME_LENGTH, sastate->filename);
				}
				else
					strncpy(key_buf, varptr, varendptr - varptr);
				if (!stashFindPointer(sastate->stMappings, key_buf, &value)) {
					if (strStartsWith(key_buf, "GenTemp:"))
					{
						// The shader needs a temp assigned
						const char *tempname = getLocalTempName(sastate->num_temps_used);
						const char *srcname = key_buf + 8; //strlen("GenTemp:");
						addMapping(sastate, srcname, true, tempname, false);
						sastate->num_temps_used++;
						if (sastate->num_temps_alloced < sastate->num_temps_used) {
							sastate->num_temps_alloced++;
                            estrConcatf(dest, FORMAT_OK(sastate->mat_profile->gen_temp), tempname);
							estrConcatStatic(dest, "\n");
						}
					} else {
						ErrorFilenamef(shader_filename, "Variable \"%s\" does not have a mapping while loading \"%s\"", key_buf, sastate->filename);
						estrConcatf(dest, "%%VarNotFound:%s%%", key_buf);
					}
				} else {
					// Found a replacement
					estrConcat(dest, value, (int)strlen(value));
				}
				src = varendptr+1;
			}
		}
	}
}

static void genConst(ShaderAssemblerState *sastate, char *buf, size_t buf_size, F32 *floats)
{
	int num_floats = eafSize(&floats);
	assert(INRANGE(num_floats, 1, 5));
	sprintf_s(SAFESTR2(buf), FORMAT_OK(sastate->mat_profile->gen_const[num_floats-1]), floats[0], (num_floats>1)?floats[1]:0, (num_floats>2)?floats[2]:0, (num_floats>3)?floats[3]:0);
}

static void generateCodeSnippet(ShaderAssemblerState *sastate, char **dest, ShaderAssemblerTreeNode *node, bool bAddDebugInfo)
{
	const char *code=NULL;
	char *freeme=NULL;
	char *freeme2=NULL;
	int i;
	ShaderOperation *op = node->op;
	const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
	char *shader_filename=NULL;
	int recursiveCount=0;
	int mappedCodeStart=0;

#define ADD_MAPPING(from, from_alloc, to, to_alloc) \
		addMapping(sastate, from, from_alloc, to, to_alloc); \
		if (bAddDebugInfo) estrConcatf(dest, "%s Mapping %%%s%%\t-> %s\n", sastate->mat_profile->comment_begin, from, to);

	if (bAddDebugInfo && op_def)
		estrConcatf(dest, "%s -- Begin Operation: \"%s\"  OpType: \"%s\" -- %s\n", sastate->mat_profile->comment_begin, op->op_name, op_def->op_type_name, sastate->mat_profile->comment_end);

	// Make table of input and output mappings
	for (i=0; i<node->num_inputs; i++) {
		char buf[MAX_VARMAPNAME_LENGTH + 1];
		const ShaderInput *op_input = op_def ? op_def->op_inputs[i] : NULL;
		// Not skipping this, because, even if they're in comments, we need to add mappings for the variables
		//if (gfxMaterialsShouldSkipOpInput(op_input))
		//	continue;
		if (!op_input || op_input->input_not_for_assembler)
			continue;
		if (node->inputs[i].con.node) {
			char *swizzle = node->inputs[i].con.input_edge->input_swizzle;
			const char *lookup = sastate->mat_profile->swizzlemap;
			sprintf(buf, "%s.%c%c%c%c", getTempName(node->inputs[i].temporary_index), lookup[swizzle[0]], lookup[swizzle[1]], lookup[swizzle[2]], lookup[swizzle[3]]);
			ADD_MAPPING(op_input->input_name, false, buf, true);
		} else {
			switch(op_input->input_default.default_type) {
			xcase SIDT_VALUE:
				genConst(sastate, SAFESTR(buf), op_input->input_default.default_floats);
				ADD_MAPPING(op_input->input_name, false, buf, true);
			xcase SIDT_TEXCOORD0:
				ADD_MAPPING(op_input->input_name, false, sastate->mat_profile->gen_texcoord0, false);
			xcase SIDT_TEXCOORD1:
				ADD_MAPPING(op_input->input_name, false, sastate->mat_profile->gen_texcoord1, false);
			xcase SIDT_COLOR: // Need to be made into a special parameter
				sastate->has_color_tint = 1;
				ADD_MAPPING(op_input->input_name, false, sastate->mat_profile->gen_color0, false);
			xcase SIDT_NODEFAULT: // Need to be made into a parameter
				if (op_input->num_texnames) {
					assert(!op_input->num_floats);
					sprintf(buf, FORMAT_OK(sastate->mat_profile->gen_texname), node->inputs[i].tex_index);
					ADD_MAPPING(op_input->input_name, false, buf, true);
					// Adding of texture samplers was done in generateTextures
				} else {
					ShaderFixedInput *fixed_input = materialFindFixedInputByName(node->op, op_input->input_name);
					if (!fixed_input) {
						ADD_MAPPING(op_input->input_name, false, getParamExpr(sastate, &node->inputs[i]), false);
					} else {
						genConst(sastate, SAFESTR(buf), fixed_input->fvalues);
						ADD_MAPPING(op_input->input_name, false, buf, true);
					}
				}
			xdefault:
				assert(0);
			}
		}
	}
	for (i=0; i<node->num_outputs; i++) {
		if (node->outputs[i].temporary_index == -1) {
			assert(0);
		} else if (op_def) {
			ADD_MAPPING(op_def->op_outputs[i]->output_name, false, getTempName(node->outputs[i].temporary_index), false);
		}
	}
	// Get the code snippet
	code = op_def ? getCodeForOperation(sastate->mat_profile, op_def->op_type_name, &shader_filename) : NULL;
	if (code) {
		char *value;
		char *s;

		FileListInsertChecksum(&sastate->source_files, shader_filename, 0);

		// Do first-pass replacement for LIGHTING_MODEL, which needs a second pass of variable replacement
		if (stashFindPointer(sastate->stMappings, "LIGHTING_MODEL", &value) && (s = strstri(code, "%LIGHTING_MODEL%"))) {
			int freeme_size = (int)strlen(code) + (int)strlen(value) + 1;
			freeme = malloc(freeme_size);
			strncpy_s(SAFESTR2(freeme), code, s - code);
			strcat_s(SAFESTR2(freeme), value);
			strcat_s(SAFESTR2(freeme), s + strlen("%LIGHTING_MODEL%"));
			code = freeme;
		}
	}

	// Fill in the code snippet (into code_block) while replacing appropriate fields, erroring on unknown variables
	appendVarmappedCode(sastate, dest, code, shader_filename);

	if (bAddDebugInfo && op_def)
		estrConcatf(dest, "%s --   End Operation: \"%s\"  OpType: \"%s\" -- %s\n", sastate->mat_profile->comment_begin, op->op_name, op_def->op_type_name, sastate->mat_profile->comment_end);

	SAFE_FREE(freeme);
	SAFE_FREE(freeme2);
#undef ADD_MAPPING
}


static void generateCode(ShaderAssemblerState *sastate)
{
	int i;
	estrClear(&sastate->code_block);
	estrConcatf(&sastate->code_block, "%s ---- Begin generateCode ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
	for (i=0; i<sastate->num_nodes; i++) {
		if (sastate->nodes[i] == sastate->root)
			break;
		resetMappings(sastate);
		generateCodeSnippet(sastate, &sastate->code_block, sastate->nodes[i], true);
	}
	estrConcatf(&sastate->code_block, "%s ----   End generateCode ---- %s\n", sastate->mat_profile->comment_begin, sastate->mat_profile->comment_end);
}

static void generateOutput(ShaderAssemblerState *sastate)
{
	estrClear(&sastate->final_output);
	estrClear(&sastate->final_output_multilight);

	// Put a header saying what template this is from, so it shows up in Pix
	estrPrintf(&sastate->final_output, "%s Template: %s %s\n", sastate->mat_profile->comment_begin, sastate->shader_graph->filename, sastate->mat_profile->comment_end);
	// Copy the same header to the multilight output
	estrConcat(&sastate->final_output_multilight, sastate->final_output, estrLength(&sastate->final_output));

	resetMappings(sastate);
	addMapping(sastate, "PARAMS", false, sastate->params_block?sastate->params_block:"", false);
	addMapping(sastate, "TEMPS", false, sastate->temps_block?sastate->temps_block:"", false);
	addMapping(sastate, "CODE", false, sastate->code_block?sastate->code_block:"", false);
	addMapping(sastate, "TEXSAMPLERS", false, sastate->textures_block?sastate->textures_block:"", false);
	addMapping(sastate, "LIGHTING_MODEL", false, "", false);
	generateCodeSnippet(sastate, &sastate->final_output, sastate->root, false);
	if (sastate->lighting_model)
	{
		resetMappings(sastate);
		addMapping(sastate, "PARAMS", false, sastate->params_block?sastate->params_block:"", false);
		addMapping(sastate, "TEMPS", false, sastate->temps_block?sastate->temps_block:"", false);
		addMapping(sastate, "CODE", false, sastate->code_block?sastate->code_block:"", false);
		addMapping(sastate, "TEXSAMPLERS", false, sastate->textures_block?sastate->textures_block:"", false);
		addMapping(sastate, "LIGHTING_MODEL", false, sastate->lighting_model->text, false);
		generateCodeSnippet(sastate, &sastate->final_output_multilight, sastate->root, false);
	}
}

void updateShaderGraph(ShaderAssemblerState *sastate)
{
	// This is set at load-time now
	// But, the load-time one doesn't traverse the graph, it just checks all nodes, even if they are un-referenced
// 	if (sastate->has_color_tint) {
// 		assert(sastate->shader_graph->graph_flags & SGRAPH_HANDLES_COLOR_TINT);
// 	} else {
// 		assert(!(sastate->shader_graph->graph_flags & SGRAPH_HANDLES_COLOR_TINT));
// 	}
}

static char **sa_free_me;

void assembleShaderFromGraph(ShaderGraph *shader_graph, const MaterialAssemblerProfile *mat_profile )
{
	static ShaderAssemblerState sastate = {0};
	ASSERT_CALLED_IN_SINGLE_THREAD; // If called from multiple threads : need to make sastate not static and destroy EStrings

	sa_free_me = &sastate.code_block;

	sastate.source_files = shader_graph->graph_render_info->source_files;
	FileListClear(&sastate.source_files);
	sastate.final_output = shader_graph->graph_render_info->shader_text;
	sastate.final_output_multilight = shader_graph->graph_render_info->shader_text_pre_light;
	sastate.shader_graph = shader_graph;
	FileListInsert(&sastate.source_files, shader_graph->filename, shader_graph->timestamp);
	sastate.mat_profile = mat_profile;
	FileListInsertChecksum(&sastate.source_files, mat_profile->filename, 0);
	sastate.filename = shader_graph->filename; // Because ShaderGraph duplicates are merged, this may not accurately reflect what file a given material is actually referencing.
	sastate.lighting_model = gfxGetLightingModel(sastate.mat_profile->device_id, "Standard");
    assert(sastate.lighting_model);
	FileListInsertChecksum(&sastate.source_files, sastate.lighting_model->filename, 0);
	// Make tree
	makeShaderTree(&sastate);
	// Order nodes (update every node's depth) (also prunes nodes)
	orderNodes(&sastate);
	// Assign temporary variables to each output
	assignTemporaries(&sastate);
	// Generate code snippets
	generateTemps(&sastate);
	generateParams(&sastate);
	generateTextures(&sastate);
	assert(sastate.num_param_vectors == shader_graph->graph_render_info->num_input_vectors);
	generateCode(&sastate);
	// Merge together
	generateOutput(&sastate);
	
	updateShaderGraph(&sastate);
	shader_graph->graph_render_info->shader_text = sastate.final_output;
	sastate.final_output = NULL;

	assert(sastate.num_param_vectors == shader_graph->graph_render_info->num_input_vectors);
	shader_graph->graph_render_info->num_textures = sastate.num_textures;

	// Set up multi-light chunks
	if (sastate.final_output_multilight) 
	{
		char *s;
		shader_graph->graph_render_info->shader_text_pre_light = sastate.final_output_multilight;
		s = strstri(shader_graph->graph_render_info->shader_text_pre_light, "$PerLight$");
		assertmsg(s, "LightingModel::Text missing $PerLight$ tag");
		*s = '\0';
		s+=strlen("$PerLight$");
		shader_graph->graph_render_info->shader_text_post_light = s;
		shader_graph->graph_render_info->shader_text_per_light = sastate.lighting_model->perlight;
		shader_graph->graph_render_info->shader_text_shadow_buffer = sastate.lighting_model->lightshadowbuffer;

		shader_graph->graph_render_info->shader_text_pre_light_length = (int)strlen(shader_graph->graph_render_info->shader_text_pre_light);
		shader_graph->graph_render_info->shader_text_per_light_length = (int)strlen(shader_graph->graph_render_info->shader_text_per_light);
		shader_graph->graph_render_info->shader_text_shadow_buffer_length = (int)strlen(shader_graph->graph_render_info->shader_text_shadow_buffer);
		shader_graph->graph_render_info->shader_text_post_light_length = (int)strlen(shader_graph->graph_render_info->shader_text_post_light);

		sastate.final_output_multilight = NULL;
	}

	shader_graph->graph_render_info->source_files = sastate.source_files;
	shader_graph->graph_render_info->mat_profile = mat_profile;

	// Cleanup
	//destroyShaderAssemblerState(&sastate);
	clearShaderAssemblerState(&sastate);

	//printf("%s", sastate.final_output);
}

void shaderAssemblerDoneAssembling(void)
{
	estrDestroy(sa_free_me);
}
