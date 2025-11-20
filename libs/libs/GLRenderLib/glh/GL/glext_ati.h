#ifndef __glext_ati_h_
#define __glext_ati_h_
//
// Copyright (C) 1998-2000 ATI Technologies Inc.
//

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APIENTRY
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif


#ifndef GL_EXT_vertex_shader
#define GL_EXT_vertex_shader 1

#define GL_VERTEX_SHADER_EXT								0x8780
#define GL_VERTEX_SHADER_BINDING_EXT						0x8781
#define GL_OP_INDEX_EXT										0x8782
#define GL_OP_NEGATE_EXT									0x8783
#define GL_OP_DOT3_EXT										0x8784
#define GL_OP_DOT4_EXT										0x8785
#define GL_OP_MUL_EXT										0x8786
#define GL_OP_ADD_EXT										0x8787
#define GL_OP_MADD_EXT										0x8788
#define GL_OP_FRAC_EXT										0x8789
#define GL_OP_MAX_EXT										0x878A
#define GL_OP_MIN_EXT										0x878B
#define GL_OP_SET_GE_EXT									0x878C
#define GL_OP_SET_LT_EXT									0x878D
#define GL_OP_CLAMP_EXT										0x878E
#define GL_OP_FLOOR_EXT										0x878F
#define GL_OP_ROUND_EXT										0x8790
#define GL_OP_EXP_BASE_2_EXT								0x8791
#define GL_OP_LOG_BASE_2_EXT								0x8792
#define GL_OP_POWER_EXT										0x8793
#define GL_OP_RECIP_EXT										0x8794
#define GL_OP_RECIP_SQRT_EXT								0x8795
#define GL_OP_SUB_EXT										0x8796
#define GL_OP_CROSS_PRODUCT_EXT								0x8797
#define GL_OP_MULTIPLY_MATRIX_EXT							0x8798
#define GL_OP_MOV_EXT										0x8799
#define GL_OUTPUT_VERTEX_EXT								0x879A
#define GL_OUTPUT_COLOR0_EXT								0x879B
#define GL_OUTPUT_COLOR1_EXT								0x879C
#define GL_OUTPUT_TEXTURE_COORD0_EXT						0x879D
#define GL_OUTPUT_TEXTURE_COORD1_EXT						0x879E
#define GL_OUTPUT_TEXTURE_COORD2_EXT						0x879F
#define GL_OUTPUT_TEXTURE_COORD3_EXT						0x87A0
#define GL_OUTPUT_TEXTURE_COORD4_EXT						0x87A1
#define GL_OUTPUT_TEXTURE_COORD5_EXT						0x87A2
#define GL_OUTPUT_TEXTURE_COORD6_EXT						0x87A3
#define GL_OUTPUT_TEXTURE_COORD7_EXT						0x87A4
#define GL_OUTPUT_TEXTURE_COORD8_EXT						0x87A5
#define GL_OUTPUT_TEXTURE_COORD9_EXT						0x87A6
#define GL_OUTPUT_TEXTURE_COORD10_EXT						0x87A7
#define GL_OUTPUT_TEXTURE_COORD11_EXT						0x87A8
#define GL_OUTPUT_TEXTURE_COORD12_EXT						0x87A9
#define GL_OUTPUT_TEXTURE_COORD13_EXT						0x87AA
#define GL_OUTPUT_TEXTURE_COORD14_EXT						0x87AB
#define GL_OUTPUT_TEXTURE_COORD15_EXT						0x87AC
#define GL_OUTPUT_TEXTURE_COORD16_EXT						0x87AD
#define GL_OUTPUT_TEXTURE_COORD17_EXT						0x87AE
#define GL_OUTPUT_TEXTURE_COORD18_EXT						0x87AF
#define GL_OUTPUT_TEXTURE_COORD19_EXT						0x87B0
#define GL_OUTPUT_TEXTURE_COORD20_EXT						0x87B1
#define GL_OUTPUT_TEXTURE_COORD21_EXT						0x87B2
#define GL_OUTPUT_TEXTURE_COORD22_EXT						0x87B3
#define GL_OUTPUT_TEXTURE_COORD23_EXT						0x87B4
#define GL_OUTPUT_TEXTURE_COORD24_EXT						0x87B5
#define GL_OUTPUT_TEXTURE_COORD25_EXT						0x87B6
#define GL_OUTPUT_TEXTURE_COORD26_EXT						0x87B7
#define GL_OUTPUT_TEXTURE_COORD27_EXT						0x87B8
#define GL_OUTPUT_TEXTURE_COORD28_EXT						0x87B9
#define GL_OUTPUT_TEXTURE_COORD29_EXT						0x87BA
#define GL_OUTPUT_TEXTURE_COORD30_EXT						0x87BB
#define GL_OUTPUT_TEXTURE_COORD31_EXT						0x87BC
#define GL_OUTPUT_FOG_EXT									0x87BD
#define GL_SCALAR_EXT										0x87BE
#define GL_VECTOR_EXT										0x87BF
#define GL_MATRIX_EXT										0x87C0
#define GL_VARIANT_EXT										0x87C1
#define GL_INVARIANT_EXT									0x87C2
#define GL_LOCAL_CONSTANT_EXT								0x87C3
#define GL_LOCAL_EXT										0x87C4
#define GL_MAX_VERTEX_SHADER_INSTRUCTIONS_EXT				0x87C5
#define GL_MAX_VERTEX_SHADER_VARIANTS_EXT					0x87C6
#define GL_MAX_VERTEX_SHADER_INVARIANTS_EXT					0x87C7
#define GL_MAX_VERTEX_SHADER_LOCAL_CONSTANTS_EXT			0x87C8
#define GL_MAX_VERTEX_SHADER_LOCALS_EXT						0x87C9
#define GL_MAX_OPTIMIZED_VERTEX_SHADER_INSTRUCTIONS_EXT		0x87CA
#define GL_MAX_OPTIMIZED_VERTEX_SHADER_VARIANTS_EXT			0x87CB
#define GL_MAX_OPTIMIZED_VERTEX_SHADER_INVARIANTS_EXT		0x87CC
#define GL_MAX_OPTIMIZED_VERTEX_SHADER_LOCAL_CONSTANTS_EXT	0x87CD
#define GL_MAX_OPTIMIZED_VERTEX_SHADER_LOCALS_EXT			0x87CE
#define GL_VERTEX_SHADER_INSTRUCTIONS_EXT					0x87CF
#define GL_VERTEX_SHADER_VARIANTS_EXT						0x87D0
#define GL_VERTEX_SHADER_INVARIANTS_EXT						0x87D1
#define GL_VERTEX_SHADER_LOCAL_CONSTANTS_EXT				0x87D2
#define GL_VERTEX_SHADER_LOCALS_EXT							0x87D3
#define GL_VERTEX_SHADER_OPTIMIZED_EXT						0x87D4
#define GL_X_EXT											0x87D5
#define GL_Y_EXT											0x87D6
#define GL_Z_EXT											0x87D7
#define GL_W_EXT											0x87D8
#define GL_NEGATIVE_X_EXT									0x87D9
#define GL_NEGATIVE_Y_EXT									0x87DA
#define GL_NEGATIVE_Z_EXT									0x87DB
#define GL_NEGATIVE_W_EXT									0x87DC
#define GL_ZERO_EXT											0x87DD
#define GL_ONE_EXT											0x87DE
#define GL_NEGATIVE_ONE_EXT									0x87DF
#define GL_NORMALIZED_RANGE_EXT								0x87E0
#define GL_FULL_RANGE_EXT									0x87E1
#define GL_CURRENT_VERTEX_EXT								0x87E2
#define GL_MVP_MATRIX_EXT									0x87E3
#define GL_VARIANT_VALUE_EXT								0x87E4
#define GL_VARIANT_DATATYPE_EXT								0x87E5
#define GL_VARIANT_ARRAY_STRIDE_EXT							0x87E6
#define GL_VARIANT_ARRAY_TYPE_EXT							0x87E7
#define GL_VARIANT_ARRAY_EXT								0x87E8
#define GL_VARIANT_ARRAY_POINTER_EXT						0x87E9
#define GL_INVARIANT_VALUE_EXT								0x87EA
#define GL_INVARIANT_DATATYPE_EXT							0x87EB
#define GL_LOCAL_CONSTANT_VALUE_EXT							0x87EC
#define GL_LOCAL_CONSTANT_DATATYPE_EXT						0x87ED

