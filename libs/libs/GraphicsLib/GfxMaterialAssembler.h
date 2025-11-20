#ifndef GFXMATERIALASSEMBLER_H
#define GFXMATERIALASSEMBLER_H
#pragma once
GCC_SYSTEM

typedef struct ShaderGraph ShaderGraph;
typedef struct ShaderInputEdge ShaderInputEdge;
typedef struct ShaderOperation ShaderOperation;
typedef struct MaterialAssemblerProfile MaterialAssemblerProfile;

// Returns an EString that needs to be destroyed
// May update shader_graph->flags
void assembleShaderFromGraph(ShaderGraph *shader_graph, const MaterialAssemblerProfile *mat_profile);
void shaderAssemblerDoneAssembling(void);

#endif