# RandomX Integration for Byze Coin

## Overview

Byze Coin uses RandomX as its proof-of-work algorithm instead of SHA256. RandomX is a CPU-friendly, ASIC-resistant algorithm that makes mining accessible to regular computers.

## Implementation Status

### ✅ Completed

1. **RandomX Library Integration**
   - Added RandomX as git submodule (`src/randomx/`)
   - Created CMake integration (`cmake/randomx.cmake`)
   - Added to build system (`src/CMakeLists.txt`)

2. **RandomX Hash Wrapper**
   - Created `src/crypto/randomx_hash.cpp` and `randomx_hash.h`
   - Implements `RandomXHash()` function to hash block headers
   - Thread-safe initialization and cleanup
   - Uses fixed network key derived from "HERM" magic bytes

3. **Proof-of-Work Updates**
   - Updated `src/pow.cpp` to use RandomX instead of SHA256
   - Added new `CheckProofOfWork()` overload that takes `CBlockHeader` directly
   - Updated `src/validation.cpp` to use RandomX for block validation
   - Updated `src/rpc/mining.cpp` to use RandomX for block generation

4. **Build System**
   - RandomX linked to `bitcoin_crypto` library
   - CMake configuration for RandomX compilation

### ⚠️ Pending

1. **Test Updates**
   - Update test files to use new RandomX `CheckProofOfWork()` signature
   - Files needing updates:
     - `src/test/util/mining.cpp`
     - `src/test/validation_block_tests.cpp`
     - `src/test/util/setup_common.cpp`
     - `src/test/peerman_tests.cpp`
     - `src/test/headers_sync_chainwork_tests.cpp`
     - `src/test/miner_tests.cpp`
     - `src/test/fuzz/p2p_headers_presync.cpp`
     - `src/test/blockencodings_tests.cpp`
     - `src/test/blockfilter_index_tests.cpp`

2. **Genesis Block**
   - Regenerate genesis block using RandomX
   - Update assertions in `src/kernel/chainparams.cpp`

3. **Mining Optimization**
   - Implement full-memory mode for mining (faster)
   - Add mining thread management
   - Optimize nonce iteration

4. **Performance Testing**
   - Benchmark RandomX hashing performance
   - Test on different CPU architectures
   - Verify memory requirements

## Technical Details

### RandomX Configuration

- **Key**: Fixed 32-byte key derived from network magic "HERM"
- **Mode**: Light mode for validation (256 MiB), can use full mode for mining (2 GiB)
- **Flags**: JIT compilation enabled, hardware AES when available

### Block Header Hashing

RandomX hashes the 80-byte block header:
- Version (4 bytes)
- Previous Block Hash (32 bytes)
- Merkle Root (32 bytes)
- Time (4 bytes)
- Bits (4 bytes)
- Nonce (4 bytes)

The hash output is 32 bytes (256 bits), same as SHA256.

### Memory Requirements

- **Light Mode** (validation): ~256 MiB per VM
- **Full Mode** (mining): ~2 GiB per VM
- **Cache**: ~64 MiB (shared across all VMs)

## Building with RandomX

RandomX is automatically included when building Byze. No additional configuration needed.

```bash
./build-ubuntu.sh
# or
cmake -B build -DENABLE_WALLET=ON -DBUILD_GUI=ON
cmake --build build -j$(nproc)
```

## Usage

The RandomX integration is transparent - all proof-of-work checks automatically use RandomX:

```cpp
// Old way (still works for compatibility):
CheckProofOfWork(hash, nBits, params);

// New way (uses RandomX):
CheckProofOfWork(block, nBits, params);
```

## Next Steps

1. Update all test files to use new API
2. Regenerate genesis block with RandomX
3. Test mining functionality
4. Add GUI mining controls
5. Performance optimization

## Notes

- RandomX requires 64-bit architecture
- AES-NI support recommended for best performance
- Large memory pages can improve performance (requires root)
- The implementation is thread-safe and can handle multiple concurrent hash operations



