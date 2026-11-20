#include "ton_crypto.h"
#include "wordlist_en.h"
#include <string.h>
#include <stdio.h>
#include <algorithm>

namespace tnv {

static inline uint64_t rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }
static inline uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static inline uint64_t be64(const uint8_t* p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}
static inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void put64(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) { p[i] = (uint8_t)v; v >>= 8; }
}
static inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static const uint64_t IV512[8] = {
    0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL
};

static inline void sha512_compress(uint64_t h[8], const uint8_t block[128]) {
    uint64_t w[16];
    for (int i = 0; i < 16; ++i) w[i] = be64(block + i * 8);
    uint64_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
    for (int i = 0; i < 80; ++i) {
        uint64_t wi;
        if (i < 16) wi = w[i];
        else {
            uint64_t x = w[(i + 1) & 15];
            uint64_t y = w[(i + 14) & 15];
            uint64_t s0 = rotr64(x, 1) ^ rotr64(x, 8) ^ (x >> 7);
            uint64_t s1 = rotr64(y, 19) ^ rotr64(y, 61) ^ (y >> 6);
            wi = w[i & 15] + s0 + w[(i + 9) & 15] + s1;
            w[i & 15] = wi;
        }
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t t1 = hh + S1 + ch + K512[i] + wi;
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void sha512_state_msg(uint64_t st[8], const uint8_t* msg, size_t len, size_t prefix_bytes) {
    size_t off = 0;
    while (len - off >= 128) { sha512_compress(st, msg + off); off += 128; }
    uint8_t tail[256];
    memset(tail, 0, sizeof(tail));
    size_t rem = len - off;
    if (rem) memcpy(tail, msg + off, rem);
    tail[rem] = 0x80;
    size_t total_bytes = prefix_bytes + len;
    uint64_t bits = (uint64_t)total_bytes * 8ULL;
    size_t tail_len = (rem + 1 + 16 > 128) ? 256 : 128;
    put64(tail + tail_len - 8, bits);
    sha512_compress(st, tail);
    if (tail_len == 256) sha512_compress(st, tail + 128);
}

void sha512(const void* data, size_t len, uint8_t out[64]) {
    uint64_t st[8]; memcpy(st, IV512, sizeof(st));
    sha512_state_msg(st, (const uint8_t*)data, len, 0);
    for (int i = 0; i < 8; ++i) put64(out + i * 8, st[i]);
}

void hmac_prepare(const uint8_t* key, size_t keylen, HmacSha512Ctx& ctx) {
    uint8_t k[128]; memset(k, 0, sizeof(k));
    if (keylen > 128) { sha512(key, keylen, k); keylen = 64; }
    else if (keylen) memcpy(k, key, keylen);
    uint8_t pad[128];
    memcpy(ctx.inner, IV512, sizeof(ctx.inner));
    for (int i = 0; i < 128; ++i) pad[i] = (uint8_t)(k[i] ^ 0x36);
    sha512_compress(ctx.inner, pad);
    memcpy(ctx.outer, IV512, sizeof(ctx.outer));
    for (int i = 0; i < 128; ++i) pad[i] = (uint8_t)(k[i] ^ 0x5c);
    sha512_compress(ctx.outer, pad);
}

void hmac_msg(const HmacSha512Ctx& ctx, const uint8_t* msg, size_t len, uint8_t out[64]) {
    uint64_t st[8]; memcpy(st, ctx.inner, sizeof(st));
    sha512_state_msg(st, msg, len, 128);
    uint8_t inner[64];
    for (int i = 0; i < 8; ++i) put64(inner + i * 8, st[i]);
    memcpy(st, ctx.outer, sizeof(st));
    sha512_state_msg(st, inner, 64, 128);
    for (int i = 0; i < 8; ++i) put64(out + i * 8, st[i]);
}

void hmac64(const HmacSha512Ctx& ctx, const uint8_t msg[64], uint8_t out[64]) {
    hmac_msg(ctx, msg, 64, out);
}

void hmac_sha512(const uint8_t* key, size_t keylen, const uint8_t* msg, size_t msglen, uint8_t out[64]) {
    HmacSha512Ctx ctx; hmac_prepare(key, keylen, ctx); hmac_msg(ctx, msg, msglen, out);
}

void pbkdf2_sha512(const uint8_t* pass, size_t passlen, const uint8_t* salt, size_t saltlen,
                   uint32_t iterations, uint8_t* out, size_t outlen) {
    HmacSha512Ctx ctx; hmac_prepare(pass, passlen, ctx);
    uint8_t u[64], t[64], first[128];
    for (uint32_t block = 1; outlen; ++block) {
        size_t take = outlen > 64 ? 64 : outlen;
        memcpy(first, salt, saltlen);
        put32(first + saltlen, block);
        hmac_msg(ctx, first, saltlen + 4, u);
        memcpy(t, u, 64);
        for (uint32_t i = 1; i < iterations; ++i) {
            hmac64(ctx, u, u);
            for (int j = 0; j < 64; ++j) t[j] ^= u[j];
        }
        memcpy(out, t, take);
        out += take; outlen -= take;
    }
}

// ---------- SHA-256 ----------
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static void sha256_compress(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}
void sha256(const void* data, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const uint8_t* p = (const uint8_t*)data;
    size_t off = 0;
    while (len - off >= 64) { sha256_compress(h, p + off); off += 64; }
    uint8_t tail[128]; memset(tail, 0, sizeof(tail));
    size_t rem = len - off; if (rem) memcpy(tail, p + off, rem);
    tail[rem] = 0x80;
    size_t tail_len = (rem + 1 + 8 > 64) ? 128 : 64;
    put64(tail + tail_len - 8, (uint64_t)len * 8ULL);
    sha256_compress(h, tail);
    if (tail_len == 128) sha256_compress(h, tail + 64);
    for (int i = 0; i < 8; ++i) put32(out + i * 4, h[i]);
}

// ---------- base64url / crc ----------
static const char B64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
std::string base64url(const uint8_t* data, size_t len) {
    std::string s; s.reserve((len * 4 + 2) / 3);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8) | data[i+2];
        s.push_back(B64URL[(v >> 18) & 63]); s.push_back(B64URL[(v >> 12) & 63]);
        s.push_back(B64URL[(v >> 6) & 63]); s.push_back(B64URL[v & 63]);
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = (uint32_t)data[i] << 16;
        s.push_back(B64URL[(v >> 18) & 63]); s.push_back(B64URL[(v >> 12) & 63]);
    } else if (i + 2 == len) {
        uint32_t v = ((uint32_t)data[i] << 16) | ((uint32_t)data[i+1] << 8);
        s.push_back(B64URL[(v >> 18) & 63]); s.push_back(B64URL[(v >> 12) & 63]); s.push_back(B64URL[(v >> 6) & 63]);
    }
    return s;
}
uint16_t crc16_xmodem(const uint8_t* data, size_t len) {
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
bool suffix_match(const std::string& address, const std::string& suffix, bool case_sensitive) {
    if (suffix.size() > address.size()) return false;
    size_t off = address.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = address[off + i], b = suffix[i];
        if (!case_sensitive) {
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        }
        if (a != b) return false;
    }
    return true;
}

