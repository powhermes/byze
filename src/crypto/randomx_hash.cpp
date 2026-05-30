// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <crypto/randomx_hash.h>

#include <crypto/common.h>
#include <randomx.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/threadnames.h>
#include <logging.h>

#include <mutex>
#include <memory>
#include <cstring>
#include <algorithm>
#include <thread>

namespace {

// Global RandomX cache and VM instances.
//
// RandomX requires a key to initialize the cache. This key is consensus-critical:
// changing it will change PoW hashes and fork the network. Keep the byte sequence
// stable once mainnet is live.
static constexpr unsigned char BYZE_RANDOMX_KEY_V1[] = {
    0x48, 0x45, 0x52, 0x4D,
    0x48, 0x65, 0x72, 0x6D,
    0x65, 0x73, 0x20, 0x43,
    0x6F, 0x69, 0x6E, 0x20,
    0x52, 0x61, 0x6E, 0x64,
    0x6F, 0x6D, 0x58, 0x20,
    0x4E, 0x65, 0x74, 0x77,
    0x6F, 0x72, 0x6B, 0x00
};

static std::mutex randomx_mutex;
static randomx_cache* g_cache = nullptr;
static randomx_dataset* g_dataset = nullptr;
static randomx_vm* g_vm = nullptr;
static randomx_flags g_init_flags = RANDOMX_FLAG_DEFAULT;
static bool randomx_initialized = false;

static RandomXMiningContext* g_rpc_mining_ctx = nullptr;
static std::mutex g_rpc_ctx_mutex;
/** Serializes RPC PoW search; VMs are not safe across concurrent RPC handlers. */
static std::mutex g_rpc_mining_exec_mutex;

} // anonymous namespace

// Initialize RandomX cache and VM (thread-safe)
// For regtest, disable JIT to prevent RPC thread starvation
bool InitializeRandomX(bool disable_jit_for_testing)
{
    std::lock_guard<std::mutex> lock(randomx_mutex);
    
    if (randomx_initialized) {
        return true;
    }
    
    // Get recommended flags for this machine
    randomx_flags flags = randomx_get_flags();
    
    // Enable JIT and full memory mode for best performance (unless disabled for testing)
    if (!disable_jit_for_testing) {
        flags = static_cast<randomx_flags>(flags | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM);
    } else {
        // For regtest: use interpreter mode (no JIT) to prevent RPC thread starvation
        flags = static_cast<randomx_flags>(flags | RANDOMX_FLAG_FULL_MEM);
    }
    
    // Allocate and initialize cache
    g_cache = randomx_alloc_cache(flags);
    if (!g_cache) {
        LogError("RandomX: Failed to allocate cache\n");
        return false;
    }
    
    randomx_init_cache(g_cache, BYZE_RANDOMX_KEY_V1, sizeof(BYZE_RANDOMX_KEY_V1));
    
    // For full memory mode, we need a dataset
    // IMPORTANT: Both mining and validation MUST use the same mode (full memory)
    // to ensure consistent hashing. Light mode produces different hashes!
    randomx_dataset* dataset = nullptr;
    if (flags & RANDOMX_FLAG_FULL_MEM) {
        // Allocate and initialize dataset for full memory mode
        dataset = randomx_alloc_dataset(flags);
        if (!dataset) {
            LogError("RandomX: Failed to allocate dataset\n");
            randomx_release_cache(g_cache);
            g_cache = nullptr;
            return false;
        }
        
        // Initialize dataset (this can take a while, but is required for consistency)
        LogInfo("RandomX: Initializing dataset for validation (this may take a moment)...\n");
        unsigned long dataset_item_count = randomx_dataset_item_count();
        randomx_init_dataset(dataset, g_cache, 0, dataset_item_count);
        LogInfo("RandomX: Dataset initialization complete\n");
    }
    
    // Create VM with the same flags as mining (full memory mode if enabled)
    g_vm = randomx_create_vm(flags, g_cache, dataset);
    
    if (!g_vm) {
        LogError("RandomX: Failed to create VM\n");
        if (dataset) {
            randomx_release_dataset(dataset);
        }
        randomx_release_cache(g_cache);
        g_cache = nullptr;
        return false;
    }
    
    // Store dataset reference for cleanup
    g_dataset = dataset;
    g_init_flags = flags;

    randomx_initialized = true;
    if (dataset) {
        LogInfo("RandomX initialized successfully with FULL MEMORY mode (dataset enabled)\n");
    } else {
        LogInfo("RandomX initialized successfully with LIGHT mode (no dataset)\n");
    }
    return true;
}

