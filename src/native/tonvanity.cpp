#include "tonvanity.h"
#include "backend_cpu.h"
#include "backend_cuda.h"
#include "backend_opencl.h"
#include "ton_crypto.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <windows.h>

using namespace tnv;

struct Session {
    TnvConfig cfg;
    CpuShared cpu;
    std::vector<std::thread> threads;
    std::atomic<bool> running{false};
    std::chrono::steady_clock::time_point start_time;
    std::string backend_name;
    std::string message;
    int backend = 0;
    int backend_state = 0;
    CudaEngine* cuda = nullptr;
    std::thread cuda_thread;
    bool cuda_ok = false;
    OpenCLEngine* ocl = nullptr;
    std::thread ocl_thread;
    bool ocl_ok = false;
};

static void start_cpu_threads(Session* s, int n) {
    s->threads.reserve(n);
    for (int i = 0; i < n; ++i) s->threads.emplace_back(cpu_worker, std::ref(s->cpu), i);
}

static bool valid_suffix(const char* s) {
    if (!s || !*s) return false;
    size_t n = strlen(s);
    if (n > 48) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static void set_msg(Session* s, const char* fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    s->message = buf;
}

void* tnv_create(const TnvConfig* cfg) {
    if (!cfg) return NULL;
    if (cfg->wallet_version < 0 || cfg->wallet_version > 2) return NULL;
    if (cfg->mnemonic_scheme < 0 || cfg->mnemonic_scheme > 1) return NULL;
    if (cfg->bounceable < 0 || cfg->bounceable > 1) return NULL;
    if (cfg->compute_mode < 0 || cfg->compute_mode > 2) return NULL;
    if (!valid_suffix(cfg->suffix)) return NULL;
    Session* s = new Session();
    s->cfg = *cfg;
    s->backend_state = 0;
    s->message = "?????? ? ??????.";
    return s;
}

int tnv_start(void* handle) {
    Session* s = (Session*)handle;
    if (!s) return -1;
    if (s->running.load()) return 0;
    {
        std::lock_guard<std::mutex> lk(s->cpu.mtx);
        s->cpu.matches.clear();
        s->cpu.seen_addresses.clear();
    }
    s->cpu.cfg = s->cfg;
    s->cpu.stop = false;
    s->cpu.attempts = 0; s->cpu.candidates = 0; s->cpu.found = 0;
    s->backend = 0;
    s->backend_state = 1;
    s->cuda_ok = false;
    s->ocl_ok = false;
    int n = s->cfg.cpu_threads;
    if (n <= 0) n = (int)std::thread::hardware_concurrency();
    if (n <= 0) n = 4;
    if (n > 64) n = 64;
    s->backend_name = "CPU " + std::to_string(n) + "t";
    bool cpu_started = false;

    if (s->cfg.compute_mode != 0) {
        std::string cname, cerr, oname, oerr;
        char envb[16];
        bool force_ocl = GetEnvironmentVariableA("TNV_FORCE_OPENCL", envb, sizeof(envb)) > 0;
        if (!force_ocl) s->cuda = cuda_create(s->cfg, cname, cerr);
        if (s->cuda) {
            s->cuda_ok = true;
            s->backend = 1;
            s->backend_name = cname;
            s->cuda_thread = std::thread(cuda_run, s->cuda, std::ref(s->cpu));
            if (s->cfg.compute_mode == 1) {
                start_cpu_threads(s, n);
                cpu_started = true;
                s->backend_name += " + CPU " + std::to_string(n) + "t";
                set_msg(s, "Поиск: CUDA + CPU (%d потоков).", n);
            } else {
                set_msg(s, "Поиск: CUDA.");
            }
        } else {
            s->ocl = opencl_create(s->cfg, oname, oerr);
            if (s->ocl) {
                s->ocl_ok = true;
                s->backend = 2;
                s->backend_name = oname;
                s->ocl_thread = std::thread(opencl_run, s->ocl, std::ref(s->cpu));
                if (s->cfg.compute_mode == 1) {
                    start_cpu_threads(s, n);
                    cpu_started = true;
                    s->backend_name += " + CPU " + std::to_string(n) + "t";
                    set_msg(s, "Поиск: OpenCL + CPU (%d потоков).", n);
                } else {
                    set_msg(s, "Поиск: OpenCL.");
                }
            } else {
                s->backend_state = 2;
                s->backend_name = "CPU " + std::to_string(n) + "t";
                set_msg(s, "GPU недоступен (CUDA: %s; OpenCL: %s). Fallback на CPU (%d потоков).",
                        cerr.c_str(), oerr.c_str(), n);
                start_cpu_threads(s, n);
                cpu_started = true;
            }
        }
    } else {
        start_cpu_threads(s, n);
        cpu_started = true;
        set_msg(s, "Поиск на CPU (%d потоков).", n);
    }
    (void)cpu_started;
    s->start_time = std::chrono::steady_clock::now();
    s->running.store(true);
    return 0;
}

void tnv_stop(void* handle) {
    Session* s = (Session*)handle;
    if (!s) return;
    s->cpu.stop.store(true);
    for (auto& t : s->threads) if (t.joinable()) t.join();
    s->threads.clear();
    if (s->cuda_thread.joinable()) s->cuda_thread.join();
    if (s->cuda) { cuda_destroy(s->cuda); s->cuda = nullptr; }
    s->cuda_ok = false;
    if (s->ocl_thread.joinable()) s->ocl_thread.join();
    if (s->ocl) { opencl_destroy(s->ocl); s->ocl = nullptr; }
    s->ocl_ok = false;
    s->running.store(false);
}

int tnv_poll(void* handle, TnvStats* stats, TnvMatch* matches, int max_matches) {
    Session* s = (Session*)handle;
    if (!s || !stats) return 0;
    int count = 0;
    if (matches && max_matches > 0) {
        std::lock_guard<std::mutex> lk(s->cpu.mtx);
        while (count < max_matches && !s->cpu.matches.empty()) {
            matches[count++] = s->cpu.matches.front();
            s->cpu.matches.pop_front();
        }
    }
    memset(stats, 0, sizeof(*stats));
    stats->attempts = s->cpu.attempts.load();
    stats->candidates = s->cpu.candidates.load();
    stats->found = s->cpu.found.load();
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - s->start_time).count();
    stats->elapsed_ms = ms;
    double sec = ms / 1000.0;
    if (sec > 0.001) {
        stats->attempts_per_sec = (double)stats->attempts / sec;
        stats->candidates_per_sec = (double)stats->candidates / sec;
    }
    stats->backend = s->backend;
    stats->backend_state = s->backend_state;
    strncpy(stats->backend_name, s->backend_name.c_str(), sizeof(stats->backend_name) - 1);
    strncpy(stats->message, s->message.c_str(), sizeof(stats->message) - 1);
    return count;
}

