#include "mife_run.h"
#include "mife_params.h"
#include "../util.h"

#include <string.h>
#include <mmap/mmap_dummy.h>

int
mife_run_setup(const mmap_vtable *mmap, const char *circuit, obf_params_t *op,
               size_t secparam, size_t *kappa, size_t npowers, size_t nthreads,
               aes_randstate_t rng)
{
    const double start = current_time();
    const circ_params_t *cp = &op->cp;
    char skname[strlen(circuit) + sizeof ".sk\0"];
    char ekname[strlen(circuit) + sizeof ".ek\0"];
    mife_t *mife = NULL;
    mife_sk_t *sk = NULL;
    mife_ek_t *ek = NULL;
    FILE *fp = NULL;
    int ret = ERR;

    if (g_verbose) {
        fprintf(stderr, "MIFE setup details:\n");
        fprintf(stderr, "* circuit: ............. %s\n", circuit);
        fprintf(stderr, "* security parameter: .. %lu\n", secparam);
        fprintf(stderr, "* # threads: ........... %lu\n", nthreads);
        fprintf(stderr, "* # encodings: ......... %lu\n", mife_num_encodings_setup(cp, npowers));
        circ_params_print(cp);
    }

    if ((mife = mife_setup(mmap, op, secparam, kappa, npowers, nthreads, rng)) == NULL)
        goto cleanup;
    {
        const double _start = current_time();
        if ((sk = mife_sk(mife)) == NULL)
            goto cleanup;
        snprintf(skname, sizeof skname, "%s.sk", circuit);
        if ((fp = fopen(skname, "w")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for writing\n",
                    __func__, skname);
            goto cleanup;
        }
        if (mife_sk_fwrite(sk, fp) == ERR)
            goto cleanup;
        fclose(fp);
        if (g_verbose) {
            fprintf(stderr, "  Writing secret key to disk: %.2fs\n",
                    current_time() - _start);
            fprintf(stderr, "    Secret key file size: %lu KB\n",
                    filesize(skname) / 1024);
        }
    }
    {
        const double _start = current_time();
        if ((ek = mife_ek(mife)) == NULL)
            goto cleanup;
        snprintf(ekname, sizeof ekname, "%s.ek", circuit);
        if ((fp = fopen(ekname, "w")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for writing\n",
                    __func__, ekname);
            goto cleanup;
        }
        if (mife_ek_fwrite(ek, fp) == ERR)
            goto cleanup;
        fclose(fp);
        if (g_verbose) {
            fprintf(stderr, "  Writing evaluation key to disk: %.2fs\n",
                    current_time() - _start);
            fprintf(stderr, "    Evaluation key file size: %lu KB\n",
                    filesize(ekname) / 1024);
        }
    }
    if (g_verbose)
        fprintf(stderr, "MIFE setup time: %.2fs\n", current_time() - start);
    fp = NULL;
    ret = OK;
cleanup:
    if (fp)
        fclose(fp);
    if (sk)
        mife_sk_free(sk);
    if (ek)
        mife_ek_free(ek);
    if (mife)
        mife_free(mife);
    return ret;
}

int
mife_run_encrypt(const mmap_vtable *mmap, const char *circuit, obf_params_t *op,
                 const long *input, size_t slot, size_t nthreads,
                 mife_sk_t *cached_sk, aes_randstate_t rng)
{
    const double start = current_time();
    const circ_params_t *cp = &op->cp;
    mife_ciphertext_t *ct = NULL;
    mife_sk_t *sk = NULL;
    size_t ninputs;
    FILE *fp;
    int ret = ERR;

    if (slot >= cp->nslots) {
        fprintf(stderr, "error: invalid MIFE slot %lu\n", slot);
        return ERR;
    }
    ninputs = cp->ds[slot];

    if (g_verbose) {
        fprintf(stderr, "MIFE encryption details:\n");
        fprintf(stderr, "* circuit: ....... %s\n", circuit);
        fprintf(stderr, "* slot: .......... %lu\n", slot);
        fprintf(stderr, "* input: ......... ");
        for (size_t i = 0; i < ninputs; ++i)
            fprintf(stderr, "%ld", input[i]);
        fprintf(stderr, "\n");
        fprintf(stderr, "* # threads: ..... %lu\n", nthreads);
        fprintf(stderr, "* # encodings: ... %lu\n",
                mife_num_encodings_encrypt(cp, slot));
    }

    if (cached_sk == NULL) {
        const double _start = current_time();
        char skname[strlen(circuit) + sizeof ".sk\0"];

        snprintf(skname, sizeof skname, "%s.sk", circuit);
        if ((fp = fopen(skname, "r")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for reading\n",
                    __func__, skname);
            goto cleanup;
        }
        if ((sk = mife_sk_fread(mmap, op, fp)) == NULL) {
            fprintf(stderr, "error: %s: unable to read secret key from disk\n",
                    __func__);
            goto cleanup;
        }
        fclose(fp);
        if (g_verbose)
            fprintf(stderr, "  Reading secret key from disk: %.2fs\n",
                    current_time() - _start);
    } else {
        sk = cached_sk;
    }

    if ((ct = mife_encrypt(sk, slot, input, nthreads, NULL, rng, false)) == NULL) {
        fprintf(stderr, "error: %s: encryption failed\n", __func__);
        goto cleanup;
    }

    {
        double _start = current_time();
        char ctname[strlen(circuit) + 10 + strlen("..ct\0")];
        snprintf(ctname, sizeof ctname, "%s.%lu.ct", circuit, slot);

        if ((fp = fopen(ctname, "w")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for writing\n",
                    __func__, ctname);
            goto cleanup;
        }
        if (mife_ciphertext_fwrite(ct, cp, fp) == ERR) {
            fprintf(stderr, "error: %s: unable to write ciphertext to disk\n",
                    __func__);
            goto cleanup;
        }
        fclose(fp);
        if (g_verbose) {
            fprintf(stderr, "  Writing ciphertext to disk: %.2fs\n", current_time() - _start);
            fprintf(stderr, "    Ciphertext file size: %lu KB\n", filesize(ctname) / 1024);
        }
    }
    if (g_verbose)
        fprintf(stderr, "MIFE encryption time: %.2fs\n", current_time() - start);
    fp = NULL;
    ret = OK;
cleanup:
    if (fp)
        fclose(fp);
    if (ct)
        mife_ciphertext_free(ct, cp);
    if (cached_sk == NULL && sk)
        mife_sk_free(sk);
    return ret;
}

