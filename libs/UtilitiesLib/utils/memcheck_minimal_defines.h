#pragma once
//the minimal set of MEM_DBG_PARMS headers needed to get things like utf8_prototypes.h compiling

#define MEM_DBG_PARMS , const char *caller_fname, int line
#define MEM_DBG_PARMS_VOID const char *caller_fname, int line
#define MEM_DBG_PARMS_CALL , caller_fname, line
#define MEM_DBG_PARMS_CALL_VOID caller_fname, line
#define MEM_DBG_PARMS_INIT , __FILE__, __LINE__
#define MEM_DBG_PARMS_INIT_VOID __FILE__, __LINE__
#define MEM_DBG_STRUCT_PARMS const char *caller_fname; int line;
#define MEM_DBG_STRUCT_PARMS_INIT(struct_ptr) ((struct_ptr)->caller_fname = caller_fname, (struct_ptr)->line = line)
#define MEM_DBG_STRUCT_PARMS_CALL(struct_ptr) , (struct_ptr)->caller_fname, (struct_ptr)->line
#define MEM_DBG_STRUCT_PARMS_CALL_VOID(struct_ptr) (struct_ptr)->caller_fname, (struct_ptr)->line