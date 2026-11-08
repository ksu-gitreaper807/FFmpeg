/* Diagnostic harness for FFmpeg commit 7afbfcbd92d6.
 * Links against baseline/patched static builds; writes active output bytes to
 * stdout for exact comparison. Progress and benchmark results go to stderr.
 * Uses internal headers only to count conversion calls; no FFmpeg edits needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "libavutil/cpu.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libswscale/swscale.h"
#include "libswscale/swscale_internal.h"
static planar2_YV12_fn saved;
static int uv_calls;
static void count_uv(uint8_t *u,uint8_t *v,const uint8_t *s,const uint8_t *s2,
                     const uint8_t *s3,int w,uint32_t *p,void *o) {
    uv_calls++; saved(u,v,s,s2,s3,w,p,o);
}
static uint32_t rng=1;
static unsigned random_byte(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng & 255;
}
static int run(enum AVPixelFormat sf, enum AVPixelFormat df, int w,int h,
               int dw,int dh,int flags,int range,int matrix,int variant,int bench) {
    uint8_t *s[4]={0},*d[4]={0}; int ss[4],ds[4];
    int sn=av_image_alloc(s,ss,w,h,sf,64);
    int dn=av_image_alloc(d,ds,dw,dh,df,64);
    if(sn<0 || dn<0) exit(2);
    uint8_t *srcbuf=s[0],*dstbuf=d[0];
    for(int i=0;i<sn;i++) srcbuf[i]=random_byte();
    memset(dstbuf,0xa5,dn);
    if(variant&2) {
        const AVPixFmtDescriptor *pd=av_pix_fmt_desc_get(sf);
        for(int p=0;p<4;p++) if(s[p]) {
            int ph=(p==1||p==2)?AV_CEIL_RSHIFT(h,pd->log2_chroma_h):h;
            s[p]+=(ph-1)*ss[p]; ss[p]=-ss[p];
        }
    }
    SwsContext *ctx=sws_getContext(w,h,sf,dw,dh,df,flags,NULL,NULL,NULL);
    if(!ctx) exit(3);
    if(sws_setColorspaceDetails(ctx,sws_getCoefficients(matrix),1,
                  sws_getCoefficients(matrix),range,0,1<<16,1<<16)<0) exit(4);
    SwsInternal *c=sws_internal(ctx);
    uv_calls=0;
    if(!bench && c->chrToYV12) { saved=c->chrToYV12; c->chrToYV12=count_uv; }
    int loops=bench?60:1;
    for(int i=0;i<(bench?5:0);i++)
        if(sws_scale(ctx,(const uint8_t *const*)s,ss,0,h,d,ds)!=dh) exit(5);
    int64_t start=av_gettime_relative();
    for(int i=0;i<loops;i++) {
        if(variant&4) {
            const AVPixFmtDescriptor *pd=av_pix_fmt_desc_get(sf);
            int total=0;
            for(int y=0;y<h;y+=2) {
                const uint8_t *part[4]={0};
                for(int p=0;p<4;p++) if(s[p])
                    part[p]=s[p]+(y >> ((p==1||p==2)?pd->log2_chroma_h:0))*ss[p];
                int n=sws_scale(ctx,part,ss,y,2,d,ds);
                if(n<0) exit(5);
                total+=n;
            }
            if(total!=dh) exit(5);
        } else if(sws_scale(ctx,(const uint8_t *const*)s,ss,0,h,d,ds)!=dh) exit(5);
    }
    int64_t elapsed=av_gettime_relative()-start;
    if(bench) {
        fprintf(stderr,"%dx%d %s -> %s %.3f ms/frame\n",w,h,
          av_get_pix_fmt_name(sf),av_get_pix_fmt_name(df),elapsed/(1000.0*loops));
    } else {
        int n=av_image_get_buffer_size(df,dw,dh,1);
        uint8_t *packed=av_malloc(n);
        if(!packed || av_image_copy_to_buffer(packed,n,(const uint8_t *const*)d,ds,df,dw,dh,1)!=n) exit(6);
        if(fwrite(packed,1,n,stdout)!=(size_t)n) exit(7);
        av_free(packed);
    }
    int count=uv_calls;
    sws_freeContext(ctx); av_free(srcbuf); av_free(dstbuf);
    return count;
}
int main(int argc,char **argv) {
    int bench=argc>1 && !strcmp(argv[1],"bench");
    int cpu0=argc>2 && !strcmp(argv[2],"c");
    if(cpu0) av_force_cpu_flags(0);
    if(argc>1 && !strcmp(argv[1],"ycheck")) {
        for(int fmt=0;fmt<2;fmt++) {
            rng=1;
            run(AV_PIX_FMT_RGB0,fmt?AV_PIX_FMT_YUV420P:AV_PIX_FMT_GRAY8,
                32,18,32,18,SWS_BILINEAR,0,SWS_CS_ITU709,0,0);
        }
        return 0;
    }
    if(bench) {
        for(int sz=0;sz<2;sz++) for(int fmt=0;fmt<2;fmt++)
            run(AV_PIX_FMT_RGB0,fmt?AV_PIX_FMT_YUV420P:AV_PIX_FMT_GRAY8,
                sz?3840:1920,sz?2160:1080,sz?3840:1920,sz?2160:1080,
                SWS_BILINEAR,0,SWS_CS_ITU709,0,1);
        return 0;
    }
    enum AVPixelFormat srcs[]={AV_PIX_FMT_RGB0,AV_PIX_FMT_BGR0,AV_PIX_FMT_0RGB,
        AV_PIX_FMT_0BGR,AV_PIX_FMT_RGB24,AV_PIX_FMT_RGBA,AV_PIX_FMT_GBRP,
        AV_PIX_FMT_RGB48LE,AV_PIX_FMT_RGB48BE,AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NV12,AV_PIX_FMT_YUYV422,AV_PIX_FMT_GRAY8};
    enum AVPixelFormat dsts[]={AV_PIX_FMT_GRAY8,AV_PIX_FMT_GRAY16LE,
        AV_PIX_FMT_GRAY16BE,AV_PIX_FMT_GRAY10LE,AV_PIX_FMT_YA8,
        AV_PIX_FMT_YUV420P,AV_PIX_FMT_RGB24};
    int widths[]={7,16,31,65}; int tests=0;
    for(unsigned s=0;s<sizeof(srcs)/sizeof(*srcs);s++)
    for(unsigned d=0;d<sizeof(dsts)/sizeof(*dsts);d++)
    for(int w=0;w<4;w++) for(int variant=0;variant<8;variant++) {
        int iw=widths[w], ih=18;
        int flags=variant&1?SWS_BICUBIC|SWS_ACCURATE_RND|SWS_BITEXACT:SWS_BILINEAR;
        int calls=run(srcs[s],dsts[d],iw,ih,variant&1?iw+6:iw,
            variant&1?26:ih,flags,variant&1,
            variant&2?SWS_CS_ITU709:SWS_CS_ITU601,variant,0);
        if(s==0 && d==0 && w==3 && variant==0)
            fprintf(stderr,"RGB0->GRAY8 U/V converter calls: %d\n",calls);
        tests++;
    }
    fprintf(stderr,"Completed %d cases; CPU flags %s\n",tests,cpu0?"forced C":"native");
    return ferror(stdout)?8:0;
}