// Cleanup RandomX resources
static void CleanupRandomX()
{
    std::lock_guard<std::mutex> lock(randomx_mutex);
    
    if (g_vm) {
        randomx_destroy_vm(g_vm);
        g_vm = nullptr;
    }
    
    if (g_dataset) {
        randomx_release_dataset(g_dataset);
        g_dataset = nullptr;
    }
    
    if (g_cache) {
        randomx_release_cache(g_cache);
        g_cache = nullptr;
    }
    
    g_init_flags = RANDOMX_FLAG_DEFAULT;
    randomx_initialized = false;
}

uint256 RandomXHash(const CBlockHeader& block, bool disable_jit_for_testing)
{
    // Ensure RandomX is initialized
    if (!randomx_initialized && !InitializeRandomX(disable_jit_for_testing)) {
        // Fallback: return zero hash if initialization fails
        // This should never happen in production
        LogError("RandomX: Not initialized, returning zero hash\n");
        return uint256();
    }
    
    std::lock_guard<std::mutex> lock(randomx_mutex);
    
    // Serialize block header (80 bytes for Bitcoin-style headers)
    // Format: version (4) + prevBlock (32) + merkleRoot (32) + time (4) + bits (4) + nonce (4) = 80 bytes
    uint8_t header_data[80];
    
    // Serialize header in little-endian format (same as Bitcoin)
    WriteLE32(header_data, static_cast<uint32_t>(block.nVersion));
    std::memcpy(header_data + 4, block.hashPrevBlock.begin(), 32);
    std::memcpy(header_data + 36, block.hashMerkleRoot.begin(), 32);
    WriteLE32(header_data + 68, block.nTime);
    WriteLE32(header_data + 72, block.nBits);
    WriteLE32(header_data + 76, block.nNonce);
    
    // Calculate RandomX hash
    uint8_t hash_output[RANDOMX_HASH_SIZE];
    randomx_calculate_hash(g_vm, header_data, sizeof(header_data), hash_output);
    
    // Convert to uint256 (little-endian, same as Bitcoin)
    uint256 result;
    std::memcpy(result.begin(), hash_output, 32);
    return result;
}

// Cleanup function to be called on shutdown (RPC context freed in DestroyRpcMiningContext)

// Mining-specific RandomX API implementation

