# Byze v0.1.0-regtest-stable Release Notes

**⚠️ WARNING: NOT FOR MAINNET USE ⚠️**

This is a **regtest-only** pre-release milestone. This software is **NOT ready for mainnet deployment** and should **ONLY** be used for development and testing purposes in regtest mode.

## Release Information

- **Version**: v0.1.0-regtest-stable
- **Release Date**: December 2024
- **Release Type**: Pre-release milestone (regtest-only)
- **Tag**: `v0.1.0-regtest-stable`

## Overview

Byze v0.1.0-regtest-stable represents a stable baseline for regtest development and testing. This release integrates RandomX proof-of-work mining with XMSS/SPHINCS+ quantum-safe signatures, providing a working foundation for future testnet and mainnet development.

## Features

### Core Integrations

1. **RandomX Proof-of-Work**
   - Integrated RandomX CPU mining algorithm
   - Replaces SHA256 with RandomX for block validation
   - Light mode implementation for validation efficiency
   - Network-wide consistent hashing via fixed network key

2. **Quantum-Safe Signatures**
   - XMSS (eXtended Merkle Signature Scheme) integration
   - SPHINCS+ (Stateless Hash-based Signatures) integration
   - Dual-signature scheme for enhanced security
   - Quantum-resistant cryptographic primitives

3. **Regtest Mining**
   - Functional regtest block generation
   - Time-capped mining to prevent RPC starvation
   - Optimized nonce search for regtest mode
   - Verified consensus bootstrapping

### Technical Details

- **Consensus**: Bitcoin Core derived with RandomX PoW
- **Mining**: CPU-based RandomX mining
- **Signatures**: XMSS + SPHINCS+ dual signatures
- **Network**: Regtest mode only (no mainnet/testnet)

## Fixed Issues

### Critical Fixes

1. **RPC DeriveTarget Crash (Fixed)**
   - **Issue**: RPC commands (`getblockchaininfo`, `getdifficulty`, `getmininginfo`) crashed when calling `DeriveTarget()` for RandomX PoW
   - **Fix**: Modified `GetTarget()` in `src/rpc/util.cpp` to bypass `DeriveTarget()` for RandomX PoW
   - **Status**: ✅ Resolved - All RPC commands now work correctly

2. **ProcRand Assertion Failure (Fixed)**
   - **Issue**: Daemon crashed during mining with assertion `num <= 32` failed in `ProcRand`
   - **Root Cause**: SPHINCS+ key generation requested 64 bytes of random data, but `ProcRand` only supports 32 bytes per call
   - **Fix**: Modified `src/crypto/quantum_safe.cpp` to generate SPHINCS+ keys in 32-byte chunks
   - **Status**: ✅ Resolved - Mining no longer crashes

3. **RPC Thread Starvation (Fixed)**
   - **Issue**: RandomX mining could block RPC threads indefinitely
   - **Fix**: Implemented 15-second time cap for regtest mining (30s for mainnet)
   - **Status**: ✅ Resolved - RPC remains responsive during mining

### Mining Optimizations

1. **Regtest Mining Speed**
   - 100,000x nonce increment for regtest (vs 1x for mainnet)
   - Increased max_tries to 1M for regtest
   - Periodic timestamp adjustment to expand search space
   - Time check optimization (every 1000 iterations)

## Known Limitations

### Critical Limitations

1. **Regtest-Only Release**
   - ⚠️ **This release is ONLY for regtest mode**
   - No testnet or mainnet support
   - Consensus rules are not finalized for production use

2. **Mining Performance**
   - RandomX is computationally expensive
   - Blocks may take 15+ seconds to find even in regtest
   - Mining success depends on CPU performance
   - No asynchronous mining implementation yet

3. **Time Caps**
   - 15-second mining time cap for regtest (prevents RPC starvation)
   - May cause mining attempts to timeout on slower systems
   - This is expected behavior and acceptable for testing

4. **No Testnet Support**
   - Testnet mode not yet implemented
   - Mainnet deployment not ready
   - Future releases will add testnet support