void tnv_destroy(void* handle) {
    Session* s = (Session*)handle;
    if (!s) return;
    tnv_stop(s);
    delete s;
}

const char* tnv_backend_info(void) {
    static std::string info;
    int n = (int)std::thread::hardware_concurrency();
    if (n <= 0) n = 4;
    info = "CPU " + std::to_string(n) + " потоков";
    std::string cname, cerr;
    if (cuda_probe(cname, cerr)) info += "; " + cname;
    else info += "; CUDA: " + cerr;
    std::string oname, oerr;
    if (opencl_probe(oname, oerr)) info += "; " + oname;
    else info += "; OpenCL: " + oerr;
    return info.c_str();
}

static bool hex_eq(const uint8_t* p, const char* hex, int n) {
    for (int i = 0; i < n; ++i) {
        unsigned v; if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
        if (p[i] != (uint8_t)v) return false;
    }
    return true;
}

int tnv_selftest(char* report, int cap) {
    std::string r;
    int fail = 0;
    auto check = [&](const char* name, bool ok) {
        r += std::string(ok ? "PASS " : "FAIL ") + name + "\n";
        if (!ok) ++fail;
    };
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)i;
    uint8_t pk[32]; ed25519_pub(seed, pk);
    check("ed25519 seed 00..1f", hex_eq(pk, "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8", 32));

    uint8_t rs[64]; for (int i = 0; i < 64; ++i) rs[i] = (uint8_t)i;
    uint8_t key[32]; slip10_ed25519(rs, key);
    check("slip10 m/44'/607'/0'", hex_eq(key, "26d9edd7b7452b14a75ac4b3ca5d7785739913439916656b8eb5a9d8f5d8d224", 32));

    const char* m12 = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    uint8_t bseed[64], bk[32], bpk[32];
    bip39_phrase_to_seed(m12, bseed); slip10_ed25519(bseed, bk); ed25519_pub(bk, bpk);
    check("bip39 abandon -> pubkey", hex_eq(bpk, "7952e94118f34607c75e23258dd9220d66ccac5a3ee074125c25068e8107bfbf", 32));

    uint8_t fixedpk[32]; for (int i = 0; i < 32; ++i) { unsigned v; sscanf("ba54488588d41dc6ec249d49f89f7dc76a56ee2be5d63b31b487b1d039b8238d" + 2 * i, "%2x", &v); fixedpk[i] = (uint8_t)v; }
    std::string v4 = wallet_address(fixedpk, WALLET_V4R2, false);
    std::string w5 = wallet_address(fixedpk, WALLET_W5R1, false);
    check("v4r2 address vector", v4 == "UQAnK_NOuLYcdGhZs6KtJ_mjW4bh50ruzOIF6KxLZcVu_jIv");
    check("w5r1 address vector", w5 == "UQCfc6SAdj2vEfb0DYJPSyVLSjOuL2J4hcxWel0t1tok4CqY");

    const char* m24 = "hospital stove relief fringe tongue always charge angry urge sentence again match nerve inquiry senior coconut label tumble carry category beauty bean road solution";
    uint8_t ts[32], ent[64];
    bool ok24 = ton24_phrase_to_seed(m24, ts, ent);
    uint8_t tpk[32]; ed25519_pub(ts, tpk);
    check("ton24 @ton/crypto seed", ok24 && hex_eq(ts, "9d659a6c2234db7f6e4f977e6e8653b9f5946d557163f31034011375d8f3f97d", 32));
    check("ton24 @ton/crypto pubkey", hex_eq(tpk, "f6c450a16bb1c514e22f1977e390a3025599aa1e7b00068a6aacf2119484c1bd", 32));

    uint8_t p2[64]; pbkdf2_sha512((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, p2, 64);
    check("pbkdf2 rfc vector", hex_eq(p2, "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d470a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce", 64));

    r += fail ? "SELFTEST FAILED\n" : "SELFTEST OK\n";
    if (report && cap > 0) { strncpy(report, r.c_str(), cap - 1); report[cap - 1] = 0; }
    return fail ? 1 : 0;
}

