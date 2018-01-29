#include "mmap.h"
#include "util.h"
#include "obf-polylog/extra.h"

#include <assert.h>
#include <stdio.h>
#include <mmap/mmap_clt_pl.h>

static void
mmap_params_fprint(FILE *fp, const mmap_params_t *params)
{
    fprintf(fp, "Multilinear map parameter settings:\n");
    fprintf(fp, "* κ:       %lu\n", params->kappa);
    fprintf(fp, "* # Zs:    %lu\n", params->nzs);
    fprintf(fp, "* # slots: %lu\n", params->nslots);
    fprintf(fp, "* toplevel: ");
    for (size_t i = 0; i < params->nzs; ++i) {
        fprintf(fp, "%d ", params->pows[i]);
    }
    fprintf(fp, "\n");
}

obf_params_t *
obf_params_new(const op_vtable *vt, acirc_t *circ, void *vparams)
{
    const size_t nconsts = acirc_nconsts(circ) + acirc_nsecrets(circ);
    const size_t has_consts = nconsts ? 1 : 0;
    obf_params_t *op;
    circ_params_t *cp;

    op = vt->new(circ, vparams);
    cp = obf_params_cp(op);
    circ_params_init(cp, acirc_nsymbols(circ) + has_consts, circ);
    for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
        cp->ds[i] = acirc_symlen(circ, i);
        cp->qs[i] = acirc_is_sigma(circ, i) ? acirc_symlen(circ, i) : 2;
    }
    if (has_consts) {
        cp->ds[cp->nslots - 1] = acirc_nconsts(circ) + acirc_nsecrets(circ);
        cp->qs[cp->nslots - 1] = 1;
    }
    if (g_verbose) {
        circ_params_print(cp);
        if (vt->print)
            vt->print(op);
    }
    return op;
}

secret_params *
secret_params_new(const sp_vtable *vt, const obf_params_t *op, size_t lambda,
                  size_t *kappa, size_t ncores, aes_randstate_t rng)
{
    int ret = ERR;
    mpz_t modulus;
    mmap_params_t params;
    const circ_params_t *cp = obf_params_cp(op);
    size_t _kappa = kappa ? *kappa : 0;
    secret_params *sp;

    sp = my_calloc(1, sizeof sp[0]);
    if (vt->init(sp, &params, op, _kappa) == ERR) {
        free(sp);
        return NULL;
    }
    if (g_verbose)
        mmap_params_fprint(stderr, &params);
    if (kappa)
        *kappa = params.kappa;
    mmap_sk_params p = {
        .lambda = lambda,
        .kappa = params.kappa,
        .gamma = params.nzs,
        .pows = params.pows,
    };
    mmap_sk_opt_params o = {
        .nslots = params.nslots,
        .modulus = NULL,
        .is_polylog = false,
    };
    if (acirc_is_binary(cp->circ)) {
        mpz_init_set_ui(modulus, 2);
        o.modulus = &modulus;
    }
    if (vt->mmap == &clt_pl_vtable) {
        if ((sp->sk = polylog_secret_params_new(vt, op, &p, &o, &params, ncores, rng)) == NULL)
            goto cleanup;
    } else {
        if ((sp->sk = vt->mmap->sk->new(&p, &o, ncores, rng, g_verbose)) == NULL)
            goto cleanup;
    }
    ret = OK;
cleanup:
    if (acirc_is_binary(cp->circ))
        mpz_clear(modulus);
    if (params.my_pows)
        free(params.pows);
    if (ret == OK)
        return sp;
    else {
        if (sp)
            free(sp);
        return NULL;
    }
}

int
secret_params_fwrite(const sp_vtable *vt, const secret_params *sp, FILE *fp)
{
    vt->fwrite(sp, fp);
    vt->mmap->sk->fwrite(sp->sk, fp);
    return OK;
}

secret_params *
secret_params_fread(const sp_vtable *vt, const circ_params_t *cp, FILE *fp)
{
    secret_params *sp = my_calloc(1, sizeof sp[0]);
    vt->fread(sp, cp, fp);
    sp->sk = vt->mmap->sk->fread(fp);
    return sp;
}

void
secret_params_free(const sp_vtable *vt, secret_params *sp)
{
    vt->clear(sp);
    if (sp->sk) {
        vt->mmap->sk->free(sp->sk);
    }
    free(sp);
}

public_params *
public_params_new(const pp_vtable *vt, const sp_vtable *sp_vt,
                  const secret_params *sp)
{
    public_params *pp = my_calloc(1, sizeof pp[0]);
    vt->init(sp_vt, pp, sp);
    pp->pp = vt->mmap->sk->pp(sp->sk);
    return pp;
}

