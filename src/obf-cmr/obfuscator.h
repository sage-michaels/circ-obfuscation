#pragma once

#include "../obfuscator.h"

typedef struct {
    size_t npowers;
} mobf_obf_params_t;

size_t mobf_num_encodings(const obf_params_t *op);

extern obfuscator_vtable mobf_obfuscator_vtable;
extern op_vtable mobf_op_vtable;
