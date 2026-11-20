#include "backend_cpu.h"
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <random>
#include <string.h>

using namespace tnv;

static unsigned long long random64() {
    unsigned long long v = 0;
    if (BCryptGenRandom(NULL, (PUCHAR)&v, sizeof(v), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        std::random_device rd;
        v = ((unsigned long long)rd() << 32) ^ rd();
    }
    return v;
}

static void write_found(const TnvMatch& m) {
    char path[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(GetModuleHandleA(NULL), path, MAX_PATH);
    if (n > 0) {
        char* slash = strrchr(path, '\\');
        if (slash) strcpy(slash + 1, "found.txt");
        else strcpy(path, "found.txt");
    } else {
        strcpy(path, "found.txt");
    }
    std::ofstream f(path, std::ios::app);
    if (!f) return;
    const char* ver = m.wallet_version == 0 ? "v4r2" : m.wallet_version == 1 ? "w5" : "tg";
    f << "version=" << ver << " scheme=" << (m.mnemonic_scheme ? 24 : 12)
      << " format=" << (m.bounceable ? "bounceable" : "non-bounceable")
      << " backend=" << (m.backend == 0 ? "CPU" : m.backend == 1 ? "CUDA" : "OpenCL")
      << " attempts=" << m.attempts << "\n"
      << "address=" << m.address << "\n"
      << "mnemonic=" << m.mnemonic << "\n";
    f.flush();
}

void CpuShared::push(const WalletCandidate& c, int backend, unsigned long long attempt_no) {
    TnvMatch m;
    memset(&m, 0, sizeof(m));
    strncpy(m.address, c.address.c_str(), sizeof(m.address) - 1);
    strncpy(m.mnemonic, c.phrase.c_str(), sizeof(m.mnemonic) - 1);
    m.wallet_version = cfg.wallet_version;
    m.mnemonic_scheme = cfg.mnemonic_scheme;
    m.bounceable = cfg.bounceable;
    m.backend = backend;
    m.attempts = attempt_no;
    {
        std::lock_guard<std::mutex> lk(mtx);
        if (!seen_addresses.insert(c.address).second) return;
        matches.push_back(m);
        found.fetch_add(1);
        write_found(m);
    }
}

void cpu_worker(CpuShared& sh, int tid) {
    uint8_t seedbuf[44];
    if (BCryptGenRandom(NULL, seedbuf, sizeof(seedbuf), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        for (auto& b : seedbuf) b = (uint8_t)random64();
    }
    uint64_t ctr = 0;
    memcpy(&ctr, seedbuf + 32, 8);
    ctr ^= (uint64_t)(tid + 1) * 0x9e3779b97f4a7c15ULL;
    ChaCha20 rng;
    rng.init(seedbuf, seedbuf + 32, ctr);

    const bool s12 = (sh.cfg.mnemonic_scheme == 0);
    const bool tg24 = (sh.cfg.wallet_version == 2 && !s12);
    const WalletVersion wv = (WalletVersion)sh.cfg.wallet_version;
    const bool bounce = sh.cfg.bounceable != 0;
    const bool cs = sh.cfg.case_sensitive != 0;
    std::string suffix = sh.cfg.suffix;

    while (!sh.stop.load(std::memory_order_relaxed)) {
        if (s12) {
            uint8_t ent[16];
            rng.fill(ent, 16);
            WalletCandidate c;
            if (!derive_bip39_12(ent, wv, bounce, c)) continue;
            unsigned long long n = sh.attempts.fetch_add(1) + 1;
            sh.candidates.fetch_add(1);
            if (suffix_match(c.address, suffix, cs)) sh.push(c, 0, n);
        } else if (!tg24) {
            uint8_t raw[33];
            rng.fill(raw, 33);
            uint16_t idx[24];
            ton24_indices_from_bytes(raw, idx);
            unsigned long long n = sh.attempts.fetch_add(1) + 1;
            WalletCandidate c;
            if (!derive_ton24(idx, wv, bounce, c)) continue;
            sh.candidates.fetch_add(1);
            if (suffix_match(c.address, suffix, cs)) sh.push(c, 0, n);
        } else {
            uint8_t anchor[16];
            rng.fill(anchor, 16);
            WalletCandidate c;
            if (!derive_bip39_12(anchor, WALLET_TG, bounce, c)) continue;
            unsigned long long n = sh.attempts.fetch_add(1) + 1;
            sh.candidates.fetch_add(1);
            if (!suffix_match(c.address, suffix, cs)) continue;
            uint8_t signing[16];
            rng.fill(signing, 16);
            WalletCandidate full;
            if (derive_tg_24(anchor, signing, bounce, full)) sh.push(full, 0, n);
        }
    }
}
