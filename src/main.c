#include "dbg.h"
#include "mmap.h"
#include "obfuscator.h"
#include "util.h"

#include "ab/obfuscator.h"
#include "lin/obfuscator.h"
#include "zim/obfuscator.h"

#include <aesrand.h>
#include <acirc.h>
#include <mmap/mmap_clt.h>
#include <mmap/mmap_dummy.h>

#include <assert.h>
#include <err.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

enum scheme_e {
    SCHEME_AB,
    SCHEME_ZIM,
    SCHEME_LIN,
};

enum mmap_e {
    MMAP_CLT,
    MMAP_DUMMY,
};

static char *
scheme_to_string(enum scheme_e scheme)
{
    switch (scheme) {
    case SCHEME_AB:
        return "Applebaum-Brakerski";
    case SCHEME_ZIM:
        return "Zimmerman";
    case SCHEME_LIN:
        return "Lin";
    default:
        return "";
    }
}

static char *
mmap_to_string(enum mmap_e mmap)
{
    switch (mmap) {
    case MMAP_CLT:
        return "CLT";
    case MMAP_DUMMY:
        return "Dummy";
    default:
        return "";
    }
}

struct args_t {
    char *circuit;
    enum mmap_e mmap;
    size_t secparam;
    bool evaluate;
    bool obfuscate;
    bool simple;
    enum scheme_e scheme;
};

static void
args_init(struct args_t *args)
{
    args->circuit = NULL;
    args->mmap = MMAP_CLT;
    args->secparam = 16;
    args->evaluate = false;
    args->obfuscate = true;
    args->simple = false;
    args->scheme = SCHEME_ZIM;
}

static void
args_print(const struct args_t *const args)
{
    printf("Obfuscation details:\n"
"* Circuit: %s\n"
"* Multilinear map: %s\n"
"* Security parameter: %lu\n"
"* Obfuscating? %s Evaluating? %s\n"
"* Scheme: %s\n",
           args->circuit, mmap_to_string(args->mmap), args->secparam,
           args->obfuscate ? "Y" : "N",
           args->evaluate ? "Y" : "N", scheme_to_string(args->scheme));
}

static void
usage(int ret)
{
    struct args_t defaults;
    args_init(&defaults);
    printf("Usage: main [options] <circuit>\n");
    printf("Options:\n"
"    --all, -a         obfuscate and evaluate\n"
"    --debug <LEVEL>   set debug level (options: ERROR, WARN, DEBUG, INFO | default: ERROR)\n"
"    --evaluate, -e    evaluate obfuscation\n"
"    --obfuscate, -o   construct obfuscation (default)\n"
"    --lambda, -l <λ>  set security parameter to <λ> when obfuscating (default=%lu)\n"
"    --scheme <NAME>   set scheme to NAME (options: AB, ZIM, LIN | default: ZIM)\n"
"    --mmap <NAME>     set mmap to NAME (options: CLT, DUMMY | default: CLT)\n"
"    --simple, -s      use SimpleObf scheme when using AB scheme\n"
"    --verbose, -v     be verbose\n"
"    --help, -h        print this message\n",
           defaults.secparam);
    exit(ret);
}

