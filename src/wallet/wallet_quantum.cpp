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
#include <string_view>

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

/** Byze: change key space. Up to v0.2.5 the receive and change descriptors both used
 *  BuildQuantumIkm(master, descriptor_index), so change index i got the same program, XMSS
 *  tree and address as receive index i. Change addresses handed out from now on use
 *  master || LE32(change_index) || tag instead; the tag makes this IKM longer than any
 *  receive-space IKM (74 or 78 bytes), so the two key spaces can never meet. */
static constexpr std::string_view QUANTUM_CHANGE_IKM_TAG{"byze/quantum/change/v1"};
static constexpr size_t QUANTUM_CHANGE_IKM_SIZE{BIP32_EXTKEY_SIZE + sizeof(uint32_t) + QUANTUM_CHANGE_IKM_TAG.size()};

static bool DeriveQuantumManagerForKey(const CExtKey& master, const QuantumKeyRef& ref, crypto::quantum_safe_manager& mgr)
{
    if (!ref.change) return DeriveQuantumManagerAtIndex(master, ref.index, mgr);
    unsigned char ikm[QUANTUM_CHANGE_IKM_SIZE];
    master.Encode(ikm);
    WriteLE32(ikm + BIP32_EXTKEY_SIZE, ref.index);
    std::memcpy(ikm + BIP32_EXTKEY_SIZE + sizeof(uint32_t), QUANTUM_CHANGE_IKM_TAG.data(), QUANTUM_CHANGE_IKM_TAG.size());
    return mgr.generate_dual_keys_from_entropy_ikm(ikm, sizeof(ikm)) && mgr.ensure_modern_keys();
}

static bool DeriveQuantumProgramForKey(const CExtKey& master, const QuantumKeyRef& ref, std::array<uint8_t, 32>& program_out)
{
    if (!ref.change) return DeriveQuantumProgramAtIndex(master, ref.index, program_out);
    crypto::quantum_safe_manager mgr;
    return DeriveQuantumManagerForKey(master, ref, mgr) && ProgramFromManager(mgr, program_out);
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

// RAII guard for the wallet-wide QUANTUM_PENDING recovery record written by
// SignQuantumTransactionSighash before it calls XMSS sign. xmss_private_key::set_index() is a
// permanent stub that always returns false, so RecoverPendingXmssDb above can never actually
// fast-forward past a leftover pending index -- it just fails permanently instead, and since
// QUANTUM_PENDING is a single un-indexed key (not per receive-index), that blocks ALL future
// quantum signing wallet-wide, not just for the key involved. This guard closes the common case:
// it erases the record on any in-process exit from SignQuantumTransactionSighash (a refused sign
// on an exhausted key, or any other failure) unless explicitly disarmed once the signature has
// actually been persisted, so a refusal no longer bricks the wallet.
// NOT covered: a hard crash (power loss/OOM/SIGKILL) between the pending record's commit and the
// signature's persisted commit -- the destructor never runs, so the record is left behind and
// next load hits the same permanently-failing recovery path. That narrower window is a real,
// accepted residual gap; actually closing it means implementing set_index() properly, which is
// out of scope here (getting one-time-signature index rewind wrong is worse than this bug).
class QuantumPendingGuard
{
public:
    explicit QuantumPendingGuard(CWallet* wallet) : m_wallet(wallet) {}
    ~QuantumPendingGuard()
    {
        if (!m_armed) return;
        WalletBatch batch(m_wallet->GetDatabase());
        if (!batch.EraseQuantumPending()) {
            m_wallet->WalletLogPrintf("QuantumPendingGuard: failed to erase QUANTUM_PENDING on cleanup; "
                                       "quantum signing may be stuck wallet-wide until this is cleared\n");
        }
    }
    void Disarm() { m_armed = false; }

private:
    CWallet* m_wallet;
    bool m_armed{true};
};

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

static bool PersistQuantumKeyPacked(WalletBatch& batch, const QuantumKeyRef& ref, bool enc, uint8_t origin, const std::array<uint8_t, 32>& program, const std::vector<uint8_t>& payload)
{
    if (!ref.change) return PersistQuantumManagerPacked(batch, ref.index, enc, origin, program, payload);
    return batch.WriteQuantumChangeIndexState(ref.index, PackQuantumDbRecordV2(enc, origin, program, payload));
}

static bool ReadCachedQuantumProgram(WalletBatch& batch, const QuantumKeyRef& ref, std::array<uint8_t, 32>& program)
{
    std::vector<unsigned char> packed;
    const bool found = ref.change ? batch.ReadQuantumChangeIndexState(ref.index, packed) : batch.ReadQuantumIndexState(ref.index, packed);
    if (!found || packed.empty()) return false;
    uint8_t fmt{0}, origin{0};
    bool enc{false};
    std::vector<uint8_t> payload;
    return UnpackQuantumDbRecord(packed, fmt, origin, enc, program, payload);
}

bool CWallet::PersistQuantumManagerForReceiveIndex(uint32_t receive_index, crypto::quantum_safe_manager& mgr)
{
    AssertLockHeld(cs_wallet);
    return PersistQuantumManagerForKey(QuantumKeyRef{.change = false, .index = receive_index}, mgr);
}

bool CWallet::PersistQuantumManagerForKey(const QuantumKeyRef& ref, crypto::quantum_safe_manager& mgr)
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
    if (!PersistQuantumKeyPacked(batch, ref, enc, m_quantum_key_origin, program, payload)) return false;

    if (ref.change) {
        m_quantum_change_programs[program] = ref.index;
    } else if (ref.index == 0) {
        m_quantum_program_bytes = program;
        m_quantum_secret_storage = payload;
        m_quantum_secret_is_encrypted = enc;
    }
    return true;
}

