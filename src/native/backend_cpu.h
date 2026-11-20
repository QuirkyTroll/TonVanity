#pragma once
#include "tonvanity.h"
#include "ton_crypto.h"
#include <atomic>
#include <mutex>
#include <deque>
#include <set>
#include <string>

struct CpuShared {
    TnvConfig cfg;
    std::atomic<bool> stop{false};
    std::atomic<unsigned long long> attempts{0};
    std::atomic<unsigned long long> candidates{0};
    std::atomic<unsigned long long> found{0};
    std::mutex mtx;
    std::deque<TnvMatch> matches;
    std::set<std::string> seen_addresses;
    void push(const tnv::WalletCandidate& c, int backend, unsigned long long attempt_no);
};

void cpu_worker(CpuShared& sh, int tid);
