// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETQUANTUM_H
#define BITCOIN_WALLET_WALLETQUANTUM_H

#include <script/signingprovider.h>

#include <span>

namespace wallet {
class CWallet;

/** SigningProvider that delegates quantum taproot signing to a CWallet's DB-backed keys. */
class WalletQuantumSigningProvider final : public SigningProvider
{
    const CWallet& m_wallet;

public:
    explicit WalletQuantumSigningProvider(const CWallet& wallet) : m_wallet(wallet) {}

    bool SignQuantumSighash(const uint256& sighash, std::span<const unsigned char> output_program, std::vector<unsigned char>& xmss_sig, std::vector<unsigned char>& sphincs_sig, std::vector<unsigned char>& dual_pubkey_bundle) const override;
};

} // namespace wallet

#endif // BITCOIN_WALLET_WALLETQUANTUM_H