bool CWallet::LoadQuantumManagerForKey(const QuantumKeyRef& ref, crypto::quantum_safe_manager& mgr) const
{
    AssertLockHeld(cs_wallet);
    if (!ref.change) return LoadQuantumManagerForReceiveIndex(ref.index, mgr);

    WalletBatch batch(GetDatabase());
    std::vector<unsigned char> packed;
    if (batch.ReadQuantumChangeIndexState(ref.index, packed) && !packed.empty()) {
        return UnpackQuantumBlobToManager(packed, mgr);
    }
    const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
    if (!master) return false;
    return DeriveQuantumManagerForKey(*master, ref, mgr);
}

bool CWallet::EnsureQuantumChangeIndexState(uint32_t change_index)
{
    AssertLockHeld(cs_wallet);
    const QuantumKeyRef ref{.change = true, .index = change_index};
    {
        WalletBatch batch(GetDatabase());
        std::array<uint8_t, 32> program{};
        if (ReadCachedQuantumProgram(batch, ref, program)) {
            m_quantum_change_programs[program] = change_index;
            return true;
        }
    }
    crypto::quantum_safe_manager mgr;
    if (!LoadQuantumManagerForKey(ref, mgr)) return false;
    return PersistQuantumManagerForKey(ref, mgr);
}

std::optional<int32_t> CWallet::QuantumChangeBaseFor(const ScriptPubKeyMan& spkm) const
{
    AssertLockHeld(cs_wallet);
    // Only the active change descriptor uses the change key space. Any other descriptor
    // (receive, inactive or imported) derives exactly as before.
    if (GetScriptPubKeyMan(OutputType::BECH32M, /*internal=*/true) != &spkm) return std::nullopt;
    const auto* desc = dynamic_cast<const DescriptorScriptPubKeyMan*>(&spkm);
    if (!desc) return std::nullopt;
    const uint256 id = desc->GetID();
    if (const auto it = m_quantum_change_base.find(id); it != m_quantum_change_base.end()) return it->second;
    int32_t base{0};
    if (WalletBatch(GetDatabase()).ReadQuantumChangeBase(id, base) && base >= 0) {
        m_quantum_change_base[id] = base;
        return base;
    }
    return std::nullopt;
}

