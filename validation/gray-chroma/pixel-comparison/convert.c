/* Same RGB0 input, separately linked baseline/patched libswscale builds. */
#include <stdio.h>
#include <stdlib.h>
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"

static planar2_YV12_fn original_chroma;
static unsigned chroma_calls;
static void count_chroma(uint8_t *u, uint8_t *v, const uint8_t *s,
                         const uint8_t *s2, const uint8_t *s3, int width,
                         uint32_t *pal, void *opaque)
{
    ++chroma_calls;
    original_chroma(u, v, s, s2, s3, width, pal, opaque);
}

int main(int argc, char **argv)
{
    const int width = 512, height = 320;
    uint8_t *src[4] = {0}, *dst[4] = {0};
    int src_stride[4], dst_stride[4];
    if (argc != 3) return 1;
    int src_size = av_image_alloc(src, src_stride, width, height,
                                  AV_PIX_FMT_RGB0, 64);
    int dst_size = av_image_alloc(dst, dst_stride, width, height,
                                  AV_PIX_FMT_GRAY8, 64);
    if (src_size < 0 || dst_size < 0) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 3;
    for (int y = 0; y < height; ++y)
        if (fread(src[0] + y * src_stride[0], 4, width, input) != width)
            return 4;
    if (fgetc(input) != EOF || ferror(input)) return 5;
    fclose(input);

    SwsContext *ctx = sws_getContext(width, height, AV_PIX_FMT_RGB0,
                                    width, height, AV_PIX_FMT_GRAY8,
                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (!ctx) return 6;
    const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
    if (sws_setColorspaceDetails(ctx, coeff, 1, coeff, 1,
                                0, 1 << 16, 1 << 16) < 0) return 7;
    SwsInternal *internal = sws_internal(ctx);
    original_chroma = internal->chrToYV12;
    if (!original_chroma) return 8;
    internal->chrToYV12 = count_chroma;
    int rows = sws_scale(ctx, (const uint8_t *const *)src, src_stride,
                         0, height, dst, dst_stride);
    if (rows != height) return 9;

    FILE *output = fopen(argv[2], "wb");
    if (!output) return 10;
    if (fprintf(output, "P5\n%d %d\n255\n", width, height) < 0) return 11;
    for (int y = 0; y < height; ++y)
        if (fwrite(dst[0] + y * dst_stride[0], 1, width, output) != width)
            return 12;
    if (fclose(output)) return 13;
    printf("{\"width\":%d,\"height\":%d,\"output_rows\":%d,"
           "\"uv_converter_calls\":%u}\n", width, height, rows, chroma_calls);
    sws_freeContext(ctx);
    av_freep(&src[0]);
    av_freep(&dst[0]);
    return 0;
}
