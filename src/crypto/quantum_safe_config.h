// Copyright (c) 2024, Byze Project
// Quantum-safe configuration constants (liboqs XMSS-SHA2_10_256 + SPHINCS+-SHA2-128s-simple)
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H
#define BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H

#define BYZE_QUANTUM_SAFE_ENABLED                 1
#define BYZE_XMSS_HEIGHT                          10
#define BYZE_SPHINCS_LEVEL                        1  // SPHINCS+-SHA2-128s (small variant)
#define BYZE_QUANTUM_KEY_FORMAT_VERSION           3

// liboqs wire sizes (fixed; chain reset v3)
#define BYZE_XMSS_PUBKEY_SIZE                     68   // OQS_SIG_STFL_alg_xmss_sha256_h10_length_pk
#define BYZE_SPHINCS_PUBKEY_SIZE                  32   // OQS_SIG_sphincs_sha2_128s_simple_length_public_key
#define BYZE_XMSS_SIGNATURE_SIZE                  2500 // OQS_SIG_STFL_alg_xmss_sha256_h10_length_signature
#define BYZE_SPHINCS_SIGNATURE_SIZE               7856 // OQS_SIG_sphincs_sha2_128s_simple_length_signature
#define BYZE_DUAL_PUBKEY_BUNDLE_SIZE              (BYZE_XMSS_PUBKEY_SIZE + BYZE_SPHINCS_PUBKEY_SIZE)

#define BYZE_QUANTUM_SAFE_ENFORCED                1
#define BYZE_QUANTUM_SAFE_ALWAYS_ENABLED          1

#define BYZE_DEFAULT_QUANTUM_ALGORITHM            "XMSS"
#define BYZE_DEFAULT_XMSS_TREE_HEIGHT             10
#define BYZE_DEFAULT_SPHINCS_LEVEL                1

#define BYZE_MIN_XMSS_TREE_HEIGHT                 8
#define BYZE_MAX_XMSS_TREE_HEIGHT                 20
#define BYZE_MIN_SPHINCS_LEVEL                    1
#define BYZE_MAX_SPHINCS_LEVEL                    10

#endif // BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H