typedef GLvoid    (APIENTRY * PFNGLBEGINVERTEXSHADEREXTPROC) (void);
typedef GLvoid    (APIENTRY * PFNGLENDVERTEXSHADEREXTPROC) (void);
typedef GLvoid    (APIENTRY * PFNGLBINDVERTEXSHADEREXTPROC) (GLuint id);
typedef GLuint    (APIENTRY * PFNGLGENVERTEXSHADERSEXTPROC) (GLuint range);
typedef GLvoid    (APIENTRY * PFNGLDELETEVERTEXSHADEREXTPROC) (GLuint id);
typedef GLvoid    (APIENTRY * PFNGLSHADEROP1EXTPROC) (GLenum op, GLuint res, GLuint arg1);
typedef GLvoid    (APIENTRY * PFNGLSHADEROP2EXTPROC) (GLenum op, GLuint res, GLuint arg1,
													  GLuint arg2);
typedef GLvoid    (APIENTRY * PFNGLSHADEROP3EXTPROC) (GLenum op, GLuint res, GLuint arg1,
													  GLuint arg2, GLuint arg3);
typedef GLvoid    (APIENTRY * PFNGLSWIZZLEEXTPROC) (GLuint res, GLuint in, GLenum outX,
													GLenum outY, GLenum outZ, GLenum outW);