// ---------- BitWriter ----------
struct BitWriter {
    std::vector<uint8_t> data;
    uint32_t bits = 0;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if ((bits & 7) == 0) data.push_back(0);
            if ((v >> i) & 1) data.back() |= (uint8_t)(0x80 >> (bits & 7));
            ++bits;
        }
    }
    void put_bytes(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; ++i) put(p[i], 8); }
    void finish() { if (bits & 7) { put(1, 1); while (bits & 7) put(0, 1); } }
};
static uint8_t hexval(char c) { return (c >= '0' && c <= '9') ? (uint8_t)(c - '0') : (uint8_t)((c | 32) - 'a' + 10); }
static void hex32(const char* s, uint8_t out[32]) {
    for (int i = 0; i < 32; ++i) out[i] = (uint8_t)((hexval(s[i*2]) << 4) | hexval(s[i*2+1]));
}

struct WalletMeta { const char* code_hash; int depth; };
static const WalletMeta META[3] = {
    {"feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0", 7},
    {"20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f", 6},
    {"9149ae51c1e4689710cebf7830297b16acfbadb363a920a537893e7ffeeca768", 0}
};

std::string wallet_address(const uint8_t pub[32], WalletVersion v, bool bounceable) {
    BitWriter w;
    if (v == WALLET_V4R2) {
        w.put(0, 32); w.put(698983191u, 32); w.put_bytes(pub, 32); w.put(0, 1);
    } else if (v == WALLET_W5R1) {
        w.put(1, 1); w.put(0, 32); w.put(2147483409u, 32); w.put_bytes(pub, 32); w.put(0, 1);
    } else {
        w.put(0, 8); w.put(0, 32); w.put(0x7FFF7F11u, 32); w.put_bytes(pub, 32);
    }
    uint32_t nbits = w.bits;
    w.finish();
    uint8_t data_hash[32];
    {
        std::vector<uint8_t> cell; cell.reserve(w.data.size() + 2);
        cell.push_back(0);
        cell.push_back((uint8_t)((nbits / 8) * 2 + (nbits % 8 ? 1 : 0)));
        cell.insert(cell.end(), w.data.begin(), w.data.end());
        sha256(cell.data(), cell.size(), data_hash);
    }
    uint8_t code_hash[32]; hex32(META[v].code_hash, code_hash);
    uint8_t si[3 + 2 + 2 + 32 + 32];
    si[0] = 2; si[1] = 1; si[2] = 0x34;
    si[3] = 0; si[4] = (uint8_t)META[v].depth;
    si[5] = 0; si[6] = 0;
    memcpy(si + 7, code_hash, 32);
    memcpy(si + 39, data_hash, 32);
    uint8_t hash[32]; sha256(si, sizeof(si), hash);
    uint8_t raw[36];
    raw[0] = bounceable ? 0x11 : 0x51;
    raw[1] = 0;
    memcpy(raw + 2, hash, 32);
    uint16_t crc = crc16_xmodem(raw, 34);
    raw[34] = (uint8_t)(crc >> 8); raw[35] = (uint8_t)(crc & 0xff);
    return base64url(raw, 36);
}

