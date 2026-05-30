// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_BIP39_H
#define BITCOIN_WALLET_BIP39_H

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wallet {

/** Generate a BIP39 English mnemonic (24 words) from 32 bytes of entropy. */
std::string EncodeMnemonic(std::span<const uint8_t, 32> entropy);

/** Validate mnemonic words and return entropy (32 bytes for 24-word mnemonics). */
bool DecodeMnemonic(const std::string& mnemonic, std::vector<uint8_t>& entropy_out, std::string& error);

/** BIP39 seed from mnemonic and optional passphrase (PBKDF2-HMAC-SHA512, 2048 rounds). */
bool MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase, uint8_t seed_out[64]);

} // namespace wallet

#endif // BITCOIN_WALLET_BIP39_H
