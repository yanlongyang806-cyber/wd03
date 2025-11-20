// This code is based on udis86, a BSD-licensed disassembler library for x86 and x86-64.

#include "disasm.h"
#include "../../3rdparty/udis86/udis86.h"
#include "wininclude.h"

#ifdef _WIN64
#pragma comment(lib, "udis86X64.lib")
#else
#pragma comment(lib, "udis86.lib")
#endif

// Disassemble ptr into ud_obj, and return the instruction length.
static unsigned disassemble(ud_t *ud_obj, char *ptr)
{
	ud_init(ud_obj);
	ud_set_input_buffer(ud_obj, ptr, 15);  // 15 is maximum x86 instruction length
#ifdef _M_X64
	ud_set_mode(ud_obj, 64);
#else
	ud_set_mode(ud_obj, 32);
#endif
	return ud_disassemble(ud_obj);
}

// Return true if this is a pointer to a valid instruction.
static bool valid_instruction(char *ptr)
{
	ud_t ud_obj;
	int length = disassemble(&ud_obj, ptr);

	return length && ud_obj.mnemonic != UD_Iinvalid;
}

// If the input pointer pointers to a valid call instruction, return a pointer to the next instruction.  If it isn't, return null.
static char *read_call_instruction(char *ptr)
{
	ud_t ud_obj;
	unsigned length = disassemble(&ud_obj, ptr);

	if (length && ud_obj.mnemonic == UD_Icall)
		return ptr + length;

	return NULL;
}

// Implementation of disasm_is_return_point()
static bool is_return_point_mightthrow(char *ptr)
{
	int offset;

	// First, check if the memory seems to be valid.
	if (IsBadCodePtr((void *)ptr))
		return false;

	// Then, make sure the instruction is actually a valid instruction.
	if (!valid_instruction(ptr))
		return false;

	// Search for a call instruction.
	// Call instruction will be 3 to 10 characters.
	// TODO: Can the lower bound be raised?  Is it actually possible to have a 2-byte call instruction?
	// TODO: Can the upper bound be lowered?  It is the longest possible instruction on x86, 15, minus the maximum prefix length.
	for (offset = 2; offset < 16; ++offset)
	{
		char *previous_insn = ptr - offset;
		char *next_insn;

		// TODO: Is it possible to prescreen here, without having to do a full disassemble?

		// If we can't read that far back, fail.
		if (IsBadCodePtr((void *)previous_insn))
			break;

		// See if this is a call instruction that gets us back to where we started.
		next_insn = read_call_instruction(previous_insn);
		if (next_insn != ptr)
			continue;

		// It worked!
		return true;
	}

	return false;
}

