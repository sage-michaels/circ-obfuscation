#ifndef __UTILS_H__
#define __UTILS_H__

#include <aesrand.h>
#include <stdbool.h>
#include <gmp.h>

#define OK    0
#define ERR (-1)

#define PRIVATE __attribute__ ((visibility ("hidden")))

typedef enum debug_e {
    ERROR = 0,
    WARN = 1,
    DEBUG = 2,
    INFO = 3
} debug_e;
extern debug_e g_debug;
extern bool g_verbose;

#define LOG_ERROR (g_debug >= ERROR)
#define LOG_WARN  (g_debug >= WARN)
#define LOG_DEBUG (g_debug >= DEBUG)
#define LOG_INFO  (g_debug >= INFO)

double current_time(void);

int max(int, int);

void array_print           (int*, size_t);
void array_print_ui        (size_t*, size_t);
void array_printstring     (int *bits, size_t n);
void array_printstring_rev (int *bits, size_t n);

void mpz_randomm_inv(mpz_t rop, aes_randstate_t rng, const mpz_t modulus);

mpz_t * mpz_vect_new(size_t n);
void mpz_vect_init(mpz_t *vec, size_t n);
mpz_t * mpz_vect_create_of_fmpz(fmpz_t *fvec, size_t n);
void mpz_vect_print(mpz_t *vec, size_t n);
void mpz_vect_clear(mpz_t *vec, size_t n);
void mpz_vect_free(mpz_t *vec, size_t n);

void mpz_vect_urandomm(mpz_t *vec, const mpz_t modulus, size_t n, aes_randstate_t rng);
void mpz_vect_urandomms(mpz_t *vec, const mpz_t *moduli, size_t n, aes_randstate_t rng);

void mpz_vect_mul(mpz_t *rop, const mpz_t *xs, const mpz_t *ys, size_t n);
void mpz_vect_mod(mpz_t *rop, const mpz_t *xs, const mpz_t *moduli, size_t n);
void mpz_vect_mul_mod(mpz_t *rop, const mpz_t *xs, const mpz_t *ys, const mpz_t *moduli, size_t n);

void mpz_vect_set(mpz_t *rop, const mpz_t *xs, size_t n);

void mpz_vect_repeat_ui(mpz_t *vec, size_t x, size_t n);

size_t bit(size_t x, size_t i);

void * my_calloc(size_t nmemb, size_t size);
void * my_malloc(size_t size);
void * my_realloc(void *ptr, size_t size);

void ulong_fread(unsigned long *x, FILE *fp);
void ulong_fwrite(unsigned long x, FILE *fp);

void size_t_fread(size_t *x, FILE *fp);
void size_t_fwrite(size_t x, FILE *fp);

void bool_fread(bool *x, FILE *fp);
void bool_fwrite(bool x, FILE *fp);

#define PUT_NEWLINE(fp) fprintf(fp, "\n")
#define PUT_SPACE(fp) fprintf(fp, " ")
#define GET_NEWLINE(fp) fscanf(fp, "\n")
#define GET_SPACE(fp) fscanf(fp, " ")

void array_add(size_t *rop, const size_t *xs, const size_t *ys, size_t n);
bool array_eq(const size_t *xs, const size_t *ys, size_t n);
size_t array_sum(const size_t *xs, size_t n);

void print_progress (size_t cur, size_t total);

#endif
