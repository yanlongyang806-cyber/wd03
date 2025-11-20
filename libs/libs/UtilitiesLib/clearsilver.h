#pragma once
GCC_SYSTEM
#include "../../3rdparty/clearsilver-0.10.5/ClearSilver.h"

// WARNING: There is a bug in something, maybe IncrediBuild, where __FILE__ will be substituted by just the filename, instead of the full path,
// which is required for renderTemplate() to be able to find templates in source code, when developing on a programmer machine.  If this
// happens to you, rebuild the file in Visual Studio without IncrediBuild.
#define renderTemplate(template_file, tpi, struct_mem, hdf_dump) renderTemplateEx((template_file), (tpi), (struct_mem), (hdf_dump), __FILE__)
char *renderTemplateEx(const char *template_file, ParseTable *tpi, void *struct_mem, bool hdf_dump, char *template_path);

void csSetCustomLoadPath(const char *path);

// Find a file, using the CS template lookup rules.
bool findTemplateFile(char **estrLocation, const char *file, char *extra_path);
