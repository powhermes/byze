// Copyright (c) 2026 The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <addresstype.h>
#include <crypto/quantum_safe_config.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <pubkey.h>
#include <script/interpreter.h>
#include <serialize.h>
#include <wallet/crypter.h>
#include <wallet/wallet.h>
#include <wallet/walletquantum.h>
#include <wallet/walletdb.h>

#include <compat/endian.h>
#include <cstring>

namespace wallet {
namespace {

/** Index 0 uses master-only IKM (legacy poolwallet quantum program); higher indices append LE32(index). */
static size_t BuildQuantumIkm(const CExtKey& master, uint32_t index, unsigned char* ikm)
{
    master.Encode(ikm);
    if (index == 0) {
        return BIP32_EXTKEY_SIZE;
    }
    WriteLE32(ikm + BIP32_EXTKEY_SIZE, index);
    return BIP32_EXTKEY_SIZE + sizeof(uint32_t);
}

static bool ProgramFromManager(const crypto::quantum_safe_manager& mgr, std::array<uint8_t, 32>& out)
{
    const std::vector<uint8_t> bundle = mgr.get_dual_public_key_bundle();
    if (bundle.empty()) return false;
    unsigned char bundle_hash[32];
    CSHA256().Write(bundle.data(), bundle.size()).Finalize(bundle_hash);
    std::memcpy(out.data(), bundle_hash, 32);
    return true;
}

static bool DeriveQuantumProgramAtIndex(const CExtKey& master, uint32_t index, std::array<uint8_t, 32>& program_out)
{
    unsigned char ikm[BIP32_EXTKEY_SIZE + sizeof(uint32_t)];
    const size_t ikm_len = BuildQuantumIkm(master, index, ikm);

    crypto::quantum_safe_manager mgr;
    if (!mgr.generate_dual_keys_from_entropy_ikm(ikm, ikm_len) || !mgr.ensure_modern_keys()) {
        return false;
    }
    return ProgramFromManager(mgr, program_out);
}

static bool DeriveQuantumManagerAtIndex(const CExtKey& master, uint32_t index, crypto::quantum_safe_manager& mgr)
{
    unsigned char ikm[BIP32_EXTKEY_SIZE + sizeof(uint32_t)];
    const size_t ikm_len = BuildQuantumIkm(master, index, ikm);
    return mgr.generate_dual_keys_from_entropy_ikm(ikm, ikm_len) && mgr.ensure_modern_keys();
}

static std::unique_ptr<crypto::quantum_safe_manager> CloneQuantumManager(const crypto::quantum_safe_manager& src)
{
    std::vector<uint8_t> plain;
    if (!src.serialize_dual_keys(plain)) return nullptr;
    auto out = std::make_unique<crypto::quantum_safe_manager>();
    if (!out->deserialize_dual_keys(plain) || !out->ensure_modern_keys()) return nullptr;
    return out;
}

static const uint256 g_quantum_wallet_iv = Hash(std::string_view{"byze_wallet_quantum_iv_v1"});

/** Packed on-disk layout v1: [1][enc_flag][32 program][payload] */
static constexpr uint8_t QUANTUM_PACK_V1 = 1;
/** Packed on-disk layout v2: [2][enc_flag][origin][32 program][payload] */
static constexpr uint8_t QUANTUM_PACK_V2 = 2;
/** Quantum key material derived from the same BIP32 extended secret as taproot descriptors. */
static constexpr uint8_t QUANTUM_ORIGIN_HD_MASTER = 1;

static std::vector<uint8_t> PackQuantumDbRecordV1(bool encrypted, const std::array<uint8_t, 32>& program, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out;
    out.reserve(1 + 1 + 32 + payload.size());
    out.push_back(QUANTUM_PACK_V1);
    out.push_back(encrypted ? uint8_t{1} : uint8_t{0});
    out.insert(out.end(), program.begin(), program.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static std::vector<uint8_t> PackQuantumDbRecordV2(bool encrypted, uint8_t origin, const std::array<uint8_t, 32>& program, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out;
    out.reserve(1 + 1 + 1 + 32 + payload.size());
    out.push_back(QUANTUM_PACK_V2);
    out.push_back(encrypted ? uint8_t{1} : uint8_t{0});
    out.push_back(origin);
    out.insert(out.end(), program.begin(), program.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

static bool UnpackQuantumDbRecord(const std::vector<uint8_t>& raw, uint8_t& format_out, uint8_t& origin_out, bool& encrypted, std::array<uint8_t, 32>& program, std::vector<uint8_t>& payload)
{
    format_out = 0;
    origin_out = 0;
    if (raw.size() < 1 + 1 + 32) return false;
    format_out = raw[0];
    if (format_out == QUANTUM_PACK_V1) {
        encrypted = raw[1] != 0;
        origin_out = 0;
        std::memcpy(program.data(), raw.data() + 2, 32);
        payload.assign(raw.begin() + 34, raw.end());
        return true;
    }
    if (format_out == QUANTUM_PACK_V2) {
        if (raw.size() < 1 + 1 + 1 + 32) return false;
        encrypted = raw[1] != 0;
        origin_out = raw[2];
        std::memcpy(program.data(), raw.data() + 3, 32);
        payload.assign(raw.begin() + 35, raw.end());
        return true;
    }
    return false;
}

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

bool CWallet::UnpackQuantumBlobToManager(const std::vector<unsigned char>& packed, crypto::quantum_safe_manager& mgr) const
{
    AssertLockHeld(cs_wallet);
    bool encrypted{false};
    std::array<uint8_t, 32> program{};
    std::vector<uint8_t> payload;
    uint8_t fmt{0};
    uint8_t origin{0};
    if (!UnpackQuantumDbRecord(packed, fmt, origin, encrypted, program, payload)) {
        return false;
    }

    std::vector<uint8_t> plain = payload;
    if (encrypted) {
        if (vMasterKey.empty()) return false;
        CKeyingMaterial secret;
        if (!DecryptSecret(vMasterKey, std::span<const unsigned char>(payload.data(), payload.size()), g_quantum_wallet_iv, secret)) {
            return false;
        }
        plain.assign(secret.begin(), secret.end());
    }

    if (!mgr.deserialize_dual_keys(plain) || !mgr.ensure_modern_keys()) {
        return false;
    }
    std::array<uint8_t, 32> derived{};
    if (!ProgramFromManager(mgr, derived) || derived != program) {
        return false;
    }
    return true;
}

bool CWallet::LoadQuantumManagerForReceiveIndex(uint32_t receive_index, crypto::quantum_safe_manager& mgr) const
{
    AssertLockHeld(cs_wallet);

    WalletBatch batch(GetDatabase());
    std::vector<unsigned char> packed;
    if (batch.ReadQuantumIndexState(receive_index, packed) && !packed.empty()) {
        return UnpackQuantumBlobToManager(packed, mgr);
    }

    if (receive_index == 0) {
        std::vector<unsigned char> legacy;
        if (batch.ReadQuantumState(legacy) && !legacy.empty()) {
            return UnpackQuantumBlobToManager(legacy, mgr);
        }
    }

    const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
    if (!master) return false;
    return DeriveQuantumManagerAtIndex(*master, receive_index, mgr);
}

static bool PersistQuantumManagerPacked(WalletBatch& batch, uint32_t receive_index, bool enc, uint8_t origin, const std::array<uint8_t, 32>& program, const std::vector<uint8_t>& payload)
{
    const std::vector<uint8_t> packed = PackQuantumDbRecordV2(enc, origin, program, payload);
    if (!batch.WriteQuantumIndexState(receive_index, packed)) return false;
    if (receive_index == 0 && !batch.WriteQuantumState(packed)) return false;
    return true;
}

bool CWallet::PersistQuantumManagerForReceiveIndex(uint32_t receive_index, crypto::quantum_safe_manager& mgr)
{
    AssertLockHeld(cs_wallet);

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

    std::array<uint8_t, 32> program{};
    if (!ProgramFromManager(mgr, program)) return false;

    WalletBatch batch(GetDatabase());
    if (!PersistQuantumManagerPacked(batch, receive_index, enc, m_quantum_key_origin, program, payload)) return false;

    if (receive_index == 0) {
        m_quantum_program_bytes = program;
        m_quantum_secret_storage = payload;
        m_quantum_secret_is_encrypted = enc;
    }
    return true;
}

bool CWallet::EnsureQuantumIndexStateForReceiveIndex(uint32_t receive_index)
{
    LOCK(cs_wallet);
    WalletBatch batch(GetDatabase());
    std::vector<unsigned char> packed;
    if (batch.ReadQuantumIndexState(receive_index, packed) && !packed.empty()) {
        return true;
    }
    crypto::quantum_safe_manager mgr;
    if (!LoadQuantumManagerForReceiveIndex(receive_index, mgr)) {
        return false;
    }
    return PersistQuantumManagerForReceiveIndex(receive_index, mgr);
}

void CWallet::RepairQuantumReceiveIndexStates()
{
    AssertLockHeld(cs_wallet);
    const ScriptPubKeyMan* raw = GetScriptPubKeyMan(OutputType::BECH32M, /*internal=*/false);
    const auto* spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(raw);
    if (!spkm) return;
    LOCK(spkm->cs_desc_man);
    const int32_t scan_upto = spkm->GetWalletDescriptor().next_index;
    for (int32_t idx = 0; idx < scan_upto; ++idx) {
        if (!EnsureQuantumIndexStateForReceiveIndex(static_cast<uint32_t>(idx))) {
            WalletLogPrintf("%s: could not ensure quantum index state for receive index %d\n", __func__, idx);
        }
    }
}

bool CWallet::IsQuantumSolvable(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    if (!QuantumCanSign()) return false;
    return IsQuantumMine(script);
}

DBErrors CWallet::ApplyQuantumStateFromPackedBlob(const std::vector<unsigned char>& raw)
{
    AssertLockHeld(cs_wallet);
    bool encrypted{false};
    std::array<uint8_t, 32> program{};
    std::vector<uint8_t> payload;
    uint8_t fmt{0};
    uint8_t origin{0};
    if (!UnpackQuantumDbRecord(raw, fmt, origin, encrypted, program, payload)) {
        WalletLogPrintf("%s: corrupt quantum state record\n", __func__);
        return DBErrors::CORRUPT;
    }
    m_quantum_blob_format = fmt;
    m_quantum_key_origin = origin;
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
    m_quantum_blob_format = QUANTUM_PACK_V2;
    m_quantum_key_origin = 0;

    std::vector<unsigned char> raw;
    if (!batch.Read(DBKeys::QUANTUM_STATE, raw) || raw.empty()) {
        return DBErrors::LOAD_OK;
    }
    const DBErrors err = ApplyQuantumStateFromPackedBlob(raw);
    if (err != DBErrors::LOAD_OK) {
        return err;
    }
    if (m_quantum_key_origin != QUANTUM_ORIGIN_HD_MASTER && !m_quantum_secret_is_encrypted) {
        WalletBatch wb(GetDatabase());
        if (!MaybeUpgradeQuantumStateToHdOrigin(wb)) {
            WalletLogPrintf("%s: legacy quantum record (origin=%u); datadir quantum_wallet.keys is not used\n",
                __func__, m_quantum_key_origin);
        }
    }
    RepairQuantumReceiveIndexStates();
    return DBErrors::LOAD_OK;
}

bool CWallet::MaybeUpgradeQuantumStateToHdOrigin(WalletBatch& batch)
{
    AssertLockHeld(cs_wallet);
    if (m_quantum_key_origin == QUANTUM_ORIGIN_HD_MASTER) {
        return true;
    }
    if (!m_quantum_program_bytes.has_value()) {
        return true;
    }
    const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
    if (!master) {
        return false;
    }

    unsigned char ext_bytes[BIP32_EXTKEY_SIZE];
    master->Encode(ext_bytes);
    crypto::quantum_safe_manager derived;
    if (!derived.generate_dual_keys_from_entropy_ikm(ext_bytes, BIP32_EXTKEY_SIZE) || !derived.ensure_modern_keys()) {
        return false;
    }
    std::array<uint8_t, 32> derived_program{};
    if (!ProgramFromManager(derived, derived_program)) {
        return false;
    }
    if (derived_program != *m_quantum_program_bytes) {
        WalletLogPrintf("%s: wallet DB quantum program does not match descriptor HD root; keeping legacy in-wallet keys\n", __func__);
        return false;
    }
    return PersistQuantumKeyMaterialFromHdMaster(batch, *master, /*overwrite_existing=*/true);
}

bool CWallet::PersistQuantumKeyMaterialFromHdMaster(WalletBatch& batch, const CExtKey& master_key, bool overwrite_existing)
{
    AssertLockHeld(cs_wallet);
    if (!overwrite_existing) {
        std::vector<uint8_t> existing;
        if (batch.ReadQuantumState(existing) && !existing.empty()) {
            return true;
        }
    }

    unsigned char ext_bytes[BIP32_EXTKEY_SIZE];
    master_key.Encode(ext_bytes);

    crypto::quantum_safe_manager mgr;
    if (!mgr.generate_dual_keys_from_entropy_ikm(ext_bytes, BIP32_EXTKEY_SIZE) || !mgr.ensure_modern_keys()) {
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
    const std::vector<uint8_t> packed = PackQuantumDbRecordV2(enc, QUANTUM_ORIGIN_HD_MASTER, program, payload);
    if (!batch.WriteQuantumState(packed)) {
        return false;
    }
    if (!batch.WriteQuantumIndexState(0, packed)) {
        return false;
    }

    m_quantum_blob_format = QUANTUM_PACK_V2;
    m_quantum_key_origin = QUANTUM_ORIGIN_HD_MASTER;
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

std::optional<CExtKey> CWallet::TryGetTaprootDescriptorRootExtKey() const
{
    AssertLockHeld(cs_wallet);
    const ScriptPubKeyMan* raw = GetScriptPubKeyMan(OutputType::BECH32M, /*internal=*/false);
    const auto* spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(raw);
    if (!spkm) return std::nullopt;
    CExtKey out;
    if (!spkm->ExportTaprootDescriptorRootExtKey(out)) return std::nullopt;
    return out;
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

    const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
    if (!master) {
        return false;
    }

    if (!RunWithinTxn(GetDatabase(), "quantum_create_hd", [&](WalletBatch& wbatch) {
            return PersistQuantumKeyMaterialFromHdMaster(wbatch, *master, /*overwrite_existing=*/false);
        })) {
        return false;
    }
    return m_quantum_program_bytes.has_value() && static_cast<bool>(m_quantum_manager);
}

bool CWallet::IsQuantumMine(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    int witnessversion{-1};
    std::vector<unsigned char> witnessprogram;
    if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
        witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
        return false;
    }
    if (FindReceiveIndexForQuantumProgram(witnessprogram)) {
        return true;
    }
    if (!m_quantum_program_bytes.has_value()) return false;
    return std::memcmp(witnessprogram.data(), m_quantum_program_bytes->data(), 32) == 0;
}

std::optional<CTxDestination> CWallet::GetQuantumTaprootAtIndex(uint32_t index) const
{
    AssertLockHeld(cs_wallet);
    std::array<uint8_t, 32> program{};
    bool have_program = false;

    // Fast path: a full XMSS keygen (1024-leaf Merkle tree) is required to derive an
    // index's program, so reuse the cached 32-byte program when this index was already
    // derived. Without this, keypool top-up would re-keygen every index on each call.
    {
        WalletBatch batch(GetDatabase());
        std::vector<unsigned char> packed;
        if (batch.ReadQuantumIndexState(index, packed) && !packed.empty()) {
            uint8_t fmt = 0, origin = 0;
            bool enc = false;
            std::vector<uint8_t> payload;
            if (UnpackQuantumDbRecord(packed, fmt, origin, enc, program, payload)) {
                have_program = true;
            }
        }
    }

    if (!have_program) {
        const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
        if (!master) return std::nullopt;
        if (!DeriveQuantumProgramAtIndex(*master, index, program)) return std::nullopt;
    }
    const XOnlyPubKey xonly{std::span<const unsigned char>(program.data(), 32)};
    return WitnessV1Taproot{xonly};
}

std::optional<uint32_t> CWallet::FindReceiveIndexForQuantumProgram(std::span<const unsigned char> program) const
{
    AssertLockHeld(cs_wallet);
    if (program.size() != WITNESS_V1_TAPROOT_SIZE) return std::nullopt;

    if (m_quantum_program_bytes.has_value() &&
        std::memcmp(program.data(), m_quantum_program_bytes->data(), program.size()) == 0) {
        return 0;
    }

    for (const auto& spk_pair : m_spk_managers) {
        const auto* spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(spk_pair.second.get());
        if (!spkm) continue;
        LOCK(spkm->cs_desc_man);
        for (const auto& [script, index] : spkm->GetScriptPubKeysMap()) {
            int witnessversion{-1};
            std::vector<unsigned char> witnessprogram;
            if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
                witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
                continue;
            }
            if (witnessprogram.size() == program.size() &&
                std::memcmp(witnessprogram.data(), program.data(), program.size()) == 0) {
                return static_cast<uint32_t>(index);
            }
        }
        // Fallback: coinbase/mining UTXOs may not be present in the script map yet; scan
        // deterministic receive indices up to the descriptor's next index.
        const int32_t scan_upto = spkm->GetWalletDescriptor().next_index;
        if (scan_upto > 0) {
            const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
            if (master) {
                for (int32_t idx = 0; idx < scan_upto; ++idx) {
                    std::array<uint8_t, 32> derived{};
                    if (!DeriveQuantumProgramAtIndex(*master, static_cast<uint32_t>(idx), derived)) {
                        continue;
                    }
                    if (std::memcmp(derived.data(), program.data(), program.size()) == 0) {
                        return static_cast<uint32_t>(idx);
                    }
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<CTxDestination> CWallet::GetQuantumReceiveDestination() const
{
    return GetQuantumTaprootAtIndex(0);
}

bool CWallet::QuantumCanSign() const
{
    AssertLockHeld(cs_wallet);
    if (m_quantum_manager) return true;
    if (IsCrypted() && vMasterKey.empty()) return false;
    return TryGetTaprootDescriptorRootExtKey().has_value();
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
    const std::vector<uint8_t> packed = m_quantum_blob_format >= QUANTUM_PACK_V2
        ? PackQuantumDbRecordV2(true, m_quantum_key_origin, *m_quantum_program_bytes, cipher)
        : PackQuantumDbRecordV1(true, *m_quantum_program_bytes, cipher);
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

    const std::optional<uint32_t> receive_index = FindReceiveIndexForQuantumProgram(output_program);
    if (!receive_index) return false;

    CWallet* const pw = const_cast<CWallet*>(this);
    crypto::quantum_safe_manager mgr_local;
    crypto::quantum_safe_manager* mgr{nullptr};
    if (*receive_index == 0 && m_quantum_manager &&
        m_quantum_program_bytes.has_value() &&
        std::memcmp(output_program.data(), m_quantum_program_bytes->data(), 32) == 0) {
        mgr = m_quantum_manager.get();
    } else if (!pw->LoadQuantumManagerForReceiveIndex(*receive_index, mgr_local)) {
        return false;
    } else {
        mgr = &mgr_local;
    }

    {
        WalletBatch batch_read(pw->GetDatabase());
        if (!RecoverPendingXmssDb(batch_read, *mgr)) return false;
    }

    std::vector<unsigned char> bundle = mgr->get_dual_public_key_bundle();
    if (bundle.empty()) return false;
    unsigned char bundle_hash[WITNESS_V1_TAPROOT_SIZE];
    CSHA256().Write(bundle.data(), bundle.size()).Finalize(bundle_hash);
    if (std::memcmp(bundle_hash, output_program.data(), output_program.size()) != 0) {
        WalletLogPrintf("%s: quantum bundle hash does not match output program for receive index %u\n", __func__, *receive_index);
        return false;
    }

    const auto reserved_index = mgr->get_xmss_index();
    if (!reserved_index.has_value()) return false;

    if (!RunWithinTxn(pw->GetDatabase(), "quantum_sign_pending", [&](WalletBatch& batch) {
            return batch.WriteQuantumPending(*reserved_index);
        })) {
        return false;
    }

    xmss_sig = mgr->sign(sighash, crypto::quantum_algorithm::XMSS);
    sphincs_sig = mgr->sign(sighash, crypto::quantum_algorithm::SPHINCS_PLUS);
    if (xmss_sig.size() != BYZE_XMSS_SIGNATURE_SIZE || sphincs_sig.size() != BYZE_SPHINCS_SIGNATURE_SIZE) {
        return false;
    }

    const auto post_sign_index = mgr->get_xmss_index();
    if (!post_sign_index.has_value()) return false;
    if (*post_sign_index < *reserved_index + 1) {
        if (!mgr->set_xmss_index(*reserved_index + 1)) return false;
    }

    bool success = false;
    if (!RunWithinTxn(pw->GetDatabase(), "quantum_sign_persist", [&](WalletBatch& batch) {
            std::vector<uint8_t> plain;
            if (!mgr->serialize_dual_keys(plain)) return false;
            const bool enc = pw->IsCrypted() && !pw->vMasterKey.empty();
            std::vector<uint8_t> payload = plain;
            if (enc) {
                CKeyingMaterial secret(plain.begin(), plain.end());
                std::vector<unsigned char> cipher;
                if (!EncryptSecret(pw->vMasterKey, secret, g_quantum_wallet_iv, cipher)) {
                    return false;
                }
                payload = std::move(cipher);
            }
            std::array<uint8_t, 32> program{};
            if (!ProgramFromManager(*mgr, program)) return false;
            if (!PersistQuantumManagerPacked(batch, *receive_index, enc, pw->m_quantum_key_origin, program, payload)) return false;
            batch.EraseQuantumPending();
            if (*receive_index == 0) {
                pw->m_quantum_program_bytes = program;
                pw->m_quantum_secret_storage = payload;
                pw->m_quantum_secret_is_encrypted = enc;
                pw->m_quantum_manager = CloneQuantumManager(*mgr);
                if (!pw->m_quantum_manager) return false;
            }
            dual_pubkey_bundle = std::move(bundle);
            success = true;
            return true;
        })) {
        return false;
    }
    return success;
}

std::optional<uint32_t> CWallet::GetQuantumXmssSigningIndex() const
{
    AssertLockHeld(cs_wallet);
    if (!m_quantum_manager) return std::nullopt;
    return m_quantum_manager->get_xmss_index();
}

} // namespace wallet
