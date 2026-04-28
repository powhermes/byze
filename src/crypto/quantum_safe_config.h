// Copyright (c) 2024, Byze Project
// Quantum-safe configuration constants
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H
#define BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H

// Quantum-Safe Configuration
#define HERMES_QUANTUM_SAFE_ENABLED               1
#define HERMES_XMSS_HEIGHT                        10
#define HERMES_SPHINCS_LEVEL                      5
#define HERMES_QUANTUM_SIGNATURE_SIZE             1024
#define HERMES_XMSS_SIGNATURE_SIZE                1028  // 1024 bytes signature + 4 bytes index
#define HERMES_SPHINCS_SIGNATURE_SIZE             1024
#define HERMES_DUAL_PUBKEY_BUNDLE_SIZE            192

// Quantum-Safe Enforcement Configuration
#define HERMES_QUANTUM_SAFE_ENFORCED             1  // MANDATORY quantum-safe features
#define HERMES_QUANTUM_SAFE_ALWAYS_ENABLED       1  // Force enable

// Quantum-Safe Algorithm Defaults
#define HERMES_DEFAULT_QUANTUM_ALGORITHM         "XMSS"
#define HERMES_DEFAULT_XMSS_TREE_HEIGHT          10
#define HERMES_DEFAULT_SPHINCS_LEVEL             5

// Quantum-Safe Signature Requirements
#define HERMES_MIN_XMSS_TREE_HEIGHT              8
#define HERMES_MAX_XMSS_TREE_HEIGHT              20
#define HERMES_MIN_SPHINCS_LEVEL                 3
#define HERMES_MAX_SPHINCS_LEVEL                 10

#endif // BITCOIN_CRYPTO_QUANTUM_SAFE_CONFIG_H

