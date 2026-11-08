/* Compare stdout byte-for-byte between baseline and patched FFmpeg builds. */
#include <stdio.h>
#include <stdlib.h>
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
static unsigned rng=1;
static unsigned byte(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng&255; }
int main(void) {
    enum AVPixelFormat srcs[]={AV_PIX_FMT_RGB0,AV_PIX_FMT_BGR0,AV_PIX_FMT_GBRP,
        AV_PIX_FMT_RGB48LE,AV_PIX_FMT_RGB48BE,AV_PIX_FMT_NV12,AV_PIX_FMT_GRAY8};
    enum AVPixelFormat dsts[]={AV_PIX_FMT_GRAY8,AV_PIX_FMT_GRAY16BE,
        AV_PIX_FMT_YA8,AV_PIX_FMT_YUV420P};
    int cases=0;
    for(unsigned s=0;s<sizeof(srcs)/sizeof(*srcs);s++)
    for(unsigned d=0;d<sizeof(dsts)/sizeof(*dsts);d++)
    for(int scale=0;scale<2;scale++) for(int th=1;th<=3;th+=2) {
        AVFrame *src=av_frame_alloc(),*dst=av_frame_alloc();
        if(!src||!dst) return 1;
        src->format=srcs[s]; src->width=64; src->height=18;
        dst->format=dsts[d]; dst->width=scale?70:64; dst->height=scale?26:18;
        src->color_range=AVCOL_RANGE_JPEG; dst->color_range=AVCOL_RANGE_MPEG;
        src->colorspace=(av_pix_fmt_desc_get(srcs[s])->flags&AV_PIX_FMT_FLAG_RGB)?AVCOL_SPC_RGB:AVCOL_SPC_BT709;
        dst->colorspace=AVCOL_SPC_BT709;
        src->color_primaries=dst->color_primaries=AVCOL_PRI_BT709;
        src->color_trc=dst->color_trc=AVCOL_TRC_BT709;
        if(av_frame_get_buffer(src,64)<0 || av_frame_get_buffer(dst,64)<0) return 2;
        const AVPixFmtDescriptor *pd=av_pix_fmt_desc_get(srcs[s]);
        for(int p=0;p<4;p++) if(src->data[p]) {
            int h=(p==1||p==2)?AV_CEIL_RSHIFT(src->height,pd->log2_chroma_h):src->height;
            int w=av_image_get_linesize(srcs[s],src->width,p);
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) src->data[p][y*src->linesize[p]+x]=byte();
        }
        SwsContext *ctx=sws_alloc_context(); if(!ctx) return 3;
        ctx->flags=SWS_BILINEAR; ctx->threads=th; ctx->backends=SWS_BACKEND_LEGACY;
        if(sws_scale_frame(ctx,dst,src)<0) return 4;
        int n=av_image_get_buffer_size(dsts[d],dst->width,dst->height,1);
        uint8_t *b=av_malloc(n); if(!b) return 5;
        if(av_image_copy_to_buffer(b,n,(const uint8_t *const*)dst->data,dst->linesize,dsts[d],dst->width,dst->height,1)!=n) return 6;
        if(fwrite(b,1,n,stdout)!=(size_t)n) return 7;
        av_free(b); sws_freeContext(ctx); av_frame_free(&src); av_frame_free(&dst); cases++;
    }
    fprintf(stderr,"Completed %d modern frame API cases, legacy backend, 1/3 threads\n",cases);
    return 0;
}
