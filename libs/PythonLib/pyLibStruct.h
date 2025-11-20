#include "pyLib.h"

PyObject *pyLibSerializeStructEx(void *pStruct, ParseTable pti[]);
#define pyLibSerializeStruct(struct, pti) pyLibSerializeStructEx(STRUCT_TYPESAFE_PTR(pti, struct), pti)