#include "ton_crypto.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

static bool check(const std::string& ver, int scheme, const std::string& fmt,
                  const std::string& addr, const std::string& mnemonic) {
    tnv::WalletVersion v = tnv::WALLET_V4R2;
    if (ver == "w5") v = tnv::WALLET_W5R1;
    else if (ver == "tg") v = tnv::WALLET_TG;
    bool bounce = fmt == "bounceable";
    auto words = split(mnemonic);
    tnv::WalletCandidate c;
    bool ok = false;
    if (scheme == 12) {
        uint8_t ent[16];
        if (words.size() == 12 && tnv::bip39_phrase_to_entropy(mnemonic, ent))
            ok = tnv::derive_bip39_12(ent, v, bounce, c);
    } else if (v == tnv::WALLET_TG) {
        if (words.size() == 24) {
            std::string a, s;
            for (int i = 0; i < 12; ++i) { if (i) a += ' '; a += words[i]; }
            for (int i = 12; i < 24; ++i) { if (i > 12) s += ' '; s += words[i]; }
            uint8_t ae[16], se[16];
            if (tnv::bip39_phrase_to_entropy(a, ae) && tnv::bip39_phrase_to_entropy(s, se))
                ok = tnv::derive_tg_24(ae, se, bounce, c);
        }
    } else {
        uint8_t seed[32], ent[64];
        if (tnv::ton24_phrase_to_seed(mnemonic, seed, ent)) {
            uint8_t pub[32];
            tnv::ed25519_pub(seed, pub);
            c.address = tnv::wallet_address(pub, v, bounce);
            c.phrase = mnemonic;
            ok = true;
        }
    }
    if (!ok) { std::cout << "FAIL derive " << ver << " " << scheme << " " << addr << "\n"; return false; }
    bool same = c.address == addr && c.phrase == mnemonic;
    std::cout << (same ? "OK   " : "FAIL ") << ver << " " << scheme << " " << addr << "\n";
    if (!same) std::cout << "     got " << c.address << "\n     phrase " << c.phrase << "\n";
    return same;
}

int main(int argc, char** argv) {
    std::ifstream f(argc > 1 ? argv[1] : "found.txt");
    if (!f) { std::cerr << "cannot open found file\n"; return 2; }
    std::string line, ver, fmt, addr, mnem;
    int scheme = 0, total = 0, good = 0;
    while (std::getline(f, line)) {
        if (line.rfind("version=", 0) == 0) {
            char v[16] = {0}, fm[32] = {0};
            int sc = 0;
            if (sscanf(line.c_str(), "version=%15s scheme=%d format=%31s", v, &sc, fm) == 3) {
                ver = v; scheme = sc; fmt = fm;
            }
        } else if (line.rfind("address=", 0) == 0) {
            addr = line.substr(8);
        } else if (line.rfind("mnemonic=", 0) == 0) {
            mnem = line.substr(9);
            ++total;
            if (check(ver, scheme, fmt, addr, mnem)) ++good;
        }
    }
    std::cout << "checked=" << total << " ok=" << good << "\n";
    return total && total == good ? 0 : 1;
}
