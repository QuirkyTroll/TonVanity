#include "backend_opencl.h"
#include "wordlist_en.h"
#include "ton_crypto.h"
#include "opencl_src.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

using namespace tnv;

typedef void* cl_platform_id;
typedef void* cl_device_id;
typedef void* cl_context;
typedef void* cl_command_queue;
typedef void* cl_mem;
typedef void* cl_program;
typedef void* cl_kernel;
typedef void* cl_event;
typedef int cl_int;
typedef unsigned int cl_uint;
typedef unsigned long long cl_ulong;
typedef unsigned long long cl_bitfield;
typedef unsigned long long cl_device_type;
typedef unsigned long long cl_mem_flags;
typedef unsigned int cl_bool;
typedef size_t cl_size_t;

#define CL_SUCCESS 0
#define CL_DEVICE_TYPE_GPU (1ULL << 2)
#define CL_DEVICE_TYPE_ALL 0xFFFFFFFFULL
#define CL_DEVICE_NAME 0x102B
#define CL_DEVICE_MAX_COMPUTE_UNITS 0x1002
#define CL_DEVICE_MAX_WORK_GROUP_SIZE 0x1004
#define CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define CL_DEVICE_VERSION 0x102F
#define CL_PLATFORM_NAME 0x0902
#define CL_PLATFORM_VENDOR 0x0903
#define CL_MEM_READ_WRITE (1ULL << 0)
#define CL_MEM_WRITE_ONLY (1ULL << 1)
#define CL_MEM_READ_ONLY (1ULL << 2)
#define CL_MEM_COPY_HOST_PTR (1ULL << 5)
#define CL_PROGRAM_BUILD_LOG 0x1183
#define CL_PROGRAM_NUM_DEVICES 0x1162
#define CL_PROGRAM_BINARY_SIZES 0x1165
#define CL_PROGRAM_BINARIES 0x1166
#define CL_KERNEL_WORK_GROUP_SIZE 0x11B0
#define CL_TRUE 1
#define CL_FALSE 0

static const char* CODE_HEX[3] = {
    "feb5ff6820e2ff0d9483e7e0d62c817d846789fb4ae580c878866d959dabd5c0",
    "20834b7b72b112147e1b2fb457b84e74d1a30f04f737d4f62a668e9552d2b72f",
    "9149ae51c1e4689710cebf7830297b16acfbadb363a920a537893e7ffeeca768"
};
static const int CODE_DEPTH[3] = {7, 6, 0};

