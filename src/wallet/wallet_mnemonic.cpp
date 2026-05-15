// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <hash.h>
#include <wallet/bip39.h>
#include <wallet/crypter.h>
#include <wallet/walletdb.h>

namespace wallet {
namespace {

static const uint256 g_mnemonic_wallet_iv = Hash(std::string_view{"byze_wallet_mnemonic_iv_v1"});

} // namespace

bool CWallet::PersistWalletMnemonic(WalletBatch& batch, const std::string& mnemonic)
{
    AssertLockHeld(cs_wallet);
    std::vector<uint8_t> plain(mnemonic.begin(), mnemonic.end());
    std::vector<uint8_t> payload = plain;
    bool enc = IsCrypted() && !vMasterKey.empty();
    if (enc) {
        CKeyingMaterial secret(plain.begin(), plain.end());
        std::vector<unsigned char> cipher;
        if (!EncryptSecret(vMasterKey, secret, g_mnemonic_wallet_iv, cipher)) {
            return false;
        }
        payload = std::move(cipher);
    }
    if (!batch.WriteWalletMnemonic(payload)) {
        return false;
    }
    m_mnemonic_storage = std::move(payload);
    m_mnemonic_is_encrypted = enc;
    return true;
}

bool CWallet::EncryptWalletMnemonicForWallet(const CKeyingMaterial& master_key, WalletBatch* batch)
{
    AssertLockHeld(cs_wallet);
    if (m_mnemonic_storage.empty()) {
        return true;
    }
    if (m_mnemonic_is_encrypted) {
        return true;
    }
    CKeyingMaterial plain(m_mnemonic_storage.begin(), m_mnemonic_storage.end());
    std::vector<unsigned char> cipher;
    if (!EncryptSecret(master_key, plain, g_mnemonic_wallet_iv, cipher)) {
        return false;
    }
    if (batch) {
        if (!batch->WriteWalletMnemonic(cipher)) {
            return false;
        }
    } else {
        WalletBatch wb(GetDatabase());
        if (!wb.WriteWalletMnemonic(cipher)) {
            return false;
        }
    }
    m_mnemonic_storage = std::move(cipher);
    m_mnemonic_is_encrypted = true;
    return true;
}

bool CWallet::HasWalletMnemonic() const
{
    AssertLockHeld(cs_wallet);
    return !m_mnemonic_storage.empty();
}

bool CWallet::GetWalletMnemonic(std::string& mnemonic_out) const
{
    AssertLockHeld(cs_wallet);
    mnemonic_out.clear();
    if (m_mnemonic_storage.empty()) {
        return false;
    }
    if (!m_mnemonic_is_encrypted) {
        mnemonic_out.assign(m_mnemonic_storage.begin(), m_mnemonic_storage.end());
        return true;
    }
    if (vMasterKey.empty()) {
        return false;
    }
    CKeyingMaterial plain;
    if (!DecryptSecret(vMasterKey, std::span<const unsigned char>(m_mnemonic_storage.data(), m_mnemonic_storage.size()), g_mnemonic_wallet_iv, plain)) {
        return false;
    }
    mnemonic_out.assign(plain.begin(), plain.end());
    return true;
}

DBErrors CWallet::LoadMnemonicFromDatabase(DatabaseBatch& batch)
{
    AssertLockHeld(cs_wallet);
    m_mnemonic_storage.clear();
    m_mnemonic_is_encrypted = false;
    std::vector<unsigned char> raw;
    if (!batch.Read(DBKeys::MNEMONIC, raw) || raw.empty()) {
        return DBErrors::LOAD_OK;
    }
    m_mnemonic_storage.assign(raw.begin(), raw.end());
    m_mnemonic_is_encrypted = IsCrypted();
    return DBErrors::LOAD_OK;
}

} // namespace wallet
