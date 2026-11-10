#include "text_utils.h"
#include <string.h>
void copy_text(char *destination, const char *source, size_t capacity) {
    if (!capacity) return;
    size_t length = strnlen(source, capacity - 1);
    memcpy(destination, source, length);
    memset(destination + length, 0, capacity - length);
}
