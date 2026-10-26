#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sixel.h>

/* The fuzz input is the image file itself (format is sniffed from content). It is staged in a
   scratch file under $TMPDIR and pushed through the whole img2sixel pipeline: loader -> frame
   conversion -> palette -> sixel encode, with the sixel output discarded. */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char path[4096];
    const char *tmpdir = getenv("TMPDIR");
    sixel_encoder_t *encoder = NULL;
    SIXELSTATUS status;
    int fd;
    size_t off = 0;

    if (size == 0) {
        return -1;
    }
    if (tmpdir == NULL || *tmpdir == '\0') {
        tmpdir = "/tmp";
    }
    if (snprintf(path, sizeof(path), "%s/sixel-load-XXXXXX", tmpdir)
            >= (int)sizeof(path)) {
        return -1;
    }
    fd = mkstemp(path);
    if (fd < 0) {
        return -1;
    }
    while (off < size) {
        ssize_t n = write(fd, data + off, size - off);
        if (n <= 0) {
            close(fd);
            unlink(path);
            return -1;
        }
        off += (size_t)n;
    }
    close(fd);

    status = sixel_encoder_new(&encoder, NULL);
    if (SIXEL_SUCCEEDED(status)) {
        status = sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_OUTFILE, "/dev/null");
    }
    if (SIXEL_SUCCEEDED(status)) {
        status = sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_IGNORE_DELAY, NULL);
    }
    if (SIXEL_SUCCEEDED(status)) {
        status = sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_LOOPMODE, "disable");
    }
    if (SIXEL_SUCCEEDED(status)) {
        status = sixel_encoder_encode(encoder, path);
    }
    if (encoder != NULL) {
        sixel_encoder_unref(encoder);
    }
    unlink(path);
    return SIXEL_FAILED(status) ? -1 : 0;
}
