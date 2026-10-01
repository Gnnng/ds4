#include "ds4_image.h"

#include <stdio.h>
#include <string.h>
#include <webp/encode.h>
#include "vision-fixtures/webp.h"

static int hex_fingerprint(const uint8_t *fp, char *out, size_t cap) {
    if (cap < 65) return 0;
    for (int i = 0; i < 32; i++)
        snprintf(out + i * 2, cap - (size_t)i * 2, "%02x", fp[i]);
    return 1;
}

static int check_jpeg(const char *path, uint32_t width, uint32_t height,
                      const char *expected_fp) {
    ds4_image image = {0};
    char error[160] = {0};
    if (!ds4_image_decode_file(&image, path, error, sizeof(error))) {
        fprintf(stderr, "decode failed for %s: %s\n", path, error);
        return 0;
    }
    char got[65] = {0};
    int ok = hex_fingerprint(image.fingerprint, got, sizeof(got)) &&
             image.width == width && image.height == height &&
             strcmp(got, expected_fp) == 0;
    if (!ok) {
        fprintf(stderr, "%s: got %ux%u fp=%s, expected %ux%u fp=%s\n",
                path, image.width, image.height, got,
                width, height, expected_fp);
    }
    ds4_image_free(&image);
    return ok;
}

static int check_webp(void) {
    ds4_image image = {0};
    char error[160] = {0};
    if (!ds4_image_decode_memory(&image, ds4_test_webp,
                                 sizeof(ds4_test_webp), error, sizeof(error))) {
        fprintf(stderr, "lossy WebP decode failed: %s\n", error);
        return 0;
    }
    int ok = image.width == 8 && image.height == 8 && image.rgb != NULL;
    ds4_image_free(&image);
    if (!ok) {
        fprintf(stderr, "lossy WebP dimensions mismatch\n");
        return 0;
    }

    const uint8_t rgba[] = {255, 0, 0, 255, 0, 255, 0, 128,
                            0, 0, 255, 64, 255, 255, 255, 255};
    const uint8_t rgb[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    uint8_t *encoded = NULL;
    size_t len = WebPEncodeLosslessRGBA(rgba, 2, 2, 8, &encoded);
    if (!len) return 0;
    ok = ds4_image_decode_memory(&image, encoded, len, error, sizeof(error));
    WebPFree(encoded);
    ok = ok && image.width == 2 && image.height == 2 &&
         !memcmp(image.rgb, rgb, sizeof(rgb));
    ds4_image_free(&image);
    if (!ok) {
        fprintf(stderr, "lossless WebP pixels mismatch: %s\n", error);
        return 0;
    }

    /* Both header truncation and a complete header with truncated pixels fail. */
    for (size_t n = 12; n < sizeof(ds4_test_webp); n++) {
        if (ds4_image_decode_memory(&image, ds4_test_webp, n, error, sizeof(error))) {
            fprintf(stderr, "accepted truncated WebP (%zu bytes)\n", n);
            ds4_image_free(&image);
            return 0;
        }
        if (image.rgb || !strstr(error, "WebP")) {
            fprintf(stderr, "truncated WebP diagnostic: %s\n", error);
            return 0;
        }
    }
    uint8_t changed[sizeof(ds4_test_webp)];
    memcpy(changed, ds4_test_webp, sizeof(changed));
    changed[20] |= 2; /* VP8X animation flag. */
    if (ds4_image_decode_memory(&image, changed, sizeof(changed), error, sizeof(error)) ||
        !strstr(error, "animated WebP")) {
        fprintf(stderr, "animated WebP diagnostic: %s\n", error);
        return 0;
    }
    memcpy(changed, ds4_test_webp, sizeof(changed));
    changed[24] = changed[27] = 0xfe; /* 16383 x 16383, above the pixel budget. */
    changed[25] = changed[28] = 0x3f;
    for (size_t i = 0; i + 7 <= sizeof(changed); i++) {
        if (!memcmp(changed + i, "\x9d\x01\x2a", 3)) {
            changed[i + 3] = changed[i + 5] = 0xff; /* Matching VP8 frame dimensions. */
            changed[i + 4] = changed[i + 6] = 0x3f;
            break;
        }
    }
    if (ds4_image_decode_memory(&image, changed, sizeof(changed), error, sizeof(error)) ||
        !strstr(error, "pixel limit")) {
        fprintf(stderr, "oversized WebP diagnostic: %s\n", error);
        return 0;
    }
    return 1;
}

int main(void) {
    if (!check_webp()) {
        fprintf(stderr, "WebP decoder regression failed\n");
        return 1;
    }
    /* 24x16 grayscale progressive JPEG. Unpatched Iris yields different
     * pixels; patched decode matches libjpeg-turbo djpeg bit-exactly. */
    if (!check_jpeg("tests/vision-fixtures/jpeg/prog_ac_refine_zrl_gray.jpg",
                    24u, 16u,
                    "63aae8863e829170c5abc746c90a5ff3ad60e9c4312e2ffa5eefbfeaacd26bfc"))
        return 1;
    /* 64x48 4:2:0 progressive JPEG. Unpatched jpeg_load returns NULL.
     * After the ZRL fix plus main's chroma interpolation, decode matches
     * libjpeg-turbo djpeg bit-exactly. */
    if (!check_jpeg("tests/vision-fixtures/jpeg/prog_ac_refine_zrl_420.jpg",
                    64u, 48u,
                    "d3be4d7078c41b6589942c82bd622ca8a3ed40adddee11cccf1de9ca1a096ba4"))
        return 1;
    return 0;
}