// ---------- BIP39 ----------
static int word_index(const std::string& w) {
    for (int i = 0; i < TNV_WORD_COUNT; ++i) if (w == TNV_WORDS[i]) return i;
    return -1;
}
static std::vector<std::string> split_words(const std::string& phrase) {
    std::vector<std::string> out; std::string cur;
    for (char c : phrase) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::string join_words(const std::vector<std::string>& w) {
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) { if (i) s.push_back(' '); s += w[i]; }
    return s;
}
bool bip39_entropy_to_phrase(const uint8_t entropy[16], std::string& phrase) {
    uint8_t chk[32]; sha256(entropy, 16, chk);
    uint8_t bits[17]; memcpy(bits, entropy, 16); bits[16] = (uint8_t)(chk[0] & 0xF0);
    std::vector<std::string> words;
    uint32_t acc = 0; int nbits = 0; int wi = 0;
    for (int i = 0; i < 17; ++i) {
        acc = (acc << 8) | bits[i]; nbits += 8;
        while (nbits >= 11) {
            nbits -= 11;
            int idx = (acc >> nbits) & 0x7FF;
            words.push_back(TNV_WORDS[idx]);
            if (++wi == 12) break;
        }
    }
    if (words.size() != 12) return false;
    phrase = join_words(words);
    return true;
}
bool bip39_phrase_to_entropy(const std::string& phrase, uint8_t entropy[16]) {
    auto words = split_words(phrase);
    if (words.size() != 12) return false;
    uint8_t bits[17]; memset(bits, 0, sizeof(bits));
    int bitpos = 0;
    for (auto& w : words) {
        int idx = word_index(w); if (idx < 0) return false;
        for (int b = 10; b >= 0; --b) {
            if ((idx >> b) & 1) bits[bitpos >> 3] |= (uint8_t)(0x80 >> (bitpos & 7));
            ++bitpos;
        }
    }
    uint8_t chk[32]; memcpy(entropy, bits, 16); sha256(entropy, 16, chk);
    if ((bits[16] & 0xF0) != (chk[0] & 0xF0)) return false;
    return true;
}
bool bip39_phrase_to_seed(const std::string& phrase, uint8_t seed[64]) {
    pbkdf2_sha512((const uint8_t*)phrase.data(), phrase.size(), (const uint8_t*)"mnemonic", 8, 2048, seed, 64);
    return true;
}
bool bip39_validate(const std::string& phrase) {
    uint8_t e[16]; return bip39_phrase_to_entropy(phrase, e);
}

