
#include "timing.h" // debug only


#include "wlTerrain.h"
#include "wlCurveScripts.h"
#include "wlCurveScripts_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

// Generic intersection and working curve types

void insert_into_node_list(CurveScriptElement *element, F32 index, CurveScriptNode *node)
{
	int k;
	// Insertion sort
	for (k = 0; k < eaSize(&element->nodes); k++)
	{
		int l;
		for (l = 0; l < eaSize(&element->nodes[k]->connectors); l++)
			if (element->nodes[k]->connectors[l]->parent == element &&
				element->nodes[k]->connectors[l]->spline_index > index)
			{
				eaInsert(&element->nodes, node, k);
				return;
			}
	}
	eaPush(&element->nodes, node);
}

void curveElementUpdate(CurveScriptElement **element_list, CurveScriptElement *element)
{
	int i, j, k;
	CurveScriptNode **temp_nodes = NULL;
	CurveScriptNode *prev_node, *next_node;
	eaCopy(&temp_nodes, &element->nodes);
	eaClear(&element->nodes);
	for (i = 0; i < eaSize(&element_list); i++)
		if (element_list[i] != element)
		{
			F32 index1, index2;
			Vec3 position, dir1, dir2;
			bool found_parent1, found_parent2;
			CurveScriptElement *element2 = element_list[i];
			if (splineCheckCollision(&element->spline, &element2->spline, 0, 0,
										&index1, &index2, position, dir1, dir2, 9.0f))
			{
				CurveScriptNode *node = NULL;

				// Search for existing node with these two parent elements
				for (j = 0; j < eaSize(&temp_nodes); j++)
				{
					found_parent1 = false;
					found_parent2 = false;
					for (k = 0; k < eaSize(&temp_nodes[j]->connectors); k++)
						if (temp_nodes[j]->connectors[k]->parent == element) found_parent1 = true;
						else if (temp_nodes[j]->connectors[k]->parent == element2) found_parent2 = true;
					if (found_parent1 && found_parent2)
					{
						if (!node) node = temp_nodes[j];
						eaRemove(&temp_nodes, j);
						insert_into_node_list(element, index1, node);
						break;
					}
				}

				// Search for a nearby node to add to
				if (!node)
				{
					found_parent1 = false;
					found_parent2 = false;
					for (j = 0; j < eaSize(&element_list); j++)
						for (k = 0; k < eaSize(&element_list[j]->nodes); k++)
							if (distance3(position, element_list[j]->nodes[k]->world_position) < 10.f)
								node = element_list[j]->nodes[k];
				}

				// Create a new node if we have to
				if (!node)
				{
					node = calloc(1, sizeof(CurveScriptNode));
				}
				
				// Set locative information
				copyVec3(position, node->world_position);
				dir1[1] = 0; // Force pointing up
				normalVec3(dir1);
				copyVec3(dir1, node->world_direction);
				setVec3(node->world_up, 0, 1, 0);

				if (!found_parent1 || !found_parent2)
				{
					found_parent1 = false;
					found_parent2 = false;
					for (k = 0; k < eaSize(&node->connectors); k++)
						if (node->connectors[k]->parent == element) found_parent1 = true;
						else if (node->connectors[k]->parent == element2) found_parent2 = true;
				}

				if (!found_parent2)
				{
					bool found_node = false;
					CurveScriptNodeConnector *new_connector = calloc(1, sizeof(CurveScriptNodeConnector));
					new_connector->parent = element2;
					copyVec3(dir2, new_connector->exit_vec);
					new_connector->spline_index = index2;
					new_connector->segments[0] = new_connector->segments[1] = NULL;
					eaPush(&node->connectors, new_connector);
					insert_into_node_list(element2, index2, node);
					element2->was_dirty = true;
				}

				if (!found_parent1)
				{
					bool found_node = false;
					CurveScriptNodeConnector *new_connector = calloc(1, sizeof(CurveScriptNodeConnector));
					new_connector->parent = element;
					copyVec3(dir1, new_connector->exit_vec);
					new_connector->spline_index = index1;
					new_connector->segments[0] = new_connector->segments[1] = NULL;
					eaPush(&node->connectors, new_connector);
					insert_into_node_list(element, index1, node);
					element->was_dirty = true;
				}
			}
		}
	for (i = 0; i < eaSize(&temp_nodes); i++)
	{
		for (j = 0; j < eaSize(&temp_nodes[i]->connectors); j++)
			if (temp_nodes[i]->connectors[j]->parent == element)
			{
				SAFE_FREE(temp_nodes[i]->connectors[j]);
				eaRemove(&temp_nodes[i]->connectors, j);
				break;
			}
		if (eaSize(&temp_nodes[i]->connectors) == 0)
			SAFE_FREE(temp_nodes[i]);
	}
	eaDestroy(&temp_nodes);

	// Create segments
	for (i = 0; i < eaSize(&element->segments); i++)
		SAFE_FREE(element->segments[i]);
	eaDestroy(&element->segments);
	prev_node = NULL;
	next_node = NULL;
	for (i = 0; i <= eaSize(&element->nodes); i++)
	{
		CurveScriptSegment *segment;
		bool skip_segment = false;

		if (i < eaSize(&element->nodes)) next_node = element->nodes[i];
		else next_node = NULL;

		if (eaSize(&element->nodes) > 0 && i == 0 && distance3(element->nodes[0]->world_position, &element->spline.spline_points[0]) < 10.f)
			skip_segment = true;
		if (eaSize(&element->nodes) > 0 && element->spline.spline_points && i > 0 && element->nodes[i-1] && i == eaSize(&element->nodes) && 
			distance3(element->nodes[i-1]->world_position, &element->spline.spline_points[eafSize(&element->spline.spline_points)-3]) < 10.f)
			skip_segment = true;

		if (!skip_segment)
		{
			segment = calloc(1, sizeof(CurveScriptSegment));
			segment->index = eaSize(&element->segments);
			segment->parent = element;
			segment->nodes[0] = segment->nodes[1] = NULL;
			segment->connectors[0] = segment->connectors[1] = NULL;

			eaPush(&element->segments, segment);

			// Add to node
			if (prev_node)
			{
				for (j = 0; j < eaSize(&prev_node->connectors); j++)
					if (prev_node->connectors[j]->parent == element)
					{
						prev_node->connectors[j]->segments[1] = segment;
						segment->nodes[0] = prev_node;
						segment->connectors[0] = prev_node->connectors[j];
					}
			}
			if (next_node)
			{
				for (j = 0; j < eaSize(&next_node->connectors); j++)
					if (next_node->connectors[j]->parent == element)
					{
						next_node->connectors[j]->segments[0] = segment;
						segment->nodes[1] = next_node;
						segment->connectors[1] = next_node->connectors[j];
					}
			}
		}

		prev_node = next_node;
	}
}

