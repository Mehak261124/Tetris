/* =============================================================================
 * math.c  —  Custom Math Library (Optimised)
 * =============================================================================
 * OS MODULE: Core Library / Process Logic Support
 *   Provides all arithmetic the project needs without touching <math.h>.
 *
 * ALGORITHMS (upgraded):
 *   t_mul  — binary (shift-and-add) in O(log b): no * operator
 *   t_div  — binary long-division in O(log² n):  no / operator
 *   t_mod  — derived from t_div + t_mul:         no % operator
 * =============================================================================
 */

#include "../include/t_math.h"

/* ---------------------------------------------------------------------------
 * t_mul(a, b)
 *   Binary (shift-and-add) multiplication — O(log b) iterations.
 *   Uses only <<, >>, +, & — never the * operator.
 *
 *   Algorithm:
 *     While b > 0:
 *       if lowest bit of b is set → accumulate a
 *       shift a left  (doubles it)
 *       shift b right (halves it, moves to next bit)
 * ---------------------------------------------------------------------------
 */
int t_mul(int a, int b) {
    int neg = 0;
    if (a < 0) { neg = !neg; a = -a; }
    if (b < 0) { neg = !neg; b = -b; }

    int result = 0;
    while (b > 0) {
        if (b & 1)        /* lowest bit set → add current 'a' */
            result += a;
        a <<= 1;          /* a * 2 */
        b >>= 1;          /* b / 2 */
    }
    return neg ? -result : result;
}

/* ---------------------------------------------------------------------------
 * t_div(a, b)
 *   Binary long-division — O(log² n), handles all sign combinations.
 *   Uses only <<, >>, - — never the / operator.
 *   Returns 0 if b == 0 (division-by-zero guard).
 *
 *   Algorithm:
 *     Work with unsigned magnitudes.
 *     For each bit position from 63 down to 0:
 *       if (b << i) still fits and is <= remainder → subtract it, set bit i
 * ---------------------------------------------------------------------------
 */
int t_div(int a, int b) {
    if (b == 0) return 0;

    int neg = 0;
    if (a < 0) { neg = !neg; a = -a; }
    if (b < 0) { neg = !neg; b = -b; }

    /* Use unsigned to avoid signed-overflow on left shift */
    unsigned int ua = (unsigned int)a;
    unsigned int ub = (unsigned int)b;
    unsigned int result = 0;
    int i;

    for (i = 30; i >= 0; i--) {
        /* Guard: shifted divisor must not overflow and must fit in remainder */
        unsigned int shifted = ub << i;
        if ((shifted >> i) == ub && shifted <= ua) {
            ua -= shifted;
            result |= (1u << i);
        }
    }
    return neg ? -(int)result : (int)result;
}

/* ---------------------------------------------------------------------------
 * t_mod(a, b)
 *   Integer modulo without % — identity: a mod b = a - (a/b)*b
 *   Returns 0 if b == 0.
 * ---------------------------------------------------------------------------
 */
int t_mod(int a, int b) {
    if (b == 0) return 0;
    return a - t_mul(t_div(a, b), b);
}

/* ---------------------------------------------------------------------------
 * t_abs(val) — absolute value
 * ---------------------------------------------------------------------------
 */
int t_abs(int val) { return (val < 0) ? -val : val; }

/* ---------------------------------------------------------------------------
 * t_in_bounds(val, min, max)
 *   Returns 1 if min <= val <= max, else 0.
 *   Primary boundary-check helper used throughout the engine.
 * ---------------------------------------------------------------------------
 */
int t_in_bounds(int val, int min, int max) {
    return (val >= min && val <= max);
}

/* ---------------------------------------------------------------------------
 * t_clamp / t_max / t_min
 * ---------------------------------------------------------------------------
 */
int t_clamp(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

int t_max(int a, int b) { return (a > b) ? a : b; }
int t_min(int a, int b) { return (a < b) ? a : b; }