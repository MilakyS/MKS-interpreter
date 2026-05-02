

#ifndef MONKEYKERNELSYNTAX_INDEXING_H
#define MONKEYKERNELSYNTAX_INDEXING_H

#include "value.h"
RuntimeValue runtime_get_index(RuntimeValue target, RuntimeValue index);
RuntimeValue runtime_set_index(RuntimeValue target, RuntimeValue index, RuntimeValue value);

#endif
