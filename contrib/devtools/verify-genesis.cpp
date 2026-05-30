// Copyright (c) 2025-present The Byze developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <crypto/randomx_hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <consensus/merkle.h>
#include <consensus/amount.h>
#include <uint256.h>
#include <arith_uint256.h>
#include <util/strencodings.h>
#include <script/script.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <common/args.h>
#include <util/translation.h>
#include <random.h>

#include <iostream>
#include <iomanip>

// Define translation function (required for linking)
const TranslateFn G_TRANSLATION_FUN{nullptr};

int main(int argc, char* argv[])
{
    std::cout << "Byze Genesis Block Verifier\n";
    std::cout << "=============================\n\n";
    
    // Initialize RandomX
    RandomInit();
    
    // Initialize ArgsManager (required for SelectParams)
    ArgsManager args;
    
    // Get chain params
    SelectParams(ChainType::MAIN);
    const CChainParams& params = Params();
    const CBlock& genesis = params.GenesisBlock();
    
    std::cout << "Genesis Block Parameters:\n";
    std::cout << "  nTime: " << genesis.nTime << "\n";
    std::cout << "  nNonce: " << genesis.nNonce << "\n";
    std::cout << "  nBits: 0x" << std::hex << std::setw(8) << std::setfill('0') << genesis.nBits << std::dec << "\n";
    std::cout << "  nVersion: " << genesis.nVersion << "\n";
    std::cout << "  hashPrevBlock: " << genesis.hashPrevBlock.GetHex() << "\n";
    std::cout << "  hashMerkleRoot: " << genesis.hashMerkleRoot.GetHex() << "\n";
    std::cout << "  vtx.size(): " << genesis.vtx.size() << "\n";
    
    // Calculate hash using RandomX
    uint256 hash = RandomXHash(genesis);
    std::cout << "\nCalculated Block Hash (RandomX): " << hash.GetHex() << "\n";
    std::cout << "Expected Block Hash: " << params.GetConsensus().hashGenesisBlock.GetHex() << "\n";
    
    // Verify hash matches
    if (hash == params.GetConsensus().hashGenesisBlock) {
        std::cout << "\n✓ Genesis block hash matches!\n";
    } else {
        std::cout << "\n✗ ERROR: Genesis block hash does NOT match!\n";
        std::cout << "  This means the genesis block parameters are incorrect.\n";
        std::cout << "  You need to update chainparams.cpp with the correct values.\n";
        return 1;
    }
    
    // Verify merkle root
    uint256 merkleRoot = BlockMerkleRoot(genesis);
    std::cout << "\nCalculated Merkle Root: " << merkleRoot.GetHex() << "\n";
    std::cout << "Block Merkle Root: " << genesis.hashMerkleRoot.GetHex() << "\n";
    
    if (merkleRoot == genesis.hashMerkleRoot) {
        std::cout << "\n✓ Merkle root matches!\n";
    } else {
        std::cout << "\n✗ ERROR: Merkle root does NOT match!\n";
        return 1;
    }
    
    // Verify proof of work
    arith_uint256 bnTarget;
    bool fNegative, fOverflow;
    bnTarget.SetCompact(genesis.nBits, &fNegative, &fOverflow);
    
    if (fNegative || bnTarget == 0 || fOverflow) {
        std::cout << "\n✗ ERROR: Invalid difficulty bits\n";
        return 1;
    }
    
    if (UintToArith256(hash) <= bnTarget) {
        std::cout << "\n✓ Proof of work is valid!\n";
    } else {
        std::cout << "\n✗ ERROR: Proof of work is invalid!\n";
        std::cout << "  Hash: " << hash.GetHex() << "\n";
        std::cout << "  Target: " << bnTarget.GetHex() << "\n";
        return 1;
    }
    
    std::cout << "\n✓ All checks passed! Genesis block is valid.\n";
    return 0;
}

