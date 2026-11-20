#include "backend_cuda.h"
#include "wordlist_en.h"
#include "ton_crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>

using namespace tnv;

typedef int CUresult;
typedef int CUdevice;
typedef void* CUcontext;
typedef void* CUmodule;
typedef void* CUfunction;
typedef void* CUstream;
typedef unsigned long long CUdeviceptr;

#define CUDA_SUCCESS 0
#define CUDA_ERROR_NOT_READY 600
#define CU_ATTR_CC_MAJOR 75
#define CU_ATTR_CC_MINOR 76

struct FoundRec {
    unsigned char entropy[64];
    unsigned char idx[33];
    unsigned char kind;
    unsigned char pad[2];
};

static const char* CODE_HEX[3] = {
    "feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0",
    "20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f",
    "9149ae51c1e4689710cebf7830297b16acfbadb363a920a537893e7ffeeca768"
};
static const int CODE_DEPTH[3] = {7, 6, 0};

struct Cudart {
    HMODULE h = NULL;
    CUresult (WINAPI *cuInit)(unsigned);
    CUresult (WINAPI *cuDeviceGetCount)(int*);
    CUresult (WINAPI *cuDeviceGet)(CUdevice*, int);
    CUresult (WINAPI *cuDeviceGetAttribute)(int*, int, CUdevice);
    CUresult (WINAPI *cuCtxCreate_v2)(CUcontext*, unsigned, CUdevice);
    CUresult (WINAPI *cuCtxDestroy_v2)(CUcontext);
    CUresult (WINAPI *cuModuleLoadDataEx)(CUmodule*, const void*, unsigned, int*, void**);
    CUresult (WINAPI *cuModuleUnload)(CUmodule);
    CUresult (WINAPI *cuModuleGetFunction)(CUfunction*, CUmodule, const char*);
    CUresult (WINAPI *cuModuleGetGlobal_v2)(CUdeviceptr*, size_t*, CUmodule, const char*);
    CUresult (WINAPI *cuMemAlloc_v2)(CUdeviceptr*, size_t);
    CUresult (WINAPI *cuMemFree_v2)(CUdeviceptr);
    CUresult (WINAPI *cuMemcpyHtoD_v2)(CUdeviceptr, const void*, size_t);
    CUresult (WINAPI *cuMemcpyHtoDAsync_v2)(CUdeviceptr, const void*, size_t, CUstream);
    CUresult (WINAPI *cuMemsetD32Async)(CUdeviceptr, unsigned, size_t, CUstream);
    CUresult (WINAPI *cuMemcpyDtoH_v2)(void*, CUdeviceptr, size_t);
    CUresult (WINAPI *cuMemsetD32_v2)(CUdeviceptr, unsigned, size_t);
    CUresult (WINAPI *cuStreamCreate)(CUstream*, unsigned);
    CUresult (WINAPI *cuStreamDestroy_v2)(CUstream);
    CUresult (WINAPI *cuStreamQuery)(CUstream);
    CUresult (WINAPI *cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, CUstream, void**, void**);
    CUresult (WINAPI *cuGetErrorString)(CUresult, const char**);
    CUresult (WINAPI *cuCtxSynchronize)(void);
    CUresult (WINAPI *cuCtxSetCurrent)(CUcontext);
    bool load() {
        h = LoadLibraryA("nvcuda.dll");
        if (!h) return false;
        #define L(n) n = (decltype(n))GetProcAddress(h, #n)
        L(cuInit); L(cuDeviceGetCount); L(cuDeviceGet); L(cuDeviceGetAttribute);
        L(cuCtxCreate_v2); L(cuCtxDestroy_v2); L(cuModuleLoadDataEx); L(cuModuleUnload);
        L(cuModuleGetFunction); L(cuModuleGetGlobal_v2); L(cuMemAlloc_v2); L(cuMemFree_v2); L(cuMemcpyHtoD_v2);
        L(cuMemcpyDtoH_v2); L(cuMemsetD32_v2); L(cuStreamCreate); L(cuStreamDestroy_v2);
        L(cuStreamQuery); L(cuLaunchKernel); L(cuGetErrorString); L(cuCtxSynchronize); L(cuCtxSetCurrent);
        #undef L
        return cuInit && cuDeviceGetCount && cuCtxCreate_v2 && cuModuleLoadDataEx && cuLaunchKernel;
    }
};

