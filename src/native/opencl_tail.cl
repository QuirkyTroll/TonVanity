/* TonVanity OpenCL TON pipeline.
   Mirrors kernels.cu: BIP-39 12-word, TON 24-word phase A (seed-version
   filter) and phase B (PBKDF2-100000 + Ed25519 + address). */

#define MAX_VALID 131072

struct HmacCtx { u64 inner[8]; u64 outer[8]; };
struct BW { u8 d[48]; int bits; };

inline u64 rotr64d(u64 x, int n) { return (x >> n) | (x << (64 - n)); }
inline u32 rotr32d(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
inline u64 be64d(const u8* p) {
    return ((u64)p[0] << 56) | ((u64)p[1] << 48) | ((u64)p[2] << 40) | ((u64)p[3] << 32) |
           ((u64)p[4] << 24) | ((u64)p[5] << 16) | ((u64)p[6] << 8) | (u64)p[7];
}
inline u32 be32d(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
inline void put64d(u8* p, u64 v) { for (int i = 7; i >= 0; --i) { p[i] = (u8)v; v >>= 8; } }
inline void put32d(u8* p, u32 v) { p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
inline void cpy(u8* d, const u8* s, int n) { for (int i = 0; i < n; ++i) d[i] = s[i]; }
inline void zero(u8* d, int n) { for (int i = 0; i < n; ++i) d[i] = 0; }

__constant u64 K512[80] = {
    0x428a2f98d728ae22UL,0x7137449123ef65cdUL,0xb5c0fbcfec4d3b2fUL,0xe9b5dba58189dbbcUL,
    0x3956c25bf348b538UL,0x59f111f1b605d019UL,0x923f82a4af194f9bUL,0xab1c5ed5da6d8118UL,
    0xd807aa98a3030242UL,0x12835b0145706fbeUL,0x243185be4ee4b28cUL,0x550c7dc3d5ffb4e2UL,
    0x72be5d74f27b896fUL,0x80deb1fe3b1696b1UL,0x9bdc06a725c71235UL,0xc19bf174cf692694UL,
    0xe49b69c19ef14ad2UL,0xefbe4786384f25e3UL,0x0fc19dc68b8cd5b5UL,0x240ca1cc77ac9c65UL,
    0x2de92c6f592b0275UL,0x4a7484aa6ea6e483UL,0x5cb0a9dcbd41fbd4UL,0x76f988da831153b5UL,
    0x983e5152ee66dfabUL,0xa831c66d2db43210UL,0xb00327c898fb213fUL,0xbf597fc7beef0ee4UL,
    0xc6e00bf33da88fc2UL,0xd5a79147930aa725UL,0x06ca6351e003826fUL,0x142929670a0e6e70UL,
    0x27b70a8546d22ffcUL,0x2e1b21385c26c926UL,0x4d2c6dfc5ac42aedUL,0x53380d139d95b3dfUL,
    0x650a73548baf63deUL,0x766a0abb3c77b2a8UL,0x81c2c92e47edaee6UL,0x92722c851482353bUL,
    0xa2bfe8a14cf10364UL,0xa81a664bbc423001UL,0xc24b8b70d0f89791UL,0xc76c51a30654be30UL,
    0xd192e819d6ef5218UL,0xd69906245565a910UL,0xf40e35855771202aUL,0x106aa07032bbd1b8UL,
    0x19a4c116b8d2d0c8UL,0x1e376c085141ab53UL,0x2748774cdf8eeb99UL,0x34b0bcb5e19b48a8UL,
    0x391c0cb3c5c95a63UL,0x4ed8aa4ae3418acbUL,0x5b9cca4f7763e373UL,0x682e6ff3d6b2b8a3UL,
    0x748f82ee5defb2fcUL,0x78a5636f43172f60UL,0x84c87814a1f0ab72UL,0x8cc702081a6439ecUL,
    0x90befffa23631e28UL,0xa4506cebde82bde9UL,0xbef9a3f7b2c67915UL,0xc67178f2e372532bUL,
    0xca273eceea26619cUL,0xd186b8c721c0c207UL,0xeada7dd6cde0eb1eUL,0xf57d4f7fee6ed178UL,
    0x06f067aa72176fbaUL,0x0a637dc5a2c898a6UL,0x113f9804bef90daeUL,0x1b710b35131c471bUL,
    0x28db77f523047d84UL,0x32caab7b40c72493UL,0x3c9ebe0a15c9bebcUL,0x431d67c49c100d4cUL,
    0x4cc5d4becb3e42b6UL,0x597f299cfc657e2aUL,0x5fcb6fab3ad6faecUL,0x6c44198c4a475817UL
};
__constant u64 IV512[8] = {
    0x6a09e667f3bcc908UL,0xbb67ae8584caa73bUL,0x3c6ef372fe94f82bUL,0xa54ff53a5f1d36f1UL,
    0x510e527fade682d1UL,0x9b05688c2b3e6c1fUL,0x1f83d9abfb41bd6bUL,0x5be0cd19137e2179UL
};

void sha512_compress(u64 h[8], const u8 block[128]) {
    u64 w[16];
    for (int i = 0; i < 16; ++i) w[i] = be64d(block + i * 8);
    u64 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 80; ++i) {
        u64 wi;
        if (i < 16) wi = w[i];
        else {
            u64 x = w[(i + 1) & 15], y = w[(i + 14) & 15];
            u64 s0 = rotr64d(x,1) ^ rotr64d(x,8) ^ (x >> 7);
            u64 s1 = rotr64d(y,19) ^ rotr64d(y,61) ^ (y >> 6);
            wi = w[i & 15] + s0 + w[(i + 9) & 15] + s1;
            w[i & 15] = wi;
        }
        u64 S1 = rotr64d(e,14) ^ rotr64d(e,18) ^ rotr64d(e,41);
        u64 ch = (e & f) ^ ((~e) & g);
        u64 t1 = hh + S1 + ch + K512[i] + wi;
        u64 S0 = rotr64d(a,28) ^ rotr64d(a,34) ^ rotr64d(a,39);
        u64 maj = (a & b) ^ (a & c) ^ (b & c);
        u64 t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

void sha512_state_msg(u64 st[8], const u8* msg, int len, int prefix) {
    int off = 0;
    while (len - off >= 128) { sha512_compress(st, msg + off); off += 128; }
    u8 tail[256];
    for (int i = 0; i < 256; ++i) tail[i] = 0;
    int rem = len - off;
    for (int i = 0; i < rem; ++i) tail[i] = msg[off + i];
    tail[rem] = 0x80;
    int total = prefix + len;
    u64 bits = (u64)total * 8ULL;
    int tl = (rem + 1 + 16 > 128) ? 256 : 128;
    put64d(tail + tl - 8, bits);
    sha512_compress(st, tail);
    if (tl == 256) sha512_compress(st, tail + 128);
}

void sha512_one(const u8* msg, int len, u8 out[64]) {
    u64 st[8]; for (int i = 0; i < 8; ++i) st[i] = IV512[i];
    sha512_state_msg(st, msg, len, 0);
    for (int i = 0; i < 8; ++i) put64d(out + i * 8, st[i]);
}

void hmac_prepare(const u8* key, int keylen, struct HmacCtx* ctx) {
    u8 k[128]; for (int i = 0; i < 128; ++i) k[i] = 0;
    if (keylen > 128) { sha512_one(key, keylen, k); keylen = 64; }
    else for (int i = 0; i < keylen; ++i) k[i] = key[i];
    u8 pad[128];
    for (int i = 0; i < 8; ++i) ctx->inner[i] = IV512[i];
    for (int i = 0; i < 128; ++i) pad[i] = (u8)(k[i] ^ 0x36);
    sha512_compress(ctx->inner, pad);
    for (int i = 0; i < 8; ++i) ctx->outer[i] = IV512[i];
    for (int i = 0; i < 128; ++i) pad[i] = (u8)(k[i] ^ 0x5c);
    sha512_compress(ctx->outer, pad);
}

void hmac_msg(const struct HmacCtx* ctx, const u8* msg, int len, u8 out[64]) {
    u64 st[8]; for (int i = 0; i < 8; ++i) st[i] = ctx->inner[i];
    sha512_state_msg(st, msg, len, 128);
    u8 inner[64]; for (int i = 0; i < 8; ++i) put64d(inner + i * 8, st[i]);
    for (int i = 0; i < 8; ++i) st[i] = ctx->outer[i];
    u8 block[128]; for (int i = 0; i < 64; ++i) block[i] = inner[i];
    block[64] = 0x80; for (int i = 65; i < 128; ++i) block[i] = 0;
    put64d(block + 120, 192ULL * 8ULL);
    sha512_compress(st, block);
    for (int i = 0; i < 8; ++i) put64d(out + i * 8, st[i]);
}

void hmac64(const struct HmacCtx* ctx, const u8 msg[64], u8 out[64]) {
    u64 st[8]; u8 block[128];
    for (int i = 0; i < 8; ++i) st[i] = ctx->inner[i];
    for (int i = 0; i < 64; ++i) block[i] = msg[i];
    block[64] = 0x80; for (int i = 65; i < 128; ++i) block[i] = 0;
    put64d(block + 120, 192ULL * 8ULL);
    sha512_compress(st, block);
    u8 inner[64]; for (int i = 0; i < 8; ++i) put64d(inner + i * 8, st[i]);
    for (int i = 0; i < 8; ++i) st[i] = ctx->outer[i];
    for (int i = 0; i < 64; ++i) block[i] = inner[i];
    block[64] = 0x80; for (int i = 65; i < 128; ++i) block[i] = 0;
    put64d(block + 120, 192ULL * 8ULL);
    sha512_compress(st, block);
    for (int i = 0; i < 8; ++i) put64d(out + i * 8, st[i]);
}

void hmac_generic(const u8* key, int keylen, const u8* msg, int len, u8 out[64]) {
    struct HmacCtx ctx; hmac_prepare(key, keylen, &ctx); hmac_msg(&ctx, msg, len, out);
}

void pbkdf2_64(const u8* pass, int passlen, const u8* salt, int saltlen, int iters, u8 out[64]) {
    struct HmacCtx ctx; hmac_prepare(pass, passlen, &ctx);
    u8 first[128], u[64], t[64];
    for (int i = 0; i < saltlen; ++i) first[i] = salt[i];
    put32d(first + saltlen, 1);
    hmac_msg(&ctx, first, saltlen + 4, u);
    for (int i = 0; i < 64; ++i) t[i] = u[i];
    for (int i = 1; i < iters; ++i) {
        hmac64(&ctx, u, u);
        for (int j = 0; j < 64; ++j) t[j] ^= u[j];
    }
    for (int i = 0; i < 64; ++i) out[i] = t[i];
}

/* ---------- SHA-256 ---------- */
__constant u32 K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

void sha256_compress(u32 h[8], const u8 block[64]) {
    u32 w[64];
    for (int i = 0; i < 16; ++i) w[i] = be32d(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        u32 s0 = rotr32d(w[i-15],7) ^ rotr32d(w[i-15],18) ^ (w[i-15] >> 3);
        u32 s1 = rotr32d(w[i-2],17) ^ rotr32d(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; ++i) {
        u32 S1 = rotr32d(e,6) ^ rotr32d(e,11) ^ rotr32d(e,25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = hh + S1 + ch + K256[i] + w[i];
        u32 S0 = rotr32d(a,2) ^ rotr32d(a,13) ^ rotr32d(a,22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

void sha256_one(const u8* msg, int len, u8 out[32]) {
    u32 h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    int off = 0;
    while (len - off >= 64) { sha256_compress(h, msg + off); off += 64; }
    u8 tail[128]; for (int i = 0; i < 128; ++i) tail[i] = 0;
    int rem = len - off;
    for (int i = 0; i < rem; ++i) tail[i] = msg[off + i];
    tail[rem] = 0x80;
    int tl = (rem + 1 + 8 > 64) ? 128 : 64;
    put64d(tail + tl - 8, (u64)len * 8ULL);
    sha256_compress(h, tail);
    if (tl == 128) sha256_compress(h, tail + 64);
    for (int i = 0; i < 8; ++i) put32d(out + i * 4, h[i]);
}

/* ---------- address ---------- */
__constant char B64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

inline void bw_put(struct BW* w, u32 v, int n) {
    for (int i = n - 1; i >= 0; --i) {
        if ((w->bits & 7) == 0) w->d[w->bits >> 3] = 0;
        if ((v >> i) & 1) w->d[w->bits >> 3] |= (u8)(0x80 >> (w->bits & 7));
        ++w->bits;
    }
}
inline void bw_bytes(struct BW* w, const u8* p, int n) { for (int i = 0; i < n; ++i) bw_put(w, p[i], 8); }
inline void bw_finish(struct BW* w) { if (w->bits & 7) { bw_put(w, 1, 1); while (w->bits & 7) bw_put(w, 0, 1); } }

void make_address(const u8 pub[32], int version, int bounce, __global const u8* code_hash,
                  int code_depth, char* out_b64) {
    struct BW w; w.bits = 0; for (int i = 0; i < 48; ++i) w.d[i] = 0;
    if (version == 0) {
        bw_put(&w, 0, 32); bw_put(&w, 698983191u, 32); bw_bytes(&w, pub, 32); bw_put(&w, 0, 1);
    } else if (version == 1) {
        bw_put(&w, 1, 1); bw_put(&w, 0, 32); bw_put(&w, 2147483409u, 32); bw_bytes(&w, pub, 32); bw_put(&w, 0, 1);
    } else {
        bw_put(&w, 0, 8); bw_put(&w, 0, 32); bw_put(&w, 0x7FFF7F11u, 32); bw_bytes(&w, pub, 32);
    }
    int nbits = w.bits; bw_finish(&w);
    u8 cell[48]; cell[0] = 0; cell[1] = (u8)((nbits / 8) * 2 + ((nbits % 8) ? 1 : 0));
    int nbytes = (w.bits / 8);
    for (int i = 0; i < nbytes; ++i) cell[2 + i] = w.d[i];
    u8 data_hash[32]; sha256_one(cell, 2 + nbytes, data_hash);
    u8 si[71]; si[0] = 2; si[1] = 1; si[2] = 0x34; si[3] = 0; si[4] = (u8)code_depth; si[5] = 0; si[6] = 0;
    for (int i = 0; i < 32; ++i) si[7 + i] = code_hash[i];
    for (int i = 0; i < 32; ++i) si[39 + i] = data_hash[i];
    u8 hash[32]; sha256_one(si, 71, hash);
    u8 raw[36]; raw[0] = bounce ? 0x11 : 0x51; raw[1] = 0;
    for (int i = 0; i < 32; ++i) raw[2 + i] = hash[i];
    u16 crc = 0;
    for (int i = 0; i < 34; ++i) { crc ^= (u16)raw[i] << 8; for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? (u16)((crc << 1) ^ 0x1021) : (u16)(crc << 1); }
    raw[34] = (u8)(crc >> 8); raw[35] = (u8)(crc & 0xff);
    char b64[48]; int o = 0;
    for (int i = 0; i < 36; i += 3) {
        u32 v = ((u32)raw[i] << 16) | ((u32)raw[i+1] << 8) | raw[i+2];
        b64[o++] = B64[(v >> 18) & 63]; b64[o++] = B64[(v >> 12) & 63];
        b64[o++] = B64[(v >> 6) & 63]; b64[o++] = B64[v & 63];
    }
    for (int i = 0; i < 48; ++i) out_b64[i] = b64[i];
}

/* ---------- ed25519 ---------- */
void ed25519_pub(const u8 seed[32], u8 pub[32]) {
    u8 h[64]; sha512_one(seed, 32, h);
    h[0] &= 248; h[31] &= 63; h[31] |= 64;
    ge_p3 A; ge_scalarmult_base(&A, h); ge_p3_tobytes(pub, &A);
}

/* ---------- mnemonic ---------- */
int build_phrase12(__global const u8* ent, __global const char* words, char* out) {
    u8 le[16]; for (int i = 0; i < 16; ++i) le[i] = ent[i];
    u8 chk[32]; sha256_one(le, 16, chk);
    u8 bits[17]; for (int i = 0; i < 16; ++i) bits[i] = ent[i]; bits[16] = (u8)(chk[0] & 0xF0);
    u32 acc = 0; int nb = 0, o = 0, wn = 0;
    for (int i = 0; i < 17 && wn < 12; ++i) {
        acc = (acc << 8) | bits[i]; nb += 8;
        while (nb >= 11 && wn < 12) {
            nb -= 11; u32 idx = (acc >> nb) & 0x7FF;
            __global const char* wp = words + idx * 9;
            if (wn) out[o++] = ' ';
            for (int k = 0; k < 8 && wp[k]; ++k) out[o++] = wp[k];
            ++wn;
        }
    }
    return o;
}

int build_phrase24(__global const u8* raw, __global const char* words, char* out) {
    u32 acc = 0; int nb = 0, o = 0, wn = 0;
    for (int i = 0; i < 33 && wn < 24; ++i) {
        acc = (acc << 8) | raw[i]; nb += 8;
        while (nb >= 11 && wn < 24) {
            nb -= 11; u32 idx = (acc >> nb) & 0x7FF;
            __global const char* wp = words + idx * 9;
            if (wn) out[o++] = ' ';
            for (int k = 0; k < 8 && wp[k]; ++k) out[o++] = wp[k];
            ++wn;
        }
    }
    return o;
}

inline void lit16(u8* dst, __constant const char* src) { for (int i = 0; i < 16; ++i) dst[i] = (u8)src[i]; }
inline void lit8(u8* dst, __constant const char* src) { for (int i = 0; i < 8; ++i) dst[i] = (u8)src[i]; }
inline void lit12(u8* dst, __constant const char* src) { for (int i = 0; i < 12; ++i) dst[i] = (u8)src[i]; }

/* ---------- kernels ---------- */
__kernel void kn_bip39_12(__global const u8* entropies, int count, __global u8* addrs,
    __global const char* words, __global const u8* code_hash, int version, int bounce)
{
    int i = get_global_id(0);
    if (i >= count) return;
    __global const u8* ent = entropies + i * 16;
    char phrase[240]; int plen = build_phrase12(ent, words, phrase);
    u8 salt[8]; lit8(salt, "mnemonic");
    u8 seed[64]; pbkdf2_64((const u8*)phrase, plen, salt, 8, 2048, seed);
    u8 digest[64], key[32], chain[32];
    u8 hk[12]; lit12(hk, "ed25519 seed");
    hmac_generic(hk, 12, seed, 64, digest);
    for (int k = 0; k < 32; ++k) { key[k] = digest[k]; chain[k] = digest[32 + k]; }
    const u32 path[3] = {44u, 607u, 0u};
    for (int p = 0; p < 3; ++p) {
        u8 data[37]; data[0] = 0; for (int k = 0; k < 32; ++k) data[1 + k] = key[k];
        put32d(data + 33, path[p] | 0x80000000u);
        hmac_generic(chain, 32, data, 37, digest);
        for (int k = 0; k < 32; ++k) { key[k] = digest[k]; chain[k] = digest[32 + k]; }
    }
    u8 pub[32]; ed25519_pub(key, pub);
    int depth = version == 0 ? 7 : (version == 1 ? 6 : 0);
    char tmp[48];
    make_address(pub, version, bounce, code_hash, depth, tmp);
    for (int k = 0; k < 48; ++k) addrs[i * 48 + k] = (u8)tmp[k];
}

__kernel void kn_ton24_a(__global const u8* raws, int count, __global const char* words,
    __global u8* valid_ent, __global u8* valid_idx, __global uint* valid_count)
{
    int i = get_global_id(0);
    if (i >= count) return;
    __global const u8* raw = raws + i * 33;
    char phrase[240]; int plen = build_phrase24(raw, words, phrase);
    u8 entropy[64];
    u8 empty[1]; empty[0] = 0;
    hmac_generic((const u8*)phrase, plen, empty, 0, entropy);
    u8 salt[16]; lit16(salt, "TON seed version");
    u8 basic[64]; pbkdf2_64(entropy, 64, salt, 16, 390, basic);
    if (basic[0] != 0) return;
    uint slot = atomic_inc(valid_count);
    if (slot < MAX_VALID) {
        for (int k = 0; k < 64; ++k) valid_ent[slot * 64 + k] = entropy[k];
        for (int k = 0; k < 33; ++k) valid_idx[slot * 33 + k] = raw[k];
    }
}

__kernel void kn_ton24_b(__global const u8* valid_ent, __global const u8* valid_idx, int count,
    __global u8* addrs, __global const char* words, __global const u8* code_hash,
    int version, int bounce)
{
    int i = get_global_id(0);
    if (i >= count) return;
    u8 ent[64];
    for (int k = 0; k < 64; ++k) ent[k] = valid_ent[i * 64 + k];
    u8 salt[16]; lit16(salt, "TON default seed");
    u8 seed[64]; pbkdf2_64(ent, 64, salt, 16, 100000, seed);
    u8 pub[32]; ed25519_pub(seed, pub);
    int depth = version == 0 ? 7 : (version == 1 ? 6 : 0);
    char tmp[48];
    make_address(pub, version, bounce, code_hash, depth, tmp);
    for (int k = 0; k < 48; ++k) addrs[i * 48 + k] = (u8)tmp[k];
}