typedef GLvoid    (APIENTRY * PFNGLWRITEMASKEXTPROC) (GLuint res, GLuint in, GLenum outX,
													  GLenum outY, GLenum outZ, GLenum outW);
typedef GLvoid    (APIENTRY * PFNGLINSERTCOMPONENTEXTPROC) (GLuint res, GLuint src, GLuint num);
typedef GLvoid    (APIENTRY * PFNGLEXTRACTCOMPONENTEXTPROC) (GLuint res, GLuint src, GLuint num);
typedef GLuint    (APIENTRY * PFNGLGENSYMBOLSEXTPROC) (GLenum dataType, GLenum storageType,
													   GLenum range, GLuint components);
typedef GLvoid    (APIENTRY * PFNGLSETINVARIANTEXTPROC) (GLuint id, GLenum type, GLvoid *addr);
typedef GLvoid    (APIENTRY * PFNGLSETLOCALCONSTANTEXTPROC) (GLuint id, GLenum type, GLvoid *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTBVEXTPROC) (GLuint id, GLbyte *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTSVEXTPROC) (GLuint id, GLshort *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTIVEXTPROC) (GLuint id, GLint *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTFVEXTPROC) (GLuint id, GLfloat *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTDVEXTPROC) (GLuint id, GLdouble *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTUBVEXTPROC) (GLuint id, GLubyte *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTUSVEXTPROC) (GLuint id, GLushort *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTUIVEXTPROC) (GLuint id, GLuint *addr);
typedef GLvoid    (APIENTRY * PFNGLVARIANTPOINTEREXTPROC) (GLuint id, GLenum type,
														   GLuint stride, GLvoid *addr);