5. **Mining Infrastructure**
   - No mining pool support
   - No standalone miner implementation
   - Mining only available via RPC `generatetoaddress`

### Technical Limitations

1. **RandomX JIT**
   - JIT compilation cannot be disabled for regtest after initialization
   - Time capping used as workaround
   - May require reinitialization for proper JIT control

2. **Quantum-Safe Key Management**
   - Key generation uses chunked random byte generation
   - Key storage and backup mechanisms need enhancement
   - Key rotation strategies not yet implemented

3. **Block Validation**
   - Full validation path verified for regtest
   - Mainnet validation paths not yet tested
   - Performance optimizations pending

## Usage

### Regtest Mining

```bash
# Start daemon in regtest mode
./build/bin/bitcoind -regtest -daemon

# Create wallet and get address
./build/bin/bitcoin-cli -regtest createwallet test
ADDR=$(./build/bin/bitcoin-cli -regtest -rpcwallet=test getnewaddress)

# Mine blocks (may take up to 15 seconds per block)
./build/bin/bitcoin-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR"

# Check block count
./build/bin/bitcoin-cli -regtest getblockcount
```

### Testing

Use the provided test scripts:
- `test-regtest-mining.sh` - Comprehensive mining test
- `test-mining-simple.sh` - Simple mining test
- `test-regtest.sh` - Basic regtest functionality test

## Build Requirements

- CMake 3.22+
- C++17 compatible compiler
- RandomX library (included)
- Quantum-safe crypto libraries (included)

See `BUILD.md` and `INSTALL.md` for detailed build instructions.

## Files Changed

### Modified Files
- `src/rpc/mining.cpp` - Mining time caps and regtest optimizations
- `src/rpc/util.cpp` - RPC target derivation fix
- `src/crypto/quantum_safe.cpp` - SPHINCS+ key generation fix
- `src/pow.cpp` - RandomX proof-of-work integration
- `src/crypto/randomx_hash.cpp` - RandomX hashing implementation

### New Files
- `test-regtest-mining.sh` - Mining test script
- `test-mining-simple.sh` - Simple mining test
- `MINING_GUIDE.md` - Mining documentation
- `REGTEST_TESTING_STATUS.md` - Regtest testing status
- `MINING_FIX_SUMMARY.md` - Mining fix documentation

## Security Considerations

⚠️ **IMPORTANT SECURITY WARNINGS:**

1. **NOT FOR PRODUCTION USE**
   - This software is experimental and not audited
   - Do not use with real funds
   - Consensus rules may change in future releases

2. **Regtest Mode Only**
   - This release only supports regtest mode
   - No network security guarantees
   - No mainnet compatibility

3. **Quantum-Safe Signatures**
   - XMSS/SPHINCS+ implementations are experimental
   - Key management requires careful handling
   - Signature verification performance needs optimization

## Future Roadmap

### Planned for Next Releases

1. **Testnet Support**
   - Testnet chain parameters
   - Testnet seed nodes
   - Testnet-specific optimizations

2. **Mining Improvements**
   - Asynchronous mining implementation
   - Mining pool support
   - Standalone miner tool

3. **Performance Optimizations**
   - RandomX JIT control improvements
   - Quantum-safe signature performance
   - Block validation optimizations

4. **Mainnet Preparation**
   - Final consensus rule review
   - Security audit preparation
   - Network deployment planning

## Support

For issues, questions, or contributions:
- Review existing documentation in `doc/` directory
- Check test scripts for usage examples
- Review `REGTEST_TESTING_STATUS.md` for current testing status

## Acknowledgments

This release builds upon Bitcoin Core and integrates:
- RandomX proof-of-work algorithm
- XMSS/SPHINCS+ quantum-safe signatures
- Various open-source cryptographic libraries

## License

Byze is released under the MIT license. See `COPYING` for details.

---

**⚠️ REMINDER: This is a regtest-only pre-release. NOT FOR MAINNET USE. ⚠️**


