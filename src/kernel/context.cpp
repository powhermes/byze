// Copyright (c) 2022 The Bitcoin Core developers
// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel/context.h>

#include <crypto/sha256.h>
#include <crypto/randomx_hash.h>
#include <logging.h>
#include <random.h>
#include <primitives/block.h>

#include <mutex>
#include <string>

namespace kernel {
Context::Context()
{
    static std::once_flag globals_initialized{};
    std::call_once(globals_initialized, []() {
        std::string sha256_algo = SHA256AutoDetect();
        LogInfo("Using the '%s' SHA256 implementation\n", sha256_algo);
        RandomInit();
        // Initialize RandomX early for Byze proof-of-work by calling RandomXHash
        // This ensures RandomX is ready before any blocks are processed
        CBlockHeader dummy;
        dummy.SetNull();
        (void)RandomXHash(dummy);  // Force initialization
    });
}


} // namespace kernel