// ---------- TON 24 ----------
bool ton24_phrase_from_indices(const uint16_t idx[24], std::string& phrase) {
    std::vector<std::string> w; w.reserve(24);
    for (int i = 0; i < 24; ++i) { if (idx[i] >= 2048) return false; w.push_back(TNV_WORDS[idx[i]]); }
    phrase = join_words(w); return true;
}
bool ton24_indices_from_bytes(const uint8_t bytes[33], uint16_t idx[24]) {
    uint32_t acc = 0; int nbits = 0; int o = 0;
    for (int i = 0; i < 33; ++i) {
        acc = (acc << 8) | bytes[i]; nbits += 8;
        while (nbits >= 11) { nbits -= 11; idx[o++] = (uint16_t)((acc >> nbits) & 0x7FF); if (o == 24) break; }
    }
    return o == 24;
}
bool ton24_phrase_to_entropy(const std::string& phrase, uint8_t entropy[64]) {
    hmac_sha512((const uint8_t*)phrase.data(), phrase.size(), (const uint8_t*)"", 0, entropy);
    return true;
}
bool ton24_validate(const std::string& phrase) {
    auto words = split_words(phrase); if (words.size() != 24) return false;
    for (auto& w : words) if (word_index(w) < 0) return false;
    uint8_t entropy[64], basic[64]; ton24_phrase_to_entropy(phrase, entropy);
    pbkdf2_sha512(entropy, 64, (const uint8_t*)"TON seed version", 16, 390, basic, 64);
    return basic[0] == 0;
}
bool ton24_entropy_from_indices(const uint16_t idx[24], uint8_t entropy[64]) {
    char phrase[240]; int o = 0;
    for (int i = 0; i < 24; ++i) {
        if (idx[i] >= TNV_WORD_COUNT) return false;
        if (i) phrase[o++] = ' ';
        const char* w = TNV_WORDS[idx[i]];
        for (int k = 0; k < 8 && w[k]; ++k) phrase[o++] = w[k];
    }
    hmac_sha512((const uint8_t*)phrase, o, (const uint8_t*)"", 0, entropy);
    return true;
}
bool ton24_phrase_to_seed(const std::string& phrase, uint8_t seed[32], uint8_t entropy[64]) {
    auto words = split_words(phrase); if (words.size() != 24) return false;
    for (auto& w : words) if (word_index(w) < 0) return false;
    ton24_phrase_to_entropy(phrase, entropy);
    uint8_t basic[64]; pbkdf2_sha512(entropy, 64, (const uint8_t*)"TON seed version", 16, 390, basic, 64);
    if (basic[0] != 0) return false;
    uint8_t full[64]; pbkdf2_sha512(entropy, 64, (const uint8_t*)"TON default seed", 16, 100000, full, 64);
    memcpy(seed, full, 32); return true;
}

// ---------- SLIP-0010 ----------
bool slip10_ed25519(const uint8_t seed[64], uint8_t key[32]) {
    uint8_t digest[64];
    hmac_sha512((const uint8_t*)"ed25519 seed", 12, seed, 64, digest);
    uint8_t k[32], c[32]; memcpy(k, digest, 32); memcpy(c, digest + 32, 32);
    const uint32_t path[3] = {44u, 607u, 0u};
    for (int i = 0; i < 3; ++i) {
        uint8_t data[37]; data[0] = 0; memcpy(data + 1, k, 32);
        put32(data + 33, path[i] | 0x80000000u);
        hmac_sha512(c, 32, data, 37, digest);
        memcpy(k, digest, 32); memcpy(c, digest + 32, 32);
    }
    memcpy(key, k, 32); return true;
}

// ---------- Ed25519 ----------
extern "C" void tnv_ed25519_pub_from_seed(const unsigned char seed[32], unsigned char pk[32]);
void ed25519_pub(const uint8_t seed[32], uint8_t pub[32]) { tnv_ed25519_pub_from_seed(seed, pub); }