std::optional<CTxDestination> CWallet::GetQuantumTaprootForSpkmIndex(const ScriptPubKeyMan& spkm, int32_t index) const
{
    AssertLockHeld(cs_wallet);
    // Descriptors being created are not active yet, so receive and change cannot be told
    // apart; SetupDescriptorScriptPubKeyMans() registers their lookahead once they are.
    if (m_quantum_setup_in_progress || index < 0) return std::nullopt;
    const std::optional<int32_t> base = QuantumChangeBaseFor(spkm);
    // Receive descriptor, or a change index handed out before the change key space existed:
    // keep the original (receive key space) derivation so no existing address ever changes.
    if (!base || index < *base) return GetQuantumTaprootAtIndex(static_cast<uint32_t>(index));

    const QuantumKeyRef ref{.change = true, .index = static_cast<uint32_t>(index - *base)};
    std::array<uint8_t, 32> program{};
    bool have_program{false};
    {
        WalletBatch batch(GetDatabase());
        have_program = ReadCachedQuantumProgram(batch, ref, program);
    }
    if (!have_program) {
        const std::optional<CExtKey> master = TryGetTaprootDescriptorRootExtKey();
        if (!master) return std::nullopt;
        if (!DeriveQuantumProgramForKey(*master, ref, program)) return std::nullopt;
    }
    m_quantum_change_programs[program] = ref.index;
    const XOnlyPubKey xonly{std::span<const unsigned char>(program.data(), 32)};
    return WitnessV1Taproot{xonly};
}

bool CWallet::EnsureQuantumStateForSpkmIndex(const ScriptPubKeyMan& spkm, int32_t index)
{
    LOCK(cs_wallet);
    if (index < 0) return false;
    const std::optional<int32_t> base = QuantumChangeBaseFor(spkm);
    if (!base || index < *base) return EnsureQuantumIndexStateForReceiveIndex(static_cast<uint32_t>(index));

    if (!IsWalletFlagSet(WALLET_FLAG_QUANTUM_CHANGE_DOMAIN)) {
        // First change-key-space address of this wallet: pin the base on disk, and mark the
        // wallet so that releases which derive change in the receive key space (and so would
        // not see these outputs) refuse to load it rather than show a short balance.
        const auto* desc = dynamic_cast<const DescriptorScriptPubKeyMan*>(&spkm);
        if (!desc || !WalletBatch(GetDatabase()).WriteQuantumChangeBase(desc->GetID(), *base)) return false;
        SetWalletFlag(WALLET_FLAG_QUANTUM_CHANGE_DOMAIN);
    }
    return EnsureQuantumChangeIndexState(static_cast<uint32_t>(index - *base));
}

void CWallet::LoadQuantumChangeIndexStates()
{
    AssertLockHeld(cs_wallet);
    m_quantum_change_programs.clear();
    m_quantum_change_base.clear();
    const auto* spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(GetScriptPubKeyMan(OutputType::BECH32M, /*internal=*/true));
    if (!spkm) return;
    const uint256 id = spkm->GetID();
    int32_t next_index{0};
    {
        LOCK(spkm->cs_desc_man);
        next_index = spkm->GetWalletDescriptor().next_index;
    }
    int32_t base{0};
    if (!WalletBatch(GetDatabase()).ReadQuantumChangeBase(id, base) || base < 0) {
        if (IsWalletFlagSet(WALLET_FLAG_QUANTUM_CHANGE_DOMAIN)) {
            WalletLogPrintf("%s: wallet uses the quantum change key space but its base record is missing; change addresses will not be handed out\n", __func__);
            return;
        }
        // Wallet created before the change key space existed: every change address handed out
        // so far is below next_index and keeps its receive-key-space program. New change starts
        // here; the base is written when the first such address is handed out.
        m_quantum_change_base[id] = next_index;
        return;
    }
    m_quantum_change_base[id] = base;
    for (int32_t idx = base; idx < next_index; ++idx) {
        if (!EnsureQuantumChangeIndexState(static_cast<uint32_t>(idx - base))) {
            WalletLogPrintf("%s: could not load quantum change state for change index %d\n", __func__, idx - base);
        }
    }
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

std::optional<uint32_t> CWallet::GetQuantumSignaturesRemaining(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    int witnessversion{-1};
    std::vector<unsigned char> witnessprogram;
    if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
        witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
        return std::nullopt;
    }
    const std::optional<QuantumKeyRef> ref = FindQuantumKeyForProgram(witnessprogram);
    if (!ref) return std::nullopt;

    CWallet* const pw = const_cast<CWallet*>(this);
    crypto::quantum_safe_manager mgr;
    if (!ref->change && ref->index == 0 && m_quantum_manager &&
        m_quantum_program_bytes.has_value() &&
        std::memcmp(witnessprogram.data(), m_quantum_program_bytes->data(), 32) == 0) {
        return m_quantum_manager->get_xmss_remaining_signatures();
    }
    if (!pw->LoadQuantumManagerForKey(*ref, mgr)) return std::nullopt;
    return mgr.get_xmss_remaining_signatures();
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
        LoadQuantumChangeIndexStates();
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
    LoadQuantumChangeIndexStates();
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
    if (FindQuantumKeyForProgram(witnessprogram)) {
        return true;
    }
    if (!m_quantum_program_bytes.has_value()) {
        return false;
    }
    const bool matches_index0 = std::memcmp(witnessprogram.data(), m_quantum_program_bytes->data(), 32) == 0;
    return matches_index0;
}