typedef GLvoid    (APIENTRY * PFNGLENABLEVARIANTCLIENTSTATEEXTPROC) (GLuint id);
typedef GLvoid    (APIENTRY * PFNGLDISABLEVARIANTCLIENTSTATEEXTPROC) (GLuint id);
typedef GLuint    (APIENTRY * PFNGLBINDLIGHTPARAMETEREXTPROC) (GLenum light, GLenum value);
typedef GLuint    (APIENTRY * PFNGLBINDMATERIALPARAMETEREXTPROC) (GLenum face, GLenum value);
typedef GLuint    (APIENTRY * PFNGLBINDTEXGENPARAMETEREXTPROC) (GLenum unit, GLenum coord,
																GLenum value);
typedef GLuint    (APIENTRY * PFNGLBINDTEXTUREUNITPARAMETEREXTPROC) (GLenum unit, GLenum value);
typedef GLuint    (APIENTRY * PFNGLBINDPARAMETEREXTPROC) (GLenum value);
typedef GLboolean (APIENTRY * PFNGLISVARIANTENABLEDEXTPROC) (GLuint id, GLenum cap);
typedef GLvoid    (APIENTRY * PFNGLGETVARIANTBOOLEANVEXTPROC) (GLuint id, GLenum value,
															   GLboolean *data);
typedef GLvoid    (APIENTRY * PFNGLGETVARIANTINTEGERVEXTPROC) (GLuint id, GLenum value,
															   GLint *data);
typedef GLvoid    (APIENTRY * PFNGLGETVARIANTFLOATVEXTPROC) (GLuint id, GLenum value,
															   GLfloat *data);
typedef GLvoid    (APIENTRY * PFNGLGETVARIANTPOINTERVEXTPROC) (GLuint id, GLenum value,
															   GLvoid **data);
typedef GLvoid    (APIENTRY * PFNGLGETINVARIANTBOOLEANVEXTPROC) (GLuint id, GLenum value,
																 GLboolean *data);
typedef GLvoid    (APIENTRY * PFNGLGETINVARIANTINTEGERVEXTPROC) (GLuint id, GLenum value,
																 GLint *data);
typedef GLvoid    (APIENTRY * PFNGLGETINVARIANTFLOATVEXTPROC) (GLuint id, GLenum value,
															   GLfloat *data);
typedef GLvoid    (APIENTRY * PFNGLGETLOCALCONSTANTBOOLEANVEXTPROC) (GLuint id, GLenum value,
																	 GLboolean *data);
typedef GLvoid    (APIENTRY * PFNGLGETLOCALCONSTANTINTEGERVEXTPROC) (GLuint id, GLenum value,
																	 GLint *data);
typedef GLvoid    (APIENTRY * PFNGLGETLOCALCONSTANTFLOATVEXTPROC) (GLuint id, GLenum value,
																   GLfloat *data);

#endif /* GL_EXT_vertex_shader */


#ifndef GL_ATI_fragment_shader
#define GL_ATI_fragment_shader 1

