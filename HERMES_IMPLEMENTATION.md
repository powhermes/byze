# Byze Coin Implementation Guide

## Overview
This document outlines the implementation plan for converting Bitcoin Core to Byze Coin with RandomX mining and GUI wallet support.

## Completed Steps

### 1. Rebranding ✓
- Updated `CMakeLists.txt`:
  - Changed `CLIENT_NAME` from "Bitcoin Core" to "Byze"
  - Updated project name to "Byze"
  - Changed version to 1.0.0
  - Updated homepage and bug report URLs
- Updated `README.md` with Byze branding

## Next Steps

### 2. Enable GUI Build
**Required Dependencies:**
```bash
sudo apt-get install qt6-base-dev qt6-tools-dev qt6-l10n-tools qt6-tools-dev-tools libgl-dev libqrencode-dev
```

**Build Configuration:**
```bash
cmake -B build -DENABLE_WALLET=ON -DENABLE_IPC=OFF -DBUILD_GUI=ON
cmake --build build -j$(nproc)
```

### 3. RandomX Integration

#### 3.1 Add RandomX Library
RandomX is a CPU-friendly proof-of-work algorithm. Integration options:

**Option A: Git Submodule (Recommended)**
```bash
git submodule add https://github.com/tevador/RandomX.git src/randomx
git submodule update --init --recursive
```

**Option B: Manual Download**
```bash
cd src
git clone https://github.com/tevador/RandomX.git randomx
```

#### 3.2 Create CMake Integration
Create `cmake/randomx.cmake` following the pattern of `secp256k1.cmake`

#### 3.3 Modify Proof-of-Work
- Update `src/pow.cpp` to use RandomX instead of SHA256
- Modify `CheckProofOfWork()` to validate RandomX hashes
- Update mining code in `src/rpc/mining.cpp` and `src/node/miner.cpp`

#### 3.4 Update Consensus Parameters
- Modify `src/chainparams.cpp` to use RandomX
- Update difficulty adjustment for RandomX
- Change block time if needed (RandomX typically uses different block times)

### 4. Mining Integration

#### 4.1 Add Mining Thread
- Create mining thread in `src/node/miner.cpp`
- Implement RandomX hash calculation
- Add mining state management

#### 4.2 RPC Mining Commands
- Update `src/rpc/mining.cpp`:
  - `startmining` - Start CPU mining
  - `stopmining` - Stop mining
  - `getmininginfo` - Get mining statistics
  - `getmininghashrate` - Get current hashrate

#### 4.3 GUI Mining Controls
- Add mining tab to Qt GUI (`src/qt/`)
- Display hashrate, blocks found, mining status
- Start/stop mining buttons
- Thread count configuration

### 5. Network Parameters
- Update network magic bytes
- Change default ports
- Update DNS seeds (if applicable)
- Modify chain parameters for Byze network

## Implementation Priority

1. **Phase 1: GUI Build** (Current)
   - Install Qt dependencies
   - Enable GUI build
   - Test basic wallet functionality

2. **Phase 2: RandomX Integration**
   - Add RandomX library
   - Replace SHA256 PoW with RandomX
   - Test block validation

3. **Phase 3: Mining Implementation**
   - Add mining thread
   - Implement RPC commands
   - Test mining functionality

4. **Phase 4: GUI Mining**
   - Add mining UI
   - Connect to mining backend
   - Test end-to-end

## Key Files to Modify

### Proof-of-Work
- `src/pow.cpp` - Replace SHA256 with RandomX
- `src/pow.h` - Update function signatures
- `src/consensus/params.h` - Update consensus parameters

### Mining
- `src/node/miner.cpp` - Add RandomX mining
- `src/rpc/mining.cpp` - Add mining RPC commands
- `src/validation.cpp` - Update block validation

### GUI
- `src/qt/forms/` - Add mining UI forms
- `src/qt/` - Add mining controller classes
- `src/qt/bitcoin.cpp` - Integrate mining UI

### Network
- `src/chainparams.cpp` - Update network parameters
- `src/chainparamsbase.cpp` - Update base parameters

## Testing

1. **Unit Tests**
   - Update `src/test/pow_tests.cpp` for RandomX
   - Add mining tests
   - Test GUI components

2. **Integration Tests**
   - Test mining on regtest
   - Test GUI wallet functionality
   - Test network connectivity

## Notes

- RandomX requires significant memory (2GB+ per mining thread)
- Consider implementing mining pools support
- GUI should show mining statistics in real-time
- Consider adding GPU mining support later (RandomX supports both CPU and GPU)

## Resources

- RandomX: https://github.com/tevador/RandomX
- Qt Documentation: https://doc.qt.io/
- Bitcoin Core Developer Notes: `doc/developer-notes.md`

