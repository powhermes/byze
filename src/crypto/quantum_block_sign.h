// Copyright (c) 2026 Byze developers
// Distributed under the MIT software license

#ifndef BITCOIN_CRYPTO_QUANTUM_BLOCK_SIGN_H
#define BITCOIN_CRYPTO_QUANTUM_BLOCK_SIGN_H

class CBlock;

namespace crypto
{
/** Attach XMSS + SPHINCS+ block tail signatures when required. Thread-safe. */
bool AttachQuantumBlockSignatures(CBlock& block, bool require_quantum, bool is_genesis);
} // namespace crypto

#endif // BITCOIN_CRYPTO_QUANTUM_BLOCK_SIGN_H
