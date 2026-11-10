#ifndef FINANCIAL_UTILS_H
#define FINANCIAL_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

/*
 * Financial amounts are stored in minor currency units (paise):
 * ₹1.00 = 100 paise
 * ₹100.50 = 10050 paise
 *
 * This prevents floating-point rounding errors and preserves precision.
 */

#define MAX_TRANSACTION_PAISE (1000000000LL)   /* ₹1 Crore per transaction */
#define MAX_BALANCE_PAISE     (100000000000LL)  /* ₹100 Crore max account balance */

/*
 * parse_monetary_amount:
 * Validates and converts user string input into integer paise.
 *
 * Rules:
 * 1. Rejects NULL, empty, or whitespace-only input.
 * 2. Rejects negative amounts (no leading '-').
 * 3. Rejects non-numeric characters (only digits and at most one decimal point '.').
 * 4. Rejects trailing garbage (e.g. "100abc", "50.00xyz").
 * 5. Rejects more than 2 decimal digits (e.g. "100.555").
 * 6. Rejects zero or negative amounts (strictly positive required).
 * 7. Rejects amounts exceeding MAX_TRANSACTION_PAISE or causing integer overflow.
 * 8. Rejects excessively long input strings.
 *
 * Returns 0 on success, -1 on validation error (with descriptive reason in err_msg).
 */
int parse_monetary_amount(const char *input, long long *out_paise, char *err_msg, size_t err_size);

/*
 * format_paise:
 * Formats integer paise into standard human-readable currency representation:
 * Example: 13270000 paise -> "Rs. 132700.00"
 */
void format_paise(long long paise, char *out_buf, size_t out_size);

/*
 * format_paise_plain:
 * Formats integer paise as number string without "Rs. " prefix (e.g. "132700.00")
 */
void format_paise_plain(long long paise, char *out_buf, size_t out_size);

/*
 * Checked addition: returns 0 on success, -1 on overflow
 */
int safe_add_paise(long long a, long long b, long long *result);

/*
 * Checked subtraction: returns 0 on success, -1 on underflow
 */
int safe_sub_paise(long long a, long long b, long long *result);

#endif /* FINANCIAL_UTILS_H */

