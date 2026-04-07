#ifndef T_MATH_H
#define T_MATH_H

/* =============================================================================
 * t_math.h  —  Custom Math Library Public Interface
 * =============================================================================
 * All arithmetic in the project uses these functions instead of <math.h>.
 * ============================================================================= */

int t_mul(int a, int b);                      /* a * b                         */
int t_div(int a, int b);                      /* a / b  (safe: returns 0 if b=0) */
int t_mod(int a, int b);                      /* a % b  (safe: returns 0 if b=0) */
int t_abs(int val);                           /* |val|                          */
int t_in_bounds(int val, int min, int max);   /* 1 if min<=val<=max, else 0    */
int t_clamp(int val, int min, int max);       /* clamp val into [min,max]      */
int t_max(int a, int b);                      /* larger of a, b                */
int t_min(int a, int b);                      /* smaller of a, b               */

#endif /* T_MATH_H */
