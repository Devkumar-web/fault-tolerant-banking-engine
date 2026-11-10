#include "app_config.h"
#include "financial_utils.h"

int parse_monetary_amount(const char *input, long long *out_paise, char *err_msg, size_t err_size) {
    if (!out_paise) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Internal error: null destination pointer.");
        return -1;
    }
    *out_paise = 0;

    if (!input) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount cannot be empty.");
        return -1;
    }

    /* Trim leading whitespace */
    const char *start = input;
    while (*start && isspace((unsigned char)*start)) start++;

    if (*start == '\0') {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount cannot be empty.");
        return -1;
    }

    /* Find end and check length */
    const char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;

    size_t len = (size_t)(end - start + 1);
    if (len > 32) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount input is excessively long.");
        return -1;
    }

    /* Check for negative sign */
    if (*start == '-') {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount cannot be negative. Must be positive.");
        return -1;
    }
    if (*start == '+') {
        start++;
        len--;
        if (len == 0) {
            if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Invalid amount format.");
            return -1;
        }
    }

    /* Validate character set: must be digits with at most one dot */
    int dot_count = 0;
    int digits_before_dot = 0;
    int digits_after_dot = 0;
    int after_dot = 0;

    for (size_t i = 0; i < len; i++) {
        char ch = start[i];
        if (ch == '.') {
            dot_count++;
            if (dot_count > 1) {
                if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Invalid amount: multiple decimal points found.");
                return -1;
            }
            after_dot = 1;
        } else if (isdigit((unsigned char)ch)) {
            if (after_dot) {
                digits_after_dot++;
            } else {
                digits_before_dot++;
            }
        } else {
            if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Invalid characters in amount. Numbers only.");
            return -1;
        }
    }

    if (digits_before_dot == 0 && digits_after_dot == 0) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount must contain digits.");
        return -1;
    }

    if (dot_count == 1 && digits_after_dot == 0) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Invalid decimal format: digits expected after decimal point.");
        return -1;
    }

    if (digits_after_dot > 2) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Invalid precision: maximum 2 decimal places allowed.");
        return -1;
    }

    /* Parse integer rupees part */
    long long rupees = 0;
    const char *p = start;
    while (*p && *p != '.') {
        int d = *p - '0';
        if (rupees > (MAX_TRANSACTION_PAISE / 100)) {
            if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount exceeds maximum permitted limit.");
            return -1;
        }
        rupees = rupees * 10 + d;
        p++;
    }

    /* Parse paise part */
    long long paise = 0;
    if (*p == '.') {
        p++;
        if (*p) {
            paise = (*p - '0') * 10;
            p++;
            if (*p) {
                paise += (*p - '0');
            }
        }
    }

    long long total = rupees * 100 + paise;

    if (total <= 0) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount must be strictly greater than zero.");
        return -1;
    }

    if (total > MAX_TRANSACTION_PAISE) {
        if (err_msg && err_size > 0) snprintf(err_msg, err_size, "Amount exceeds maximum transaction limit of Rs. 10,000,000.00.");
        return -1;
    }

    *out_paise = total;
    return 0;
}

void format_paise(long long paise, char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return;
    long long abs_paise = llabs(paise);
    long long rupees = abs_paise / 100;
    long long rem = abs_paise % 100;

    if (paise < 0) {
        snprintf(out_buf, out_size, "-Rs. %lld.%02lld", rupees, rem);
    } else {
        snprintf(out_buf, out_size, "Rs. %lld.%02lld", rupees, rem);
    }
}

void format_paise_plain(long long paise, char *out_buf, size_t out_size) {
    if (!out_buf || out_size == 0) return;
    long long abs_paise = llabs(paise);
    long long rupees = abs_paise / 100;
    long long rem = abs_paise % 100;

    if (paise < 0) {
        snprintf(out_buf, out_size, "-%lld.%02lld", rupees, rem);
    } else {
        snprintf(out_buf, out_size, "%lld.%02lld", rupees, rem);
    }
}

int safe_add_paise(long long a, long long b, long long *result) {
    if (!result) return -1;
    if (b > 0 && a > (MAX_BALANCE_PAISE - b)) return -1;
    if (b < 0 && a < (LLONG_MIN - b)) return -1;
    *result = a + b;
    return 0;
}

int safe_sub_paise(long long a, long long b, long long *result) {
    if (!result) return -1;
    if (b < 0 && a > (LLONG_MAX + b)) return -1;
    if (b > 0 && a < (LLONG_MIN + b)) return -1;
    *result = a - b;
    return 0;
}