struct CudaEngine {
    Cudart a;
    CUdevice dev = 0;
    CUcontext ctx = NULL;
    CUmodule mod = NULL;
    CUfunction f12 = NULL, fa = NULL, fb = NULL;
    CUstream stream = NULL;
    CUdeviceptr d_words = 0, d_suffix = 0, d_codehash = 0, d_ent = 0, d_raw = 0, d_found = 0, d_found_count = 0;
    CUdeviceptr d_valid_ent = 0, d_valid_idx = 0, d_valid_count = 0;
    CUdeviceptr d_out = 0;
    CUdeviceptr d_addrs = 0;
    size_t ent_cap = 0;
    TnvConfig cfg;
    std::string name;
    int cc_major = 0, cc_minor = 0;
    int batch12 = 65536;
    int batch24 = 262144;
};

static HMODULE self_module() {
    HMODULE h = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&self_module, &h);
    return h;
}
static bool get_res(int id, const void** ptr, size_t* size) {
    HMODULE h = self_module();
    HRSRC r = FindResourceA(h, MAKEINTRESOURCEA(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(h, r);
    if (!g) return false;
    *ptr = LockResource(g);
    *size = SizeofResource(h, r);
    return *ptr && *size;
}

static std::string errstr(Cudart& a, CUresult r) {
    const char* s = NULL;
    if (a.cuGetErrorString) a.cuGetErrorString(r, &s);
    char buf[128]; snprintf(buf, sizeof(buf), "CUDA error %d%s%s", (int)r, s ? ": " : "", s ? s : "");
    return buf;
}

template <typename T>
static T* getfn(HMODULE h, const char* n) { return (T*)GetProcAddress(h, n); }

static bool wait_stream(Cudart& a, CUstream s, int timeout_ms) {
    ULONGLONG t0 = GetTickCount64();
    for (;;) {
        CUresult r = a.cuStreamQuery(s);
        if (r == CUDA_SUCCESS) return true;
        if (r != CUDA_ERROR_NOT_READY) return false;
        if (GetTickCount64() - t0 > (ULONGLONG)timeout_ms) return false;
        Sleep(5);
    }
}

static bool load_module(CudaEngine* e) {
    const void* data = NULL; size_t size = 0;
    int id = 0;
    bool force_ptx = false;
    char env[16]; if (GetEnvironmentVariableA("TNV_FORCE_PTX", env, sizeof(env)) > 0) force_ptx = true;
    if (!force_ptx) {
        if (e->cc_major == 7 && e->cc_minor == 5) id = 100;
        else if (e->cc_major == 8 && e->cc_minor == 6) id = 101;
        else if (e->cc_major == 8 && e->cc_minor == 9) id = 102;
        else if (e->cc_major == 12 && e->cc_minor == 0) id = 103;
    }
    if (id && get_res(id, &data, &size)) {
        CUresult r = e->a.cuModuleLoadDataEx(&e->mod, data, 0, NULL, NULL);
        if (r == CUDA_SUCCESS) return true;
        e->mod = NULL;
    }
    int ptxid = (e->cc_major >= 12) ? 105 : 104;
    if (!get_res(ptxid, &data, &size) && !get_res(104, &data, &size)) return false;
    CUresult r = e->a.cuModuleLoadDataEx(&e->mod, data, 0, NULL, NULL);
    return r == CUDA_SUCCESS;
}

static void hex32(const char* s, unsigned char out[32]) {
    for (int i = 0; i < 32; ++i) {
        unsigned v; sscanf(s + 2 * i, "%2x", &v); out[i] = (unsigned char)v;
    }
}

CudaEngine* cuda_create(const TnvConfig& cfg, std::string& name, std::string& error) {
    CudaEngine* e = new CudaEngine();
    e->cfg = cfg;
    if (!e->a.load()) { error = "nvcuda.dll ?? ?????? (??? NVIDIA-????????)"; delete e; return NULL; }
    CUresult r = e->a.cuInit(0);
    if (r != CUDA_SUCCESS) { error = errstr(e->a, r); delete e; return NULL; }
    int n = 0;
    if (e->a.cuDeviceGetCount(&n) != CUDA_SUCCESS || n <= 0) { error = "CUDA-?????????? ?? ???????"; delete e; return NULL; }
    if (e->a.cuDeviceGet(&e->dev, 0) != CUDA_SUCCESS) { error = "cuDeviceGet failed"; delete e; return NULL; }
    e->a.cuDeviceGetAttribute(&e->cc_major, CU_ATTR_CC_MAJOR, e->dev);
    e->a.cuDeviceGetAttribute(&e->cc_minor, CU_ATTR_CC_MINOR, e->dev);
    r = e->a.cuCtxCreate_v2(&e->ctx, 0, e->dev);
    if (r != CUDA_SUCCESS) { error = errstr(e->a, r); delete e; return NULL; }
    if (!load_module(e)) { error = "?? ??????? ????????? CUDA-??????"; e->a.cuCtxDestroy_v2(e->ctx); delete e; return NULL; }
    if (e->a.cuModuleGetFunction(&e->f12, e->mod, "kn_bip39_12") != CUDA_SUCCESS ||
        e->a.cuModuleGetFunction(&e->fa, e->mod, "kn_ton24_a") != CUDA_SUCCESS ||
        e->a.cuModuleGetFunction(&e->fb, e->mod, "kn_ton24_b") != CUDA_SUCCESS) {
        error = "CUDA-??????? ?? ???????"; e->a.cuModuleUnload(e->mod); e->a.cuCtxDestroy_v2(e->ctx); delete e; return NULL;
    }
    if (e->a.cuStreamCreate(&e->stream, 0) != CUDA_SUCCESS) { error = "cuStreamCreate failed"; delete e; return NULL; }
    if (e->a.cuMemAlloc_v2(&e->d_words, TNV_WORD_COUNT * 9) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_suffix, 64) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_codehash, 32) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_ent, (size_t)262144 * 64) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_raw, (size_t)262144 * 33) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_valid_ent, 131072 * 64) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_valid_idx, 131072 * 33) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_valid_count, 4) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_out, 4 + 64 * sizeof(FoundRec)) != CUDA_SUCCESS ||
        e->a.cuMemAlloc_v2(&e->d_addrs, 131072 * 48) != CUDA_SUCCESS) {
        error = "cuMemAlloc failed"; delete e; return NULL;
    }
    e->a.cuMemcpyHtoD_v2(e->d_words, TNV_WORDS, TNV_WORD_COUNT * 9);
    e->a.cuMemcpyHtoD_v2(e->d_suffix, cfg.suffix, 64);
    unsigned char ch[32]; hex32(CODE_HEX[cfg.wallet_version], ch);
    e->a.cuMemcpyHtoD_v2(e->d_codehash, ch, 32);
        e->ent_cap = 262144 * 64;
    char nbuf[64]; snprintf(nbuf, sizeof(nbuf), "CUDA sm_%d%d", e->cc_major, e->cc_minor);
    e->name = nbuf;
    name = e->name;
    return e;
}

