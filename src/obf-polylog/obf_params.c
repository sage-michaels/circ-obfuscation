#include "obf_params.h"
#include "obfuscator.h"
#include "../util.h"

PRIVATE size_t
obf_params_nzs(const circ_params_t *cp)
{
    return 2 * acirc_ninputs(cp->circ) + 1;
}

PRIVATE index_set *
obf_params_new_toplevel(const circ_params_t *const cp, size_t nzs)
{
    (void) cp;
    index_set *ix;

    if ((ix = index_set_new(nzs)) == NULL)
        return NULL;
    /* IX_Z(ix) = 1; */
    /* for (size_t i = 0; i < acirc_ninputs(cp->circ); ++i) { */
    /*     IX_W(ix, cp, i) = 1; */
    /*     IX_X(ix, cp, i) = acirc_max_var_degree(cp->circ, i); */
    /* } */
    return ix;
}

PRIVATE size_t
obf_num_encodings(const circ_params_t *cp)
{
    const size_t ninputs = acirc_ninputs(cp->circ);
    const size_t nconsts = acirc_nconsts(cp->circ);
    const size_t noutputs = acirc_noutputs(cp->circ);
    return 2 * 2 * ninputs + 2 * nconsts + ninputs * 2 * noutputs + 2 * noutputs;
}

static obf_params_t *
_new(acirc_t *circ, void *vparams)
{
    const polylog_obf_params_t *params = vparams;
    obf_params_t *op;
    const size_t ninputs = acirc_ninputs(circ);
    const size_t noutputs = acirc_noutputs(circ);

    if ((op = my_calloc(1, sizeof op[0])) == NULL)
        return NULL;
    op->nlevels = max(acirc_max_depth(circ) + 1, ninputs);
    op->nswitches = acirc_nrefs(circ) + (ninputs + 2) * noutputs;
    op->wordsize = params->wordsize;
    return op;
}

static void
_print(const obf_params_t *op)
{
    fprintf(stderr, "Obfuscation parameters:\n");
    fprintf(stderr, "* # levels: .. %lu\n", op->nlevels);
    fprintf(stderr, "* # switches:  %lu\n", op->nswitches);
    fprintf(stderr, "* # encodings: %lu\n", obf_num_encodings(&op->cp));
}

static int
_fwrite(const obf_params_t *op, FILE *fp)
{
    circ_params_fwrite(&op->cp, fp);
    size_t_fwrite(op->nlevels, fp);
    size_t_fwrite(op->nswitches, fp);
    return OK;
}

static obf_params_t *
_fread(acirc_t *circ, FILE *fp)
{
    obf_params_t *op;

    op = my_calloc(1, sizeof op[0]);
    circ_params_fread(&op->cp, circ, fp);
    size_t_fread(&op->nlevels, fp);
    size_t_fread(&op->nswitches, fp);
    return op;
}

static void
_free(obf_params_t *op)
{
    circ_params_clear(&op->cp);
    free(op);
}

op_vtable polylog_op_vtable =
{
    .new = _new,
    .free = _free,
    .fwrite = _fwrite,
    .fread = _fread,
    .print = _print,
};
