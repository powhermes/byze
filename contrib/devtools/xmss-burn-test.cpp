// Standalone, non-production test harness: burns a tiny XMSS tree down to
// exhaustion to study the get_index()/set_index() boundary bug directly,
// without RPC/wallet overhead. Not linked into any production binary.
#include <crypto/quantum_safe.h>
#include <uint256.h>
#include <util/translation.h>

#include <chrono>
#include <cstdio>
#include <cstring>

const TranslateFn G_TRANSLATION_FUN{nullptr};

static uint256 MakeMessage(uint32_t i)
{
    uint256 h;
    std::memcpy(h.begin(), &i, sizeof(i));
    return h;
}

int main(int argc, char** argv)
{
    using namespace crypto;

    const uint32_t attempts = argc > 1 ? static_cast<uint32_t>(std::stoul(argv[1])) : 1030;

    quantum_safe_manager mgr;
    if (!mgr.generate_dual_keys(/*xmss_tree_height=*/10, /*sphincs_level=*/1)) {
        std::printf("generate_dual_keys FAILED\n");
        return 1;
    }
    std::printf("keys generated. has_dual_keys=%d\n", mgr.has_dual_keys());

    const auto t0 = std::chrono::steady_clock::now();
    uint32_t first_empty_sig = UINT32_MAX;
    uint32_t first_fallback_hit = UINT32_MAX;

    for (uint32_t i = 0; i < attempts; ++i) {
        const auto idx_before = mgr.get_xmss_index();
        const auto remain_before = mgr.get_xmss_remaining_signatures();
        const bool verbose = !remain_before.has_value() || *remain_before <= 5 || i < 3;

        if (verbose) {
            std::printf("--- attempt %u: index_before=%s remain_before=%s\n", i,
                        idx_before ? std::to_string(*idx_before).c_str() : "nullopt",
                        remain_before ? std::to_string(*remain_before).c_str() : "nullopt");
        } else if (i % 100 == 0) {
            std::printf("... attempt %u: remain_before=%s\n", i,
                        remain_before ? std::to_string(*remain_before).c_str() : "nullopt");
        }

        const uint256 msg = MakeMessage(i);
        std::vector<uint8_t> sig = mgr.sign(msg, quantum_algorithm::XMSS);
        if (sig.empty() && first_empty_sig == UINT32_MAX) first_empty_sig = i;

        const auto idx_after = mgr.get_xmss_index();
        const auto remain_after = mgr.get_xmss_remaining_signatures();

        if (verbose) {
            std::printf("    sign() returned %zu bytes; index_after=%s remain_after=%s\n", sig.size(),
                        idx_after ? std::to_string(*idx_after).c_str() : "nullopt",
                        remain_after ? std::to_string(*remain_after).c_str() : "nullopt");
            if (!sig.empty()) {
                std::printf("    verify()=%d\n", mgr.verify(msg, sig, quantum_algorithm::XMSS));
            }
        }

        if (idx_before.has_value() && idx_after.has_value() && *idx_after < *idx_before + 1) {
            if (first_fallback_hit == UINT32_MAX) first_fallback_hit = i;
            if (verbose) std::printf("    [fallback needed] calling set_xmss_index(%u)\n", *idx_before + 1);
            bool set_ok = mgr.set_xmss_index(*idx_before + 1);
            if (verbose) std::printf("    set_xmss_index result=%d\n", set_ok);
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("\n=== summary: %u attempts in %.2fs (%.3fs/attempt) ===\n", attempts, secs, secs / attempts);
    std::printf("=== first empty signature at attempt: %s ===\n",
                first_empty_sig == UINT32_MAX ? "never" : std::to_string(first_empty_sig).c_str());
    std::printf("=== first index-correction-fallback needed at attempt: %s ===\n",
                first_fallback_hit == UINT32_MAX ? "never" : std::to_string(first_fallback_hit).c_str());
    return 0;
}
