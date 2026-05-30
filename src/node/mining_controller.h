// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#ifndef BITCOIN_NODE_MINING_CONTROLLER_H
#define BITCOIN_NODE_MINING_CONTROLLER_H

#include <node/context.h>
#include <primitives/block.h>
#include <addresstype.h>
#include <sync.h>
#include <thread>
#include <atomic>
#include <memory>
#include <optional>

class ChainstateManager;
class CTxMemPool;
struct RandomXMiningContext;

namespace node {

// Mining controller that runs entirely within the existing daemon
// without re-entering node initialization paths
class MiningController
{
public:
    explicit MiningController(NodeContext& node);
    ~MiningController();

    // Start mining to the given address
    // If threads <= 0, a default based on std::thread::hardware_concurrency()
    // will be chosen and clamped to a sane range.
    bool StartMining(const std::string& address, int threads = 0);
    
    // Stop mining
    void StopMining();
    
    // Check if mining is active
    bool IsMining() const { return m_mining_active.load(); }
    
    // Get mining statistics
    struct MiningStats {
        bool active;
        std::string address;
        uint64_t blocks_found;
        uint64_t hashes_tried;
        double hashrate; // hashes per second
    };
    
    MiningStats GetStats() const;

private:
    // Mining thread function
    void MiningThread(size_t thread_index);
    
    // Mine a single block
    bool MineBlock(size_t thread_index);
    
    NodeContext& m_node;
    mutable Mutex m_stats_mutex;
    
    std::atomic<bool> m_mining_active{false};
    std::atomic<bool> m_should_stop{false};
    std::vector<std::thread> m_mining_threads;
    unsigned int m_num_threads{0};
    
    // Mining-specific RandomX context (per-thread VMs)
    RandomXMiningContext* m_mining_context{nullptr};
    
    std::string m_mining_address;
    std::atomic<uint64_t> m_blocks_found{0};
    std::atomic<uint64_t> m_hashes_tried{0};
    std::atomic<uint64_t> m_hashrate_counter{0};
    
    std::chrono::steady_clock::time_point m_hashrate_start_time;
};

} // namespace node

#endif // BITCOIN_NODE_MINING_CONTROLLER_H

