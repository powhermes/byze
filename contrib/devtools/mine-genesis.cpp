// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <crypto/randomx_hash.h>
#include <thread>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <consensus/merkle.h>
#include <consensus/amount.h>
#include <uint256.h>
#include <arith_uint256.h>
#include <util/strencodings.h>
#include <script/script.h>

#include <util/translation.h>

#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <vector>
#include <cstring>

// Define translation function (required for linking)
const TranslateFn G_TRANSLATION_FUN{nullptr};

static void PrintUsage(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  -time <timestamp>    Block timestamp (default: current time)\n";
    std::cout << "  -bits <hex>          Difficulty bits (default: 0x1e0ffff0)\n";
    std::cout << "  -reward <amount>     Genesis reward in coins (default: 50)\n";
    std::cout << "  -help                Show this help message\n";
}

int main(int argc, char* argv[])
{
    uint32_t nTime = static_cast<uint32_t>(std::time(nullptr));
    uint32_t nBits = 0x1e0ffff0;  // Easier difficulty for initial mining
    CAmount genesisReward = 50 * COIN;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-help" || arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "-time" && i + 1 < argc) {
            nTime = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "-bits" && i + 1 < argc) {
            nBits = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 16));
        } else if (arg == "-reward" && i + 1 < argc) {
            genesisReward = static_cast<CAmount>(std::stoll(argv[++i]) * COIN);
        }
    }
    
    std::cout << "Byze Genesis Block Miner (RandomX)\n";
    std::cout << "====================================\n\n";
    std::cout << "Parameters:\n";
    time_t time_val = nTime;
    struct tm* timeinfo = std::gmtime(&time_val);
    char time_str[100];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S UTC", timeinfo);
    std::cout << "  Timestamp: " << nTime << " (" << time_str << ")\n";
    std::cout << "  Difficulty Bits: 0x" << std::hex << std::setw(8) << std::setfill('0') << nBits << std::dec << "\n";
    std::cout << "  Genesis Reward: " << genesisReward / COIN << " BYZ\n";
    std::cout << "\nMining genesis block...\n";
    std::cout << "This may take a while...\n\n";
    
    // Create genesis block
    CBlockHeader genesis;
    genesis.nVersion = 1;
    genesis.hashPrevBlock.SetNull();
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = 0;
    
    // Create coinbase transaction (same as CreateHermesGenesisBlock)
    const char* pszTimestamp = "Byze (BYZ) genesis - RandomX PoW - 2026-04-27";
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) 
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp, 
                                       (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout.resize(1);
    txNew.vout[0].nValue = genesisReward;
    // Use the same genesis output script as in chainparams
    txNew.vout[0].scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    
    CBlock block(genesis);
    block.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    
    // Update genesis header with merkle root
    genesis.hashMerkleRoot = block.hashMerkleRoot;
    
    // Calculate target
    arith_uint256 bnTarget;
    bool fNegative, fOverflow;
    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    
    if (fNegative || bnTarget == 0 || fOverflow) {
        std::cerr << "Error: Invalid difficulty bits\n";
        return 1;
    }
    
    std::cout << "Target: " << bnTarget.GetHex() << "\n\n";

    const unsigned n_threads = std::max(1u, std::thread::hardware_concurrency());
    RandomXMiningContext* mine_ctx = CreateMiningContext(n_threads);
    if (!mine_ctx) {
        std::cerr << "Error: Failed to create RandomX mining context\n";
        return 1;
    }

    // Mine the block (use mining VMs — no global mutex per hash)
    auto startTime = std::chrono::steady_clock::now();
    uint64_t hashCount = 0;

    const uint32_t maxNonce = 0xFFFFFFFF;
    uint32_t nNonce = 0;
    while (nNonce < maxNonce) {
        genesis.nNonce = nNonce;
        uint256 hash;
        RandomXMiningHash(mine_ctx, 0, genesis, hash);
        hashCount++;

        if (UintToArith256(hash) <= bnTarget) {
            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();

            std::cout << "\n✓ Genesis block found!\n\n";
            std::cout << "Results:\n";
            std::cout << "  Nonce: " << nNonce << " (0x" << std::hex << nNonce << std::dec << ")\n";
            std::cout << "  Block Hash: " << hash.GetHex() << "\n";
            std::cout << "  Merkle Root: " << genesis.hashMerkleRoot.GetHex() << "\n";
            std::cout << "  Time: " << duration << " seconds\n";
            std::cout << "  Hashes: " << hashCount << "\n";
            if (duration > 0) {
                std::cout << "  Hash Rate: " << (hashCount / duration) << " H/s\n";
            }

            std::cout << "\nUpdate chainparams.cpp with these values:\n";
            std::cout << "  genesis = CreateHermesGenesisBlock(" << nTime << ", " << nNonce << ", 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << nBits << std::dec
                      << ", 1, " << (genesisReward / COIN) << " * COIN);\n";
            std::cout << "  consensus.hashGenesisBlock = genesis.GetHash();\n";
            std::cout << "  assert(consensus.hashGenesisBlock == uint256{\"" << hash.GetHex() << "\"});\n";
            std::cout << "  assert(genesis.hashMerkleRoot == uint256{\"" << genesis.hashMerkleRoot.GetHex() << "\"});\n";

            DestroyMiningContext(mine_ctx);
            return 0;
        }

        if (nNonce % 100000 == 0) {
            std::cout << "\rTrying nonce: " << nNonce << " (hash: " << hash.GetHex().substr(0, 16) << "...)     " << std::flush;
        }

        nNonce++;
    }

    DestroyMiningContext(mine_ctx);
    
    std::cerr << "\nError: Could not find valid genesis block with given parameters.\n";
    std::cerr << "Try adjusting difficulty (bits) or timestamp.\n";
    return 1;
}

