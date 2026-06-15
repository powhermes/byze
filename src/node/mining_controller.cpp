// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <node/mining_controller.h>

#include <addresstype.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <logging.h>
#include <node/miner.h>
#include <pow.h>
#include <arith_uint256.h>
#include <rpc/server_util.h>
#include <consensus/merkle.h>
#include <util/signalinterrupt.h>
#include <util/strencodings.h>
#include <util/thread.h>
#include <util/threadnames.h>
#include <validation.h>
#include <crypto/randomx_hash.h>
#include <crypto/quantum_block_sign.h>

#include <chrono>

namespace node {

MiningController::MiningController(NodeContext& node)
    : m_node(node)
    , m_hashrate_start_time(std::chrono::steady_clock::now())
{
}

MiningController::~MiningController()
{
    StopMining();
}

bool MiningController::StartMining(const std::string& address, int threads)
{
    // Stop any existing mining threads first
    if (!m_mining_threads.empty()) {
        LogWarning("Mining threads still exist, stopping them first\n");
        StopMining();
        // Wait a moment for threads to fully stop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Double-check after cleanup
    if (m_mining_active.load()) {
        LogWarning("Mining still marked as active after cleanup, forcing reset\n");
        m_mining_active = false;
    }

    // Validate address
    CTxDestination dest = DecodeDestination(address);
    if (!IsValidDestination(dest)) {
        LogError("Invalid mining address: %s\n", address);
        return false;
    }
    // Determine thread count (auto-detect if not provided or invalid)
    unsigned int hw_threads = std::thread::hardware_concurrency();
    if (hw_threads == 0) {
        hw_threads = 1;
    }
    int requested_threads = threads;
    if (requested_threads <= 0) {
        requested_threads = static_cast<int>(hw_threads);
    }
    // Clamp to [1, hw_threads]
    requested_threads = std::max(1, std::min(requested_threads, static_cast<int>(hw_threads)));
    m_num_threads = static_cast<unsigned int>(requested_threads);

    // Create mining-specific RandomX context with per-thread VMs
    m_mining_context = CreateMiningContext(m_num_threads);
    if (!m_mining_context) {
        LogError("Failed to create RandomX mining context\n");
        return false;
    }
    
    // Verify mining context is working by testing a hash
    CBlockHeader test_header;
    test_header.SetNull();
    test_header.nNonce = 12345;
    uint256 test_hash;
    RandomXMiningHash(m_mining_context, 0, test_header, test_hash);
    uint256 test_consensus = RandomXHash(test_header);
    if (test_hash != test_consensus) {
        LogError("CRITICAL: Mining and consensus hashes don't match! Mining: %s, Consensus: %s\n",
                test_hash.ToString(), test_consensus.ToString());
        DestroyMiningContext(m_mining_context);
        m_mining_context = nullptr;
        return false;
    } else {
        LogInfo("Mining context verified: mining and consensus hashes match (test hash: %s)\n", test_hash.ToString());
    }
    
    {
        LOCK(m_stats_mutex);
        m_mining_address = address;
        m_should_stop = false;
        m_mining_active = true;
        m_hashrate_counter.store(0);
    }

    // Start mining worker threads (each with its own VM index)
    m_mining_threads.clear();
    m_mining_threads.reserve(m_num_threads);
    for (unsigned int i = 0; i < m_num_threads; ++i) {
        m_mining_threads.emplace_back(&util::TraceThread, "mining", [this, i] {
            MiningThread(i);
        });
    }

    LogInfo("Mining started to address: %s with %u threads\n", address, m_num_threads);
    return true;
}

void MiningController::StopMining()
{
    // Set stop flag first
    {
        LOCK(m_stats_mutex);
        m_should_stop = true;
    }

    // Wait for worker threads to finish if they exist
    for (auto& t : m_mining_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_mining_threads.clear();

    // Destroy mining context
    if (m_mining_context) {
        DestroyMiningContext(m_mining_context);
        m_mining_context = nullptr;
    }

    // Reset state
    m_mining_active = false;
    m_should_stop = false;
    m_num_threads = 0;
    LogInfo("Mining stopped\n");
}

MiningController::MiningStats MiningController::GetStats() const
{
    LOCK(m_stats_mutex);
    
    MiningStats stats;
    stats.active = m_mining_active.load();
    stats.address = m_mining_address;
    stats.blocks_found = m_blocks_found.load();
    stats.hashes_tried = m_hashes_tried.load();
    
    // Calculate hashrate based on recent activity
    // Use a rolling 60-second window for hashrate calculation
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_hashrate_start_time).count();
    if (elapsed > 0) {
        uint64_t counter = m_hashrate_counter.load();
        stats.hashrate = static_cast<double>(counter) / elapsed;
    } else {
        stats.hashrate = 0.0;
    }
    
    return stats;
}

void MiningController::MiningThread(size_t thread_index)
{
    util::ThreadRename("mining");
    
    auto last_hashrate_reset = std::chrono::steady_clock::now();
    
    while (!m_should_stop.load()) {
        // Check if node is shutting down
        if (m_node.shutdown_signal && static_cast<bool>(*m_node.shutdown_signal)) {
            LogInfo("Mining paused: node shutdown requested\n");
            break;
        }
        
        // Check if we're in IBD
        // Exception: allow mining at height 0 (genesis tip) even during IBD
        if (m_node.chainman) {
            bool in_ibd = false;
            int chain_height = -1;
            {
                LOCK(cs_main);
                in_ibd = m_node.chainman->IsInitialBlockDownload();
                const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip();
                if (tip) {
                    chain_height = tip->nHeight;
                }
            }
            
            if (in_ibd && chain_height != 0) {
                LogInfo("Mining paused: IBD (chain height: %d)\n", chain_height);
                // Wait a bit before checking again
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            } else if (in_ibd && chain_height == 0) {
                LogInfo("Mining allowed: at genesis height (height 0) despite IBD\n");
            }
        }
        
        // Reset hashrate counter every 60 seconds to prevent overflow
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_hashrate_reset).count();
        if (elapsed >= 60) {
            LOCK(m_stats_mutex);
            m_hashrate_counter.store(0);
            m_hashrate_start_time = now;
            last_hashrate_reset = now;
        }
        
        // Try to mine a block (using this thread's VM)
        if (MineBlock(thread_index)) {
            m_blocks_found.fetch_add(1);
            LogInfo("Block found! Total blocks: %lu\n", m_blocks_found.load());
        }
        
        // Small sleep to prevent CPU spinning if mining fails quickly
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    m_mining_active = false;
}

bool MiningController::MineBlock(size_t thread_index)
{
    // Ensure we have required node components
    if (!m_node.chainman || !m_node.mempool || !m_node.args) {
        LogInfo("Mining paused: missing node components (chainman: %d, mempool: %d, args: %d)\n",
                m_node.chainman != nullptr, m_node.mempool != nullptr, m_node.args != nullptr);
        return false;
    }
    
    ChainstateManager& chainman = *m_node.chainman;
    CTxMemPool& mempool = *m_node.mempool;
    
    // Create block template using BlockAssembler
    BlockAssembler::Options assembler_options;
    ApplyArgsManOptions(*m_node.args, assembler_options);
    
    // Set the coinbase output address
    CTxDestination dest = DecodeDestination(m_mining_address);
    CScript scriptPubKey = GetScriptForDestination(dest);
    assembler_options.coinbase_output_script = scriptPubKey;
    
    BlockAssembler assembler(chainman.ActiveChainstate(), &mempool, assembler_options);
    std::unique_ptr<CBlockTemplate> pblocktemplate = assembler.CreateNewBlock();
    
    if (!pblocktemplate) {
        LogInfo("Mining paused: waiting for template (CreateNewBlock returned null)\n");
        return false;
    }
    
    CBlock* pblock = &pblocktemplate->block;
    
    // Get tip and update time (must hold cs_main for this)
    const CBlockIndex* pindexPrev;
    {
        LOCK(cs_main);
        pindexPrev = chainman.ActiveChain().Tip();
        if (!pindexPrev) {
            LogInfo("Mining paused: no chain tip available\n");
            return false;
        }
        UpdateTime(pblock, Params().GetConsensus(), pindexPrev);
    }
    // Release cs_main before entering hashing loop to avoid blocking RPC

    // Ensure the block's merkle root matches its transactions before mining.
    // Without this, RandomX will hash a header whose hashMerkleRoot may not
    // match BlockMerkleRoot(block), causing validation to fail with
    // bad-txnmrklroot even when the PoW is sufficient.
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    
    pblock->nNonce = 0;
    
    // Mine the block using RandomX (no locks held during hashing)
    const uint64_t max_tries = 10000000; // Increased limit for low difficulty
    const auto mining_start = std::chrono::steady_clock::now();
    const auto max_mining_time = std::chrono::minutes(5); // Increased time limit for low difficulty
    
    uint32_t initial_nonce = pblock->nNonce;
    int64_t initial_time = pblock->nTime;
    uint32_t time_increment = 0;
    uint64_t tries = 0;
    
    LogInfo("Hash loop entered\n");
    
    while (tries < max_tries && !m_should_stop.load() && !static_cast<bool>(chainman.m_interrupt)) {
        // Check time limit
        auto elapsed = std::chrono::steady_clock::now() - mining_start;
        if (elapsed > max_mining_time) {
            // Time limit reached, update stats and return
            LogInfo("Mining paused: time limit reached (%lu hashes tried)\n", tries);
            m_hashrate_counter.fetch_add(tries);
            return false;
        }
        
        // Calculate hash using mining-specific RandomX (per-thread VM, no mutex)
        uint256 hash;
        if (!m_mining_context) {
            LogError("Mining context not available\n");
            return false;
        }
        RandomXMiningHash(m_mining_context, thread_index, *pblock, hash);
        
        // Debug: Log hash comparison periodically (every 1000 hashes) and for first 10
        if (tries < 10 || tries % 1000 == 0) {
            uint256 consensus_hash = RandomXHash(*pblock);
            if (hash != consensus_hash) {
                LogError("Mining hash mismatch! Mining: %s, Consensus: %s, nonce: %u, tries: %lu\n",
                        hash.ToString(), consensus_hash.ToString(), pblock->nNonce, tries);
            } else {
                // Log target comparison for debugging
                auto bnTarget = DeriveTarget(pblock->nBits, Params().GetConsensus().powLimit);
                if (bnTarget) {
                    auto hash_arith = UintToArith256(hash);
                    auto target_arith = *bnTarget;
                    bool passes = hash_arith <= target_arith;
                    if (tries < 10 || passes) {
                        LogInfo("Hash check: hash=%s, target=%s, passes=%d, nonce=%u, tries=%lu\n",
                                hash.ToString(), ArithToUint256(target_arith).ToString(), passes, pblock->nNonce, tries);
                    }
                } else {
                    LogError("Failed to derive target from nBits: %08x\n", pblock->nBits);
                }
            }
        }
        
        // Check proof of work - also log the comparison for debugging
        auto bnTarget = DeriveTarget(pblock->nBits, Params().GetConsensus().powLimit);
        bool pow_passes = false;
        if (bnTarget) {
            auto hash_arith = UintToArith256(hash);
            auto target_arith = *bnTarget;
            pow_passes = hash_arith <= target_arith;
            
            // Log if we're close to finding a block (for debugging)
            if (tries % 10000 == 0 || pow_passes) {
                LogInfo("PoW check: hash_arith=%s, target_arith=%s, passes=%d, tries=%lu\n",
                        hash_arith.GetHex(), target_arith.GetHex(), pow_passes, tries);
            }
        } else {
            LogError("Failed to derive target! nBits=%08x\n", pblock->nBits);
        }
        
        if (CheckProofOfWork(hash, pblock->nBits, Params().GetConsensus())) {
            // Found a valid block!
            LogInfo("Block found! Total hashes tried: %lu\n", tries + 1);
            m_hashrate_counter.fetch_add(tries + 1);
            
            // Mainnet-only by default; can be enabled on all networks for testing.
            // Signatures are added after mining completes, so we sign the final block hash.
            const bool isMainnet = !Params().IsTestChain();
            const bool enforce = gArgs.GetBoolArg("-enforcequantumblocksigs", false);
            const bool require_quantum = isMainnet || enforce;
            const bool isGenesis = (pblock->GetHash() == Params().GetConsensus().hashGenesisBlock);
            
            if (require_quantum && !isGenesis) {
                if (!crypto::AttachQuantumBlockSignatures(*pblock, require_quantum, isGenesis)) {
                    LogError("Failed to attach quantum signatures for block\n");
                } else {
                    LogInfo("Quantum signatures added to block\n");
                }
            }
            // For non-enforced test networks (and genesis): quantum_signatures remain empty (Bitcoin-compatible)
            
            // Verify the block still passes PoW after adding quantum signatures (if any)
            // Recalculate hash to ensure it still passes
            uint256 final_hash;
            RandomXMiningHash(m_mining_context, thread_index, *pblock, final_hash);
            if (!CheckProofOfWork(final_hash, pblock->nBits, Params().GetConsensus())) {
                LogError("Block PoW check failed after adding quantum signatures! Hash: %s\n", final_hash.ToString());
                // Continue anyway - validation will catch it
            }
            
            // Submit the block
            std::shared_ptr<const CBlock> blockptr = std::make_shared<const CBlock>(*pblock);
            bool new_block = false;
            bool accepted = chainman.ProcessNewBlock(blockptr, /*force_processing=*/true, /*min_pow_checked=*/true, &new_block);
            
            if (accepted && new_block) {
                LogInfo("Successfully mined and submitted block (hash: %s, pow_hash: %s)\n",
                        pblock->GetHash().ToString(), final_hash.ToString());
                return true;
            } else if (accepted) {
                LogInfo("Block was accepted but already known (hash: %s)\n", pblock->GetHash().ToString());
                return true;
            } else {
                LogWarning("Mined block was rejected (hash: %s, pow_hash: %s, nBits: %08x)\n",
                          pblock->GetHash().ToString(), final_hash.ToString(), pblock->nBits);
                // Try to get more info about why it was rejected
                uint256 validation_hash = RandomXHash(*pblock);
                LogWarning("Validation hash: %s (should match pow_hash: %s)\n",
                          validation_hash.ToString(), final_hash.ToString());
                return false;
            }
        }
        
        // Increment nonce and try again
        pblock->nNonce++;
        tries++;
        
        // Increment hashes_tried inside the inner hash loop (not just at the end)
        m_hashes_tried.fetch_add(1);
        m_hashrate_counter.fetch_add(1);
        
        // Handle nonce overflow
        if (pblock->nNonce < initial_nonce) {
            // Wrapped around, increment time
            pblock->nTime = initial_time + (++time_increment);
            pblock->nNonce = 0;
            initial_nonce = 0;
        }
    }
    
    // Only log remaining tries if loop exited early (not via max_tries)
    if (tries < max_tries) {
        LogInfo("Hash loop exited early: tries=%lu, should_stop=%d, interrupt=%d\n",
                tries, m_should_stop.load(), static_cast<bool>(chainman.m_interrupt));
    }
    
    return false;
}

} // namespace node

