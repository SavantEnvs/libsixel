#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sixel.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char *copy;
    unsigned char *pixels = NULL;
    unsigned char *palette = NULL;
    int width = 0;
    int height = 0;
    int ncolors = 0;
    SIXELSTATUS status;

    if (size == 0 || size > INT_MAX) {
        return -1;
    }
    copy = (unsigned char *)malloc(size);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, data, size);

    status = sixel_decode_raw(copy, (int)size, &pixels, &width, &height,
                              &palette, &ncolors, NULL);
    free(copy);
    if (SIXEL_FAILED(status)) {
        return -1;
    }

    /* touch every decoded index and palette entry the library reports */
    {
        volatile unsigned int sum = 0;
        size_t i;
        size_t n = (size_t)width * (size_t)height;
        for (i = 0; i < n; ++i) {
            sum += pixels[i];
        }
        for (i = 0; i < (size_t)ncolors * 3; ++i) {
            sum += palette[i];
        }
    }
    free(pixels);
    free(palette);
    return 0;
}
