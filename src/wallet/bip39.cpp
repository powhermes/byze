// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/bip39.h>

#include <wallet/bip39_wordlist.h>

#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>

#include <tinyformat.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace wallet {
namespace {

static void PBKDF2_HMAC_SHA512(const std::string& password, const std::string& salt, uint8_t out[64])
{
    constexpr int iterations = 2048;
    unsigned char U[CHMAC_SHA512::OUTPUT_SIZE];
    unsigned char T[CHMAC_SHA512::OUTPUT_SIZE];

    CHMAC_SHA512 hmac(reinterpret_cast<const unsigned char*>(password.data()), password.size());
    hmac.Write(reinterpret_cast<const unsigned char*>(salt.data()), salt.size());
    hmac.Write(reinterpret_cast<const unsigned char*>("\x00\x00\x00\x01"), 4);
    hmac.Finalize(U);
    std::memcpy(T, U, CHMAC_SHA512::OUTPUT_SIZE);

    for (int i = 1; i < iterations; ++i) {
        CHMAC_SHA512 iter_hmac(reinterpret_cast<const unsigned char*>(password.data()), password.size());
        iter_hmac.Write(U, CHMAC_SHA512::OUTPUT_SIZE);
        iter_hmac.Finalize(U);
        for (size_t j = 0; j < CHMAC_SHA512::OUTPUT_SIZE; ++j) {
            T[j] ^= U[j];
        }
    }
    std::memcpy(out, T, 64);
}

static int WordIndex(std::string_view word)
{
    const auto it = std::lower_bound(BIP39_ENGLISH_WORDLIST.begin(), BIP39_ENGLISH_WORDLIST.end(), word);
    if (it == BIP39_ENGLISH_WORDLIST.end() || *it != word) return -1;
    return static_cast<int>(it - BIP39_ENGLISH_WORDLIST.begin());
}

} // namespace

std::string EncodeMnemonic(std::span<const uint8_t, 32> entropy)
{
    uint8_t hash[32];
    CSHA256().Write(entropy.data(), entropy.size()).Finalize(hash);
    const uint32_t checksum_bits = 8; // 256 bits entropy -> 8 checksum bits

    std::vector<bool> bits;
    bits.reserve(264);
    for (size_t i = 0; i < entropy.size(); ++i) {
        for (int b = 7; b >= 0; --b) {
            bits.push_back((entropy[i] >> b) & 1);
        }
    }
    for (uint32_t i = 0; i < checksum_bits; ++i) {
        bits.push_back((hash[0] >> (7 - i)) & 1);
    }

    std::ostringstream out;
    for (size_t i = 0; i < bits.size(); i += 11) {
        int index = 0;
        for (int b = 0; b < 11; ++b) {
            index <<= 1;
            if (bits[i + b]) index |= 1;
        }
        if (!out.str().empty()) out << ' ';
        out << BIP39_ENGLISH_WORDLIST[index];
    }
    return out.str();
}

bool DecodeMnemonic(const std::string& mnemonic, std::vector<uint8_t>& entropy_out, std::string& error)
{
    std::istringstream iss(mnemonic);
    std::vector<int> indices;
    std::string word;
    while (iss >> word) {
        const int idx = WordIndex(word);
        if (idx < 0) {
            error = strprintf("Unknown mnemonic word: %s", word);
            return false;
        }
        indices.push_back(idx);
    }
    if (indices.size() != 24) {
        error = "Mnemonic must contain exactly 24 words";
        return false;
    }

    std::vector<bool> bits;
    bits.reserve(indices.size() * 11);
    for (int idx : indices) {
        for (int b = 10; b >= 0; --b) {
            bits.push_back((idx >> b) & 1);
        }
    }

    constexpr size_t entropy_bits = 256;
    constexpr size_t checksum_bits = 8;
    if (bits.size() != entropy_bits + checksum_bits) {
        error = "Invalid mnemonic length";
        return false;
    }

    entropy_out.assign(32, 0);
    for (size_t i = 0; i < entropy_bits; ++i) {
        if (bits[i]) entropy_out[i / 8] |= (1 << (7 - (i % 8)));
    }

    uint8_t hash[32];
    CSHA256().Write(entropy_out.data(), entropy_out.size()).Finalize(hash);
    for (size_t i = 0; i < checksum_bits; ++i) {
        const bool expected = (hash[0] >> (7 - i)) & 1;
        if (bits[entropy_bits + i] != expected) {
            error = "Mnemonic checksum invalid";
            return false;
        }
    }
    return true;
}

bool MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase, uint8_t seed_out[64])
{
    const std::string salt = std::string("mnemonic") + passphrase;
    PBKDF2_HMAC_SHA512(mnemonic, salt, seed_out);
    return true;
}

} // namespace wallet