// ---------- ChaCha20 ----------
static inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static void chacha_block(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16]; memcpy(x, in, sizeof(x));
    for (int i = 0; i < 10; ++i) {
        x[0]+=x[4]; x[12]=rotl32(x[12]^x[0],16); x[8]+=x[12]; x[4]=rotl32(x[4]^x[8],12);
        x[0]+=x[4]; x[12]=rotl32(x[12]^x[0],8);  x[8]+=x[12]; x[4]=rotl32(x[4]^x[8],7);
        x[1]+=x[5]; x[13]=rotl32(x[13]^x[1],16); x[9]+=x[13]; x[5]=rotl32(x[5]^x[9],12);
        x[1]+=x[5]; x[13]=rotl32(x[13]^x[1],8);  x[9]+=x[13]; x[5]=rotl32(x[5]^x[9],7);
        x[2]+=x[6]; x[14]=rotl32(x[14]^x[2],16); x[10]+=x[14]; x[6]=rotl32(x[6]^x[10],12);
        x[2]+=x[6]; x[14]=rotl32(x[14]^x[2],8);  x[10]+=x[14]; x[6]=rotl32(x[6]^x[10],7);
        x[3]+=x[7]; x[15]=rotl32(x[15]^x[3],16); x[11]+=x[15]; x[7]=rotl32(x[7]^x[11],12);
        x[3]+=x[7]; x[15]=rotl32(x[15]^x[3],8);  x[11]+=x[15]; x[7]=rotl32(x[7]^x[11],7);
        x[0]+=x[5]; x[15]=rotl32(x[15]^x[0],16); x[10]+=x[15]; x[5]=rotl32(x[5]^x[10],12);
        x[0]+=x[5]; x[15]=rotl32(x[15]^x[0],8);  x[10]+=x[15]; x[5]=rotl32(x[5]^x[10],7);
        x[1]+=x[6]; x[12]=rotl32(x[12]^x[1],16); x[11]+=x[12]; x[6]=rotl32(x[6]^x[11],12);
        x[1]+=x[6]; x[12]=rotl32(x[12]^x[1],8);  x[11]+=x[12]; x[6]=rotl32(x[6]^x[11],7);
        x[2]+=x[7]; x[13]=rotl32(x[13]^x[2],16); x[8]+=x[13]; x[7]=rotl32(x[7]^x[8],12);
        x[2]+=x[7]; x[13]=rotl32(x[13]^x[2],8);  x[8]+=x[13]; x[7]=rotl32(x[7]^x[8],7);
        x[3]+=x[4]; x[14]=rotl32(x[14]^x[3],16); x[9]+=x[14]; x[4]=rotl32(x[4]^x[9],12);
        x[3]+=x[4]; x[14]=rotl32(x[14]^x[3],8);  x[9]+=x[14]; x[4]=rotl32(x[4]^x[9],7);
    }
    for (int i = 0; i < 16; ++i) out[i] = x[i] + in[i];
}
void ChaCha20::init(const uint8_t key[32], const uint8_t nonce[8], uint64_t counter) {
    state[0]=0x61707865; state[1]=0x3320646e; state[2]=0x79622d32; state[3]=0x6b206574;
    for (int i = 0; i < 8; ++i) state[4+i] = (uint32_t)key[i*4] | ((uint32_t)key[i*4+1]<<8) | ((uint32_t)key[i*4+2]<<16) | ((uint32_t)key[i*4+3]<<24);
    state[12] = (uint32_t)counter; state[13] = (uint32_t)(counter >> 32);
    state[14] = (uint32_t)nonce[0] | ((uint32_t)nonce[1]<<8) | ((uint32_t)nonce[2]<<16) | ((uint32_t)nonce[3]<<24);
    state[15] = (uint32_t)nonce[4] | ((uint32_t)nonce[5]<<8) | ((uint32_t)nonce[6]<<16) | ((uint32_t)nonce[7]<<24);
    pos = 64;
}
void ChaCha20::fill(uint8_t* out, size_t n) {
    while (n) {
        if (pos == 64) { uint32_t block[16]; chacha_block(block, state); memcpy(buf, block, 64); ++state[12]; if (!state[12]) ++state[13]; pos = 0; }
        size_t take = 64 - pos; if (take > n) take = n;
        memcpy(out, buf + pos, take); pos += take; out += take; n -= take;
    }
}
uint32_t ChaCha20::u32() { uint8_t b[4]; fill(b, 4); return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24); }

// ---------- candidates ----------
bool derive_bip39_12(const uint8_t entropy[16], WalletVersion v, bool bounceable, WalletCandidate& out) {
    std::string phrase;
    if (!bip39_entropy_to_phrase(entropy, phrase)) return false;
    uint8_t seed[64], key[32];
    bip39_phrase_to_seed(phrase, seed);
    slip10_ed25519(seed, key);
    ed25519_pub(key, out.public_key);
    out.phrase = phrase;
    out.address = wallet_address(out.public_key, v, bounceable);
    return true;
}
bool derive_ton24(const uint16_t idx[24], WalletVersion v, bool bounceable, WalletCandidate& out) {
    std::string phrase;
    if (!ton24_phrase_from_indices(idx, phrase)) return false;
    uint8_t seed[32], entropy[64];
    if (!ton24_phrase_to_seed(phrase, seed, entropy)) return false;
    ed25519_pub(seed, out.public_key);
    out.phrase = phrase;
    out.address = wallet_address(out.public_key, v, bounceable);
    return true;
}
bool derive_tg_24(const uint8_t anchor_entropy[16], const uint8_t signing_entropy[16], bool bounceable, WalletCandidate& out) {
    std::string a, s;
    if (!bip39_entropy_to_phrase(anchor_entropy, a)) return false;
    if (!bip39_entropy_to_phrase(signing_entropy, s)) return false;
    uint8_t seed[64], key[32];
    bip39_phrase_to_seed(a, seed);
    slip10_ed25519(seed, key);
    ed25519_pub(key, out.public_key);
    out.phrase = a + " " + s;
    out.address = wallet_address(out.public_key, WALLET_TG, bounceable);
    return true;
}

} // namespace tnv