int
public_params_fwrite(const pp_vtable *vt, const public_params *pp, FILE *fp)
{
    vt->fwrite(pp, fp);
    vt->mmap->pp->fwrite(pp->pp, fp);
    return OK;
}

public_params *
public_params_fread(const pp_vtable *vt, const obf_params_t *op, FILE *fp)
{
    public_params *pp = my_calloc(1, sizeof pp[0]);
    vt->fread(pp, op, fp);
    pp->pp = vt->mmap->pp->fread(fp);
    return pp;
}

void
public_params_free(const pp_vtable *vt, public_params *pp)
{
    vt->clear(pp);
    vt->mmap->pp->free(pp->pp);
    free(pp);
}

encoding *
encoding_new(const encoding_vtable *vt, const pp_vtable *pp_vt,
             const public_params *pp)
{
    encoding *enc = calloc(1, sizeof enc[0]);
    (void) vt->new(pp_vt, enc, pp);
    enc->enc = vt->mmap->enc->new(pp->pp);
    return enc;
}

void
encoding_free(const encoding_vtable *vt, encoding *enc)
{
    if (enc) {
        vt->mmap->enc->free(enc->enc);
        vt->free(enc);
        free(enc);
    }
}

encoding *
encoding_copy(const encoding_vtable *vt, const pp_vtable *pp_vt,
              const public_params *pp, const encoding *enc)
{
    encoding *rop;

    rop = encoding_new(vt, pp_vt, pp);
    encoding_set(vt, rop, enc);
    return rop;
}

int
encoding_print(const encoding_vtable *vt, const encoding *enc)
{
    (void) vt->print(enc);
    vt->mmap->enc->print(enc->enc);
    return OK;
}

int
encode(const encoding_vtable *vt, encoding *rop, mpz_t *inps, size_t nins,
       const void *set, const secret_params *sp, size_t level)
{
    int *pows;

    pows = vt->encode(rop, set);
    vt->mmap->enc->encode(rop->enc, sp->sk, nins, inps, pows, level);
    free(pows);
    return OK;
}

int
encoding_set(const encoding_vtable *vt, encoding *rop, const encoding *x)
{
    (void) vt->set(rop, x);
    vt->mmap->enc->set(rop->enc, x->enc);
    return OK;
}

int
encoding_mul(const encoding_vtable *vt, const pp_vtable *pp_vt, encoding *rop,
             const encoding *x, const encoding *y, const public_params *p)
{

    if (vt->mul(pp_vt, rop, x, y, p) == ERR)
        return ERR;
    vt->mmap->enc->mul(rop->enc, p->pp, x->enc, y->enc);
    return OK;
}

int
encoding_add(const encoding_vtable *vt, const pp_vtable *pp_vt, encoding *rop,
             const encoding *x, const encoding *y, const public_params *p)
{
    if (vt->add(pp_vt, rop, x, y, p) == ERR)
        return ERR;
    vt->mmap->enc->add(rop->enc, p->pp, x->enc, y->enc);
    return OK;
}

int
encoding_sub(const encoding_vtable *vt, const pp_vtable *pp_vt, encoding *rop,
             const encoding *x, const encoding *y, const public_params *p)
{
    if (vt->sub(pp_vt, rop, x, y, p) == ERR)
        return ERR;
    vt->mmap->enc->sub(rop->enc, p->pp, x->enc, y->enc);
    return OK;
}

int
encoding_is_zero(const encoding_vtable *vt, const pp_vtable *pp_vt,
                 const encoding *x, const public_params *pp)
{
    if (vt->is_zero(pp_vt, x, pp) == ERR)
        return ERR;
    else
        return vt->mmap->enc->is_zero(x->enc, pp->pp);
}

unsigned int
encoding_get_degree(const encoding_vtable *vt, const encoding *x)
{
    return vt->mmap->enc->degree ? vt->mmap->enc->degree(x->enc) : 0;
}

encoding *
encoding_fread(const encoding_vtable *vt, FILE *fp)
{
    encoding *const x = my_calloc(1, sizeof x[0]);
    vt->fread(x, fp);
    x->enc = vt->mmap->enc->fread(fp);
    return x;
}

int
encoding_fwrite(const encoding_vtable *vt, const encoding *x, FILE *fp)
{
    vt->fwrite(x, fp);
    vt->mmap->enc->fwrite(x->enc, fp);
    return OK;
}
