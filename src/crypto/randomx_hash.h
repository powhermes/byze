// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#ifndef BITCOIN_CRYPTO_RANDOMX_HASH_H
#define BITCOIN_CRYPTO_RANDOMX_HASH_H

#include <uint256.h>
#include <vector>
#include <mutex>

class CBlockHeader;

// Forward declarations for RandomX types
struct randomx_cache;
struct randomx_dataset;
struct randomx_vm;

/**
 * Initialize RandomX (internal, called automatically)
 * @param disable_jit_for_testing If true, disable JIT (for regtest to prevent RPC starvation)
 */
bool InitializeRandomX(bool disable_jit_for_testing = false);

/**
 * Calculate RandomX hash of a block header.
 * This replaces SHA256 double-hash for proof-of-work in Byze.
 * 
 * @param block The block header to hash
 * @param disable_jit_for_testing If true, initialize RandomX without JIT (for regtest)
 * @return The RandomX hash as a uint256
 */
uint256 RandomXHash(const CBlockHeader& block, bool disable_jit_for_testing = false);

/**
 * Cleanup RandomX resources (call on shutdown)
 */
void CleanupRandomXResources();

// Mining-specific RandomX API (per-thread VMs for parallel mining)

/**
 * Mining context structure for per-thread VMs
 * Cache and dataset are shared, but each thread has its own VM
 */
struct RandomXMiningContext {
    randomx_cache* cache{nullptr};
    randomx_dataset* dataset{nullptr};
    std::vector<randomx_vm*> vms;
    /** If false, cache/dataset are shared with global validation RandomX and must not be freed. */
    bool owns_cache_dataset{true};
};

/**
 * Create a mining context with multiple VMs (one per thread)
 * @param threads Number of threads (VMs to create)
 * @param disable_jit If true, use interpreter mode (avoid for production mining)
 * @param share_global_dataset If true, attach VMs to the validation cache/dataset instead of allocating another copy
 * @return Pointer to mining context, or nullptr on failure
 */
RandomXMiningContext* CreateMiningContext(size_t threads, bool disable_jit = false, bool share_global_dataset = false);

/**
 * Destroy a mining context and free all resources
 * @param ctx Mining context to destroy (can be nullptr)
 */
void DestroyMiningContext(RandomXMiningContext* ctx);

/**
 * Calculate RandomX hash using mining context (no mutex, thread-safe per VM)
 * @param ctx Mining context (must not be nullptr)
 * @param thread_index Index of the thread (0 to ctx->vms.size()-1)
 * @param header Block header to hash
 * @param out Output hash (will be written to this reference)
 */
void RandomXMiningHash(
    RandomXMiningContext* ctx,
    size_t thread_index,
    const CBlockHeader& header,
    uint256& out);

/**
 * Lazy RandomX context for RPC generatetoaddress / generatetodescriptor.
 * Uses per-thread VMs (no global mutex) so functional tests are not starved.
 * @param is_test_chain If true (regtest/signet/testnet), attach per-thread VMs to the shared validation dataset (no second dataset).
 */
RandomXMiningContext* GetOrCreateRpcMiningContext(bool is_test_chain);

/** Release RPC mining context (called from CleanupRandomXResources). */
void DestroyRpcMiningContext();

/** Mutex held for the duration of RPC generatetoaddress PoW search. */
std::mutex& RpcMiningExecMutex();

#endif // BITCOIN_CRYPTO_RANDOMX_HASH_H