static void verify_found(CudaEngine* e, CpuShared& sh, const FoundRec* recs, int n, ChaCha20& rng) {
    for (int i = 0; i < n; ++i) {
        const FoundRec& r = recs[i];
        WalletCandidate c;
        bool ok = false;
        if (e->cfg.mnemonic_scheme == 0 || e->cfg.wallet_version == 2) {
            ok = derive_bip39_12(r.entropy, (WalletVersion)e->cfg.wallet_version, e->cfg.bounceable != 0, c);
            if (ok && e->cfg.wallet_version == 2 && e->cfg.mnemonic_scheme == 1) {
                unsigned char sig[16]; rng.fill(sig, 16);
                WalletCandidate full;
                if (!derive_tg_24(r.entropy, sig, e->cfg.bounceable != 0, full)) continue;
                c = full;
            }
        } else {
            uint16_t idx[24]; ton24_indices_from_bytes(r.idx, idx);
            ok = derive_ton24(idx, (WalletVersion)e->cfg.wallet_version, e->cfg.bounceable != 0, c);
        }
        if (!ok) continue;
        if (!suffix_match(c.address, e->cfg.suffix, e->cfg.case_sensitive != 0)) continue;
        sh.push(c, 1, sh.attempts.load());
    }
}

void cuda_run(CudaEngine* e, CpuShared& sh) {
    if (e->a.cuCtxSetCurrent) e->a.cuCtxSetCurrent(e->ctx);
    unsigned char seedbuf[44];
    if (BCryptGenRandom(NULL, seedbuf, sizeof(seedbuf), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        for (auto& b : seedbuf) b = (unsigned char)GetTickCount64();
    uint64_t ctr; memcpy(&ctr, seedbuf + 32, 8);
    ChaCha20 rng; rng.init(seedbuf, seedbuf + 32, ctr);
    std::vector<unsigned char> host(e->ent_cap);
    std::vector<char> abuf(131072 * 48);
    int version = e->cfg.wallet_version;
    int bounce = e->cfg.bounceable;
    int cs = e->cfg.case_sensitive;
    int depth = CODE_DEPTH[version];
    while (!sh.stop.load(std::memory_order_relaxed)) {
        if (e->cfg.mnemonic_scheme == 0 || version == 2) {
            int count = e->batch12;
            int bytes = count * 16;
            rng.fill(host.data(), bytes);
            e->a.cuMemcpyHtoD_v2(e->d_ent, host.data(), bytes);
            CUdeviceptr d_ent = e->d_ent, d_words = e->d_words, d_codehash = e->d_codehash, d_addrs = e->d_addrs;
            int c = count;
            void* args[] = { &d_ent, &c, &d_addrs, &d_words, &d_codehash, &version, &bounce };
            unsigned grid = (unsigned)((count + 255) / 256);
            CUresult rr = e->a.cuLaunchKernel(e->f12, grid, 1, 1, 256, 1, 1, 0, e->stream, args, NULL);
            if (rr != CUDA_SUCCESS) return;
            if (!wait_stream(e->a, e->stream, 20000)) return;
            e->a.cuMemcpyDtoH_v2(abuf.data(), e->d_addrs, count * 48);
            for (int i = 0; i < count; ++i) {
                std::string a(abuf.data() + i * 48, 48);
                if (!suffix_match(a, e->cfg.suffix, cs != 0)) continue;
                WalletCandidate cand;
                if (!derive_bip39_12(host.data() + i * 16, (WalletVersion)version, bounce != 0, cand)) continue;
                if (cand.address != a) continue;
                if (version == 2 && e->cfg.mnemonic_scheme == 1) {
                    unsigned char sig[16]; rng.fill(sig, 16);
                    WalletCandidate full;
                    if (!derive_tg_24(host.data() + i * 16, sig, bounce != 0, full)) continue;
                    cand = full;
                }
                sh.push(cand, 1, sh.attempts.load());
            }
            sh.attempts.fetch_add(count);
            sh.candidates.fetch_add(count);
        } else {
            int count = e->batch24;
            const int TARGET = 16384;
            std::vector<unsigned char> host_raw(count * 33);
            e->a.cuMemsetD32_v2(e->d_valid_count, 0, 1);
            int rounds = 0, total_valid = 0;
            while (!sh.stop.load() && total_valid < TARGET && rounds < 64) {
                rng.fill(host_raw.data(), count * 33);
                e->a.cuMemcpyHtoD_v2(e->d_raw, host_raw.data(), count * 33);
                CUdeviceptr d_raw = e->d_raw, d_words = e->d_words, d_ve = e->d_valid_ent, d_vi = e->d_valid_idx, d_vc = e->d_valid_count;
                int c = count;
                void* argsA[] = { &d_raw, &c, &d_words, &d_ve, &d_vi, &d_vc };
                unsigned grid = (unsigned)((count + 255) / 256);
                CUresult rr = e->a.cuLaunchKernel(e->fa, grid, 1, 1, 256, 1, 1, 0, e->stream, argsA, NULL);
                if (rr != CUDA_SUCCESS) return;
                if (!wait_stream(e->a, e->stream, 20000)) return;
                unsigned vcnow = 0; e->a.cuMemcpyDtoH_v2(&vcnow, e->d_valid_count, 4);
                total_valid = (int)vcnow;
                sh.attempts.fetch_add((unsigned long long)count);
                ++rounds;
            }
            if (total_valid <= 0) continue;
            unsigned vc = (unsigned)total_valid; if (vc > 131072) vc = 131072;
            std::vector<unsigned char> vidx(vc * 33);
            e->a.cuMemcpyDtoH_v2(vidx.data(), e->d_valid_idx, vc * 33);
            CUdeviceptr d_ve = e->d_valid_ent, d_vi = e->d_valid_idx, d_addrs = e->d_addrs, d_words = e->d_words, d_codehash = e->d_codehash;
            int vci = (int)vc;
            void* argsB[] = { &d_ve, &d_vi, &vci, &d_addrs, &d_words, &d_codehash, &version, &bounce };
            unsigned gridB = (unsigned)((vc + 255) / 256);
            CUresult rr = e->a.cuLaunchKernel(e->fb, gridB, 1, 1, 256, 1, 1, 0, e->stream, argsB, NULL);
            if (rr != CUDA_SUCCESS) return;
            if (!wait_stream(e->a, e->stream, 20000)) return;
            sh.candidates.fetch_add(vc);
            e->a.cuMemcpyDtoH_v2(abuf.data(), e->d_addrs, vc * 48);
            for (unsigned i = 0; i < vc; ++i) {
                std::string a(abuf.data() + i * 48, 48);
                if (!suffix_match(a, e->cfg.suffix, cs != 0)) continue;
                uint16_t idx[24]; ton24_indices_from_bytes(vidx.data() + i * 33, idx);
                WalletCandidate cand;
                if (!derive_ton24(idx, (WalletVersion)version, bounce != 0, cand)) continue;
                if (cand.address != a) continue;
                sh.push(cand, 1, sh.attempts.load());
            }
        }
    }
}

void cuda_destroy(CudaEngine* e) {
    if (!e) return;
    if (e->ctx) {
        if (e->mod) e->a.cuModuleUnload(e->mod);
        if (e->stream) e->a.cuStreamDestroy_v2(e->stream);
        if (e->d_words) e->a.cuMemFree_v2(e->d_words);
        if (e->d_suffix) e->a.cuMemFree_v2(e->d_suffix);
        if (e->d_codehash) e->a.cuMemFree_v2(e->d_codehash);
        if (e->d_ent) e->a.cuMemFree_v2(e->d_ent);
        if (e->d_raw) e->a.cuMemFree_v2(e->d_raw);
        if (e->d_valid_ent) e->a.cuMemFree_v2(e->d_valid_ent);
        if (e->d_valid_idx) e->a.cuMemFree_v2(e->d_valid_idx);
        if (e->d_valid_count) e->a.cuMemFree_v2(e->d_valid_count);
        if (e->d_out) e->a.cuMemFree_v2(e->d_out);
        if (e->d_addrs) e->a.cuMemFree_v2(e->d_addrs);
        e->a.cuCtxDestroy_v2(e->ctx);
    }
    delete e;
}

bool cuda_probe(std::string& name, std::string& error) {
    Cudart a; if (!a.load()) { error = "nvcuda.dll ?? ??????"; return false; }
    CUresult r = a.cuInit(0);
    if (r != CUDA_SUCCESS) { error = errstr(a, r); return false; }
    int n = 0; if (a.cuDeviceGetCount(&n) != CUDA_SUCCESS || n <= 0) { error = "??? CUDA-?????????"; return false; }
    CUdevice d; a.cuDeviceGet(&d, 0);
    int maj = 0, min = 0; a.cuDeviceGetAttribute(&maj, CU_ATTR_CC_MAJOR, d); a.cuDeviceGetAttribute(&min, CU_ATTR_CC_MINOR, d);
    char b[64]; snprintf(b, sizeof(b), "CUDA sm_%d%d", maj, min); name = b;
    return true;
}



bool cuda_debug_selftest(std::string& report) {
    TnvConfig cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.wallet_version = 1; cfg.mnemonic_scheme = 0; cfg.bounceable = 0; cfg.compute_mode = 2;
    cfg.case_sensitive = 1; strcpy(cfg.suffix, "AB");
    std::string name, err;
    CudaEngine* e = cuda_create(cfg, name, err);
    if (!e) { report = "CUDA create failed: " + err; return false; }
    CUfunction f = NULL;
    CUresult r = e->a.cuModuleGetFunction(&f, e->mod, "kn_selftest");
    if (r != CUDA_SUCCESS) { report = "kn_selftest missing"; cuda_destroy(e); return false; }
    CUdeviceptr d_out = 0;
    CUdeviceptr d_addrs = 0;
    if (e->a.cuMemAlloc_v2(&d_out, 512) != CUDA_SUCCESS) { report = "cuMemAlloc debug failed"; cuda_destroy(e); return false; }
    CUdeviceptr d_words = e->d_words, d_codehash = e->d_codehash, d_o = d_out;
    int depth = CODE_DEPTH[1];
    void* args[] = { &d_words, &d_codehash, &depth, &d_o };
    r = e->a.cuLaunchKernel(f, 1, 1, 1, 1, 1, 1, 0, e->stream, args, NULL);
    if (r != CUDA_SUCCESS) { report = "debug launch failed"; cuda_destroy(e); return false; }
    if (!wait_stream(e->a, e->stream, 20000)) { report = "debug wait failed"; cuda_destroy(e); return false; }
    unsigned char out[512]; memset(out, 0, sizeof(out));
    e->a.cuMemcpyDtoH_v2(out, d_out, 512);
    e->a.cuMemFree_v2(d_out);
    bool ok = true;
    auto check = [&](const char* label, const unsigned char* a, const unsigned char* b, int n) {
        bool same = memcmp(a, b, n) == 0;
        report += std::string(same ? "PASS " : "FAIL ") + label + "\n";
        if (!same) ok = false;
    };
    unsigned char buf[512];
    tnv::sha512("abc", 3, buf); check("gpu sha512 abc", out, buf, 64);
    tnv::sha256("abc", 3, buf); check("gpu sha256 abc", out + 64, buf, 32);
    tnv::pbkdf2_sha512((const unsigned char*)"password", 8, (const unsigned char*)"salt", 4, 1, buf, 64);
    check("gpu pbkdf2", out + 96, buf, 64);
    unsigned char z[16]; memset(z, 0, 16);
    std::string phrase; tnv::bip39_entropy_to_phrase(z, phrase);
    check("gpu phrase", out + 160, (const unsigned char*)phrase.data(), (int)phrase.size());
    unsigned char seed[64]; tnv::bip39_phrase_to_seed(phrase, seed); check("gpu seed", out + 288, seed, 64);
    unsigned char key[32]; tnv::slip10_ed25519(seed, key); check("gpu slip key", out + 352, key, 32);
    unsigned char pub[32]; tnv::ed25519_pub(key, pub); check("gpu pubkey", out + 384, pub, 32);
    std::string addr = tnv::wallet_address(pub, tnv::WALLET_W5R1, false);
    check("gpu address", out + 416, (const unsigned char*)addr.data(), 48);
    cuda_destroy(e);
    return ok;
}
















