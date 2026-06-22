// Copyright (c) 2026 Byze developers
// Distributed under the MIT software license

#include <crypto/quantum_block_sign.h>

#include <crypto/quantum_safe.h>
#include <crypto/quantum_safe_config.h>
#include <logging.h>
#include <primitives/block.h>
#include <random.h>
#include <uint256.h>

#include <pthread.h>

#include <functional>
#include <mutex>

namespace crypto
{
namespace {

constexpr size_t QUANTUM_BLOCK_KEYGEN_STACK = 8 * 1024 * 1024;
constexpr size_t QUANTUM_BLOCK_SIGN_STACK = 16 * 1024 * 1024;

// Rotate the block signing key when this many leaves have been used.
// Tree height 10 → 1024 leaves total; rotate at 950 to keep a safety margin.
constexpr uint32_t XMSS_ROTATION_THRESHOLD = 950;

template <typename Fn>
bool RunOnLargeStack(size_t stack_size, Fn&& fn)
{
    struct Payload {
        Fn* fn;
        bool ok;
    };
    Payload payload{&fn, false};

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size);

    auto trampoline = [](void* arg) -> void* {
        auto* p = static_cast<Payload*>(arg);
        p->ok = (*p->fn)();
        return nullptr;
    };

    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, trampoline, &payload);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        LogError("Quantum block sign: pthread_create failed (%d)\n", rc);
        return false;
    }
    pthread_join(thread, nullptr);
    return payload.ok;
}

std::mutex g_block_signer_mutex;
quantum_safe_manager g_block_signer;
bool g_block_signer_ready{false};

bool EnsureBlockSignerKeys()
{
    if (g_block_signer_ready) return true;
    const bool ok = RunOnLargeStack(QUANTUM_BLOCK_KEYGEN_STACK, []() -> bool {
        std::lock_guard<std::mutex> lock(g_block_signer_mutex);
        if (g_block_signer_ready) return true;
        unsigned char seed[32];
        GetRandBytes(seed);
        if (!g_block_signer.generate_dual_keys_from_entropy_ikm(seed, sizeof(seed))) {
            LogError("Quantum block sign: key generation failed\n");
            return false;
        }
        g_block_signer_ready = true;
        return true;
    });
    return ok;
}

bool SignBlockWithReadyKeys(CBlock& block)
{
    const uint256 block_hash{block.GetHash()};
    std::vector<uint8_t> xmss_sig;
    std::vector<uint8_t> sphincs_sig;
    std::vector<uint8_t> dual_bundle;

    const bool ok = RunOnLargeStack(QUANTUM_BLOCK_SIGN_STACK, [&]() -> bool {
        std::lock_guard<std::mutex> lock(g_block_signer_mutex);
        if (!g_block_signer_ready) return false;

        // Auto-rotate XMSS key when leaves are nearly exhausted.
        // The pubkey bundle is embedded in every block, so rotation is
        // transparent to stratum — callers never need to track the key.
        const auto xmss_index = g_block_signer.get_xmss_index();
        if (xmss_index && *xmss_index >= XMSS_ROTATION_THRESHOLD) {
            LogPrintf("Quantum block sign: XMSS key exhaustion threshold reached "
                      "(index=%u), rotating to a fresh key pair\n", *xmss_index);
            unsigned char new_seed[32];
            GetRandBytes(new_seed);
            if (!g_block_signer.generate_dual_keys_from_entropy_ikm(new_seed, sizeof(new_seed))) {
                LogError("Quantum block sign: key rotation failed\n");
                return false;
            }
            LogPrintf("Quantum block sign: key rotation complete\n");
        }

        xmss_sig = g_block_signer.sign(block_hash, quantum_algorithm::XMSS);
        if (xmss_sig.size() != BYZE_XMSS_SIGNATURE_SIZE) {
            LogError("Quantum block sign: XMSS sign failed (size=%zu)\n", xmss_sig.size());
            return false;
        }
        sphincs_sig = g_block_signer.sign(block_hash, quantum_algorithm::SPHINCS_PLUS);
        if (sphincs_sig.size() != BYZE_SPHINCS_SIGNATURE_SIZE) {
            LogError("Quantum block sign: SPHINCS sign failed (size=%zu)\n", sphincs_sig.size());
            return false;
        }
        dual_bundle = g_block_signer.get_dual_public_key_bundle();
        return true;
    });

    if (!ok) return false;
    block.quantum_signatures.xmss_signature = std::move(xmss_sig);
    block.quantum_signatures.sphincs_signature = std::move(sphincs_sig);
    block.quantum_signatures.dual_public_key = std::move(dual_bundle);
    return true;
}

} // namespace

bool AttachQuantumBlockSignatures(CBlock& block, bool require_quantum, bool is_genesis)
{
    block.quantum_signatures.SetNull();
    if (!require_quantum || is_genesis) {
        return true;
    }
    if (!EnsureBlockSignerKeys()) {
        return false;
    }
    return SignBlockWithReadyKeys(block);
}

} // namespace crypto
