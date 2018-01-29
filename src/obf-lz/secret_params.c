#include "obf_params.h"
#include "../circ_params.h"
#include "../index_set.h"
#include "../vtables.h"
#include "../util.h"

#include <err.h>

struct sp_info {
    index_set *toplevel;
    const circ_params_t *cp;
};
#define my(x) (x)->info

static int
_sp_init(secret_params *sp, mmap_params_t *params, const obf_params_t *op,
         size_t kappa)
{
    const circ_params_t *cp = &op->cp;

    if ((my(sp) = calloc(1, sizeof my(sp)[0])) == NULL)
        return ERR;
    my(sp)->toplevel = obf_params_new_toplevel(cp, obf_params_nzs(cp));
    my(sp)->cp = cp;

    params->kappa = kappa ? kappa : acirc_delta(cp->circ) + acirc_nsymbols(cp->circ);
    params->nzs = my(sp)->toplevel->nzs;
    if ((params->pows = calloc(params->nzs, sizeof params->pows[0])) == NULL)
        goto error;
    for (size_t i = 0; i < params->nzs; ++i) {
        if (my(sp)->toplevel->pows[i] < 0) {
            fprintf(stderr, "error: toplevel overflow\n");
            goto error;
        }
        params->pows[i] = my(sp)->toplevel->pows[i];
    }
    params->my_pows = true;
    params->nslots = 2;
    return OK;
error:
    if (params->pows)
        free(params->pows);
    index_set_free(my(sp)->toplevel);
    free(my(sp));
    return ERR;
}

static void
_sp_clear(secret_params *sp)
{
    index_set_free(my(sp)->toplevel);
    free(sp->info);
}

static const void *
_sp_toplevel(const secret_params *sp)
{
    return my(sp)->toplevel;
}

static const void *
_sp_params(const secret_params *sp)
{
    return my(sp)->cp;
}

static sp_vtable _sp_vtable = {
    .mmap = NULL,
    .init = _sp_init,
    .clear = _sp_clear,
    .toplevel = _sp_toplevel,
    .params = _sp_params,
};

PRIVATE const sp_vtable *
get_sp_vtable(const mmap_vtable *mmap)
{
    _sp_vtable.mmap = mmap;
    return &_sp_vtable;
}
