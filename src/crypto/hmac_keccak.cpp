// Copyright (c) 2024, Byze Project
// Adapted from an earlier internal prototype
// Distributed under the MIT software license

#include <crypto/hmac_keccak.h>
#include <crypto/common.h>

#include <algorithm>
#include <cstring>

void hmac_keccak_hash(unsigned char* output, const unsigned char* key, size_t keylen, 
                      const unsigned char* data, size_t datalen)
{
    static constexpr size_t BLOCK_SIZE = 136; // Keccak-256 block size (1088 bits / 8)
    static constexpr size_t HASH_SIZE = 32;
    
    unsigned char o_key_pad[BLOCK_SIZE];
    unsigned char i_key_pad[BLOCK_SIZE];
    
    // Prepare key
    if (keylen > BLOCK_SIZE) {
        // Hash key if it's too long
        SHA3_256 hash;
        hash.Write(std::span<const unsigned char>(key, keylen));
        unsigned char hashed_key[HASH_SIZE];
        hash.Finalize(std::span<unsigned char>(hashed_key, HASH_SIZE));
        std::memset(o_key_pad, 0, BLOCK_SIZE);
        std::memset(i_key_pad, 0, BLOCK_SIZE);
        std::memcpy(o_key_pad, hashed_key, HASH_SIZE);
        std::memcpy(i_key_pad, hashed_key, HASH_SIZE);
    } else {
        std::memset(o_key_pad, 0, BLOCK_SIZE);
        std::memset(i_key_pad, 0, BLOCK_SIZE);
        std::memcpy(o_key_pad, key, keylen);
        std::memcpy(i_key_pad, key, keylen);
    }
    
    // XOR with padding constants
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        o_key_pad[i] ^= 0x5c;
        i_key_pad[i] ^= 0x36;
    }
    
    // Inner hash: H(i_key_pad || data)
    SHA3_256 inner;
    inner.Write(std::span<const unsigned char>(i_key_pad, BLOCK_SIZE));
    inner.Write(std::span<const unsigned char>(data, datalen));
    unsigned char inner_hash[HASH_SIZE];
    inner.Finalize(std::span<unsigned char>(inner_hash, HASH_SIZE));
    
    // Outer hash: H(o_key_pad || inner_hash)
    SHA3_256 outer;
    outer.Write(std::span<const unsigned char>(o_key_pad, BLOCK_SIZE));
    outer.Write(std::span<const unsigned char>(inner_hash, HASH_SIZE));
    outer.Finalize(std::span<unsigned char>(output, HASH_SIZE));
}