int
mife_run_decrypt(const char *ek_s, char **cts_s, long *rop,
                 const mmap_vtable *mmap, obf_params_t *op, size_t *kappa,
                 size_t nthreads)
{
    const double start = current_time();
    const circ_params_t *cp = &op->cp;
    const size_t has_consts = acirc_nconsts(cp->circ) ? 1 : 0;
    mife_ciphertext_t *cts[cp->nslots];
    mife_ek_t *ek = NULL;
    FILE *fp;
    int ret = ERR;

    memset(cts, '\0', sizeof cts);

    if (g_verbose) {
        fprintf(stderr, "MIFE decryption details:\n");
        fprintf(stderr, "* evaluation key: . %s\n", ek_s);
        fprintf(stderr, "* ciphertexts: .... ");
        for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
            fprintf(stderr, "%s%s\n", i == 0 ? "" : "                    ", cts_s[i]);
        }
    }

    {
        const double _start = current_time();
        if ((fp = fopen(ek_s, "r")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for reading\n",
                    __func__, ek_s);
            goto cleanup;
        }
        if ((ek = mife_ek_fread(mmap, op, fp)) == NULL) {
            fprintf(stderr, "error: %s: unable to read evaluation key\n",
                    __func__);
            goto cleanup;
        }
        fclose(fp);
        if (g_verbose)
            fprintf(stderr, "  Reading evaluation key from disk: %.2fs\n",
                    current_time() - _start);
    }

    for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
        const double _start = current_time();
        if ((fp = fopen(cts_s[i], "r")) == NULL) {
            fprintf(stderr, "error: %s: unable to open '%s' for reading\n",
                    __func__, cts_s[i]);
            goto cleanup;
        }
        if ((cts[i] = mife_ciphertext_fread(mmap, cp, fp)) == NULL) {
            fprintf(stderr, "error: %s: unable to read ciphertext for slot %lu\n",
                    __func__, i);
            goto cleanup;
        }
        fclose(fp);
        if (g_verbose)
            fprintf(stderr, "  Reading ciphertext #%lu from disk: %.2fs\n",
                    i, current_time() - _start);
    }
    if (mife_decrypt(ek, rop, cts, nthreads, kappa) == ERR) {
        fprintf(stderr, "error: %s: decryption failed\n", __func__);
        goto cleanup;
    }
    if (g_verbose)
        fprintf(stderr, "MIFE decryption time: %.2fs\n", current_time() - start);
    fp = NULL;
    ret = OK;
cleanup:
    if (fp)
        fclose(fp);
    if (ek)
        mife_ek_free(ek);
    for (size_t i = 0; i < cp->nslots; ++i) {
        if (cts[i])
            mife_ciphertext_free(cts[i], cp);
    }
    return ret;
}

