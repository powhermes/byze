// Copyright (c) 2024, Byze Project
// Adapted from an earlier internal prototype
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_HMAC_KECCAK_H
#define BITCOIN_CRYPTO_HMAC_KECCAK_H

#include <crypto/sha3.h>
#include <cstddef>
#include <cstdint>

/** HMAC-Keccak-256 implementation for quantum-safe signatures */
void hmac_keccak_hash(unsigned char* output, const unsigned char* key, size_t keylen, 
                      const unsigned char* data, size_t datalen);

#endif // BITCOIN_CRYPTO_HMAC_KECCAK_H

