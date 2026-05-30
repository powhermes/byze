# Mining Fix Summary

## Problem Identified

The daemon was crashing during mining attempts with the following assertion failure:

```
bitcoind: ./random.cpp:567: void {anonymous}::ProcRand(unsigned char*, int, {anonymous}::RNGLevel, bool): Assertion `num <= 32' failed.
```

## Root Cause

The crash occurred in the quantum-safe signature code when generating SPHINCS+ keys. SPHINCS+ uses `KEY_SIZE = 64` bytes, but the `ProcRand` function in `src/random.cpp` has an assertion that limits random byte generation to 32 bytes per call:

```cpp
assert(num <= 32);
```

The SPHINCS+ key generation code was calling:
```cpp
GetStrongRandBytes(std::span<unsigned char>(m_seed.data(), KEY_SIZE));  // KEY_SIZE = 64
```

This violated the 32-byte limit and caused the assertion failure, crashing the daemon.

## Solution

Modified `src/crypto/quantum_safe.cpp` to generate random bytes in chunks of 32 bytes for SPHINCS+ keys:

**Before:**
```cpp
GetStrongRandBytes(std::span<unsigned char>(m_seed.data(), KEY_SIZE));
GetStrongRandBytes(std::span<unsigned char>(m_private_key.data(), KEY_SIZE));
```

**After:**
```cpp
// Generate random seed (in chunks of 32 bytes since ProcRand has a 32-byte limit)
GetStrongRandBytes(std::span<unsigned char>(m_seed.data(), 32));
GetStrongRandBytes(std::span<unsigned char>(m_seed.data() + 32, KEY_SIZE - 32));

// Generate private key from seed (in chunks of 32 bytes)
GetStrongRandBytes(std::span<unsigned char>(m_private_key.data(), 32));
GetStrongRandBytes(std::span<unsigned char>(m_private_key.data() + 32, KEY_SIZE - 32));
```

## Files Modified

- `src/crypto/quantum_safe.cpp` - Fixed SPHINCS+ key generation to use 32-byte chunks

## Verification

After the fix:
- ✅ Daemon no longer crashes during mining
- ✅ Blocks are successfully mined
- ✅ RandomX mining works correctly
- ✅ Quantum-safe signatures are generated properly

## Test Results

The comprehensive test script (`test-regtest-mining.sh`) now successfully:
- Starts the daemon
- Creates wallets
- Mines blocks successfully
- Verifies blockchain state

Example successful mining output:
```
Attempt  1/30 (height:   0): ✓ BLOCK MINED! (1s, hash: 62b24d5b59fd916d...)
Attempt  2/30 (height:   1): ✓ BLOCK MINED! (1s, hash: 0dab8b11dfd96263...)
Attempt  3/30 (height:   2): ✓ BLOCK MINED! (0s, hash: 589ebd2053c0fc90...)
```

## Notes

- XMSS keys use `KEY_SIZE = 32`, which is within the limit, so no changes were needed
- SPHINCS+ keys use `KEY_SIZE = 64`, requiring the chunked approach
- The fix maintains the same cryptographic security while respecting the RNG limitations


