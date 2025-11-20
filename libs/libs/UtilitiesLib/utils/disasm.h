// Machine code disassembly utilities

// This file can be expanded as-needed to include any routines which depend on analyzing machine code.

#ifndef CRYPTIC_DISASM_H
#define CRYPTIC_DISASM_H

// Return true if, based on heuristics, this pointer is likely to be the next instruction after a call instruction.
bool disasm_is_return_point(void *ptr);

// Try to guess the distance back to the frame pointer from the current stack pointer, by examining the code.
size_t disasm_guess_frame_size(void *instruction_pointer);

#endif  // CRYPTIC_DISASM_H