// Return true if, based on heuristics, this pointer is likely to be the next instruction after a call instruction.
// We consider a pointer to be a valid return point if it meets two criteria:
//  1. It points to a valid instruction.
//  2. The immediately previous instruction is a call instruction.  (This disallows "continuations" and other strange control styles.)
// The algorithm is to search from immediately before the pointer, increasingly further back, to try to find a valid call instruction that
// ends exactly on on our pointer.
bool disasm_is_return_point(void *ptr)
{
	// Exception wrapper: if it throws, it wasn't a return point.
	__try
	{
		return is_return_point_mightthrow((char *)ptr);
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
#pragma warning(suppress:6322)
	{
	}
	return false;
}

// Return true if this is a stack pointer operand type.
static bool stack_pointer_operand(const struct ud_operand *operand)
{
	if (operand->type == UD_OP_REG)
	{
		switch (operand->base)
		{
			case UD_R_SPL:
			case UD_R_SP:
			case UD_R_ESP:
			case UD_R_RSP:
			return true;
		}
	}
	return false;
}

// Implementation of disasm_guess_frame_size()
static size_t disasm_guess_frame_size_mightthrow(char *ptr)
{
	ud_t ud_obj;
	int int3_count = 0;
	size_t offset = 0;

	// First, check if the memory seems to be valid.
	if (IsBadCodePtr((void *)ptr))
		return false;

	// Initialize disassembly context.
	ud_init(&ud_obj);
	ud_set_input_buffer(&ud_obj, ptr, 4096*16);  // Maximum function size?
#ifdef _M_X64
	ud_set_mode(&ud_obj, 64);
#else
	ud_set_mode(&ud_obj, 32);
#endif

#ifdef _M_X64
#define STACK_REGISTER_BYTES 8
#else
#define STACK_REGISTER_BYTES 4
#endif

	// Keep disassembling until we get to the end of the function, or we hit our maximum size, set above.
	// If we run off the end of the valid code pages, something here will throw, and it will be handled as a failure in the caller.
	while (ud_disassemble(&ud_obj)) {
		
		enum ud_type op1_bytes = ud_obj.operand[0].size/8;

		// Check for operations on stack pointers.
		if (stack_pointer_operand(&ud_obj.operand[0]))
		{
			switch (ud_obj.mnemonic)
			{
				case UD_Iadd:
					if (ud_obj.operand[1].type == UD_OP_IMM)
					{
						offset += ud_obj.operand[1].lval.uqword;
						continue;
					}
					break;

				case UD_Isub:
					if (ud_obj.operand[1].type == UD_OP_IMM)
					{
						offset -= ud_obj.operand[1].lval.uqword;
						continue;
					}
					break;
			}
			
			return -1;  // Unimplemented
		}

		// Check for specific instructions that we recognize.
		switch (ud_obj.mnemonic)
		{
			// *** Instructions that directly manipulate the stack ***
			case UD_Ipush:
				offset -= op1_bytes;
				break;

			case UD_Ipusha:
				offset -= 16;
				break;
				
			case UD_Ipushad:
				offset -= 32;
				break;

			case UD_Ipushfw:
			case UD_Ipushfd:
			case UD_Ipushfq:
				offset -= op1_bytes;
				break;

			case UD_Ipop:
				offset -= op1_bytes;
				break;

			case UD_Ipopa:
				offset -= 16;
				break;

			case UD_Ipopad:
				offset -= 32;
				break;

			case UD_Ipopfw:
			case UD_Ipopfd:
			case UD_Ipopfq:
				offset += op1_bytes;
				break;

			case UD_Ienter:
				offset -= STACK_REGISTER_BYTES + ud_obj.operand[0].lval.sword + ud_obj.operand[1].lval.ubyte * STACK_REGISTER_BYTES;

			case UD_Ileave:
				// There's no way to know what's happening here without ebp.
				// However, in this case, the frame pointer should be able to be used directly, so the caller will be OK if it uses that.
				return -1;

			// *** Other special cases ***

			// Assume a string of int3 means that we've missed the end of a function somehow.
			case UD_Iint3:
				if (++int3_count > 4)
					return -1;

			// Decoding error
			// One of four cases:
			// 1. We missed the end of the function
			// 2. We started decoding at the wrong offset.
			// 3. The text of the function is corrupted, or it's not actually code.
			// 4. There is data embedded in the function, that it jumps over.  Reasonable, but we don't attempt to cope with it.
			// In all cases, fail.
			case UD_Iinvalid:
				return -1;

			// End of function
			case UD_Iret:
			case UD_Iretf:
				return offset;								// Yay!  I hope it worked!

			//default:
			// Assume that this instruction does not affect the stack pointer.
		}
	}

	// We hit the instruction byte limit.
	return -1;
}

// Try to guess the distance back to the frame pointer from the current stack pointer, by examining the code.
size_t disasm_guess_frame_size(void *instruction_pointer)
{
	// Exception wrapper: if it throws, it wasn't a return point.
	__try
	{
		return disasm_guess_frame_size_mightthrow((char *)instruction_pointer);
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
#pragma warning(suppress:6322)
	{
	}
	return -1;
}
