//// Interface for raising errors at specific scope/fields for UGC.
////
//// This is used to give tasks to authors to do.
#include"UGCError.h"

#include"StringCache.h"
#include"TextParser.h"
#include"WorldGrid.h"

UGCRuntimeStage *g_CurrentStage = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void ugcRaiseErrorV(UGCRuntimeErrorType type, UGCRuntimeErrorContext* context, const char* fieldName, const char *message_key, const char* extraText, char* message, va_list ap )
{
	char msg_buffer[1024];
	vsprintf(msg_buffer, message, ap);

	assert( g_CurrentStage );
	{
		UGCRuntimeError *error = StructCreate(parse_UGCRuntimeError);
		error->type = type;
		error->context = StructClone( parse_UGCRuntimeErrorContext, context );
		error->field_name = StructAllocString(fieldName);
		error->message = StructAllocString(msg_buffer);
		error->message_key = allocAddString(message_key);
		error->extraText = StructAllocString( extraText );
		eaPush(&g_CurrentStage->errors, error);
	}
}

void ugcRaiseErrorContext(UGCRuntimeErrorType type, UGCRuntimeErrorContext* context, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	ugcRaiseErrorV( type, context, NULL, "Internal", NULL, message, ap );
	va_end( ap );
}

void ugcRaiseErrorInternal(UGCRuntimeErrorType type, const char* dict_name, const char *object_name, char *message, ...)
{
	va_list ap;
	UGCRuntimeErrorContext context = { 0 };
	context.scope = UGC_SCOPE_INTERNAL_DICT;
	context.dict_name = allocAddString(dict_name);
	context.resource_name = allocAddString(object_name);

	va_start( ap, message );
	ugcRaiseErrorV( type, &context, NULL, "Internal", NULL, message, ap );
	va_end( ap );
}

void ugcRaiseErrorInternalCode(UGCRuntimeErrorType type, char *message, ...)
{
	va_list ap;
	UGCRuntimeErrorContext context = { 0 };
	char buffer[1024];
	context.scope = UGC_SCOPE_INTERNAL_CODE;

	va_start( ap, message );
	vsprintf(buffer, message, ap );
	ugcRaiseError( type, &context, "%s", buffer );
	va_end( ap );
}

void ugcRaiseError(UGCRuntimeErrorType type, UGCRuntimeErrorContext* context, const char *message_key, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	ugcRaiseErrorV( type, context, NULL, message_key, NULL, message, ap );
	va_end( ap );
}

void ugcRaiseErrorInField(UGCRuntimeErrorType type, UGCRuntimeErrorContext* context, const char* fieldName, const char *message_key, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	ugcRaiseErrorV( type, context, fieldName, message_key, NULL, message, ap );
	va_end( ap );
}

void ugcRaiseErrorInFieldExtraText(UGCRuntimeErrorType type, UGCRuntimeErrorContext* context, const char* fieldName, const char *message_key, const char* extra_text, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	ugcRaiseErrorV( type, context, fieldName, message_key, extra_text, message, ap );
	va_end( ap );
}