#define GL_FRAGMENT_SHADER_ATI						0x8920
#define GL_REG_0_ATI								0x8921
#define GL_REG_1_ATI								0x8922
#define GL_REG_2_ATI								0x8923
#define GL_REG_3_ATI								0x8924
#define GL_REG_4_ATI								0x8925
#define GL_REG_5_ATI								0x8926
#define GL_REG_6_ATI								0x8927
#define GL_REG_7_ATI								0x8928
#define GL_REG_8_ATI								0x8929
#define GL_REG_9_ATI								0x892A
#define GL_REG_10_ATI								0x892B
#define GL_REG_11_ATI								0x892C
#define GL_REG_12_ATI								0x892D
#define GL_REG_13_ATI								0x892E
#define GL_REG_14_ATI								0x892F
#define GL_REG_15_ATI								0x8930
#define GL_REG_16_ATI								0x8931
#define GL_REG_17_ATI								0x8932
#define GL_REG_18_ATI								0x8933
#define GL_REG_19_ATI								0x8934
#define GL_REG_20_ATI								0x8935
#define GL_REG_21_ATI								0x8936
#define GL_REG_22_ATI								0x8937
#define GL_REG_23_ATI								0x8938
#define GL_REG_24_ATI								0x8939
#define GL_REG_25_ATI								0x893A
#define GL_REG_26_ATI								0x893B
#define GL_REG_27_ATI								0x893C
#define GL_REG_28_ATI								0x893D
#define GL_REG_29_ATI								0x893E
#define GL_REG_30_ATI								0x893F
#define GL_REG_31_ATI								0x8940
#define GL_CON_0_ATI								0x8941
#define GL_CON_1_ATI								0x8942
#define GL_CON_2_ATI								0x8943
#define GL_CON_3_ATI								0x8944
#define GL_CON_4_ATI								0x8945
#define GL_CON_5_ATI								0x8946
#define GL_CON_6_ATI								0x8947
#define GL_CON_7_ATI								0x8948
#define GL_CON_8_ATI								0x8949
#define GL_CON_9_ATI								0x894A
#define GL_CON_10_ATI								0x894B
#define GL_CON_11_ATI								0x894C
#define GL_CON_12_ATI								0x894D
#define GL_CON_13_ATI								0x894E
#define GL_CON_14_ATI								0x894F
#define GL_CON_15_ATI								0x8950
#define GL_CON_16_ATI								0x8951
#define GL_CON_17_ATI								0x8952
#define GL_CON_18_ATI								0x8953
#define GL_CON_19_ATI								0x8954
#define GL_CON_20_ATI								0x8955
#define GL_CON_21_ATI								0x8956
#define GL_CON_22_ATI								0x8957
#define GL_CON_23_ATI								0x8958
#define GL_CON_24_ATI								0x8959
#define GL_CON_25_ATI								0x895A
#define GL_CON_26_ATI								0x895B
#define GL_CON_27_ATI								0x895C
#define GL_CON_28_ATI								0x895D
#define GL_CON_29_ATI								0x895E
#define GL_CON_30_ATI								0x895F
#define GL_CON_31_ATI								0x8960
#define GL_MOV_ATI									0x8961
#define GL_ADD_ATI									0x8963
#define GL_MUL_ATI									0x8964
#define GL_SUB_ATI									0x8965
#define GL_DOT3_ATI									0x8966
#define GL_DOT4_ATI									0x8967
#define GL_MAD_ATI									0x8968
#define GL_LERP_ATI									0x8969
#define GL_CND_ATI									0x896A
#define GL_CND0_ATI									0x896B
#define GL_DOT2_ADD_ATI								0x896C
#define GL_SECONDARY_INTERPOLATOR_ATI				0x896D
#define GL_NUM_FRAGMENT_REGISTERS_ATI				0x896E
#define GL_NUM_FRAGMENT_CONSTANTS_ATI				0x896F
#define GL_NUM_PASSES_ATI							0x8970
#define GL_NUM_INSTRUCTIONS_PER_PASS_ATI			0x8971
#define GL_NUM_INSTRUCTIONS_TOTAL_ATI				0x8972
#define GL_NUM_INPUT_INTERPOLATOR_COMPONENTS_ATI	0x8973
#define GL_NUM_LOOPBACK_COMPONENTS_ATI				0x8974
#define GL_COLOR_ALPHA_PAIRING_ATI					0x8975
#define GL_SWIZZLE_STR_ATI							0x8976
#define GL_SWIZZLE_STQ_ATI							0x8977
#define GL_SWIZZLE_STR_DR_ATI						0x8978
#define GL_SWIZZLE_STQ_DQ_ATI						0x8979
#define GL_SWIZZLE_STRQ_ATI							0x897A
#define GL_SWIZZLE_STRQ_DQ_ATI						0x897B
#define GL_RED_BIT_ATI								0x00000001
#define GL_GREEN_BIT_ATI							0x00000002
#define GL_BLUE_BIT_ATI								0x00000004
#define GL_2X_BIT_ATI								0x00000001
#define GL_4X_BIT_ATI								0x00000002
#define GL_8X_BIT_ATI								0x00000004
#define GL_HALF_BIT_ATI								0x00000008
#define GL_QUARTER_BIT_ATI							0x00000010
#define GL_EIGHTH_BIT_ATI							0x00000020
#define GL_SATURATE_BIT_ATI							0x00000040
#define GL_COMP_BIT_ATI								0x00000002
#define GL_NEGATE_BIT_ATI							0x00000004
#define GL_BIAS_BIT_ATI								0x00000008


