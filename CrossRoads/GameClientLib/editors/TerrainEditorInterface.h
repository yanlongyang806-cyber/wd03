#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EMEditor EMEditor;

void terEdDrawCursor( TerrainEditorState *state, const Vec3 cursor_world_pos );
void terEdUIRegisterInfoWinEntries(EMEditor *editor);
void terEdDrawNewBlock(const Vec3 pos, F32 dims[3], bool valid);
void terEdDrawNewBlockSelection(const Vec3 start_pos, const Vec3 end_pos, bool valid);

#endif