void curveElementsUpdateAll(CurveScriptElement **element_list)
{
	int i, j;
	bool *dirty_array = calloc(eaSize(&element_list), sizeof(bool));

	loadstart_printf("Updating curves...");

	// Calculate dirty-by-association array
	for (i = 0; i < eaSize(&element_list); i++)
	{
		dirty_array[i] = element_list[i]->dirty;
		element_list[i]->was_dirty = element_list[i]->dirty;
	}
	for (i = 0; i < eaSize(&element_list); i++)
	{
		if (element_list[i]->dirty)
		{
			for (j = 0; j < eaSize(&element_list); j++)
				if (i != j)
				{
					F32 index1, index2;
					F32 start_index1 = 0, start_index2 = 0;
					Vec3 position, dir1, dir2;
					int k, l;
					if (splineCheckCollision(&element_list[i]->spline, &element_list[j]->spline, start_index1, start_index2,
												&index1, &index2, position, dir1, dir2, 9.0f))
					{
						dirty_array[j] = true;
					}
					for (k = 0; k < eaSize(&element_list[j]->nodes); k++)
						for (l = 0; l < eaSize(&element_list[j]->nodes[k]->connectors); l++)
							if (element_list[j]->nodes[k]->connectors[l]->parent == element_list[i])
								dirty_array[j] = true;
				}
			element_list[i]->dirty = false;
		}
	}

	// Remove existing connectors
	for (i = 0; i < eaSize(&element_list); i++)
		if (dirty_array[i])
			for (j = eaSize(&element_list[i]->nodes)-1; j >= 0; j--)
			{
				int k;
				CurveScriptNode *node = element_list[i]->nodes[j];
				for (k = 0; k < eaSize(&node->connectors); k++)
					if (node->connectors[k]->parent == element_list[i])
					{
						SAFE_FREE(node->connectors[k]);
						eaRemove(&node->connectors, k);
						break;
					}
				if (eaSize(&node->connectors) == 0)
					SAFE_FREE(node);
				eaRemove(&element_list[i]->nodes, j);
			}
	// Update elements that are dirty
	for (i = 0; i < eaSize(&element_list); i++)
		if (dirty_array[i])
			curveElementUpdate(element_list, element_list[i]);

	loadend_printf(" done.");

	SAFE_FREE(dirty_array);
}

CurveScriptParameterValue missing_value = { "MISSING", -1 };

CurveScriptParameterValue *curveScriptGetParameter(CurveScriptSegment *segment, char *param_name)
{
	int i;
	CurveScriptElement *element = segment->parent;
	for (i = 0; i < eaSize(&element->parameters); i++)
	{
		CurveScriptParameterDef *def = GET_REF(element->parameters[i]->param);
		if (def && !strcmp(def->param_name, param_name))
		{
			CurveScriptParameterValue *ret = GET_REF(element->parameters[i]->param_value);
			if (ret) return ret;
		}
	}
	{
		CurveScriptParameterDef *def = (CurveScriptParameterDef *)RefSystem_ReferentFromString("CurveScriptParameterDef", param_name);
		if (def && eaSize(&def->param_values) > 0)
		{
			CurveScriptParameterValue *ret = RefSystem_ReferentFromString("CurveScriptParameterValue", def->param_values[0]);
			if (ret) return ret;
		}
	}
	return &missing_value;
}