typedef GLuint (APIENTRY *PFNGLGENFRAGMENTSHADERSATIPROC)(GLuint range);
typedef GLvoid (APIENTRY *PFNGLBINDFRAGMENTSHADERATIPROC)(GLuint id);
typedef GLvoid (APIENTRY *PFNGLDELETEFRAGMENTSHADERATIPROC)(GLuint id);
typedef GLvoid (APIENTRY *PFNGLBEGINFRAGMENTSHADERATIPROC)(GLvoid);
typedef GLvoid (APIENTRY *PFNGLENDFRAGMENTSHADERATIPROC)(GLvoid);
typedef GLvoid (APIENTRY *PFNGLPASSTEXCOORDATIPROC)(GLuint dst, GLuint coord, GLenum swizzle);
typedef GLvoid (APIENTRY *PFNGLSAMPLEMAPATIPROC)(GLuint dst, GLuint interp, GLenum swizzle);
typedef GLvoid (APIENTRY *PFNGLCOLORFRAGMENTOP1ATIPROC)(GLenum op, GLuint dst, GLuint dstMask,
									   GLuint dstMod, GLuint arg1, GLuint arg1Rep,
									   GLuint arg1Mod);
typedef GLvoid (APIENTRY *PFNGLCOLORFRAGMENTOP2ATIPROC)(GLenum op, GLuint dst, GLuint dstMask,
									   GLuint dstMod, GLuint arg1, GLuint arg1Rep,
									   GLuint arg1Mod, GLuint arg2, GLuint arg2Rep,
									   GLuint arg2Mod);
typedef GLvoid (APIENTRY *PFNGLCOLORFRAGMENTOP3ATIPROC)(GLenum op, GLuint dst, GLuint dstMask,
									   GLuint dstMod, GLuint arg1, GLuint arg1Rep,
									   GLuint arg1Mod, GLuint arg2, GLuint arg2Rep,
									   GLuint arg2Mod, GLuint arg3, GLuint arg3Rep,
									   GLuint arg3Mod);
typedef GLvoid (APIENTRY *PFNGLALPHAFRAGMENTOP1ATIPROC)(GLenum op, GLuint dst, GLuint dstMod,
									   GLuint arg1, GLuint arg1Rep, GLuint arg1Mod);
typedef GLvoid (APIENTRY *PFNGLALPHAFRAGMENTOP2ATIPROC)(GLenum op, GLuint dst, GLuint dstMod,
									   GLuint arg1, GLuint arg1Rep, GLuint arg1Mod,
									   GLuint arg2, GLuint arg2Rep, GLuint arg2Mod);
typedef GLvoid (APIENTRY *PFNGLALPHAFRAGMENTOP3ATIPROC)(GLenum op, GLuint dst, GLuint dstMod,
									   GLuint arg1, GLuint arg1Rep, GLuint arg1Mod,
									   GLuint arg2, GLuint arg2Rep, GLuint arg2Mod,
									   GLuint arg3, GLuint arg3Rep, GLuint arg3Mod);
typedef GLvoid (APIENTRY *PFNGLSETFRAGMENTSHADERCONSTANTATIPROC)(GLuint dst, const GLfloat *value);

#endif /* GL_ATI_fragment_shader */



#ifdef __cplusplus
}
#endif

#endif /* __glext_ati_h_ */
