#include "text_utils.h"
#include "app_config.h"
#include "account_storage.h"

int migrate_customers_if_legacy(const char *path) {
    if (!path) return -1;
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0; /* File does not exist yet */
    }
    if (st.st_size == 0) return 0;

    size_t legacy_size = sizeof(struct CustomerLegacy); /* 324 bytes */
    size_t new_size = sizeof(struct Customer);

    /* If file size is multiple of legacy size and NOT multiple of new size */
    if ((st.st_size % (off_t)legacy_size == 0) && (st.st_size % (off_t)new_size != 0)) {
        printf("[Storage] Detected legacy customer database (%lld bytes, %zu bytes/rec). Migrating to integer paise...\n",
               (long long)st.st_size, legacy_size);

        int in_fd = open(path, O_RDONLY);
        if (in_fd < 0) {
            perror("[Storage] Error opening legacy file for migration");
            return -1;
        }

        char tmp_path[256];
        snprintf(tmp_path, sizeof(tmp_path), "%s.migration_tmp", path);
        int out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            perror("[Storage] Error creating temporary migration file");
            close(in_fd);
            return -1;
        }

        struct CustomerLegacy leg;
        struct Customer new_cust;
        int count = 0;

        while (read(in_fd, &leg, sizeof(leg)) == (ssize_t)sizeof(leg)) {
            memset(&new_cust, 0, sizeof(new_cust));
            new_cust.accountNumber = leg.accountNumber;
            /* Safe conversion from float to integer paise */
            double scaled = (double)leg.balance * 100.0;
            new_cust.balance = (long long)(scaled >= 0.0 ? (scaled + 0.5) : (scaled - 0.5));
            copy_text(new_cust.customerName, leg.customerName, sizeof(new_cust.customerName));
            copy_text(new_cust.password, leg.password, sizeof(new_cust.password));
            new_cust.accountType = leg.accountType;
            new_cust.activeStatus = leg.activeStatus;

            if (write(out_fd, &new_cust, sizeof(new_cust)) != (ssize_t)sizeof(new_cust)) {
                perror("[Storage] Write error during migration");
                close(in_fd);
                close(out_fd);
                unlink(tmp_path);
                return -1;
            }
            count++;
        }

        fsync(out_fd);
        close(in_fd);
        close(out_fd);

        /* Backup original file */
        char bak_path[256];
        snprintf(bak_path, sizeof(bak_path), "%s.legacy_bak", path);
        rename(path, bak_path);
        rename(tmp_path, path);

        printf("[Storage] Successfully migrated %d accounts to integer paise format. Backup saved to %s.\n", count, bak_path);
        return 1;
    }
    return 0;
}

int find_customer_record(int fd, int account_number, off_t *out_offset, struct Customer *out_customer) {
    if (fd < 0 || account_number <= 0) return -1;

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        return -1;
    }

    struct Customer c;
    off_t cur_offset = 0;

    while (1) {
        ssize_t bytes_read = read(fd, &c, sizeof(struct Customer));
        if (bytes_read == 0) {
            return 1; /* EOF: Not found */
        }
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (bytes_read != (ssize_t)sizeof(struct Customer)) {
            /* Corrupted or truncated record */
            return -1;
        }

        if (c.accountNumber == account_number) {
            if (out_offset) *out_offset = cur_offset;
            if (out_customer) *out_customer = c;
            return 0; /* Found */
        }

        cur_offset += (off_t)sizeof(struct Customer);
    }
}

int read_customer_record(int fd, off_t offset, struct Customer *out_customer) {
    if (fd < 0 || offset < 0 || !out_customer) return -1;
    ssize_t n = pread(fd, out_customer, sizeof(struct Customer), offset);
    return (n == (ssize_t)sizeof(struct Customer)) ? 0 : -1;
}

int write_customer_record(int fd, off_t offset, const struct Customer *customer) {
    if (fd < 0 || offset < 0 || !customer) return -1;
    ssize_t n = pwrite(fd, customer, sizeof(struct Customer), offset);
    return (n == (ssize_t)sizeof(struct Customer)) ? 0 : -1;
}

int lock_customer_record(int fd, off_t offset, int lock_type) {
    if (fd < 0 || offset < 0) return -1;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = (short)lock_type;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = (off_t)sizeof(struct Customer);
    fl.l_pid = getpid();

    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int unlock_customer_record(int fd, off_t offset) {
    if (fd < 0 || offset < 0) return -1;
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = (off_t)sizeof(struct Customer);
    fl.l_pid = getpid();

    while (fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int lock_two_customer_records(int fd, off_t offset1, off_t offset2, int lock_type) {
    if (offset1 == offset2) {
        return lock_customer_record(fd, offset1, lock_type);
    }
    off_t first = (offset1 < offset2) ? offset1 : offset2;
    off_t second = (offset1 < offset2) ? offset2 : offset1;

    if (lock_customer_record(fd, first, lock_type) != 0) {
        return -1;
    }
    if (lock_customer_record(fd, second, lock_type) != 0) {
        unlock_customer_record(fd, first);
        return -1;
    }
    return 0;
}

int unlock_two_customer_records(int fd, off_t offset1, off_t offset2) {
    int ret1 = unlock_customer_record(fd, offset1);
    if (offset1 == offset2) return ret1;
    int ret2 = unlock_customer_record(fd, offset2);
    return (ret1 == 0 && ret2 == 0) ? 0 : -1;
}
