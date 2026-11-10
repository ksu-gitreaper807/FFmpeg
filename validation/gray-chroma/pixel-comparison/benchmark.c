/* Conversion-only benchmark. No internal headers or callback instrumentation. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "libswscale/swscale.h"

int main(int argc, char **argv)
{
    const int width = 512, height = 320, warmup = 500;
    uint8_t *src[4] = {0}, *dst[4] = {0};
    int src_stride[4], dst_stride[4];
    if (argc != 4) return 1;
    int iterations = atoi(argv[2]);
    if (iterations < 1 || iterations > 1000000) return 2;
    int src_size = av_image_alloc(src, src_stride, width, height,
                                  AV_PIX_FMT_RGB0, 64);
    int dst_size = av_image_alloc(dst, dst_stride, width, height,
                                  AV_PIX_FMT_GRAY8, 64);
    if (src_size < 0 || dst_size < 0) return 3;
    memset(src[0], 0, src_size);
    memset(dst[0], 0, dst_size);
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 4;
    for (int y = 0; y < height; ++y)
        if (fread(src[0] + y * src_stride[0], 4, width, input) != width)
            return 5;
    if (fgetc(input) != EOF || ferror(input)) return 6;
    fclose(input);

    SwsContext *ctx = sws_getContext(width, height, AV_PIX_FMT_RGB0,
                                    width, height, AV_PIX_FMT_GRAY8,
                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (!ctx) return 7;
    const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
    if (sws_setColorspaceDetails(ctx, coeff, 1, coeff, 1,
                                0, 1 << 16, 1 << 16) < 0) return 8;
    for (int i = 0; i < warmup; ++i)
        if (sws_scale(ctx, (const uint8_t *const *)src, src_stride,
                      0, height, dst, dst_stride) != height) return 9;
    int64_t start = av_gettime_relative();
    for (int i = 0; i < iterations; ++i)
        if (sws_scale(ctx, (const uint8_t *const *)src, src_stride,
                      0, height, dst, dst_stride) != height) return 10;
    int64_t elapsed_us = av_gettime_relative() - start;

    /* Check/write final output after timing. */
    FILE *output = fopen(argv[3], "wb");
    if (!output) return 11;
    if (fprintf(output, "P5\n%d %d\n255\n", width, height) < 0) return 12;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            hash ^= dst[0][y * dst_stride[0] + x];
            hash *= UINT64_C(1099511628211);
        }
        if (fwrite(dst[0] + y * dst_stride[0], 1, width, output) != width)
            return 13;
    }
    if (fclose(output)) return 14;
    printf("{\"iterations\":%d,\"warmup\":%d,\"elapsed_us\":%" PRId64
           ",\"us_per_frame\":%.9f,\"cpu_flags\":%d,\"output_fnv1a64\":\"%016"
           PRIx64 "\"}\n", iterations, warmup, elapsed_us,
           (double)elapsed_us / iterations, av_get_cpu_flags(), hash);
    sws_freeContext(ctx);
    av_freep(&src[0]);
    av_freep(&dst[0]);
    return 0;
}