static UGCRuntimeErrorContext g_ctx = { 0 };
static UGCRuntimeErrorContext* ugcMakeErrorScopeCommon(UGCRuntimeErrorScope scope, bool alloc MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx;
	if (alloc)
		ctx = StructCreate_dbg( parse_UGCRuntimeErrorContext, __FUNCTION__ MEM_DBG_PARMS_CALL );
	else
		ctx = &g_ctx;
		StructReset( parse_UGCRuntimeErrorContext, ctx );
	ctx->scope = scope;
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextDefault_Internal(bool alloc MEM_DBG_PARMS)
{
	return ugcMakeErrorScopeCommon(UGC_SCOPE_DEFAULT, alloc MEM_DBG_PARMS_CALL);
}

UGCRuntimeErrorContext* ugcMakeErrorContextMap_Internal(bool alloc, const char *map_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_MAP, alloc MEM_DBG_PARMS_CALL);
	ctx->map_name = StructAllocString_dbg( map_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextRoom_Internal(bool alloc, const char *room_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_ROOM, alloc MEM_DBG_PARMS_CALL);
	ctx->location_name = StructAllocString_dbg( room_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextRoomDoor_Internal(bool alloc, const char *challenge_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_ROOM_DOOR, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( challenge_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextPath_Internal(bool alloc, const char *path_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_PATH, alloc MEM_DBG_PARMS_CALL);
	ctx->location_name = StructAllocString_dbg( path_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextChallenge_Internal(bool alloc, const char *challenge_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_CHALLENGE, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( challenge_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextObjective_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_OBJECTIVE, alloc MEM_DBG_PARMS_CALL);
	ctx->objective_name = StructAllocString_dbg( objective_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextPrompt_Internal(bool alloc, const char *prompt_name, const char *block_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_PROMPT, alloc MEM_DBG_PARMS_CALL);
	ctx->prompt_name = StructAllocString_dbg( prompt_name, caller_fname, line );
	ctx->prompt_block_name = StructAllocString_dbg( block_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextLockedDoor_Internal(bool alloc, const char *target_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_PROMPT, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( target_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextMission_Internal(bool alloc, const char *mission_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_MISSION, alloc MEM_DBG_PARMS_CALL);
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextPortal_Internal(bool alloc, const char *portal_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_PORTAL, alloc MEM_DBG_PARMS_CALL);
	ctx->portal_name = StructAllocString_dbg( portal_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextLayout_Internal(bool alloc, const char *layout_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_LAYOUT, alloc MEM_DBG_PARMS_CALL);
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextEpisodePart_Internal(bool alloc, const char *episode_part MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_EPISODE_PART, alloc MEM_DBG_PARMS_CALL);
	ctx->ep_part_name = StructAllocString_dbg( episode_part, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextSolSysDetailObject_Internal(bool alloc, const char *detail_object_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_SOLSYS_DETAIL_OBJECT, alloc MEM_DBG_PARMS_CALL);
	ctx->solsys_detail_object_name = StructAllocString_dbg( detail_object_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextDictionary_Internal(bool alloc, const char *dict_name, const char *res_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_INTERNAL_DICT, alloc MEM_DBG_PARMS_CALL);
	ctx->dict_name = StructAllocString_dbg( dict_name, caller_fname, line );
	ctx->resource_name = StructAllocString_dbg( res_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextMapTransition_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_MAP_TRANSITION, alloc MEM_DBG_PARMS_CALL);
	ctx->objective_name = StructAllocString_dbg( objective_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorContextUGCItem_Internal(bool alloc, const char *item_name MEM_DBG_PARMS)
{
	UGCRuntimeErrorContext* ctx = ugcMakeErrorScopeCommon(UGC_SCOPE_UGC_ITEM, alloc MEM_DBG_PARMS_CALL);
	ctx->ugc_item_name = StructAllocString_dbg( item_name, caller_fname, line );
	return ctx;
}

UGCRuntimeErrorContext* ugcMakeErrorAutoGen(UGCRuntimeErrorContext *ctx)
{
	ctx->auto_generated = true;
	return ctx;
}

void ugcErrorPrintContextStr( const UGCRuntimeErrorContext* context, char* text, size_t text_size )
{
	char auto_generated[256];
	char mission_context[256];

	if( context->auto_generated ) {
		sprintf( auto_generated, "AUTOGENERATED " );
	} else {
		sprintf( auto_generated, "" );
	}
	if( context->mission_name ) {
		sprintf( mission_context, "Mission: %s,", context->mission_name );
	} else {
		sprintf( mission_context, "Shared" );
	}
	
	switch( context->scope ) {
		case UGC_SCOPE_DEFAULT:
			sprintf_s(SAFESTR2(text), "");
		xcase UGC_SCOPE_MAP:
			sprintf_s(SAFESTR2(text), "%sMap", auto_generated);
		xcase UGC_SCOPE_ROOM:
			sprintf_s(SAFESTR2(text), "%sLayout: %s, Room: %s", auto_generated, context->layout_name, context->location_name);
		xcase UGC_SCOPE_PATH:
			sprintf_s(SAFESTR2(text), "%sLayout: %s, Path: %s", auto_generated, context->layout_name, context->location_name);
		xcase UGC_SCOPE_CHALLENGE:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Challenge: %s", auto_generated, mission_context, context->layout_name, context->challenge_name);
		xcase UGC_SCOPE_OBJECTIVE:
			sprintf_s(SAFESTR2(text), "%s%s Objective: %s", auto_generated, mission_context, context->objective_name);
		xcase UGC_SCOPE_PROMPT:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Prompt: %s", auto_generated, mission_context, context->layout_name, context->prompt_name);
		xcase UGC_SCOPE_PORTAL:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Portal: %s", auto_generated, mission_context, context->layout_name, context->portal_name);
		xcase UGC_SCOPE_MISSION:
			sprintf_s(SAFESTR2(text), "%sMission: %s", auto_generated, context->mission_name);
		xcase UGC_SCOPE_LAYOUT:
			sprintf_s(SAFESTR2(text), "%sLayout: %s", auto_generated, context->layout_name);
		xcase UGC_SCOPE_SOLSYS_DETAIL_OBJECT:
			sprintf_s(SAFESTR2(text), "%sDetail Obj: %s", auto_generated, context->solsys_detail_object_name);
		xcase UGC_SCOPE_EPISODE_PART:
			sprintf_s(SAFESTR2(text), "%sEp Part: %s", auto_generated, context->ep_part_name);

		xcase UGC_SCOPE_INTERNAL_DICT:
			sprintf_s(SAFESTR2(text), "Dict %s: %s", context->dict_name, context->resource_name);
		xcase UGC_SCOPE_INTERNAL_CODE:
			sprintf_s(SAFESTR2(text), "Internal Error" );

		xdefault:
			sprintf_s(SAFESTR2(text), "Unknown Component");
	}
}

void ugcErrorPrint( const UGCRuntimeError* error, char* text, size_t text_size )
{
	char contextText[ 256 ] = "";
	ugcErrorPrintContextStr( error->context, SAFESTR( contextText ));
	
	switch( error->type ) {
		case UGC_FATAL_ERROR:
			sprintf_s( SAFESTR2( text ), "FATAL ERROR: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );
			
		xcase UGC_ERROR:
			sprintf_s( SAFESTR2( text ), "ERROR: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );

		xcase UGC_WARNING:
			sprintf_s( SAFESTR2( text ), "WARNING: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );

		xdefault:
			sprintf_s( SAFESTR2( text ), "UNKNOWN: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );
	}
}

bool ugcStatusFailed(UGCRuntimeStatus *status)
{
	return ugcStatusHasErrors(status, UGC_FATAL_ERROR);
}

bool ugcStatusHasErrors(UGCRuntimeStatus *status, UGCRuntimeErrorType errorLevel)
{
	int i, j;
	for (i = 0; i < eaSize(&status->stages); i++)
		for (j = 0; j < eaSize(&status->stages[i]->errors); j++)
			if (status->stages[i]->errors[j]->type >= errorLevel)
				return true;
	return false;
}

void ugcClearStage(void)
{
	g_CurrentStage = NULL;
}

void ugcSetStageAndAdd(UGCRuntimeStatus *status, char* stageName, ...)
{
	char stageNameBuffer[ 256 ];
	UGCRuntimeStage* stage;

	va_list ap;
	va_start(ap, stageName);
	vsprintf(stageNameBuffer, stageName, ap);
	va_end(ap);

	stage = StructCreate( parse_UGCRuntimeStage );
	stage->name = StructAllocString( stageNameBuffer );
	eaPush(&status->stages, stage);
	g_CurrentStage = stage;
}

UGCRuntimeError* ugcStatusMostImportantError( UGCRuntimeStatus* status )
{
	int stageIt;
	int errorIt;

	UGCRuntimeError* highest_priority_error = NULL;
	
	for( stageIt = 0; stageIt != eaSize( &status->stages ); ++stageIt ) {
		for( errorIt = 0; errorIt != eaSize( &status->stages[ stageIt ]->errors ); ++errorIt ) {
			UGCRuntimeError* error = status->stages[ stageIt ]->errors[ errorIt ];

			if( !highest_priority_error || error->type > highest_priority_error->type ) {
				highest_priority_error = error;
			}
		}
	}

	return highest_priority_error;
}

int ugcStatusErrorCount( const UGCRuntimeStatus* status )
{
	int accum = 0;
	int stageIt;

	UGCRuntimeError* highest_priority_error = NULL;
	
	for( stageIt = 0; stageIt != eaSize( &status->stages ); ++stageIt ) {
		accum += eaSize( &status->stages[ stageIt ]->errors );
	}

	return accum;
}

#include "UGCError_h_ast.c"
