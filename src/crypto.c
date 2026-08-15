#include "crypto.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#if AMIGMAIL_AMIGA
#include <exec/libraries.h>
#include <proto/amissl.h>
#include <openssl/rand.h>

extern struct Library *AmiSSLBase;
#endif

typedef struct Sha256Context {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} Sha256Context;

static uint32_t rotate_right(uint32_t x, unsigned n) { return (x >> n) | (x << (32U - n)); }

static void sha_transform(Sha256Context *ctx, const unsigned char block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    uint32_t w[64], a,b,c,d,e,f,g,h;
    unsigned i;
    for (i = 0; i < 16U; ++i) w[i] = ((uint32_t)block[i*4U] << 24) | ((uint32_t)block[i*4U+1U] << 16) |
                                             ((uint32_t)block[i*4U+2U] << 8) | block[i*4U+3U];
    for (; i < 64U; ++i) {
        uint32_t s0 = rotate_right(w[i-15U],7)^rotate_right(w[i-15U],18)^(w[i-15U]>>3);
        uint32_t s1 = rotate_right(w[i-2U],17)^rotate_right(w[i-2U],19)^(w[i-2U]>>10);
        w[i] = w[i-16U] + s0 + w[i-7U] + s1;
    }
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i=0;i<64U;++i) {
        uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
        uint32_t ch=(e&f)^((~e)&g), t1=h+s1+ch+k[i]+w[i];
        uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
        uint32_t maj=(a&b)^(a&c)^(b&c), t2=s0+maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

static void sha_init(Sha256Context *ctx)
{
    static const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                      0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(ctx->state,initial,sizeof(initial));ctx->bits=0;ctx->used=0;
}

static void sha_update(Sha256Context *ctx,const unsigned char *data,size_t length)
{
    while(length){size_t space=64U-ctx->used,take=length<space?length:space;
        memcpy(ctx->block+ctx->used,data,take);ctx->used+=take;data+=take;length-=take;ctx->bits+=(uint64_t)take*8U;
        if(ctx->used==64U){sha_transform(ctx,ctx->block);ctx->used=0;}}
}

static void sha_final(Sha256Context *ctx,unsigned char digest[32])
{
    unsigned i;ctx->block[ctx->used++]=0x80U;
    if(ctx->used>56U){while(ctx->used<64U)ctx->block[ctx->used++]=0;sha_transform(ctx,ctx->block);ctx->used=0;}
    while(ctx->used<56U)ctx->block[ctx->used++]=0;
    for(i=0;i<8U;++i)ctx->block[63U-i]=(unsigned char)(ctx->bits>>(i*8U));
    sha_transform(ctx,ctx->block);
    for(i=0;i<8U;++i){digest[i*4U]=(unsigned char)(ctx->state[i]>>24);digest[i*4U+1U]=(unsigned char)(ctx->state[i]>>16);
        digest[i*4U+2U]=(unsigned char)(ctx->state[i]>>8);digest[i*4U+3U]=(unsigned char)ctx->state[i];}
    amg_secure_clear(ctx,sizeof(*ctx));
}

void amg_sha256(const unsigned char *data, size_t length, unsigned char digest[32])
{
    Sha256Context ctx;sha_init(&ctx);sha_update(&ctx,data,length);sha_final(&ctx,digest);
}

int amg_random_bytes(unsigned char *output, size_t length)
{
    if (!output && length) return AMG_ERR_ARGUMENT;
#if AMIGMAIL_AMIGA
    if (!AmiSSLBase) return AMG_ERR_TLS;
    return RAND_bytes(output, (int)length) == 1 ? AMG_OK : AMG_ERR_TLS;
#else
    FILE *file = fopen("/dev/urandom", "rb");
    if (file) {
        size_t read_count = fread(output, 1U, length, file);
        fclose(file);
        if (read_count == length) return AMG_OK;
    }
    return AMG_ERR_IO;
#endif
}
