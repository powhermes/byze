// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <common/args.h>
#include <crypto/quantum_safe_config.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <util/fs.h>
#include <wallet/crypter.h>
#include <wallet/wallet.h>
#include <wallet/walletquantum.h>
#include <wallet/walletdb.h>

#include <cstring>

namespace wallet {
namespace {

static const uint256 g_quantum_wallet_iv = Hash(std::string_view{"byze_wallet_quantum_iv_v1"});

static bool ProgramFromManager(const crypto::quantum_safe_manager& mgr, std::array<uint8_t, 32>& out)
{
    const std::vector<uint8_t> bundle = mgr.get_dual_public_key_bundle();
    if (bundle.empty()) return false;
    unsigned char bundle_hash[32];
    CSHA256().Write(bundle.data(), bundle.size()).Finalize(bundle_hash);
    std::memcpy(out.data(), bundle_hash, 32);
    return true;
}

static std::vector<uint8_t> PackQuantumDbRecord(bool encrypted, const std::array<uint8_t, 32>& program, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out;
    out.reserve(1 + 1 + 32 + payload.size());
    out.push_back(1); // format version
    out.push_back(encrypted ? uint8_t{1} : uint8_t{0});
    out.insert(out.end(), program.begin(), program.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static bool UnpackQuantumDbRecord(const std::vector<uint8_t>& raw, bool& encrypted, std::array<uint8_t, 32>& program, std::vector<uint8_t>& payload)
{
    if (raw.size() < 1 + 1 + 32) return false;
    if (raw[0] != 1) return false;
    encrypted = raw[1] != 0;
    std::memcpy(program.data(), raw.data() + 2, 32);
    payload.assign(raw.begin() + 34, raw.end());
    return true;
}

/** Recover XMSS index after an interrupted prior sign (same semantics as legacy .pending file). */
static bool RecoverPendingXmssDb(WalletBatch& batch, crypto::quantum_safe_manager& mgr)
{
    uint32_t pending_index{0};
    if (!batch.ReadQuantumPending(pending_index)) return true;

    const auto current = mgr.get_xmss_index();
    if (!current.has_value()) return false;
    if (pending_index >= *current) {
        if (!mgr.set_xmss_index(pending_index + 1)) return false;
    }
    batch.EraseQuantumPending();
    return true;
}

} // namespace

DBErrors CWallet::ApplyQuantumStateFromPackedBlob(const std::vector<unsigned char>& raw)
{
    AssertLockHeld(cs_wallet);
    bool encrypted{false};
    std::array<uint8_t, 32> program{};
    std::vector<uint8_t> payload;
    if (!UnpackQuantumDbRecord(raw, encrypted, program, payload)) {
        WalletLogPrintf("%s: corrupt quantum state record\n", __func__);
        return DBErrors::CORRUPT;
    }
    m_quantum_program_bytes = program;
    m_quantum_secret_storage = std::move(payload);
    m_quantum_secret_is_encrypted = encrypted;
    m_quantum_manager.reset();

    if (!encrypted) {
        m_quantum_manager = std::make_unique<crypto::quantum_safe_manager>();
        if (!m_quantum_manager->deserialize_dual_keys(m_quantum_secret_storage) ||
            !m_quantum_manager->ensure_modern_keys()) {
            WalletLogPrintf("%s: failed to deserialize quantum keys from wallet DB\n", __func__);
            m_quantum_manager.reset();
            return DBErrors::CORRUPT;
        }
    } else if (!IsCrypted()) {
        WalletLogPrintf("%s: quantum payload marked encrypted but wallet is not encrypted\n", __func__);
        return DBErrors::CORRUPT;
    }
    return DBErrors::LOAD_OK;
}

bool WalletQuantumSigningProvider::SignQuantumSighash(const uint256& sighash, std::span<const unsigned char> output_program, std::vector<unsigned char>& xmss_sig, std::vector<unsigned char>& sphincs_sig, std::vector<unsigned char>& dual_pubkey_bundle) const
{
    AssertLockHeld(m_wallet.cs_wallet);
    return m_wallet.SignQuantumTransactionSighash(sighash, output_program, xmss_sig, sphincs_sig, dual_pubkey_bundle);
}

DBErrors CWallet::LoadQuantumRecordsFromDatabase(DatabaseBatch& batch)
{
    AssertLockHeld(cs_wallet);
    m_quantum_program_bytes.reset();
    m_quantum_secret_storage.clear();
    m_quantum_secret_is_encrypted = false;
    m_quantum_manager.reset();

    std::vector<unsigned char> raw;
    if (!batch.Read(DBKeys::QUANTUM_STATE, raw) || raw.empty()) {
        if (!MigrateLegacyQuantumKeysFromDiskIfNeeded()) {
            return DBErrors::LOAD_FAIL;
        }
        if (!batch.Read(DBKeys::QUANTUM_STATE, raw) || raw.empty()) {
            return DBErrors::LOAD_OK;
        }
    }

    return ApplyQuantumStateFromPackedBlob(raw);
}

bool CWallet::MigrateLegacyQuantumKeysFromDiskIfNeeded()
{
    AssertLockHeld(cs_wallet);

    WalletBatch probe_batch(GetDatabase());
    std::vector<unsigned char> existing;
    if (probe_batch.ReadQuantumState(existing) && !existing.empty()) {
        return true;
    }

    const fs::path legacy = gArgs.GetDataDirNet() / "quantum_wallet.keys";
    if (!fs::exists(legacy)) {
        return true;
    }

    const std::string& wname = GetName();
    const bool eligible = wname.empty() || wname == "default_wallet";
    if (!eligible) {
        WalletLogPrintf(
            "%s: WARN: legacy quantum_wallet.keys exists under the data directory but was not imported: wallet \"%s\" "
            "is not the designated primary wallet (only \"\" or \"default_wallet\" may consume the legacy file). "
            "An external quantum key file may still be required for older setups until you remove it or migrate manually.\n",
            __func__,
            wname);
        return true;
    }

    if (IsCrypted() && vMasterKey.empty()) {
        WalletLogPrintf(
            "%s: WARN: legacy quantum_wallet.keys found but wallet is encrypted; unlock once with walletpassphrase so the "
            "legacy quantum key file can be migrated into the wallet database. Until then, migration is incomplete.\n",
            __func__);
        return true;
    }

    crypto::quantum_safe_manager mgr;
    if (!mgr.load_dual_keys(fs::PathToString(legacy)) || !mgr.ensure_modern_keys()) {
        WalletLogPrintf(
            "%s: WARN: legacy quantum_wallet.keys present but failed to load; migration incomplete. Wallet will not use this file for new operations.\n",
            __func__);
        return true;
    }
    std::array<uint8_t, 32> program{};
    if (!ProgramFromManager(mgr, program)) {
        WalletLogPrintf("%s: WARN: legacy quantum_wallet.keys loaded but program derivation failed; migration incomplete.\n", __func__);
        return true;
    }
    std::vector<uint8_t> plain;
    if (!mgr.serialize_dual_keys(plain)) {
        WalletLogPrintf("%s: WARN: legacy quantum_wallet.keys serialization failed; migration incomplete.\n", __func__);
        return true;
    }

    const bool enc = IsCrypted() && !vMasterKey.empty();
    std::vector<uint8_t> payload = plain;
    if (enc) {
        CKeyingMaterial secret(plain.begin(), plain.end());
        std::vector<unsigned char> cipher;
        if (!EncryptSecret(vMasterKey, secret, g_quantum_wallet_iv, cipher)) {
            WalletLogPrintf("%s: ERROR: could not encrypt migrated quantum keys for wallet database; migration failed.\n", __func__);
            return false;
        }
        payload = std::move(cipher);
    }
    const std::vector<uint8_t> packed = PackQuantumDbRecord(enc, program, payload);
    if (!RunWithinTxn(GetDatabase(), "quantum_migrate_legacy", [&](WalletBatch& wb) { return wb.WriteQuantumState(packed); })) {
        WalletLogPrintf("%s: ERROR: failed to persist migrated quantum keys into wallet database.\n", __func__);
        return false;
    }

    try {
        const fs::path bak = legacy + ".migrated.bak";
        fs::rename(legacy, bak);
        WalletLogPrintf("%s: Legacy quantum key file migrated into wallet database (previous file moved to %s)\n", __func__, fs::PathToString(bak));
    } catch (const std::exception& e) {
        WalletLogPrintf(
            "%s: WARN: quantum keys were written to the wallet database but the legacy file could not be renamed (%s). "
            "The wallet no longer reads quantum_wallet.keys; remove %s manually when convenient.\n",
            __func__,
            e.what(),
            fs::PathToString(legacy));
    }
    return true;
}

bool CWallet::EnsureQuantumKeysForReceive()
{
    AssertLockHeld(cs_wallet);
    if (m_quantum_program_bytes.has_value()) {
        return true;
    }
    if (IsCrypted() && vMasterKey.empty()) {
        return false;
    }
    crypto::quantum_safe_manager mgr;
    if (!mgr.generate_dual_keys() || !mgr.ensure_modern_keys()) {
        return false;
    }
    std::array<uint8_t, 32> program{};
    if (!ProgramFromManager(mgr, program)) return false;
    std::vector<uint8_t> plain;
    if (!mgr.serialize_dual_keys(plain)) return false;

    const bool enc = IsCrypted() && !vMasterKey.empty();
    std::vector<uint8_t> payload = plain;
    if (enc) {
        CKeyingMaterial secret(plain.begin(), plain.end());
        std::vector<unsigned char> cipher;
        if (!EncryptSecret(vMasterKey, secret, g_quantum_wallet_iv, cipher)) {
            return false;
        }
        payload = std::move(cipher);
    }
    const std::vector<uint8_t> packed = PackQuantumDbRecord(enc, program, payload);

    if (!RunWithinTxn(GetDatabase(), "quantum_create", [&](WalletBatch& wbatch) {
            return wbatch.WriteQuantumState(packed);
        })) {
        return false;
    }

    m_quantum_program_bytes = program;
    m_quantum_secret_storage = payload;
    m_quantum_secret_is_encrypted = enc;
    m_quantum_manager = std::make_unique<crypto::quantum_safe_manager>();
    if (!m_quantum_manager->deserialize_dual_keys(plain) || !m_quantum_manager->ensure_modern_keys()) {
        m_quantum_manager.reset();
        return false;
    }
    return true;
}

bool CWallet::IsQuantumMine(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    if (!m_quantum_program_bytes.has_value()) return false;
    int witnessversion{-1};
    std::vector<unsigned char> witnessprogram;
    if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
        witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
        return false;
    }
    return std::memcmp(witnessprogram.data(), m_quantum_program_bytes->data(), 32) == 0;
}

std::optional<CTxDestination> CWallet::GetQuantumReceiveDestination() const
{
    AssertLockHeld(cs_wallet);
    if (!m_quantum_program_bytes.has_value()) return std::nullopt;
    const XOnlyPubKey xonly{std::span<const unsigned char>(m_quantum_program_bytes->data(), 32)};
    return WitnessV1Taproot{xonly};
}

bool CWallet::QuantumCanSign() const
{
    AssertLockHeld(cs_wallet);
    return static_cast<bool>(m_quantum_manager);
}

bool CWallet::TryLoadQuantumManagerAfterUnlock()
{
    AssertLockHeld(cs_wallet);
    m_quantum_manager.reset();
    if (!m_quantum_program_bytes.has_value() || m_quantum_secret_storage.empty()) {
        return true;
    }
    if (!m_quantum_secret_is_encrypted) {
        m_quantum_manager = std::make_unique<crypto::quantum_safe_manager>();
        if (!m_quantum_manager->deserialize_dual_keys(m_quantum_secret_storage) || !m_quantum_manager->ensure_modern_keys()) {
            m_quantum_manager.reset();
            return false;
        }
        return true;
    }
    if (vMasterKey.empty()) return false;
    CKeyingMaterial plain;
    if (!DecryptSecret(vMasterKey, std::span<const unsigned char>(m_quantum_secret_storage.data(), m_quantum_secret_storage.size()), g_quantum_wallet_iv, plain)) {
        return false;
    }
    std::vector<uint8_t> plain_vec(plain.begin(), plain.end());
    m_quantum_manager = std::make_unique<crypto::quantum_safe_manager>();
    if (!m_quantum_manager->deserialize_dual_keys(plain_vec) || !m_quantum_manager->ensure_modern_keys()) {
        m_quantum_manager.reset();
        return false;
    }
    return true;
}

void CWallet::WipeQuantumSecretsFromMemory()
{
    AssertLockHeld(cs_wallet);
    m_quantum_manager.reset();
}

bool CWallet::EncryptQuantumKeysForWallet(const CKeyingMaterial& master_key, WalletBatch* batch)
{
    AssertLockHeld(cs_wallet);
    if (!m_quantum_manager || !m_quantum_program_bytes.has_value()) {
        return true;
    }
    std::vector<uint8_t> plain;
    if (!m_quantum_manager->serialize_dual_keys(plain)) return false;
    CKeyingMaterial secret(plain.begin(), plain.end());
    std::vector<unsigned char> cipher;
    if (!EncryptSecret(master_key, secret, g_quantum_wallet_iv, cipher)) return false;
    const std::vector<uint8_t> packed = PackQuantumDbRecord(true, *m_quantum_program_bytes, cipher);
    if (batch) {
        if (!batch->WriteQuantumState(packed)) return false;
    } else {
        WalletBatch wb(GetDatabase());
        if (!wb.WriteQuantumState(packed)) return false;
    }
    m_quantum_secret_storage = cipher;
    m_quantum_secret_is_encrypted = true;
    return true;
}

bool CWallet::SignQuantumTransactionSighash(const uint256& sighash, std::span<const unsigned char> output_program, std::vector<unsigned char>& xmss_sig, std::vector<unsigned char>& sphincs_sig, std::vector<unsigned char>& dual_pubkey_bundle) const
{
    AssertLockHeld(cs_wallet);
    if (output_program.size() != WITNESS_V1_TAPROOT_SIZE) return false;
    if (!m_quantum_manager) return false;
    if (!m_quantum_program_bytes.has_value()) return false;
    if (std::memcmp(output_program.data(), m_quantum_program_bytes->data(), 32) != 0) return false;

    CWallet* const pw = const_cast<CWallet*>(this);
    crypto::quantum_safe_manager& mgr = *m_quantum_manager;

    bool success = false;
    const bool enc = m_quantum_secret_is_encrypted;

    if (!RunWithinTxn(pw->GetDatabase(), "quantum_sign", [&](WalletBatch& batch) {
            if (!RecoverPendingXmssDb(batch, mgr)) return false;

            std::vector<unsigned char> bundle = mgr.get_dual_public_key_bundle();
            if (bundle.empty()) return false;
            unsigned char bundle_hash[WITNESS_V1_TAPROOT_SIZE];
            CSHA256().Write(bundle.data(), bundle.size()).Finalize(bundle_hash);
            if (std::memcmp(bundle_hash, output_program.data(), output_program.size()) != 0) return false;

            const auto reserved_index = mgr.get_xmss_index();
            if (!reserved_index.has_value()) return false;
            if (!batch.WriteQuantumPending(*reserved_index)) return false;

            xmss_sig = mgr.sign(sighash, crypto::quantum_algorithm::XMSS);
            sphincs_sig = mgr.sign(sighash, crypto::quantum_algorithm::SPHINCS_PLUS);
            if (xmss_sig.size() != BYZE_XMSS_SIGNATURE_SIZE || sphincs_sig.size() != BYZE_SPHINCS_SIGNATURE_SIZE) {
                return false;
            }

            const auto post_sign_index = mgr.get_xmss_index();
            if (!post_sign_index.has_value()) return false;
            if (*post_sign_index < *reserved_index + 1) {
                if (!mgr.set_xmss_index(*reserved_index + 1)) return false;
            }

            std::vector<uint8_t> plain_after;
            if (!mgr.serialize_dual_keys(plain_after)) return false;

            std::vector<uint8_t> payload = plain_after;
            if (enc) {
                if (pw->vMasterKey.empty()) return false;
                CKeyingMaterial secret(plain_after.begin(), plain_after.end());
                std::vector<unsigned char> cipher;
                if (!EncryptSecret(pw->vMasterKey, secret, g_quantum_wallet_iv, cipher)) return false;
                payload = std::move(cipher);
            }
            const std::array<uint8_t, 32> prog_copy = *m_quantum_program_bytes;
            const std::vector<uint8_t> packed = PackQuantumDbRecord(enc, prog_copy, payload);
            if (!batch.WriteQuantumState(packed)) return false;
            batch.EraseQuantumPending();

            pw->m_quantum_secret_storage = std::move(payload);
            dual_pubkey_bundle = std::move(bundle);
            success = true;
            return true;
        })) {
        return false;
    }
    return success;
}

} // namespace wallet
