#include "../mmap.h"
#include "../vtables.h"

#include "../circ_params.h"
#include "../index_set.h"
#include "mife_params.h"
#include "../util.h"

struct sp_info {
    index_set *toplevel;
    const circ_params_t *cp;
};
#define my(x) (x)->info

static int
_sp_init(secret_params *sp, mmap_params_t *mp, const obf_params_t *op, size_t kappa)
{
    const circ_params_t *cp = &op->cp;
    if ((my(sp) = calloc(1, sizeof my(sp)[0])) == NULL)
        return ERR;
    my(sp)->toplevel = mife_params_new_toplevel(cp, mife_params_nzs(cp));
    my(sp)->cp = cp;

    mp->kappa = kappa ? kappa : (size_t) max(acirc_delta(cp->circ) + 1, acirc_nsymbols(cp->circ));
    mp->nzs = my(sp)->toplevel->nzs;
    mp->pows = my_calloc(mp->nzs, sizeof mp->pows[0]);
    for (size_t i = 0; i < mp->nzs; ++i) {
        if (my(sp)->toplevel->pows[i] < 0) {
            fprintf(stderr, "error: toplevel overflow\n");
            free(mp->pows);
            index_set_free(my(sp)->toplevel);
            free(my(sp));
            return ERR;
        }
        mp->pows[i] = my(sp)->toplevel->pows[i];
    }
    mp->my_pows = true;
    mp->nslots = 1 + cp->nslots;
    return OK;
}

static int
_sp_fwrite(const secret_params *sp, FILE *fp)
{
    (void) sp; (void) fp;
    return OK;
}

static int
_sp_fread(secret_params *sp, const circ_params_t *cp, FILE *fp)
{
    (void) fp;
    if ((my(sp) = calloc(1, sizeof my(sp)[0])) == NULL)
        return ERR;
    my(sp)->toplevel = mife_params_new_toplevel(cp, mife_params_nzs(cp));
    my(sp)->cp = cp;
    return OK;
}

static void
_sp_clear(secret_params *sp)
{
    index_set_free(my(sp)->toplevel);
    free(my(sp));
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
    .fwrite = _sp_fwrite,
    .fread = _sp_fread,
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
