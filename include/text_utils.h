#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H
#include <stddef.h>
/* Copy at most capacity-1 bytes and always terminate a nonempty destination.
 * Source must be readable through its NUL or capacity-1 bytes. No overlap.
 */
void copy_text(char *destination, const char *source, size_t capacity);
#endif
