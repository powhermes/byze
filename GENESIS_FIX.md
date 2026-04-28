# Fixing "Failed to read block" Error

## Problem

When running `bitcoin-qt` or `bitcoind`, you get:
```
Error: A fatal internal error occurred, see debug.log for details: Failed to read block.
Error: A fatal internal error occurred, see debug.log for details: Failed to connect best block (Failed to read block.).
```

## Root Cause

This error occurs when:
1. The genesis block hash in `chainparams.cpp` doesn't match what was actually mined
2. There's an existing database with an incompatible genesis block
3. The genesis block parameters (nTime, nNonce, nBits) don't match between `mine-genesis` output and `chainparams.cpp`

## Solution

### Step 1: Verify the Genesis Block

First, build and run the verification tool:

```bash
cd /home/h/byze
cmake --build build -t verify-genesis
./build/bin/verify-genesis
```

This will tell you if the genesis block hash matches what's expected.

### Step 2: Check What You Mined

If you ran `mine-genesis` and got output like:
```
✓ Genesis block found!

Results:
  Nonce: 284720
  Block Hash: 00000521873f3897712eb8a35a7e0c7a7e73fe04ff15a7d0f9f565f47f4fab92
  Merkle Root: ec80e57f3f84ef5f044e1614ff1514de2dae0264101426a3b716d95cdcfd0490
```

You need to ensure these values match what's in `src/kernel/chainparams.cpp`.

### Step 3: Update chainparams.cpp (if needed)

If the hash doesn't match, update `src/kernel/chainparams.cpp` with the values from `mine-genesis`:

For **mainnet** (around line 147):
```cpp
genesis = CreateHermesGenesisBlock(1735689600, <YOUR_NONCE>, 0x1e0ffff0, 1, 50 * COIN);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256{"<YOUR_BLOCK_HASH>"});
assert(genesis.hashMerkleRoot == uint256{"<YOUR_MERKLE_ROOT>"});
```

For **testnet** (around line 261):
```cpp
genesis = CreateHermesGenesisBlock(1735693200, <YOUR_NONCE>, 0x1e0fffff, 1, 50 * COIN);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256{"<YOUR_BLOCK_HASH>"});
assert(genesis.hashMerkleRoot == uint256{"<YOUR_MERKLE_ROOT>"});
```

For **stagenet** (around line 453):
```cpp
genesis = CreateHermesGenesisBlock(1735696800, <YOUR_NONCE>, 0x1e0ffff0, 1, 50 * COIN);
consensus.hashGenesisBlock = genesis.GetHash();
assert(consensus.hashGenesisBlock == uint256{"<YOUR_BLOCK_HASH>"});
assert(genesis.hashMerkleRoot == uint256{"<YOUR_MERKLE_ROOT>"});
```

### Step 4: Clear the Database

If you've already run the client and it created a database with the wrong genesis block, you need to clear it:

**Option A: Delete the data directory (fresh start)**
```bash
# For mainnet (default)
rm -rf ~/.bitcoin/blocks ~/.bitcoin/chainstate ~/.bitcoin/blocks/index

# Or if using a custom datadir
rm -rf <your-datadir>/blocks <your-datadir>/chainstate <your-datadir>/blocks/index
```

**Option B: Use -reindex flag**
```bash
./build/bin/bitcoind -reindex
# or
./build/bin/bitcoin-qt -reindex
```

**Option C: Use -reindex-chainstate (if only chainstate is wrong)**
```bash
./build/bin/bitcoind -reindex-chainstate
```

### Step 5: Rebuild and Test

After updating `chainparams.cpp`:

```bash
cd /home/h/byze
cmake --build build
./build/bin/verify-genesis  # Verify the genesis block is correct
./build/bin/bitcoin-qt      # Test the client
```

## Important Notes

1. **Always verify**: After mining a genesis block, always run `verify-genesis` to ensure the hash matches
2. **Database compatibility**: If you change the genesis block, you MUST clear the database or use `-reindex`
3. **Network separation**: Make sure you're using the correct network (mainnet/testnet/stagenet) when mining and running the client

## Troubleshooting

If `verify-genesis` shows the hash doesn't match:
- Check that the `nTime`, `nNonce`, and `nBits` values in `chainparams.cpp` exactly match what `mine-genesis` output
- Ensure the timestamp message in `CreateHermesGenesisBlock` matches what `mine-genesis` uses
- Rebuild the project after making changes

If you still get errors after clearing the database:
- Check `debug.log` for more details
- Ensure RandomX is properly initialized
- Verify the genesis block construction matches between `mine-genesis.cpp` and `chainparams.cpp`

