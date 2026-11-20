#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

namespace tnv {

enum WalletVersion { WALLET_V4R2 = 0, WALLET_W5R1 = 1, WALLET_TG = 2 };
enum MnemonicScheme { SCHEME_12 = 0, SCHEME_24 = 1 };

// ---------- hashes ----------
void sha512(const void* data, size_t len, uint8_t out[64]);
void sha256(const void* data, size_t len, uint8_t out[32]);
void hmac_sha512(const uint8_t* key, size_t keylen, const uint8_t* msg, size_t msglen, uint8_t out[64]);

struct HmacSha512Ctx {
    uint64_t inner[8];
    uint64_t outer[8];
};
void hmac_prepare(const uint8_t* key, size_t keylen, HmacSha512Ctx& ctx);
void hmac64(const HmacSha512Ctx& ctx, const uint8_t msg[64], uint8_t out[64]);
void hmac_msg(const HmacSha512Ctx& ctx, const uint8_t* msg, size_t len, uint8_t out[64]);

void pbkdf2_sha512(const uint8_t* pass, size_t passlen,
                   const uint8_t* salt, size_t saltlen,
                   uint32_t iterations, uint8_t* out, size_t outlen);

// ---------- ed25519 ----------
void ed25519_pub(const uint8_t seed[32], uint8_t pub[32]);

// ---------- mnemonic ----------
bool bip39_entropy_to_phrase(const uint8_t entropy[16], std::string& phrase);
bool bip39_phrase_to_entropy(const std::string& phrase, uint8_t entropy[16]);
bool bip39_phrase_to_seed(const std::string& phrase, uint8_t seed[64]);
bool bip39_validate(const std::string& phrase);
void bip39_entropy_to_phrase_static(const uint8_t entropy[16], char* out, size_t cap);

bool ton24_phrase_from_indices(const uint16_t idx[24], std::string& phrase);
bool ton24_indices_from_bytes(const uint8_t bytes[33], uint16_t idx[24]);
bool ton24_phrase_to_entropy(const std::string& phrase, uint8_t entropy[64]);
bool ton24_validate(const std::string& phrase);
// returns false when the phrase is not a valid TON basic-seed mnemonic
bool ton24_phrase_to_seed(const std::string& phrase, uint8_t seed[32], uint8_t entropy[64]);
bool ton24_entropy_from_indices(const uint16_t idx[24], uint8_t entropy[64]);

bool slip10_ed25519(const uint8_t seed[64], uint8_t key[32]);

// ---------- TON address ----------
std::string wallet_address(const uint8_t pub[32], WalletVersion v, bool bounceable);
std::string base64url(const uint8_t* data, size_t len);
uint16_t crc16_xmodem(const uint8_t* data, size_t len);
bool suffix_match(const std::string& address, const std::string& suffix, bool case_sensitive);

// ---------- PRNG ----------
struct ChaCha20 {
    uint32_t state[16];
    uint8_t buf[64];
    size_t pos;
    void init(const uint8_t key[32], const uint8_t nonce[8], uint64_t counter);
    void fill(uint8_t* out, size_t n);
    uint32_t u32();
};

// ---------- Derivation for a candidate ----------
struct WalletCandidate {
    std::string phrase;          // 12 or 24 words
    std::string address;         // friendly address
    uint8_t public_key[32];
};

// entropy must be 16 bytes for 12-word schemes; for TON-24 indices are used.
bool derive_bip39_12(const uint8_t entropy[16], WalletVersion v, bool bounceable, WalletCandidate& out);
bool derive_ton24(const uint16_t idx[24], WalletVersion v, bool bounceable, WalletCandidate& out);
// TG-24: address from anchor (12 words) + signing 12 words supplied/generated.
bool derive_tg_24(const uint8_t anchor_entropy[16], const uint8_t signing_entropy[16], bool bounceable, WalletCandidate& out);

} // namespace tnv
