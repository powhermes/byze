// Copyright (c) 2024, Byze Project
// Quantum-safe configuration constants
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H
#define BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H

// Quantum-Safe Configuration
#define BYZE_QUANTUM_SAFE_ENABLED                 1
#define BYZE_XMSS_HEIGHT                          10
#define BYZE_SPHINCS_LEVEL                        5
#define BYZE_QUANTUM_SIGNATURE_SIZE               1024
#define BYZE_XMSS_SIGNATURE_SIZE                  1028  // 1024 bytes signature + 4 bytes index
#define BYZE_SPHINCS_SIGNATURE_SIZE               1024
#define BYZE_DUAL_PUBKEY_BUNDLE_SIZE              192

// Quantum-Safe Enforcement Configuration
#define BYZE_QUANTUM_SAFE_ENFORCED                1  // MANDATORY quantum-safe features
#define BYZE_QUANTUM_SAFE_ALWAYS_ENABLED          1  // Force enable

// Quantum-Safe Algorithm Defaults
#define BYZE_DEFAULT_QUANTUM_ALGORITHM            "XMSS"
#define BYZE_DEFAULT_XMSS_TREE_HEIGHT             10
#define BYZE_DEFAULT_SPHINCS_LEVEL                5

// Quantum-Safe Signature Requirements
#define BYZE_MIN_XMSS_TREE_HEIGHT                 8
#define BYZE_MAX_XMSS_TREE_HEIGHT                 20
#define BYZE_MIN_SPHINCS_LEVEL                    3
#define BYZE_MAX_SPHINCS_LEVEL                    10

#endif // BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H

