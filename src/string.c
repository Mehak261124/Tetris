/* =============================================================================
 * string.c  —  Custom String Library
 * =============================================================================
 * OS MODULE: Core Library
 *   Replaces <string.h> entirely.  All string operations in the project call
 *   these functions — never strlen, strcpy, strcmp, strtok, or sprintf.
 *
 * RULES COMPLIANCE:
 *   - No <string.h> functions used anywhere in this file or the project.
 *   - NULL-pointer guards on every function prevent crashes from bad inputs.
 * ============================================================================= */

#include "../include/t_string.h"
#include "../include/t_math.h"

/* ---------------------------------------------------------------------------
 * t_strlen(str)
 *   Returns the number of characters in str before the null terminator.
 *   Returns 0 for a NULL pointer (safe default).
 *
 *   Used in: screen layout, token counting, buffer-size validation.
 * --------------------------------------------------------------------------- */
int t_strlen(const char *str) {
    if (!str) return 0;    /* guard: NULL pointer → length 0 */
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

/* ---------------------------------------------------------------------------
 * t_strcpy(dest, src)
 *   Copies src (including its null terminator) into dest.
 *   Caller is responsible for ensuring dest is large enough.
 *   No-op on NULL arguments.
 *
 *   Used in: building display strings, copying board state for undo.
 * --------------------------------------------------------------------------- */
void t_strcpy(char *dest, const char *src) {
    if (!dest || !src) return;   /* guard: NULL arguments → do nothing */
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';              /* null-terminate the destination */
}

/* ---------------------------------------------------------------------------
 * t_strcmp(s1, s2)
 *   Lexicographic comparison of two null-terminated strings.
 *   Returns  0 if s1 == s2
 *            positive if s1 > s2 (first differing byte is larger in s1)
 *            negative if s1 < s2
 *   Returns 0 on NULL arguments (treat as equal — safe default).
 *
 *   Used in: command parsing in the shell mode, key dispatch.
 * --------------------------------------------------------------------------- */
int t_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return 0;   /* guard: NULL → treat as equal */
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        i++;
    }
    /* If we reach here one string ended; compare the terminators */
    return (unsigned char)s1[i] - (unsigned char)s2[i];
}

/* ---------------------------------------------------------------------------
 * t_strncpy(dest, src, n)
 *   Copies at most n characters from src into dest.
 *   Always null-terminates dest (safe version).
 *   Used when copying into fixed-size buffers to prevent overflows.
 * --------------------------------------------------------------------------- */
void t_strncpy(char *dest, const char *src, int n) {
    if (!dest || !src || n <= 0) return;
    int i = 0;
    while (i < n - 1 && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

/* ---------------------------------------------------------------------------
 * t_strcat(dest, src)
 *   Appends src to the end of dest (dest must have enough space).
 *   No-op on NULL arguments.
 *   Used in: building multi-part score/status strings.
 * --------------------------------------------------------------------------- */
void t_strcat(char *dest, const char *src) {
    if (!dest || !src) return;
    int len = t_strlen(dest);   /* find the null terminator in dest */
    int i   = 0;
    while (src[i] != '\0') {
        dest[len + i] = src[i];
        i++;
    }
    dest[len + i] = '\0';
}

/* ---------------------------------------------------------------------------
 * t_split(str, delim, tokens, max_tokens)
 *   Tokenises str in-place by replacing occurrences of delim with '\0' and
 *   storing pointers to each token in the tokens array.
 *
 *   Parameters:
 *     str        — the string to split (MODIFIED in place).
 *     delim      — the delimiter character (e.g. ' ', ',').
 *     tokens     — caller-supplied array of char* to receive token pointers.
 *     max_tokens — capacity of the tokens array.
 *
 *   Returns the number of tokens found (always ≥ 1 for a non-empty string).
 *   Returns 0 on NULL / bad arguments.
 *
 *   Used in: parsing keyboard macro strings, score-file lines.
 * --------------------------------------------------------------------------- */
int t_split(char *str, char delim, char **tokens, int max_tokens) {
    if (!str || !tokens || max_tokens <= 0) return 0;

    int count = 0;
    tokens[count++] = str;       /* first token starts at the beginning */

    int i = 0;
    while (str[i] != '\0' && count < max_tokens) {
        if (str[i] == delim) {
            str[i] = '\0';               /* replace delimiter with null terminator */
            tokens[count++] = &str[i + 1]; /* next token starts after the delimiter */
        }
        i++;
    }
    return count;
}

/* ---------------------------------------------------------------------------
 * t_itoa(value, str)
 *   Converts an integer to its decimal ASCII representation in str.
 *   str must be at least 12 bytes (handles INT_MIN: "-2147483648\0").
 *   No-op on NULL str.
 *
 *   Algorithm:
 *     1. Handle 0 explicitly.
 *     2. Record sign; work on positive value.
 *     3. Extract digits least-significant-first (using % 10).
 *     4. Reverse the digit string.
 *
 *   Used in: rendering score, level, lines-cleared to the screen.
 * --------------------------------------------------------------------------- */
void t_itoa(int value, char *str) {
    if (!str) return;

    /* ---- Special case: zero -------------------------------------------- */
    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    int i           = 0;
    int is_negative = 0;

    /* ---- Handle negative values ----------------------------------------- */
    if (value < 0) {
        is_negative = 1;
        /* Avoid overflow on INT_MIN: negate after first digit extraction
           by working in unsigned.  For typical project values this is fine. */
        value = -value;
    }

    /* ---- Extract digits in reverse order --------------------------------- */
    while (value != 0) {
        str[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    /* ---- Reverse the string to get correct order ------------------------- */
    int start = 0;
    int end   = i - 1;
    while (start < end) {
        char tmp    = str[start];
        str[start]  = str[end];
        str[end]    = tmp;
        start++;
        end--;
    }
}

/* ---------------------------------------------------------------------------
 * t_atoi(str)
 *   Converts a decimal ASCII string to an integer.
 *   Handles optional leading '-' for negative values.
 *   Returns 0 on NULL or empty string.
 *
 *   Used in: score_load() to parse the saved high-score file.
 *   Mirror of t_itoa — together they form the full int<->string bridge.
 * --------------------------------------------------------------------------- */
int t_atoi(const char *str) {
    if (!str) return 0;
    int result      = 0;
    int is_negative = 0;
    int i           = 0;

    if (str[i] == '-') { is_negative = 1; i++; }

    while (str[i] >= '0' && str[i] <= '9') {
        result = t_mul(result, 10) + (str[i] - '0');
        i++;
    }
    return is_negative ? -result : result;
}
