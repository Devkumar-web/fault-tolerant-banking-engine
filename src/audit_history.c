#include "app_config.h"
#include "audit_history.h"
#include "txn_journal.h"
#include "financial_utils.h"
int record_transaction_history(int acc_no, uint64_t txn_id, const char *action, long long amount_paise, int other_acc) {
    int fd = open(HISTORYPATH, O_RDWR | O_APPEND | O_CREAT, 0600);
    if (fd < 0) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno == EINTR) continue;
        close(fd);
        return -1;
    }

    time_t now = time(NULL);
    struct tm tm_value;
    if (localtime_r(&now, &tm_value) == NULL) {
        fl.l_type = F_UNLCK; (void)fcntl(fd, F_SETLK, &fl); close(fd); return -1;
    }
    char amt_str[64];
    format_paise_plain(amount_paise, amt_str, sizeof(amt_str));
    struct trans_histroy th;
    memset(&th, 0, sizeof(th));
    th.acc_no = acc_no;
    if (other_acc > 0) {
        snprintf(th.hist, sizeof(th.hist), "[Txn #%llu] %s %s to/from acc no %d at %02d:%02d:%02d %04d-%02d-%02d\n",
                 (unsigned long long)txn_id, amt_str, action, other_acc,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    } else {
        snprintf(th.hist, sizeof(th.hist), "[Txn #%llu] %s %s at %02d:%02d:%02d %04d-%02d-%02d\n",
                 (unsigned long long)txn_id, amt_str, action,
                 tm_value.tm_hour, tm_value.tm_min, tm_value.tm_sec,
                 tm_value.tm_year + 1900, tm_value.tm_mon + 1, tm_value.tm_mday);
    }
    int rc = -1;
    size_t written = 0;
    while (written < sizeof(th)) {
        ssize_t n = write(fd, (const char *)&th + written, sizeof(th) - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    if (written == sizeof(th) && fsync(fd) == 0) rc = 0;
    fl.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &fl) == -1) rc = -1;
    if (close(fd) != 0) rc = -1;
    return rc;
}
