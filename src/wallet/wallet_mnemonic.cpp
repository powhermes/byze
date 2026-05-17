// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <hash.h>
#include <wallet/bip39.h>
#include <wallet/crypter.h>
#include <wallet/wallet_mnemonic.h>
#include <wallet/walletdb.h>

namespace wallet {
namespace {

static const uint256 g_mnemonic_wallet_iv = Hash(std::string_view{"byze_wallet_mnemonic_iv_v1"});

} // namespace

bool MasterExtKeyFromEntropy(std::span<const uint8_t, 32> entropy, CExtKey& master_out)
{
    CKey seed_key;
    seed_key.Set(entropy.data(), entropy.data() + entropy.size(), true);
    if (!seed_key.IsValid()) {
        return false;
    }
    master_out.SetSeed(seed_key);
    return true;
}

bool MasterExtKeyFromMnemonic(const std::string& mnemonic, CExtKey& master_out, std::string& error)
{
    std::vector<uint8_t> entropy;
    if (!DecodeMnemonic(mnemonic, entropy, error)) {
        return false;
    }
    if (entropy.size() != 32) {
        error = "Invalid mnemonic entropy length";
        return false;
    }
    if (!MasterExtKeyFromEntropy(std::span<const uint8_t, 32>(entropy.data(), entropy.size()), master_out)) {
        error = "Failed to derive HD master from mnemonic entropy";
        return false;
    }
    return true;
}

bool DescriptorRootExtKeysMatch(const CExtKey& a, const CExtKey& b)
{
    if (!a.key.IsValid() || !b.key.IsValid()) {
        return false;
    }
    return a.key.GetPubKey() == b.key.GetPubKey() && a.chaincode == b.chaincode;
}

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

bool CWallet::StoredMnemonicMatchesActiveDescriptors() const
{
    AssertLockHeld(cs_wallet);
    if (!HasWalletMnemonic()) {
        return true;
    }
    std::string mnemonic;
    if (!GetWalletMnemonic(mnemonic)) {
        return true; // locked encrypted wallet: cannot verify until unlock
    }
    CExtKey from_mnemonic;
    std::string error;
    if (!MasterExtKeyFromMnemonic(mnemonic, from_mnemonic, error)) {
        return false;
    }
    const std::optional<CExtKey> active = TryGetTaprootDescriptorRootExtKey();
    if (!active) {
        return true;
    }
    return DescriptorRootExtKeysMatch(from_mnemonic, *active);
}

void CWallet::MaybeWarnMnemonicDescriptorMismatch(std::vector<bilingual_str>& warnings) const
{
    AssertLockHeld(cs_wallet);
    if (!HasWalletMnemonic()) {
        return;
    }
    if (StoredMnemonicMatchesActiveDescriptors()) {
        return;
    }
    warnings.emplace_back(Untranslated(
        "The stored BIP39 recovery phrase does not match the active taproot descriptor keys in this wallet. "
        "Funds may have been received on addresses from an earlier key generation (for example after "
        "encrypting an unencrypted wallet). Back up wallet.dat and use restorewallet, or contact support. "
        "restorefrommnemonic will not recover mined balances for this wallet."));
}

} // namespace wallet
