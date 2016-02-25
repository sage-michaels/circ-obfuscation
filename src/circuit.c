#include "circuit.h"

void ensure_space(circuit *c, circref ref);

void circ_init(circuit *c) {
    c->ninputs = 0;
    c->nconsts = 0;
    c->ngates  = 0;
    c->ntests  = 0;
    c->outref  = -1;
    c->nrefs   = 0;
    c->_refalloc  = 2;
    c->_testalloc = 2;
    c->args     = malloc(c->_refalloc * sizeof(int **));
    c->ops      = malloc(c->_refalloc * sizeof(operation));
    c->testinps = malloc(c->_testalloc * sizeof(int **));
    c->testouts = malloc(c->_testalloc * sizeof(int *));
}

void circ_clear(circuit *c) {
    for (int i = 0; i < c->ngates; i++) {
        free(c->args[i]);
    }
    free(c->args);
    free(c->ops);
    for (int i = 0; i < c->ntests; i++) {
        free(c->testinps[i]);
    }
    free(c->testinps);
    free(c->testouts);
}

int eval_circ(circuit *c, circref ref, int *xs) {
    operation op = c->ops[ref];
    switch (op) {
        case XINPUT: return xs[c->args[ref][0]];
        case YINPUT: return c->args[ref][1];
    }
    int xres = eval_circ(c, c->args[ref][0], xs);
    int yres = eval_circ(c, c->args[ref][1], xs);
    switch (op) {
        case ADD: return xres + yres;
        case SUB: return xres - yres;
        case MUL: return xres * yres;
    }
    exit(EXIT_FAILURE); // should never be reached
}

void topo_helper(int ref, int* topo, int* seen, int* i, circuit* c) {
    if (seen[ref]) return;
    operation op = c->ops[ref];
    if (op == ADD || op == SUB || op == MUL) {
        topo_helper(c->args[ref][0], topo, seen, i, c);
        topo_helper(c->args[ref][1], topo, seen, i, c);
    }
    topo[(*i)++] = ref;
    seen[ref]    = 1;
}

void topological_order(int* topo, circuit* c) {
    int* seen = calloc(c->_refalloc, sizeof(int));
    int i = 0;
    topo_helper(c->outref, topo, seen, &i, c);
    free(seen);
}

// dependencies fills an array with the refs to the subcircuit rooted at ref.
// deps is the target array, i is an index into it.
void dependencies_helper(int* deps, int* seen, int* i, circuit* c, int ref) {
    if (seen[ref]) return;
    operation op = c->ops[ref];
    if (op == XINPUT || op == YINPUT) return;
    // otherwise it's an ADD, SUB, or MUL gate
    int xarg = c->args[ref][0];
    int yarg = c->args[ref][1];
    deps[(*i)++] = xarg;
    deps[(*i)++] = yarg;
    dependencies_helper(deps, seen, i, c, xarg);
    dependencies_helper(deps, seen, i, c, yarg);
    seen[ref] = 1;
}

int dependencies(int* deps, circuit* c, int ref) {
    int* seen = calloc(c->nrefs, sizeof(int));
    int ndeps = 0;
    dependencies_helper(deps, seen, &ndeps, c, ref);
    free(seen);
    return ndeps;
}

// returns number of levels
int topological_levels(int** levels, int* level_sizes, circuit* c) {
    int* topo = calloc(c->nrefs, sizeof(int));
    int* deps = malloc(c->nrefs * sizeof(int));
    for (int i = 0; i < c->nrefs; i++)
        level_sizes[i] = 0;
    int max_level = 0;

    topological_order(topo, c);

    for (int i = 0; i < c->nrefs; i++) {
        int ref = topo[i];
        int ndeps = dependencies(deps, c, ref);
        // find the right level for this ref
        for (int j = 0; j < c->nrefs; j++) {
            bool has_dep = any_in_array(deps, ndeps, levels[j], level_sizes[j]);
            if (has_dep) continue; // try the next level
            // otherwise the ref belongs on this level
            levels[j][level_sizes[j]++] = ref; // push this ref
            if (j > max_level) max_level = j;
            break;
        }
    }

    free(topo);
    free(deps);
    return max_level + 1;
}