struct OclApi {
    HMODULE h = NULL;
    cl_int (WINAPI *clGetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
    cl_int (WINAPI *clGetPlatformInfo)(cl_platform_id, cl_uint, cl_size_t, void*, cl_size_t*);
    cl_int (WINAPI *clGetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
    cl_int (WINAPI *clGetDeviceInfo)(cl_device_id, cl_uint, cl_size_t, void*, cl_size_t*);
    cl_context (WINAPI *clCreateContext)(const void*, cl_uint, const cl_device_id*, void*, void*, cl_int*);
    cl_command_queue (WINAPI *clCreateCommandQueue)(cl_context, cl_device_id, cl_bitfield, cl_int*);
    cl_command_queue (WINAPI *clCreateCommandQueueWithProperties)(cl_context, cl_device_id, const void*, cl_int*);
    cl_mem (WINAPI *clCreateBuffer)(cl_context, cl_mem_flags, cl_size_t, void*, cl_int*);
    cl_program (WINAPI *clCreateProgramWithSource)(cl_context, cl_uint, const char**, const cl_size_t*, cl_int*);
    cl_program (WINAPI *clCreateProgramWithBinary)(cl_context, cl_uint, const cl_device_id*, const cl_size_t*, const unsigned char**, cl_int*, cl_int*);
    cl_int (WINAPI *clGetProgramInfo)(cl_program, cl_uint, cl_size_t, void*, cl_size_t*);
    cl_int (WINAPI *clBuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*, void*, void*);
    cl_int (WINAPI *clGetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, cl_size_t, void*, cl_size_t*);
    cl_kernel (WINAPI *clCreateKernel)(cl_program, const char*, cl_int*);
    cl_int (WINAPI *clSetKernelArg)(cl_kernel, cl_uint, cl_size_t, const void*);
    cl_int (WINAPI *clEnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const cl_size_t*, const cl_size_t*, const cl_size_t*, cl_uint, const cl_event*, cl_event*);
    cl_int (WINAPI *clEnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, cl_size_t, cl_size_t, void*, cl_uint, const cl_event*, cl_event*);
    cl_int (WINAPI *clEnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, cl_size_t, cl_size_t, const void*, cl_uint, const cl_event*, cl_event*);
    cl_int (WINAPI *clFinish)(cl_command_queue);
    cl_int (WINAPI *clGetKernelWorkGroupInfo)(cl_kernel, cl_device_id, cl_uint, cl_size_t, void*, cl_size_t*);
    cl_int (WINAPI *clReleaseMemObject)(cl_mem);
    cl_int (WINAPI *clReleaseKernel)(cl_kernel);
    cl_int (WINAPI *clReleaseProgram)(cl_program);
    cl_int (WINAPI *clReleaseCommandQueue)(cl_command_queue);
    cl_int (WINAPI *clReleaseContext)(cl_context);
    bool load() {
        h = LoadLibraryA("OpenCL.dll");
        if (!h) return false;
        #define L(n) n = (decltype(n))GetProcAddress(h, #n)
        L(clGetPlatformIDs); L(clGetPlatformInfo); L(clGetDeviceIDs); L(clGetDeviceInfo);
        L(clCreateContext); L(clCreateCommandQueue); L(clCreateCommandQueueWithProperties);
        L(clCreateBuffer); L(clCreateProgramWithSource); L(clCreateProgramWithBinary);
        L(clGetProgramInfo); L(clBuildProgram);
        L(clGetProgramBuildInfo); L(clCreateKernel); L(clSetKernelArg);
        L(clEnqueueNDRangeKernel); L(clEnqueueReadBuffer); L(clEnqueueWriteBuffer);
        L(clFinish); L(clGetKernelWorkGroupInfo);
        L(clReleaseMemObject); L(clReleaseKernel); L(clReleaseProgram);
        L(clReleaseCommandQueue); L(clReleaseContext);
        #undef L
        return clGetPlatformIDs && clGetDeviceIDs && clCreateContext && clCreateBuffer &&
               clCreateProgramWithSource && clBuildProgram && clCreateKernel && clSetKernelArg &&
               clEnqueueNDRangeKernel && clEnqueueReadBuffer && clEnqueueWriteBuffer && clFinish;
    }
};

struct OclDevice {
    cl_platform_id plat = NULL;
    cl_device_id dev = NULL;
    std::string name;
    std::string vendor;
    std::string version;
    cl_uint cu = 0;
};

struct OpenCLEngine {
    OclApi a;
    cl_device_id dev = NULL;
    cl_context ctx = NULL;
    cl_command_queue q = NULL;
    cl_program prog = NULL;
    cl_kernel f12 = NULL, fa = NULL, fb = NULL;
    cl_mem d_words = NULL, d_codehash = NULL, d_ent = NULL, d_raw = NULL;
    cl_mem d_valid_ent = NULL, d_valid_idx = NULL, d_valid_count = NULL, d_addrs = NULL;
    TnvConfig cfg;
    std::string name;
    size_t local = 256;
    int batch12 = 16384;
    int batch24 = 65536;
    int target24 = 16384;
};

static std::string get_platform_str(OclApi& a, cl_platform_id p, cl_uint param) {
    cl_size_t n = 0;
    if (a.clGetPlatformInfo(p, param, 0, NULL, &n) != CL_SUCCESS || n == 0) return std::string();
    std::string s(n, '\0');
    if (a.clGetPlatformInfo(p, param, n, &s[0], NULL) != CL_SUCCESS) return std::string();
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static std::string get_device_str(OclApi& a, cl_device_id d, cl_uint param) {
    cl_size_t n = 0;
    if (a.clGetDeviceInfo(d, param, 0, NULL, &n) != CL_SUCCESS || n == 0) return std::string();
    std::string s(n, '\0');
    if (a.clGetDeviceInfo(d, param, n, &s[0], NULL) != CL_SUCCESS) return std::string();
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static std::string lower_ascii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

static bool contains_nocase(const std::string& s, const char* needle) {
    return lower_ascii(s).find(needle) != std::string::npos;
}

static std::vector<OclDevice> enumerate(OclApi& a) {
    std::vector<OclDevice> out;
    cl_uint np = 0;
    if (a.clGetPlatformIDs(0, NULL, &np) != CL_SUCCESS || np == 0) return out;
    std::vector<cl_platform_id> plats(np);
    if (a.clGetPlatformIDs(np, plats.data(), NULL) != CL_SUCCESS) return out;
    char envb[8];
    bool allow_nvidia = GetEnvironmentVariableA("TNV_OPENCL_ALLOW_NVIDIA", envb, sizeof(envb)) > 0;
    for (cl_uint pi = 0; pi < np; ++pi) {
        std::string pname = get_platform_str(a, plats[pi], CL_PLATFORM_NAME);
        std::string pvendor = get_platform_str(a, plats[pi], CL_PLATFORM_VENDOR);
        // The approved plan keeps NVIDIA on CUDA: NVIDIA's OpenCL path is
        // slower for 64-bit SHA-512 and must not be selected by fallback.
        if (!allow_nvidia && (contains_nocase(pname, "nvidia") || contains_nocase(pvendor, "nvidia"))) continue;
        for (int pass = 0; pass < 2; ++pass) {
            cl_device_type type = pass == 0 ? CL_DEVICE_TYPE_GPU : CL_DEVICE_TYPE_ALL;
            cl_uint nd = 0;
            if (a.clGetDeviceIDs(plats[pi], type, 0, NULL, &nd) != CL_SUCCESS || nd == 0) continue;
            std::vector<cl_device_id> devs(nd);
            if (a.clGetDeviceIDs(plats[pi], type, nd, devs.data(), NULL) != CL_SUCCESS) continue;
            for (cl_uint di = 0; di < nd; ++di) {
                OclDevice d;
                d.plat = plats[pi]; d.dev = devs[di];
                d.name = get_device_str(a, devs[di], CL_DEVICE_NAME);
                d.version = get_device_str(a, devs[di], CL_DEVICE_VERSION);
                d.vendor = pvendor;
                a.clGetDeviceInfo(devs[di], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(d.cu), &d.cu, NULL);
                // Skip devices already added by the GPU pass.
                bool dup = false;
                for (auto& x : out) if (x.dev == d.dev) { dup = true; break; }
                if (!dup) out.push_back(d);
            }
            if (pass == 0 && !out.empty()) { /* GPU devices found for this platform */ }
        }
    }
    return out;
}

static OclDevice* pick_device(std::vector<OclDevice>& devs) {
    if (devs.empty()) return NULL;
    int want = -1;
    char env[32];
    DWORD n = GetEnvironmentVariableA("TNV_OPENCL_DEVICE", env, sizeof(env));
    if (n > 0) want = atoi(env);
    if (want >= 0 && want < (int)devs.size()) return &devs[want];
    return &devs[0];
}

static std::string build_log(OclApi& a, cl_program p, cl_device_id d) {
    if (!a.clGetProgramBuildInfo) return std::string();
    cl_size_t n = 0;
    a.clGetProgramBuildInfo(p, d, CL_PROGRAM_BUILD_LOG, 0, NULL, &n);
    if (n == 0) return std::string();
    std::string s(n, '\0');
    a.clGetProgramBuildInfo(p, d, CL_PROGRAM_BUILD_LOG, n, &s[0], NULL);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static void hex32(const char* s, unsigned char out[32]) {
    for (int i = 0; i < 32; ++i) {
        unsigned v; sscanf(s + 2 * i, "%2x", &v); out[i] = (unsigned char)v;
    }
}

static uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static bool ensure_dir(const std::string& d) {
    if (CreateDirectoryA(d.c_str(), NULL)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::string cache_path(uint64_t key) {
    std::string dir;
    char base[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH);
    if (n > 0) {
        std::string root = std::string(base) + "\\TonVanity";
        if (ensure_dir(root)) {
            std::string sub = root + "\\opencl";
            if (ensure_dir(sub)) dir = sub;
        }
    }
    if (dir.empty()) {
        char exe[MAX_PATH] = {0};
        if (GetModuleFileNameA(NULL, exe, MAX_PATH) > 0) {
            std::string p(exe);
            size_t slash = p.find_last_of("\\/");
            if (slash != std::string::npos) p.resize(slash);
            p += "\\opencl_cache";
            if (ensure_dir(p)) dir = p;
        }
    }
    if (dir.empty()) dir = ".";
    char name[32]; snprintf(name, sizeof(name), "\\%016llx.bin", (unsigned long long)key);
    return dir + name;
}

static bool read_all(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

static void write_all(const std::string& path, const std::vector<unsigned char>& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

static bool build_source(OpenCLEngine* e, std::string& error) {
    const char* src = OPENCL_SRC;
    cl_size_t len = sizeof(OPENCL_SRC) - 1;
    cl_int err = 0;
    e->prog = e->a.clCreateProgramWithSource(e->ctx, 1, &src, &len, &err);
    if (!e->prog) { error = "clCreateProgramWithSource failed"; return false; }
    err = e->a.clBuildProgram(e->prog, 1, &e->dev, "-cl-std=CL1.2", NULL, NULL);
    if (err != CL_SUCCESS) {
        std::string log = build_log(e->a, e->prog, e->dev);
        if (log.size() > 1200) log = log.substr(0, 1200) + "...";
        error = "OpenCL build failed: " + log;
        e->a.clReleaseProgram(e->prog);
        e->prog = NULL;
        return false;
    }
    return true;
}

static void save_program(OpenCLEngine* e, const std::string& path) {
    if (!e->a.clGetProgramInfo) return;
    cl_size_t bs = 0;
    if (e->a.clGetProgramInfo(e->prog, CL_PROGRAM_BINARY_SIZES, sizeof(bs), &bs, NULL) != CL_SUCCESS || bs == 0) return;
    std::vector<unsigned char> bin(bs);
    unsigned char* p = bin.data();
    if (e->a.clGetProgramInfo(e->prog, CL_PROGRAM_BINARIES, sizeof(p), &p, NULL) != CL_SUCCESS) return;
    write_all(path, bin);
}

OpenCLEngine* opencl_create(const TnvConfig& cfg, std::string& name, std::string& error) {
    OpenCLEngine* e = new OpenCLEngine();
    e->cfg = cfg;
    if (!e->a.load()) { error = "OpenCL.dll not found (OpenCL driver required)"; delete e; return NULL; }
    std::vector<OclDevice> devs = enumerate(e->a);
    OclDevice* d = pick_device(devs);
    if (!d) { error = "No OpenCL devices found"; delete e; return NULL; }
    e->dev = d->dev;
    cl_int err = 0;
    e->ctx = e->a.clCreateContext(NULL, 1, &e->dev, NULL, NULL, &err);
    if (!e->ctx) { error = "clCreateContext failed"; delete e; return NULL; }
    if (e->a.clCreateCommandQueue) e->q = e->a.clCreateCommandQueue(e->ctx, e->dev, 0, &err);
    else if (e->a.clCreateCommandQueueWithProperties) e->q = e->a.clCreateCommandQueueWithProperties(e->ctx, e->dev, NULL, &err);
    if (!e->q) { error = "clCreateCommandQueue failed"; e->a.clReleaseContext(e->ctx); delete e; return NULL; }

    uint64_t h = fnv1a(OPENCL_SRC, sizeof(OPENCL_SRC) - 1);
    h = fnv1a(d->name.data(), d->name.size(), h);
    h = fnv1a(d->version.data(), d->version.size(), h);
    h = fnv1a("-cl-std=CL1.2", sizeof("-cl-std=CL1.2") - 1, h);
    std::string cache = cache_path(h);
    char dbgenv[8];
    bool dbg = GetEnvironmentVariableA("TNV_OCL_DEBUG", dbgenv, sizeof(dbgenv)) > 0;
    ULONGLONG t0 = GetTickCount64();
    bool loaded_binary = false;
    {
        std::vector<unsigned char> bin;
        bool got = e->a.clCreateProgramWithBinary && read_all(cache, bin);
        if (dbg) fprintf(stderr, "[ocl] cache read=%d size=%llu\n", (int)got, (unsigned long long)bin.size());
        if (got) {
            cl_size_t blen = bin.size();
            const unsigned char* bp = bin.data();
            cl_int bstatus = -1;
            e->prog = e->a.clCreateProgramWithBinary(e->ctx, 1, &e->dev, &blen, &bp, &bstatus, &err);
            if (dbg) fprintf(stderr, "[ocl] binary prog=%p status=%d err=%d\n", (void*)e->prog, (int)bstatus, (int)err);
            if (e->prog && bstatus == CL_SUCCESS) loaded_binary = true;
            else { if (e->prog) { e->a.clReleaseProgram(e->prog); e->prog = NULL; } }
        }
    }
    if (dbg) fprintf(stderr, "[ocl] cache=%s loaded=%d t=%llums\n", cache.c_str(), (int)loaded_binary, (unsigned long long)(GetTickCount64() - t0));
    if (!loaded_binary && !build_source(e, error)) {
        e->a.clReleaseCommandQueue(e->q); e->a.clReleaseContext(e->ctx);
        delete e; return NULL;
    }
    if (dbg) fprintf(stderr, "[ocl] program ready t=%llums\n", (unsigned long long)(GetTickCount64() - t0));
    e->f12 = e->a.clCreateKernel(e->prog, "kn_bip39_12", &err);
    e->fa = e->a.clCreateKernel(e->prog, "kn_ton24_a", &err);
    e->fb = e->a.clCreateKernel(e->prog, "kn_ton24_b", &err);
    if ((!e->f12 || !e->fa || !e->fb) && loaded_binary) {
        // A cached binary from another driver revision can be unusable.
        if (e->f12) { e->a.clReleaseKernel(e->f12); e->f12 = NULL; }
        if (e->fa) { e->a.clReleaseKernel(e->fa); e->fa = NULL; }
        if (e->fb) { e->a.clReleaseKernel(e->fb); e->fb = NULL; }
        e->a.clReleaseProgram(e->prog); e->prog = NULL;
        DeleteFileA(cache.c_str());
        if (!build_source(e, error)) {
            e->a.clReleaseCommandQueue(e->q); e->a.clReleaseContext(e->ctx);
            delete e; return NULL;
        }
        e->f12 = e->a.clCreateKernel(e->prog, "kn_bip39_12", &err);
        e->fa = e->a.clCreateKernel(e->prog, "kn_ton24_a", &err);
        e->fb = e->a.clCreateKernel(e->prog, "kn_ton24_b", &err);
    }
    if (!e->f12 || !e->fa || !e->fb) {
        error = "OpenCL kernels not found";
        e->a.clReleaseKernel(e->f12); e->a.clReleaseKernel(e->fa); e->a.clReleaseKernel(e->fb);
        e->a.clReleaseProgram(e->prog); e->a.clReleaseCommandQueue(e->q); e->a.clReleaseContext(e->ctx);
        delete e; return NULL;
    }
    if (!loaded_binary) save_program(e, cache);
    size_t wg = 256;
    if (e->a.clGetKernelWorkGroupInfo) {
        size_t kwg = 0;
        if (e->a.clGetKernelWorkGroupInfo(e->f12, e->dev, CL_KERNEL_WORK_GROUP_SIZE, sizeof(kwg), &kwg, NULL) == CL_SUCCESS && kwg > 0)
            wg = kwg < 256 ? kwg : 256;
    }
    e->local = wg ? wg : 64;

    unsigned char code[32]; hex32(CODE_HEX[cfg.wallet_version], code);
    e->d_words = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, TNV_WORD_COUNT * 9, (void*)TNV_WORDS, &err);
    e->d_codehash = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32, code, &err);
    e->d_ent = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_ONLY, (size_t)e->batch12 * 16, NULL, &err);
    e->d_raw = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_ONLY, (size_t)e->batch24 * 33, NULL, &err);
    e->d_valid_ent = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_WRITE, 131072 * 64, NULL, &err);
    e->d_valid_idx = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_WRITE, 131072 * 33, NULL, &err);
    e->d_valid_count = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_WRITE, 4, NULL, &err);
    e->d_addrs = e->a.clCreateBuffer(e->ctx, CL_MEM_READ_WRITE, 131072 * 48, NULL, &err);
    if (!e->d_words || !e->d_codehash || !e->d_ent || !e->d_raw || !e->d_valid_ent ||
        !e->d_valid_idx || !e->d_valid_count || !e->d_addrs) {
        error = "OpenCL buffer allocation failed";
        opencl_destroy(e);
        return NULL;
    }
    e->name = "OpenCL " + d->name;
    if (contains_nocase(d->name, "intel")) e->target24 = 1024;
    else if (contains_nocase(d->name, "amd") || contains_nocase(d->name, "radeon")) e->target24 = 4096;
    name = e->name;
    return e;
}

static bool set_mem(OpenCLEngine* e, cl_kernel k, int idx, cl_mem m) {
    return e->a.clSetKernelArg(k, (cl_uint)idx, sizeof(cl_mem), &m) == CL_SUCCESS;
}
static bool set_int(OpenCLEngine* e, cl_kernel k, int idx, int v) {
    return e->a.clSetKernelArg(k, (cl_uint)idx, sizeof(int), &v) == CL_SUCCESS;
}
static bool launch(OpenCLEngine* e, cl_kernel k, int count) {
    size_t global = ((size_t)count + e->local - 1) / e->local * e->local;
    cl_int rc = e->a.clEnqueueNDRangeKernel(e->q, k, 1, NULL, &global, &e->local, 0, NULL, NULL);
    if (rc != CL_SUCCESS) return false;
    return e->a.clFinish(e->q) == CL_SUCCESS;
}

static void verify_found(OpenCLEngine* e, CpuShared& sh, const std::vector<unsigned char>& entropy16,
                         const std::vector<unsigned char>& idxbuf, const char* addrs, int i,
                         ChaCha20& rng) {
    std::string a(addrs + (size_t)i * 48, 48);
    if (!suffix_match(a, e->cfg.suffix, e->cfg.case_sensitive != 0)) return;
    WalletCandidate c;
    bool ok = false;
    if (e->cfg.mnemonic_scheme == 0 || e->cfg.wallet_version == 2) {
        ok = derive_bip39_12(entropy16.data() + (size_t)i * 16, (WalletVersion)e->cfg.wallet_version,
                             e->cfg.bounceable != 0, c);
        if (ok && e->cfg.wallet_version == 2 && e->cfg.mnemonic_scheme == 1) {
            unsigned char sig[16]; rng.fill(sig, 16);
            WalletCandidate full;
            if (!derive_tg_24(entropy16.data() + (size_t)i * 16, sig, e->cfg.bounceable != 0, full)) return;
            c = full;
        }
    } else {
        uint16_t idx[24]; ton24_indices_from_bytes(idxbuf.data() + (size_t)i * 33, idx);
        ok = derive_ton24(idx, (WalletVersion)e->cfg.wallet_version, e->cfg.bounceable != 0, c);
    }
    if (!ok) return;
    if (c.address != a) return;
    sh.push(c, 2, sh.attempts.load());
}

void opencl_run(OpenCLEngine* e, CpuShared& sh) {
    char dbgenv[8];
    bool dbg = GetEnvironmentVariableA("TNV_OCL_DEBUG", dbgenv, sizeof(dbgenv)) > 0;
    ULONGLONG trun = GetTickCount64();
    unsigned char seedbuf[44];
    if (BCryptGenRandom(NULL, seedbuf, sizeof(seedbuf), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        for (auto& b : seedbuf) b = (unsigned char)GetTickCount64();
    uint64_t ctr; memcpy(&ctr, seedbuf + 32, 8);
    ChaCha20 rng; rng.init(seedbuf, seedbuf + 32, ctr);
    int version = e->cfg.wallet_version;
    int bounce = e->cfg.bounceable;
    std::vector<unsigned char> host((size_t)e->batch12 * 16);
    std::vector<char> abuf(131072 * 48);
    while (!sh.stop.load(std::memory_order_relaxed)) {
        if (e->cfg.mnemonic_scheme == 0 || version == 2) {
            int count = e->batch12;
            int bytes = count * 16;
            rng.fill(host.data(), bytes);
            if (e->a.clEnqueueWriteBuffer(e->q, e->d_ent, CL_TRUE, 0, bytes, host.data(), 0, NULL, NULL) != CL_SUCCESS) return;
            if (!set_mem(e, e->f12, 0, e->d_ent) || !set_int(e, e->f12, 1, count) ||
                !set_mem(e, e->f12, 2, e->d_addrs) || !set_mem(e, e->f12, 3, e->d_words) ||
                !set_mem(e, e->f12, 4, e->d_codehash) || !set_int(e, e->f12, 5, version) ||
                !set_int(e, e->f12, 6, bounce)) return;
            if (!launch(e, e->f12, count)) return;
            if (e->a.clEnqueueReadBuffer(e->q, e->d_addrs, CL_TRUE, 0, (size_t)count * 48, abuf.data(), 0, NULL, NULL) != CL_SUCCESS) return;
            for (int i = 0; i < count; ++i) verify_found(e, sh, host, host, abuf.data(), i, rng);
            sh.attempts.fetch_add(count);
            sh.candidates.fetch_add(count);
        } else {
            int count = e->batch24;
            const int TARGET = e->target24;
            std::vector<unsigned char> host_raw((size_t)count * 33);
            cl_uint zero = 0;
            if (e->a.clEnqueueWriteBuffer(e->q, e->d_valid_count, CL_TRUE, 0, 4, &zero, 0, NULL, NULL) != CL_SUCCESS) return;
            int rounds = 0, total_valid = 0;
            while (!sh.stop.load() && total_valid < TARGET && rounds < 64) {
                rng.fill(host_raw.data(), (size_t)count * 33);
                if (e->a.clEnqueueWriteBuffer(e->q, e->d_raw, CL_TRUE, 0, (size_t)count * 33, host_raw.data(), 0, NULL, NULL) != CL_SUCCESS) return;
                if (!set_mem(e, e->fa, 0, e->d_raw) || !set_int(e, e->fa, 1, count) ||
                    !set_mem(e, e->fa, 2, e->d_words) || !set_mem(e, e->fa, 3, e->d_valid_ent) ||
                    !set_mem(e, e->fa, 4, e->d_valid_idx) || !set_mem(e, e->fa, 5, e->d_valid_count)) return;
                if (!launch(e, e->fa, count)) return;
                cl_uint vcnow = 0;
                if (e->a.clEnqueueReadBuffer(e->q, e->d_valid_count, CL_TRUE, 0, 4, &vcnow, 0, NULL, NULL) != CL_SUCCESS) return;
                total_valid = (int)vcnow;
                sh.attempts.fetch_add(count);
                if (dbg) fprintf(stderr, "[ocl] phaseA done t=%llums valid=%d\n", (unsigned long long)(GetTickCount64() - trun), total_valid);
                ++rounds;
            }
            if (total_valid <= 0) continue;
            int vc = total_valid; if (vc > 131072) vc = 131072;
            std::vector<unsigned char> vidx((size_t)vc * 33);
            if (e->a.clEnqueueReadBuffer(e->q, e->d_valid_idx, CL_TRUE, 0, (size_t)vc * 33, vidx.data(), 0, NULL, NULL) != CL_SUCCESS) return;
            if (!set_mem(e, e->fb, 0, e->d_valid_ent) || !set_mem(e, e->fb, 1, e->d_valid_idx) ||
                !set_int(e, e->fb, 2, vc) || !set_mem(e, e->fb, 3, e->d_addrs) ||
                !set_mem(e, e->fb, 4, e->d_words) || !set_mem(e, e->fb, 5, e->d_codehash) ||
                !set_int(e, e->fb, 6, version) || !set_int(e, e->fb, 7, bounce)) return;
            if (dbg) fprintf(stderr, "[ocl] phaseB start t=%llums vc=%d\n", (unsigned long long)(GetTickCount64() - trun), vc);
            if (!launch(e, e->fb, vc)) return;
            sh.candidates.fetch_add(vc);
            if (dbg) fprintf(stderr, "[ocl] phaseB done t=%llums\n", (unsigned long long)(GetTickCount64() - trun));
            if (e->a.clEnqueueReadBuffer(e->q, e->d_addrs, CL_TRUE, 0, (size_t)vc * 48, abuf.data(), 0, NULL, NULL) != CL_SUCCESS) return;
            for (int i = 0; i < vc; ++i) verify_found(e, sh, host, vidx, abuf.data(), i, rng);
        }
    }
    if (dbg) fprintf(stderr, "[ocl] run loop exit t=%llums\n", (unsigned long long)(GetTickCount64() - trun));
}

void opencl_destroy(OpenCLEngine* e) {
    if (!e) return;
    char dbgenv[8];
    bool dbg = GetEnvironmentVariableA("TNV_OCL_DEBUG", dbgenv, sizeof(dbgenv)) > 0;
    ULONGLONG t0 = GetTickCount64();
    if (e->a.clReleaseKernel) {
        if (e->f12) e->a.clReleaseKernel(e->f12);
        if (e->fa) e->a.clReleaseKernel(e->fa);
        if (e->fb) e->a.clReleaseKernel(e->fb);
    }
    if (dbg) fprintf(stderr, "[ocl] kernels released t=%llums\n", (unsigned long long)(GetTickCount64() - t0));
    if (e->a.clReleaseMemObject) {
        if (e->d_words) e->a.clReleaseMemObject(e->d_words);
        if (e->d_codehash) e->a.clReleaseMemObject(e->d_codehash);
        if (e->d_ent) e->a.clReleaseMemObject(e->d_ent);
        if (e->d_raw) e->a.clReleaseMemObject(e->d_raw);
        if (e->d_valid_ent) e->a.clReleaseMemObject(e->d_valid_ent);
        if (e->d_valid_idx) e->a.clReleaseMemObject(e->d_valid_idx);
        if (e->d_valid_count) e->a.clReleaseMemObject(e->d_valid_count);
        if (e->d_addrs) e->a.clReleaseMemObject(e->d_addrs);
    }
    if (dbg) fprintf(stderr, "[ocl] buffers released t=%llums\n", (unsigned long long)(GetTickCount64() - t0));
    if (e->prog) e->a.clReleaseProgram(e->prog);
    if (dbg) fprintf(stderr, "[ocl] program released t=%llums\n", (unsigned long long)(GetTickCount64() - t0));
    if (e->q) e->a.clReleaseCommandQueue(e->q);
    if (e->ctx) e->a.clReleaseContext(e->ctx);
    if (dbg) fprintf(stderr, "[ocl] context released t=%llums\n", (unsigned long long)(GetTickCount64() - t0));
    delete e;
}

bool opencl_probe(std::string& name, std::string& error) {
    OclApi a;
    if (!a.load()) { error = "OpenCL.dll not found"; return false; }
    std::vector<OclDevice> devs = enumerate(a);
    OclDevice* d = pick_device(devs);
    if (!d) { error = "No OpenCL devices found"; return false; }
    name = "OpenCL " + d->name;
    return true;
}
