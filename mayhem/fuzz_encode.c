#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sixel.h>

/* Input layout: 8 header bytes, then raw pixel bytes (zero-padded when short).
     [0] pixel format   [1] width-1 (0..63)   [2] height-1 (0..63)
     [3] -p / -b / -e / -I color selection    [4] -d diffusion | -q quality
     [5] -w / -h / -c geometry                [6] -r -f -s -t -E -C tuning   [7] flag bits
   The remaining bytes are the image; palette formats get a full 2^bpp palette so every index is
   valid. Output goes to /dev/null. */

static const int formats[] = {
    SIXEL_PIXELFORMAT_RGB555,  SIXEL_PIXELFORMAT_RGB565,  SIXEL_PIXELFORMAT_RGB888,
    SIXEL_PIXELFORMAT_BGR555,  SIXEL_PIXELFORMAT_BGR565,  SIXEL_PIXELFORMAT_BGR888,
    SIXEL_PIXELFORMAT_ARGB8888, SIXEL_PIXELFORMAT_RGBA8888,
    SIXEL_PIXELFORMAT_G1, SIXEL_PIXELFORMAT_G2, SIXEL_PIXELFORMAT_G4, SIXEL_PIXELFORMAT_G8,
    SIXEL_PIXELFORMAT_AG88, SIXEL_PIXELFORMAT_GA88,
    SIXEL_PIXELFORMAT_PAL1, SIXEL_PIXELFORMAT_PAL2, SIXEL_PIXELFORMAT_PAL4, SIXEL_PIXELFORMAT_PAL8,
};
#define NFORMATS ((int)(sizeof(formats) / sizeof(formats[0])))

/* -b / -e / -I read out of bounds in upstream for every other pixel format (quant.c, tosixel.c),
   so those modes only get the formats where they are sound. */
static const int safe_formats[] = {
    SIXEL_PIXELFORMAT_RGB888, SIXEL_PIXELFORMAT_BGR888,
    SIXEL_PIXELFORMAT_ARGB8888, SIXEL_PIXELFORMAT_RGBA8888,
};

static const char *const colors[] = { "2", "3", "16", "64", "256" };
static const char *const builtins[] = {
    "xterm16", "xterm256", "vt340mono", "vt340color", "gray1", "gray2", "gray4", "gray8",
};
static const char *const diffusion[] = {
    "auto", "none", "fs", "atkinson", "jajuni", "stucki", "burkes", "a_dither", "x_dither",
};
static const char *const quality[] = { "auto", "low", "high", "full" };
static const char *const sizes[] = { "1", "7", "33", "100", "50%", "200%" };
static const char *const crops[] = { "1x1+0+0", "5x5+1+1", "20x10+3+2", "64x64+0+0" };
static const char *const resampling[] = {
    "nearest", "gaussian", "hanning", "hamming", "bilinear", "welsh",
    "bicubic", "lanczos2", "lanczos3", "lanczos4",
};
static const char *const largest[] = { "auto", "norm", "lum" };
static const char *const selects[] = { "auto", "center", "average", "histogram" };
static const char *const ptypes[] = { "auto", "hls", "rgb" };
static const char *const policies[] = { "auto", "fast", "size" };
static const char *const complexion[] = { "1", "2", "5", "10" };
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    sixel_encoder_t *encoder = NULL;
    SIXELSTATUS status;
    unsigned char *pixels;
    unsigned char *palette = NULL;
    int fmt, width, height, ncolors = 0;
    size_t cap, body;

    if (size < 9) {
        return -1;
    }
    fmt = formats[data[0] % NFORMATS];
    if (data[3] % 8 >= 1 && data[3] % 8 <= 3) {
        fmt = safe_formats[data[0] % 4];
    }
    width = 1 + (data[1] & 63);
    height = 1 + (data[2] & 63);

    cap = (size_t)width * (size_t)height * 4;
    pixels = (unsigned char *)calloc(cap + 1, 1);
    if (pixels == NULL) {
        return -1;
    }
    body = size - 8;
    if (body > cap) {
        body = cap;
    }
    memcpy(pixels, data + 8, body);

    if (fmt & SIXEL_FORMATTYPE_PALETTE) {
        int i;
        ncolors = 1 << (1 << (fmt & 3));
        if (ncolors > 256) {
            ncolors = 256;
        }
        palette = (unsigned char *)malloc((size_t)ncolors * 3);
        if (palette == NULL) {
            free(pixels);
            return -1;
        }
        for (i = 0; i < ncolors * 3; ++i) {
            palette[i] = (unsigned char)(data[8 + (size_t)i % (size - 8)] ^ (i * 37));
        }
    }

    status = sixel_encoder_new(&encoder, NULL);
    if (SIXEL_FAILED(status)) {
        goto end;
    }
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_OUTFILE, "/dev/null");

    switch (data[3] % 8) {
    case 0: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_COLORS, colors[(data[3] >> 3) % N(colors)]); break;
    case 1: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_BUILTIN_PALETTE, builtins[(data[3] >> 3) % N(builtins)]); break;
    case 2: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_MONOCHROME, NULL); break;
    case 3: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_HIGH_COLOR, NULL); break;
    default: break;
    }
    if (data[4] & 0x80) {
        sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_QUALITY, quality[data[4] % N(quality)]);
    } else {
        sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_DIFFUSION, diffusion[data[4] % N(diffusion)]);
    }
    switch (data[5] % 6) {
    case 0: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_WIDTH, sizes[(data[5] >> 3) % N(sizes)]); break;
    case 1: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_HEIGHT, sizes[(data[5] >> 3) % N(sizes)]); break;
    case 2: sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_CROP, crops[(data[5] >> 3) % N(crops)]); break;
    default: break;
    }
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_RESAMPLING, resampling[data[6] % N(resampling)]);
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_FIND_LARGEST, largest[(data[6] >> 4) % N(largest)]);
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_SELECT_COLOR, selects[(data[6] >> 2) % N(selects)]);
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_PALETTE_TYPE, ptypes[(data[6] >> 6) % N(ptypes)]);
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_ENCODE_POLICY, policies[(data[6] >> 1) % N(policies)]);
    sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_COMPLEXION_SCORE, complexion[(data[6] >> 5) % N(complexion)]);
    if (data[7] & 1) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_8BIT_MODE, NULL);
    if (data[7] & 2) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_USE_MACRO, NULL);
    if (data[7] & 4) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_HAS_GRI_ARG_LIMIT, NULL);
    if (data[7] & 8) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_INVERT, NULL);
    if (data[7] & 16) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_PENETRATE, NULL);
    if (data[7] & 32) sixel_encoder_setopt(encoder, SIXEL_OPTFLAG_BGCOLOR, "#80c0ff");

    status = sixel_encoder_encode_bytes(encoder, pixels, width, height, fmt,
                                        palette, ncolors);

end:
    if (encoder != NULL) {
        sixel_encoder_unref(encoder);
    }
    free(palette);
    free(pixels);
    return SIXEL_FAILED(status) ? -1 : 0;
}
