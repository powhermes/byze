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

#include <validationinterface.h>

#include <chrono>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>

namespace node {

// ---------------------------------------------------------------------------------------------------------------------
// Measurement instrumentation (behaviour-neutral).  Enabled only with -miningmetrics=<dir>.  One buffered file per mining
// thread (written by that thread only) plus a shared tip-event file.  All timestamps are steady_clock microseconds.
//   T <us> <height> <ntime> <nbits> <parent> <merkle>      thread started hashing a new work unit
//   h <us> <nonce>                                          one RandomX hash completed for the current work unit
//   A <us> <reason>                                         thread left the work unit
//   P <us> <height> <hash>                                  node's active tip changed (validation interface callback)
// ---------------------------------------------------------------------------------------------------------------------
class MiningMetrics
{
public:
    MiningMetrics(const std::string& dir, size_t threads) : m_thr(threads)
    {
        for (size_t i = 0; i < threads; ++i) {
            m_thr[i].f = std::fopen((dir + "/thread" + std::to_string(i) + ".log").c_str(), "w");
            m_thr[i].buf.reserve(1 << 16);
        }
        m_tip = std::fopen((dir + "/tips.log").c_str(), "w");
    }
    ~MiningMetrics()
    {
        Flush();
        for (auto& t : m_thr) if (t.f) std::fclose(t.f);
        if (m_tip) std::fclose(m_tip);
    }
    static int64_t NowUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void Tip(int height, const uint256& hash)
    {
        std::lock_guard<std::mutex> l(m_tip_mu);
        if (m_tip) { std::fprintf(m_tip, "P %lld %d %s\n", (long long)NowUs(), height, hash.ToString().c_str()); std::fflush(m_tip); }
    }
    void Template(size_t thr, const CBlock& b, int height)
    {
        char line[256];
        std::snprintf(line, sizeof line, "T %lld %d %u %08x ", (long long)NowUs(), height, (unsigned)b.nTime, (unsigned)b.nBits);
        Append(thr, line);
        Append(thr, b.hashPrevBlock.ToString());
        Append(thr, " ");
        Append(thr, b.hashMerkleRoot.ToString());
        Append(thr, "\n");
    }
    void Hash(size_t thr, uint32_t nonce)
    {
        char line[64];
        std::snprintf(line, sizeof line, "h %lld %u\n", (long long)NowUs(), (unsigned)nonce);
        Append(thr, line);
    }
    void End(size_t thr, const char* reason)
    {
        char line[64];
        std::snprintf(line, sizeof line, "A %lld %s\n", (long long)NowUs(), reason);
        Append(thr, line);
        FlushThread(thr);
    }
    void Flush()
    {
        for (size_t i = 0; i < m_thr.size(); ++i) FlushThread(i);
    }

private:
    struct PerThread { std::FILE* f{nullptr}; std::string buf; };
    void Append(size_t thr, const std::string& s)
    {
        if (thr >= m_thr.size()) return;
        auto& t = m_thr[thr];
        t.buf += s;
        if (t.buf.size() > (1 << 15)) FlushThread(thr);
    }
    void FlushThread(size_t thr)
    {
        auto& t = m_thr[thr];
        if (t.f && !t.buf.empty()) { std::fwrite(t.buf.data(), 1, t.buf.size(), t.f); std::fflush(t.f); t.buf.clear(); }
    }
    std::vector<PerThread> m_thr;
    std::FILE* m_tip{nullptr};
    std::mutex m_tip_mu;
};

class MiningTipListener : public CValidationInterface
{
public:
    MiningTipListener(std::shared_ptr<MiningMetrics> metrics, std::shared_ptr<std::atomic<uint64_t>> generation)
        : m_metrics(std::move(metrics)), m_generation(std::move(generation)) {}
protected:
    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex*, bool) override
    {
        if (!pindexNew) return;
        if (m_metrics) m_metrics->Tip(pindexNew->nHeight, pindexNew->GetBlockHash());
        if (m_generation) m_generation->fetch_add(1);
    }
private:
    std::shared_ptr<MiningMetrics> m_metrics;
    std::shared_ptr<std::atomic<uint64_t>> m_generation;
};

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
    
    // Measurement instrumentation / test hooks (no effect unless the options are given)
    m_test_nbits = 0;
    const std::string test_nbits = gArgs.GetArg("-miningtestnbits", "");
    if (!test_nbits.empty()) {
        if (!Params().IsTestChain()) {
            LogError("-miningtestnbits is only allowed on test chains\n");
            DestroyMiningContext(m_mining_context);
            m_mining_context = nullptr;
            return false;
        }
        try {
            m_test_nbits = static_cast<uint32_t>(std::stoul(test_nbits, nullptr, 16));
        } catch (const std::exception&) {
            LogError("Invalid -miningtestnbits value: %s\n", test_nbits);
            DestroyMiningContext(m_mining_context);
            m_mining_context = nullptr;
            return false;
        }
    }
    const std::string metrics_dir = gArgs.GetArg("-miningmetrics", "");
    m_metrics.reset();
    if (!metrics_dir.empty()) m_metrics = std::make_shared<MiningMetrics>(metrics_dir, m_num_threads);
    if (m_metrics && m_node.chainman) {
        LOCK(cs_main);
        if (const CBlockIndex* tip = m_node.chainman->ActiveChain().Tip()) m_metrics->Tip(tip->nHeight, tip->GetBlockHash());
    }
    m_debug_verify = gArgs.GetBoolArg("-miningdebugverify", false);
    {
        std::lock_guard<std::mutex> lock(m_work_mutex);
        m_work.reset();
    }
    if (m_node.validation_signals) {
        m_tip_listener = std::make_shared<MiningTipListener>(m_metrics, m_tip_generation);
        m_node.validation_signals->RegisterSharedValidationInterface(m_tip_listener);
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

    if (m_tip_listener && m_node.validation_signals) {
        m_node.validation_signals->UnregisterSharedValidationInterface(m_tip_listener);
        m_tip_listener.reset();
    }
    if (m_metrics) { m_metrics->Flush(); m_metrics.reset(); }

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
        if (m_metrics) m_metrics->End(thread_index, "exit");
        
        // (no fixed sleep: MineBlock returns immediately on a tip change and AcquireWork backs off itself if no template is available)
    }
    
    m_mining_active = false;
}

