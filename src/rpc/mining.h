// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_MINING_H
#define BITCOIN_RPC_MINING_H

/**
 * Default max RandomX hash attempts for RPC generatetoaddress / generatetodescriptor.
 * Bitcoin Core used 1e6 for double-SHA256; at soft-launch nBits (e.g. 1e0ffff0) the
 * mean is ~1.05e6 RandomX hashes per block, so 1e6 maxtries often exhausts before a hit.
 */
static const uint64_t DEFAULT_MAX_TRIES{50000000};

#endif // BITCOIN_RPC_MINING_H