static int
mife_run_all(const mmap_vtable *mmap, const char *circuit, obf_params_t *op,
             long **inp, long *outp, size_t *kappa, size_t nthreads, aes_randstate_t rng)
{
    const circ_params_t *cp = &op->cp;
    const acirc_t *const circ = cp->circ;
    double start, end;
    mife_sk_t *sk;
    size_t consts = acirc_nconsts(circ) ? 1 : 0;
    int ret = ERR;

    {
        char skname[strlen(circuit) + sizeof ".sk\0"];
        FILE *fp;

        start = current_time();
        snprintf(skname, sizeof skname, "%s.sk", circuit);
        if ((fp = fopen(skname, "r")) == NULL) {
            fprintf(stderr, "error: unable to open '%s' for reading\n", skname);
            exit(EXIT_FAILURE);
        }
        sk = mife_sk_fread(mmap, op, fp);
        fclose(fp);

        end = current_time();
        if (g_verbose)
            fprintf(stderr, "  Reading sk from disk: %.2fs\n", end - start);
    }

    /* Encrypt each input in the right slot */
    for (size_t i = 0; i < cp->nslots - consts; ++i) {
        ret = mife_run_encrypt(mmap, circuit, op, inp[i], i, nthreads, sk, rng);
        if (ret == ERR) {
            fprintf(stderr, "error: mife encryption of '");
            for (size_t j = 0; j < cp->ds[i]; ++j) {
                fprintf(stderr, "%ld", inp[i][j]);
            }
            fprintf(stderr, "' in slot %lu failed\n", i);
            return ERR;
        }
    }
    mife_sk_free(sk);

    {
        char ek[strlen(circuit) + sizeof ".ek\0"];
        char *cts[cp->nslots];

        snprintf(ek, sizeof ek, "%s.ek", circuit);
        for (size_t j = 0; j < cp->nslots - consts; ++j) {
            size_t length = strlen(circuit) + 10 + sizeof ".ct\0";
            cts[j] = my_calloc(length, sizeof cts[j][0]);
            snprintf(cts[j], length, "%s.%lu.ct", circuit, j);
        }
        ret = mife_run_decrypt(ek, cts, outp, mmap, op, kappa, nthreads);
        for (size_t j = 0; j < cp->nslots - consts; ++j) {
            free(cts[j]);
        }
    }
    return ret;
}

int
mife_run_test(const mmap_vtable *mmap, const char *circuit, obf_params_t *op,
              size_t secparam, size_t *kappa, size_t npowers, size_t nthreads,
              aes_randstate_t rng)
{
    const circ_params_t *cp = &op->cp;
    const acirc_t *const circ = cp->circ;
    const size_t has_consts = acirc_nconsts(circ) ? 1 : 0;
    int ret;

    ret = mife_run_setup(mmap, circuit, op, secparam, kappa, npowers, nthreads, rng);
    if (ret == ERR) {
        fprintf(stderr, "error: mife setup failed\n");
        return ERR;
    }

    for (size_t t = 0; t < acirc_ntests(circ); ++t) {
        long *inps[acirc_nsymbols(circ)];
        long outp[acirc_noutputs(circ)];
        size_t idx = 0;
        for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
            inps[i] = my_calloc(cp->ds[i], sizeof inps[i][0]);
            memcpy(inps[i], &acirc_test_input(circ, t)[idx], cp->ds[i] * sizeof inps[i][0]);
            idx += cp->ds[i];
        }
        mife_run_all(mmap, circuit, op, inps, outp, kappa, nthreads, rng);
        if (!print_test_output(t + 1, acirc_test_input(circ, t), acirc_ninputs(circ),
                               acirc_test_output(circ, t), outp, acirc_noutputs(circ), false))
            ret = ERR;
        for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
            free(inps[i]);
        }
    }
    if (g_verbose) {
        unsigned long size, resident;
        if (memory(&size, &resident) == OK)
            fprintf(stderr, "memory: %lu MB\n", resident);
    }
    return ret;
}

size_t
mife_run_smart_kappa(const char *circuit, obf_params_t *op, size_t npowers,
                     size_t nthreads, aes_randstate_t rng)
{
    const circ_params_t *cp = &op->cp;
    const size_t has_consts = acirc_nconsts(cp->circ) ? 1 : 0;
    long *inps[cp->nslots - has_consts];
    size_t kappa = 1;
    bool verbosity = g_verbose;

    if (g_verbose)
        fprintf(stderr, "Choosing κ smartly...\n");

    g_verbose = false;
    if (mife_run_setup(&dummy_vtable, circuit, op, 8, &kappa, npowers, nthreads, rng) == ERR) {
        fprintf(stderr, "error: mife setup failed\n");
        goto cleanup;
    }

    for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
        inps[i] = my_calloc(cp->ds[i], sizeof inps[i][0]);
    }
    if (mife_run_all(&dummy_vtable, circuit, op, inps, NULL, &kappa, nthreads, rng) == ERR) {
        fprintf(stderr, "error: unable to determine κ smartly\n");
        kappa = 0;
    }
    for (size_t i = 0; i < cp->nslots - has_consts; ++i) {
        free(inps[i]);
    }
cleanup:
    g_verbose = verbosity;
    return kappa;
}
