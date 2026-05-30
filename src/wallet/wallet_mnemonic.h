// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLET_MNEMONIC_H
#define BITCOIN_WALLET_WALLET_MNEMONIC_H

#include <key.h>
#include <span>

#include <string>
#include <vector>

namespace wallet {

/** Derive the taproot descriptor HD master from 32-byte BIP39 entropy (same as create/restore). */
bool MasterExtKeyFromEntropy(std::span<const uint8_t, 32> entropy, CExtKey& master_out);

/** Decode mnemonic and derive the descriptor HD master. */
bool MasterExtKeyFromMnemonic(const std::string& mnemonic, CExtKey& master_out, std::string& error);

/** True when both keys share the same root xpub (depth 0). */
bool DescriptorRootExtKeysMatch(const CExtKey& a, const CExtKey& b);

} // namespace wallet

#endif // BITCOIN_WALLET_WALLET_MNEMONIC_H