bool CWallet::IsUnspendableDescriptorTaproot(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    int witnessversion{-1};
    std::vector<unsigned char> witnessprogram;
    if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
        witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
        return false;
    }
    // Only scripts a descriptor SPKM registered (SetCache/TopUp) can be plain tr() keys; a
    // quantum program that is mine only via the IsQuantumMine() fallback is not cached.
    // Deliberately NOT decided via IsQuantumMine(): FindReceiveIndexForQuantumProgram()
    // matches any witness-v1 script in the SPKM maps, and after a reload SetCache() has put
    // the plain tr() scripts there, so IsQuantumMine() is true for them too. Instead ask the
    // owning descriptor whether this is its own standard expansion at that index (a quantum
    // program substituted by GetNewDestination/TopUp never is).
    const auto it = m_cached_spks.find(script);
    if (it == m_cached_spks.end()) return false;
    for (const ScriptPubKeyMan* spkm : it->second) {
        const auto* desc_spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(spkm);
        if (desc_spkm && desc_spkm->IsDescriptorExpansionScript(script)) return true;
    }
    return false;
}

bool CWallet::IsWalletDerivedPlainTaproot(const CScript& script) const
{
    AssertLockHeld(cs_wallet);
    if (IsUnspendableDescriptorTaproot(script)) return true;
    int witnessversion{-1};
    std::vector<unsigned char> witnessprogram;
    if (!script.IsWitnessProgram(witnessversion, witnessprogram) || witnessversion != 1 ||
        witnessprogram.size() != WITNESS_V1_TAPROOT_SIZE) {
        return false;
    }
    for (const auto& spk_pair : m_spk_managers) {
        const auto* spkm = dynamic_cast<const DescriptorScriptPubKeyMan*>(spk_pair.second.get());
        if (spkm && spkm->IsSubstitutedExpansion(script)) return true;
    }
    return false;
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

std::optional<QuantumKeyRef> CWallet::FindQuantumKeyForProgram(std::span<const unsigned char> program) const
{
    AssertLockHeld(cs_wallet);
    if (program.size() != WITNESS_V1_TAPROOT_SIZE) return std::nullopt;
    std::array<uint8_t, 32> key{};
    std::memcpy(key.data(), program.data(), key.size());
    if (const auto it = m_quantum_change_programs.find(key); it != m_quantum_change_programs.end()) {
        return QuantumKeyRef{.change = true, .index = it->second};
    }
    if (const auto index = FindReceiveIndexForQuantumProgram(program)) {
        return QuantumKeyRef{.change = false, .index = *index};
    }
    return std::nullopt;
}

std::optional<uint32_t> CWallet::FindReceiveIndexForQuantumProgram(std::span<const unsigned char> program) const
{
    AssertLockHeld(cs_wallet);
    if (program.size() != WITNESS_V1_TAPROOT_SIZE) return std::nullopt;
    {
        // A change-key-space program sits in the change descriptor's script map at its
        // descriptor index, which is not a receive index: never report it as one.
        std::array<uint8_t, 32> key{};
        std::memcpy(key.data(), program.data(), key.size());
        if (m_quantum_change_programs.contains(key)) return std::nullopt;
    }

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
                // Byze: SetCache() (wallet load) registers each descriptor's own standard
                // expansion, e.g. the plain BIP86 key of tr(); that is never a quantum program.
                if (spkm->IsDescriptorExpansionScript(script)) continue;
                return static_cast<uint32_t>(index);
            }
        }
        // Fallback: coinbase/mining UTXOs may not be present in the script map yet; scan
        // deterministic receive indices up to the descriptor's next index.
        const int32_t scan_upto = spkm->GetWalletDescriptor().next_index;
        if (scan_upto > 0) {
            // Byze: read each index's CACHED 32-byte program instead of running a full XMSS
            // keygen per index. IsQuantumMine() calls this for every output processed during
            // wallet load, so deriving here is O(txouts * next_index) keygens and effectively
            // hangs load on a wallet with any history. Every handed-out index is cached; an
            // uncached index below next_index was never actually used, so skipping it is safe.
            WalletBatch batch(GetDatabase());
            for (int32_t idx = 0; idx < scan_upto; ++idx) {
                std::vector<unsigned char> packed;
                if (!batch.ReadQuantumIndexState(idx, packed) || packed.empty()) continue;
                uint8_t fmt = 0, origin = 0;
                bool enc = false;
                std::array<uint8_t, 32> cached_prog{};
                std::vector<uint8_t> payload;
                if (!UnpackQuantumDbRecord(packed, fmt, origin, enc, cached_prog, payload)) continue;
                if (std::memcmp(cached_prog.data(), program.data(), program.size()) == 0) {
                    return static_cast<uint32_t>(idx);
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

    const std::optional<QuantumKeyRef> ref = FindQuantumKeyForProgram(output_program);
    if (!ref) return false;

    CWallet* const pw = const_cast<CWallet*>(this);
    crypto::quantum_safe_manager mgr_local;
    crypto::quantum_safe_manager* mgr{nullptr};
    if (!ref->change && ref->index == 0 && m_quantum_manager &&
        m_quantum_program_bytes.has_value() &&
        std::memcmp(output_program.data(), m_quantum_program_bytes->data(), 32) == 0) {
        mgr = m_quantum_manager.get();
    } else if (!pw->LoadQuantumManagerForKey(*ref, mgr_local)) {
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
        WalletLogPrintf("%s: quantum bundle hash does not match output program for %s index %u\n", __func__, ref->change ? "change" : "receive", ref->index);
        return false;
    }

    const auto reserved_index = mgr->get_xmss_index();
    if (!reserved_index.has_value()) return false;

    if (!RunWithinTxn(pw->GetDatabase(), "quantum_sign_pending", [&](WalletBatch& batch) {
            return batch.WriteQuantumPending(*reserved_index);
        })) {
        return false;
    }
    // Erases the pending record on every exit below (a refused/failed sign included) unless
    // Disarm() is reached, which only happens once the signature is durably persisted.
    QuantumPendingGuard pending_guard(pw);

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
            if (!PersistQuantumKeyPacked(batch, *ref, enc, pw->m_quantum_key_origin, program, payload)) return false;
            batch.EraseQuantumPending();
            if (!ref->change && ref->index == 0) {
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
    // Only reached once the persist txn (which erases QUANTUM_PENDING as part of the same
    // atomic commit, above) has actually committed -- the guard's own erase would be redundant
    // here, and must stay armed on every other return path so it can clean up after a rollback.
    pending_guard.Disarm();
    return success;
}

std::optional<uint32_t> CWallet::GetQuantumXmssSigningIndex() const
{
    AssertLockHeld(cs_wallet);
    if (!m_quantum_manager) return std::nullopt;
    return m_quantum_manager->get_xmss_index();
}

} // namespace wallet