int xdegree(circuit *c, circref ref, int xid) {
    operation op = c->ops[ref];
    if (op == XINPUT)
        if (c->args[ref][0] == xid)
            return 1;
        else
            return 0;
    else if (op == YINPUT)
        return 0;
    int xres = xdegree(c, c->args[ref][0], xid);
    int yres = xdegree(c, c->args[ref][1], xid);
    if (op == ADD || op == SUB)
        return max(xres, yres);
    else if (op == MUL)
        return xres + yres;
    exit(EXIT_FAILURE);
}

int ydegree(circuit *c, circref ref) {
    operation op = c->ops[ref];
    if (op == XINPUT)
        return 0;
    else if (op == YINPUT)
        return 1;
    int xres = ydegree(c, c->args[ref][0]);
    int yres = ydegree(c, c->args[ref][1]);
    if (op == ADD || op == SUB)
        return max(xres, yres);
    else if (op == MUL)
        return xres + yres;
    exit(EXIT_FAILURE);
}

int ensure(circuit *c) {
    int res;
    bool ok = true;
    for (int i = 0; i < c->ntests; i++) {
        res = eval_circ(c, c->outref, c->testinps[i]);
        if (g_verbose) {
            printf("test %d input=", i);
            for (int j = 0; j < c->ninputs; j++) {
                printf("%d", c->testinps[i][j]);
            }
            printf(" expected=%d got=%d\n", c->testouts[i], res > 0);
        }
        ok = ((res > 0) == c->testouts[i]) && ok;
    }
    return ok;
}

void circ_add_test(circuit *c, char *inpstr, char *out) {
    if (c->ntests >= c->_testalloc) {
        c->testinps = realloc(c->testinps, 2 * c->_testalloc * sizeof(int**));
        c->testouts = realloc(c->testouts, 2 * c->_testalloc * sizeof(int*));
        c->_testalloc *= 2;
        return circ_add_test(c, inpstr, out);
    }

    int len = strlen(inpstr);
    int *inp = malloc(len * sizeof(int));

    for (int i = 0; i < len; i++) {
        if (inpstr[len-1 - i] == '1') {
            inp[i] = 1;
        } else {
            inp[i] = 0;
        }
    }

    c->testinps[c->ntests] = inp;
    c->testouts[c->ntests] = atoi(out);
    c->ntests += 1;
}

void circ_add_xinput(circuit *c, int ref, int id) {
    ensure_space(c, ref);
    c->ninputs += 1;
    c->nrefs   += 1;
    c->ops[ref] = XINPUT;
    int *args = malloc(2 * sizeof(int));
    args[0] = id;
    args[1] = -1;
    c->args[ref] = args;
}

void circ_add_yinput(circuit *c, int ref, int id, int val) {
    ensure_space(c, ref);
    c->nconsts  += 1;
    c->nrefs    += 1;
    c->ops[ref]  = YINPUT;
    int *args = malloc(2 * sizeof(int));
    args[0] = id;
    args[1] = val;
    c->args[ref] = args;
}

void circ_add_gate(circuit *c, int ref, operation op, int xref, int yref, bool is_output) {
    ensure_space(c, ref);
    c->ngates   += 1;
    c->nrefs    += 1;
    c->ops[ref]  = op;
    int *args = malloc(2 * sizeof(int));
    args[0] = xref;
    args[1] = yref;
    c->args[ref] = args;
    if (is_output) c->outref = ref;
}

void ensure_space(circuit *c, circref ref) {
    if (ref >= c->_refalloc) {
        c->args = realloc(c->args, 2 * c->_refalloc * sizeof(int**));
        c->ops  = realloc(c->ops,  2 * c->_refalloc * sizeof(operation));
        c->_refalloc *= 2;
        ensure_space(c, ref);
    }
}

operation str2op(char *s) {
    if (strcmp(s, "ADD") == 0) {
        return ADD;
    } else if (strcmp(s, "SUB") == 0) {
        return SUB;
    } else if (strcmp(s, "MUL") == 0) {
        return MUL;
    }
    exit(EXIT_FAILURE);
}