RandomXMiningContext* CreateMiningContext(size_t threads, bool disable_jit, bool share_global_dataset)
{
    if (threads == 0) {
        LogError("RandomX: Cannot create mining context with 0 threads\n");
        return nullptr;
    }

    randomx_cache* cache = nullptr;
    randomx_dataset* dataset = nullptr;
    bool owns_cache_dataset = true;
    randomx_flags flags = randomx_get_flags();

    if (share_global_dataset) {
        std::lock_guard<std::mutex> lock(randomx_mutex);
        if (!randomx_initialized || !g_cache) {
            LogError("RandomX: Cannot share global dataset before validation RandomX is initialized\n");
            return nullptr;
        }
        cache = g_cache;
        dataset = g_dataset;
        flags = g_init_flags; // VMs must match validation flags exactly
        owns_cache_dataset = false;
        disable_jit = false; // unused when sharing; silences -Wunused-parameter
        LogInfo("RandomX: Reusing validation cache/dataset for mining context\n");
    } else {
        // Use the same key as consensus validation (consensus-critical; see above).
        constexpr unsigned char BYZE_RANDOMX_KEY_V1[] = {
            0x48, 0x45, 0x52, 0x4D,
            0x48, 0x65, 0x72, 0x6D,
            0x65, 0x73, 0x20, 0x43,
            0x6F, 0x69, 0x6E, 0x20,
            0x52, 0x61, 0x6E, 0x64,
            0x6F, 0x6D, 0x58, 0x20,
            0x4E, 0x65, 0x74, 0x77,
            0x6F, 0x72, 0x6B, 0x00
        };

        if (!disable_jit) {
            flags = static_cast<randomx_flags>(flags | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM);
        } else {
            flags = static_cast<randomx_flags>(flags | RANDOMX_FLAG_FULL_MEM);
        }

        cache = randomx_alloc_cache(flags);
        if (!cache) {
            LogError("RandomX: Failed to allocate cache for mining context\n");
            return nullptr;
        }

        randomx_init_cache(cache, BYZE_RANDOMX_KEY_V1, sizeof(BYZE_RANDOMX_KEY_V1));

        if (flags & RANDOMX_FLAG_FULL_MEM) {
            dataset = randomx_alloc_dataset(flags);
            if (!dataset) {
                LogError("RandomX: Failed to allocate dataset for mining context\n");
                randomx_release_cache(cache);
                return nullptr;
            }

            LogInfo("RandomX: Initializing dataset for mining (this may take a moment)...\n");
            const unsigned long dataset_item_count = randomx_dataset_item_count();
            randomx_init_dataset(dataset, cache, 0, dataset_item_count);
            LogInfo("RandomX: Dataset initialization complete\n");
        }
    }

    // Create mining context
    RandomXMiningContext* ctx = new RandomXMiningContext;
    ctx->cache = cache;
    ctx->dataset = dataset;
    ctx->owns_cache_dataset = owns_cache_dataset;
    ctx->vms.reserve(threads);
    
    // Create one VM per thread (all share the same cache and dataset)
    for (size_t i = 0; i < threads; ++i) {
        randomx_vm* vm = randomx_create_vm(flags, cache, dataset);
        if (!vm) {
            LogError("RandomX: Failed to create VM %zu for mining context\n", i);
            // Cleanup: destroy already created VMs
            for (randomx_vm* v : ctx->vms) {
                randomx_destroy_vm(v);
            }
            if (owns_cache_dataset) {
                if (dataset) {
                    randomx_release_dataset(dataset);
                }
                randomx_release_cache(cache);
            }
            delete ctx;
            return nullptr;
        }
        ctx->vms.push_back(vm);
    }

    LogInfo("RandomX: Created mining context with %zu VM(s)%s\n", threads,
            owns_cache_dataset ? " (dedicated dataset)" : " (shared validation dataset)");
    return ctx;
}

void DestroyMiningContext(RandomXMiningContext* ctx)
{
    if (!ctx) {
        return;
    }
    
    // Destroy all VMs
    for (randomx_vm* vm : ctx->vms) {
        randomx_destroy_vm(vm);
    }
    ctx->vms.clear();
    
    // Release dataset and cache only when this context owns them
    if (ctx->owns_cache_dataset) {
        if (ctx->dataset) {
            randomx_release_dataset(ctx->dataset);
            ctx->dataset = nullptr;
        }

        if (ctx->cache) {
            randomx_release_cache(ctx->cache);
            ctx->cache = nullptr;
        }
    }
    
    delete ctx;
    LogInfo("RandomX: Destroyed mining context\n");
}

