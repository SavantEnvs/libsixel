/* Known-answer probe for libsixel, built against the normal-flags library by mayhem/build.sh and
   asserted by mayhem/test.sh. Prints one line per probe; test.sh compares each against golden text. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sixel.h>

struct sink { char buf[8192]; size_t len; };

static int
sink_write(char *data, int size, void *priv)
{
    struct sink *s = (struct sink *)priv;
    if (size < 0 || s->len + (size_t)size > sizeof(s->buf)) {
        return -1;
    }
    memcpy(s->buf + s->len, data, (size_t)size);
    s->len += (size_t)size;
    return size;
}

static void
print_escaped(const char *tag, const char *p, size_t n)
{
    size_t i;
    printf("%s ", tag);
    for (i = 0; i < n; ++i) {
        if ((unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7e) {
            printf("\\x%02x", (unsigned char)p[i]);
        } else {
            putchar(p[i]);
        }
    }
    putchar('\n');
}

/* two 2-wide stripes (palette index 0 then 1), 6 rows: one sixel band */
static int
probe_decode(void)
{
    static const char six[] =
        "\x1bPq\"1;1;4;6#0;2;100;0;0#1;2;0;100;0#0!2~#1!2~\x1b\\";
    unsigned char *pixels = NULL, *palette = NULL;
    int w = 0, h = 0, nc = 0, x;
    SIXELSTATUS st = sixel_decode_raw((unsigned char *)six, (int)sizeof(six) - 1,
                                      &pixels, &w, &h, &palette, &nc, NULL);
    if (SIXEL_FAILED(st)) {
        printf("DECODE failed 0x%x\n", (unsigned)st);
        return 1;
    }
    printf("DECODE w=%d h=%d ncolors=%d row0=", w, h, nc);
    for (x = 0; x < w; ++x) {
        printf("%d%s", pixels[x], x + 1 < w ? "," : "");
    }
    printf(" row5=");
    for (x = 0; x < w; ++x) {
        printf("%d%s", pixels[5 * w + x], x + 1 < w ? "," : "");
    }
    printf(" pal0=%02x%02x%02x pal1=%02x%02x%02x\n",
           palette[0], palette[1], palette[2], palette[3], palette[4], palette[5]);
    free(pixels);
    free(palette);
    return 0;
}

/* 4x6 RGB image (left half red, right half green) through a fixed 2-colour palette */
static int
probe_encode(void)
{
    unsigned char pixels[4 * 6 * 3];
    unsigned char pal[6] = { 255, 0, 0, 0, 255, 0 };
    sixel_dither_t *dither = NULL;
    sixel_output_t *out = NULL;
    struct sink s;
    int x, y;
    SIXELSTATUS st;

    s.len = 0;
    for (y = 0; y < 6; ++y) {
        for (x = 0; x < 4; ++x) {
            unsigned char *p = pixels + (y * 4 + x) * 3;
            p[0] = x < 2 ? 255 : 0;
            p[1] = x < 2 ? 0 : 255;
            p[2] = 0;
        }
    }
    st = sixel_dither_new(&dither, 2, NULL);
    if (SIXEL_SUCCEEDED(st)) {
        sixel_dither_set_palette(dither, pal);
        st = sixel_output_new(&out, sink_write, &s, NULL);
    }
    if (SIXEL_SUCCEEDED(st)) {
        st = sixel_encode(pixels, 4, 6, 3, dither, out);
    }
    if (SIXEL_FAILED(st)) {
        printf("ENCODE failed 0x%x\n", (unsigned)st);
        return 1;
    }
    print_escaped("ENCODE", s.buf, s.len);
    sixel_output_unref(out);
    sixel_dither_unref(dither);
    return 0;
}

/* encode 8x12 gradient -> decode -> report geometry and a checksum of the decoded indices */
static int
probe_roundtrip(void)
{
    unsigned char pixels[8 * 12 * 3];
    unsigned char *dec = NULL, *dpal = NULL;
    sixel_dither_t *dither = NULL;
    sixel_output_t *out = NULL;
    struct sink s;
    int x, y, w = 0, h = 0, nc = 0;
    unsigned sum = 0;
    SIXELSTATUS st;

    s.len = 0;
    for (y = 0; y < 12; ++y) {
        for (x = 0; x < 8; ++x) {
            unsigned char *p = pixels + (y * 8 + x) * 3;
            p[0] = (unsigned char)(x < 4 ? 255 : 0);
            p[1] = (unsigned char)(y < 6 ? 255 : 0);
            p[2] = (unsigned char)((x < 4) == (y < 6) ? 255 : 0);
        }
    }
    st = sixel_dither_new(&dither, 8, NULL);
    if (SIXEL_SUCCEEDED(st)) {
        st = sixel_dither_initialize(dither, pixels, 8, 12, SIXEL_PIXELFORMAT_RGB888,
                                     SIXEL_LARGE_NORM, SIXEL_REP_CENTER_BOX,
                                     SIXEL_QUALITY_HIGH);
    }
    if (SIXEL_SUCCEEDED(st)) {
        sixel_dither_set_diffusion_type(dither, SIXEL_DIFFUSE_NONE);
        st = sixel_output_new(&out, sink_write, &s, NULL);
    }
    if (SIXEL_SUCCEEDED(st)) {
        st = sixel_encode(pixels, 8, 12, 3, dither, out);
    }
    if (SIXEL_SUCCEEDED(st)) {
        st = sixel_decode_raw((unsigned char *)s.buf, (int)s.len, &dec, &w, &h, &dpal, &nc, NULL);
    }
    if (SIXEL_FAILED(st)) {
        printf("ROUNDTRIP failed 0x%x\n", (unsigned)st);
        return 1;
    }
    /* each of the four quadrants must decode to one flat colour, distinct per quadrant */
    {
        int q[4] = { dec[0], dec[7], dec[11 * 8], dec[11 * 8 + 7] };
        int flat = 1;
        for (y = 0; y < 12; ++y) {
            for (x = 0; x < 8; ++x) {
                int expect = q[(y >= 6) * 2 + (x >= 4)];
                if (dec[y * 8 + x] != expect) {
                    flat = 0;
                }
                sum += dec[y * 8 + x];
            }
        }
        printf("ROUNDTRIP w=%d h=%d quadrants_flat=%d distinct=%d\n", w, h, flat,
               (q[0] != q[1]) + (q[0] != q[2]) + (q[1] != q[3]) + (q[2] != q[3]));
    }
    free(dec);
    free(dpal);
    sixel_output_unref(out);
    sixel_dither_unref(dither);
    return 0;
}

int
main(void)
{
    int rc = 0;
    rc |= probe_decode();
    rc |= probe_encode();
    rc |= probe_roundtrip();
    return rc;
}
