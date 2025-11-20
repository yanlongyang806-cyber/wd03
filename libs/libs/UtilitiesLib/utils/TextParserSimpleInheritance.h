#include "textparser.h"

typedef int (*SimpleInheritanceCopyFunc)(ParseTable *pti, int column, void *dst, void *src, void *userdata);

void SimpleInheritanceApply(ParseTable *pti, void *dst, void *src, SimpleInheritanceCopyFunc func, void *userdata);