void RandomXMiningHash(
    RandomXMiningContext* ctx,
    size_t thread_index,
    const CBlockHeader& header,
    uint256& out)
{
    if (!ctx || thread_index >= ctx->vms.size() || !ctx->vms[thread_index]) {
        LogError("RandomX: Invalid mining context or thread index\n");
        out.SetNull();
        return;
    }

    // When attached to the global validation cache/dataset, coordinate with RandomXHash.
    std::unique_lock<std::mutex> shared_lock;
    if (!ctx->owns_cache_dataset) {
        shared_lock = std::unique_lock<std::mutex>{randomx_mutex};
    }
    
    // Serialize block header (80 bytes for Bitcoin-style headers)
    // Format: version (4) + prevBlock (32) + merkleRoot (32) + time (4) + bits (4) + nonce (4) = 80 bytes
    uint8_t header_data[80];
    
    // Serialize header in little-endian format (same as Bitcoin)
    WriteLE32(header_data, static_cast<uint32_t>(header.nVersion));
    std::memcpy(header_data + 4, header.hashPrevBlock.begin(), 32);
    std::memcpy(header_data + 36, header.hashMerkleRoot.begin(), 32);
    WriteLE32(header_data + 68, header.nTime);
    WriteLE32(header_data + 72, header.nBits);
    WriteLE32(header_data + 76, header.nNonce);
    
    // Calculate RandomX hash using the thread-specific VM (no mutex needed)
    uint8_t hash_output[RANDOMX_HASH_SIZE];
    randomx_calculate_hash(ctx->vms[thread_index], header_data, sizeof(header_data), hash_output);
    
    // Convert to uint256 (little-endian, same as Bitcoin)
    std::memcpy(out.begin(), hash_output, 32);
}

void DestroyRpcMiningContext()
{
    std::lock_guard<std::mutex> lock(g_rpc_ctx_mutex);
    if (g_rpc_mining_ctx) {
        DestroyMiningContext(g_rpc_mining_ctx);
        g_rpc_mining_ctx = nullptr;
    }
}

RandomXMiningContext* GetOrCreateRpcMiningContext(bool is_test_chain)
{
    std::lock_guard<std::mutex> lock(g_rpc_ctx_mutex);
    if (g_rpc_mining_ctx) {
        return g_rpc_mining_ctx;
    }

    // Always one VM: parallel RPC mining (multiple VMs) segfaulted on mainnet.
    // Test chains: attach to the validation dataset (fast init for functional tests).
    // Mainnet: dedicated dataset/cache so PoW search does not take randomx_mutex on
    // every hash (that path was ~1 H/s and caused generatetoaddress RPC timeouts).
    constexpr size_t RPC_MINING_THREADS = 1;
    if (is_test_chain) {
        InitializeRandomX(false);
        g_rpc_mining_ctx = CreateMiningContext(RPC_MINING_THREADS, /*disable_jit=*/false, /*share_global_dataset=*/true);
    } else {
        g_rpc_mining_ctx = CreateMiningContext(RPC_MINING_THREADS, /*disable_jit=*/false, /*share_global_dataset=*/false);
        if (g_rpc_mining_ctx) {
            CBlockHeader probe;
            probe.SetNull();
            probe.nNonce = 42;
            uint256 mining_hash;
            RandomXMiningHash(g_rpc_mining_ctx, 0, probe, mining_hash);
            InitializeRandomX(false);
            const uint256 consensus_hash = RandomXHash(probe);
            if (mining_hash != consensus_hash) {
                LogError("RandomX: RPC mining hash mismatch (mining %s != consensus %s)\n",
                         mining_hash.ToString(), consensus_hash.ToString());
                DestroyMiningContext(g_rpc_mining_ctx);
                g_rpc_mining_ctx = nullptr;
            }
        }
    }
    return g_rpc_mining_ctx;
}

std::mutex& RpcMiningExecMutex()
{
    return g_rpc_mining_exec_mutex;
}

void CleanupRandomXResources()
{
    DestroyRpcMiningContext();
    CleanupRandomX();
}