static const struct option opts[] = {
    {"all", no_argument, 0, 'a'},
    {"debug", required_argument, 0, 'D'},
    {"evaluate", no_argument, 0, 'e'},
    {"obfuscate", no_argument, 0, 'o'},
    {"lambda", required_argument, 0, 'l'},
    {"mmap", required_argument, 0, 'M'},
    {"scheme", required_argument, 0, 'S'},
    {"simple", no_argument, 0, 's'},
    {"verbose", no_argument, 0, 'v'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};
static const char *short_opts = "aD:eol:M:sS:vh";

static int
_evaluate(const obfuscator_vtable *const vt, const struct args_t *const args,
          const acirc *const c, const obfuscation *const obf)
{
    (void) args;
    int res[c->noutputs];

    for (size_t i = 0; i < c->ntests; i++) {
        if (vt->evaluate(res, c->testinps[i], obf) == ERR)
            return ERR;
        bool ok = true;
        for (size_t j = 0; j < c->noutputs; ++j) {
            if (!!res[j] != !!c->testouts[i][j])
                ok = false;
        }
        if (!ok)
            printf("\033[1;41m");
        printf("test %lu input=", i);
        array_printstring_rev(c->testinps[i], c->ninputs);
        printf(" expected=");
        array_printstring_rev(c->testouts[i], c->noutputs);
        printf(" got=");
        array_printstring_rev(res, c->noutputs);
        if (!ok)
            printf("\033[0m");
        puts("");
    }
    return OK;
}

static int
run(const struct args_t *const args)
{
    obf_params_t *params;
    acirc c;
    int ret = 1;
    const mmap_vtable *mmap;
    const obfuscator_vtable *vt;
    const op_vtable *op_vt;

    args_print(args);

    switch (args->mmap) {
    case MMAP_CLT:
        mmap = &clt_vtable;
        break;
    case MMAP_DUMMY:
        mmap = &dummy_vtable;
        break;
    default:
        abort();
    }

    switch (args->scheme) {
    case SCHEME_AB:
        vt = &ab_obfuscator_vtable;
        op_vt = &ab_op_vtable;
        break;
    case SCHEME_LIN:
        vt = &lin_obfuscator_vtable;
        op_vt = &lin_op_vtable;
        break;
    case SCHEME_ZIM:
        vt = &zim_obfuscator_vtable;
        op_vt = &zim_op_vtable;
        break;
    default:
        abort();
    }

    acirc_init(&c);
    log_info("reading circuit '%s'...", args->circuit);
    if (acirc_parse(&c, args->circuit) == ACIRC_ERR) {
        log_err("parsing circuit '%s' failed!", args->circuit);
        return 1;
    }

    log_info("circuit: ninputs=%lu nconsts=%lu noutputs=%lu ngates=%lu ntests=%lu nrefs=%lu",
             c.ninputs, c.nconsts, c.noutputs, c.ngates, c.ntests, c.nrefs);
    /* printf("consts: "); */
    /* array_print(c.consts, c.nconsts); */
    /* puts(""); */

    params = op_vt->new(&c, args->simple ? AB_FLAG_SIMPLE : AB_FLAG_NONE);

    /* for (size_t i = 0; i < c.noutputs; i++) { */
    /*     printf("output bit %lu: type=", i); */
    /*     array_print_ui(params.types[i], params.n + params.m + 1); */
    /*     puts(""); */
    /* } */

#ifndef NDEBUG
    acirc_ensure(&c);
#endif

    /* if (args->obfuscate) { */
        /* char fname[strlen(args->circuit) + 5]; */
        obfuscation *obf;
        /* FILE *f; */

        obf = vt->new(mmap, params, args->secparam);
        if (obf == NULL)
            goto cleanup;
        if (vt->obfuscate(obf) == ERR) {
            errx(1, "obfuscation failed");
        }

    /*     snprintf(fname, sizeof fname, "%s.obf", args->circuit); */
    /*     f = fopen(fname, "w"); */
    /*     if (f == NULL) { */
    /*         log_err("unable to open '%s' for writing", fname); */
    /*         goto cleanup; */
    /*     } */
    /*     vt->fwrite(obf, f); */
    /*     vt->free(obf); */
    /*     fclose(f); */
    /* } */

    /* if (args->evaluate) { */
        /* char fname[strlen(args->circuit) + 5]; */
        /* obfuscation *obf; */
        /* FILE *f; */

        /* snprintf(fname, sizeof fname, "%s.obf", args->circuit); */
        /* f = fopen(fname, "r"); */
        /* if (f == NULL) { */
        /*     log_err("unable to open '%s' for reading", fname); */
        /*     goto cleanup; */
        /* } */
        /* obf = vt->fread(args->mmap, params, f); */
        /* fclose(f); */
        /* if (obf == NULL) { */
        /*     log_err("unable to read obfuscation"); */
        /*     goto cleanup; */
        /* } */
        if (_evaluate(vt, args, &c, obf) == ERR)
            errx(1, "evaluation failed");
        vt->free(obf);
    /* } */
    ret = 0;
cleanup:
    op_vt->free(params);
    acirc_clear(&c);

    return ret;
}

int
main(int argc, char **argv)
{
    int c, idx;
    struct args_t args;

    args_init(&args);

    while ((c = getopt_long(argc, argv, short_opts, opts, &idx)) != -1) {
        switch (c) {
        case 'a':
            args.evaluate = true;
            args.obfuscate = true;
            break;
        case 'D':
            if (strcmp(optarg, "ERROR") == 0) {
                g_debug = ERROR;
            } else if (strcmp(optarg, "WARN") == 0) {
                g_debug = WARN;
            } else if (strcmp(optarg, "DEBUG") == 0) {
                g_debug = DEBUG;
            } else if (strcmp(optarg, "INFO") == 0) {
                g_debug = INFO;
            } else {
                fprintf(stderr, "error: unknown debug level \"%s\"\n", optarg);
                usage(EXIT_FAILURE);
            }
        case 'e':
            args.evaluate = true;
            args.obfuscate = false;
            break;
        case 'o':
            args.obfuscate = true;
            args.evaluate = false;
            break;
        case 'l':
            args.secparam = atoi(optarg);
            break;
        case 'M':
            if (strcmp(optarg, "CLT") == 0) {
                args.mmap = MMAP_CLT;
            } else if (strcmp(optarg, "DUMMY") == 0) {
                args.mmap = MMAP_DUMMY;
            } else {
                fprintf(stderr, "error: unknown mmap \"%s\"\n", optarg);
                usage(EXIT_FAILURE);
            }
            break;
        case 's':
            args.simple = true;
            break;
        case 'S':
            if (strcmp(optarg, "AB") == 0) {
                args.scheme = SCHEME_AB;
            } else if (strcmp(optarg, "ZIM") == 0) {
                args.scheme = SCHEME_ZIM;
            } else if (strcmp(optarg, "LIN") == 0) {
                args.scheme = SCHEME_LIN;
            } else {
                fprintf(stderr, "error: unknown scheme \"%s\"\n", optarg);
                usage(EXIT_FAILURE);
            }
            break;
        case 'v':
            g_verbose = true;
            break;
        case 'h':
            usage(EXIT_SUCCESS);
            break;
        default:
            usage(EXIT_FAILURE);
            break;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "[%s] error: circuit required\n", __func__);
        usage(EXIT_FAILURE);
    } else if (optind == argc - 1) {
        args.circuit = argv[optind];
    } else {
        fprintf(stderr, "[%s] error: unexpected argument \"%s\"\n", __func__,
                argv[optind]);
        usage(EXIT_FAILURE);
    }

    return run(&args);
}
