#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

typedef int fp_t;

#define FP_SHIFT 14
#define FP_F (1 << FP_SHIFT)

/* Convert integer N to fixed-point format. */
static inline fp_t int_to_fp(int n) {
  return n * FP_F;
}

/* Convert fixed-point X to integer (truncate toward zero). */
static inline int fp_to_int_zero(fp_t x) {
  return x / FP_F;
}

/* Convert fixed-point X to integer (rounding to nearest). */
static inline int fp_to_int_round(fp_t x) {
  if (x >= 0)
    return (x + FP_F / 2) / FP_F;
  else
    return (x - FP_F / 2) / FP_F;
}

/* Add two fixed-point numbers X and Y. */
static inline fp_t fp_add(fp_t x, fp_t y) {
  return x + y;
}

/* Subtract fixed-point number Y from X. */
static inline fp_t fp_sub(fp_t x, fp_t y) {
  return x - y;
}

/* Add integer N to fixed-point X. */
static inline fp_t fp_add_int(fp_t x, int n) {
  return x + n * FP_F;
}

/* Subtract integer N from fixed-point X. */
static inline fp_t fp_sub_int(fp_t x, int n) {
  return x - n * FP_F;
}

/* Multiply two fixed-point numbers X and Y. */
static inline fp_t fp_mul(fp_t x, fp_t y) {
  return (fp_t)(((int64_t)x * y) / FP_F);
}

/* Multiply fixed-point X by integer N. */
static inline fp_t fp_mul_int(fp_t x, int n) {
  return x * n;
}

/* Divide fixed-point X by fixed-point Y. */
static inline fp_t fp_div(fp_t x, fp_t y) {
  return (fp_t)(((int64_t)x * FP_F) / y);
}

/* Divide fixed-point X by integer N. */
static inline fp_t fp_div_int(fp_t x, int n) {
  return x / n;
}

#endif /* threads/fixed-point.h */