static int64_t NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static constexpr int64_t WORK_MAX_AGE_US = 60'000'000; // refresh nTime/mempool at least every 60 s

std::shared_ptr<const MiningController::WorkUnit> MiningController::BuildWork(uint64_t generation)
{
    ChainstateManager& chainman = *m_node.chainman;
    CTxMemPool& mempool = *m_node.mempool;

    BlockAssembler::Options assembler_options;
    ApplyArgsManOptions(*m_node.args, assembler_options);
    CTxDestination dest = DecodeDestination(m_mining_address);
    assembler_options.coinbase_output_script = GetScriptForDestination(dest);

    BlockAssembler assembler(chainman.ActiveChainstate(), &mempool, assembler_options);
    std::unique_ptr<CBlockTemplate> pblocktemplate = assembler.CreateNewBlock();
    if (!pblocktemplate) return nullptr;

    CBlock blk = pblocktemplate->block;
    int height = 0;
    {
        LOCK(cs_main);
        const CBlockIndex* pindexPrev = chainman.ActiveChain().Tip();
        if (!pindexPrev) return nullptr;
        // The tip may have moved while the template was being assembled: never hash a template whose parent is not the tip.
        if (blk.hashPrevBlock != pindexPrev->GetBlockHash()) return nullptr;
        UpdateTime(&blk, Params().GetConsensus(), pindexPrev);
        height = pindexPrev->nHeight + 1;
    }
    blk.hashMerkleRoot = BlockMerkleRoot(blk);
    blk.nNonce = 0;
    if (m_test_nbits) blk.nBits = m_test_nbits;

    auto work = std::make_shared<WorkUnit>();
    work->block = std::move(blk);
    work->generation = generation;
    work->built_us = NowUs();
    work->height = height;
    return work;
}

std::shared_ptr<const MiningController::WorkUnit> MiningController::AcquireWork()
{
    std::lock_guard<std::mutex> lock(m_work_mutex);
    const uint64_t gen = m_tip_generation->load(std::memory_order_acquire);
    if (m_work && m_work->generation == gen && NowUs() - m_work->built_us < WORK_MAX_AGE_US) return m_work;
    m_work = BuildWork(gen); // may be null (retry by the caller)
    return m_work;
}

