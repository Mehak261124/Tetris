#ifndef T_STRING_H
#define T_STRING_H

/* =============================================================================
 * t_string.h  —  Custom String Library Public Interface
 * =============================================================================
 * Full replacement for <string.h>.  No standard string functions used anywhere.
 * ============================================================================= */

int  t_strlen (const char *str);
void t_strcpy (char *dest, const char *src);
void t_strncpy(char *dest, const char *src, int n);
void t_strcat (char *dest, const char *src);
int  t_strcmp (const char *s1, const char *s2);
int  t_split  (char *str, char delim, char **tokens, int max_tokens);
void t_itoa   (int value, char *str);
int  t_atoi   (const char *str);

#endif /* T_STRING_H */