bool MiningController::MineBlock(size_t thread_index)
{
    if (!m_node.chainman || !m_node.mempool || !m_node.args) {
        LogInfo("Mining paused: missing node components (chainman: %d, mempool: %d, args: %d)\n",
                m_node.chainman != nullptr, m_node.mempool != nullptr, m_node.args != nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return false;
    }
    ChainstateManager& chainman = *m_node.chainman;

    std::shared_ptr<const WorkUnit> work = AcquireWork();
    if (!work) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return false;
    }

    CBlock blk = work->block;                      // private copy: this thread owns its nonce stream
    const uint64_t stride = std::max(1u, m_num_threads);
    uint64_t nonce = thread_index;                 // strided partition: thread i hashes i, i+N, i+2N ... (no two threads share a header+nonce)
    if (m_metrics) m_metrics->Template(thread_index, blk, work->height);
    const auto bnTarget = DeriveTarget(blk.nBits, Params().GetConsensus().powLimit);
    if (!bnTarget) {
        LogError("Failed to derive target from nBits: %08x\n", blk.nBits);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return false;
    }
    uint64_t tries = 0;

    while (!m_should_stop.load() && !static_cast<bool>(chainman.m_interrupt)) {
        // Abandon the work unit the moment the tip moves (generation is bumped by the validation callback) or it gets old.
        if (m_tip_generation->load(std::memory_order_acquire) != work->generation) return false;
        if (NowUs() - work->built_us > WORK_MAX_AGE_US) return false;
        if (nonce > std::numeric_limits<uint32_t>::max()) {
            std::lock_guard<std::mutex> lock(m_work_mutex);
            m_work.reset(); // nonce space exhausted for this thread's stride: force a fresh work unit
            return false;
        }
        blk.nNonce = static_cast<uint32_t>(nonce);

        uint256 hash;
        if (!m_mining_context) {
            LogError("Mining context not available\n");
            return false;
        }
        RandomXMiningHash(m_mining_context, thread_index, blk, hash);
        if (m_metrics) m_metrics->Hash(thread_index, blk.nNonce);
        m_hashes_tried.fetch_add(1);
        m_hashrate_counter.fetch_add(1);
        ++tries;

        if (m_debug_verify && (tries < 10 || tries % 1000 == 0)) {
            const uint256 consensus_hash = RandomXHash(blk);
            if (hash != consensus_hash) {
                LogError("Mining hash mismatch! Mining: %s, Consensus: %s, nonce: %u, tries: %lu\n",
                         hash.ToString(), consensus_hash.ToString(), blk.nNonce, tries);
            }
        }

        if (CheckProofOfWork(hash, blk.nBits, Params().GetConsensus())) {
            LogInfo("Block found! Total hashes tried by this thread on this work: %lu\n", tries);

            const bool isMainnet = !Params().IsTestChain();
            const bool enforce = gArgs.GetBoolArg("-enforcequantumblocksigs", false);
            const bool require_quantum = isMainnet || enforce;
            const bool isGenesis = (blk.GetHash() == Params().GetConsensus().hashGenesisBlock);

            if (require_quantum && !isGenesis) {
                if (!crypto::AttachQuantumBlockSignatures(blk, require_quantum, isGenesis)) {
                    LogError("Failed to attach quantum signatures for block\n");
                } else {
                    LogInfo("Quantum signatures added to block\n");
                }
            }

            uint256 final_hash;
            RandomXMiningHash(m_mining_context, thread_index, blk, final_hash);
            if (!CheckProofOfWork(final_hash, blk.nBits, Params().GetConsensus())) {
                LogError("Block PoW check failed after adding quantum signatures! Hash: %s\n", final_hash.ToString());
            }

            std::shared_ptr<const CBlock> blockptr = std::make_shared<const CBlock>(blk);
            bool new_block = false;
            bool accepted = chainman.ProcessNewBlock(blockptr, /*force_processing=*/true, /*min_pow_checked=*/true, &new_block);

            if (accepted && new_block) {
                LogInfo("Successfully mined and submitted block (hash: %s, pow_hash: %s)\n", blk.GetHash().ToString(), final_hash.ToString());
                return true;
            } else if (accepted) {
                LogInfo("Block was accepted but already known (hash: %s)\n", blk.GetHash().ToString());
                return true;
            } else {
                LogWarning("Mined block was rejected (hash: %s, pow_hash: %s, nBits: %08x)\n", blk.GetHash().ToString(), final_hash.ToString(), blk.nBits);
                return false;
            }
        }
        nonce += stride;
    }
    return false;
}

} // namespace node

