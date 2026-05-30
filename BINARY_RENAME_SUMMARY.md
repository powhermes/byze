# Binary and Data Directory Rename Summary

## Changes Made

### 1. Binary Renaming (bitcoin-* → byze-*)

All binaries have been renamed using CMake `OUTPUT_NAME` properties:

- `bitcoin` → `byze`
- `bitcoind` → `byzed`
- `bitcoin-cli` → `byze-cli`
- `bitcoin-tx` → `byze-tx`
- `bitcoin-util` → `byze-util`
- `bitcoin-wallet` → `byze-wallet`
- `bitcoin-node` → `byze-node` (if IPC enabled)
- `bitcoin-qt` → `byze-qt` (if GUI enabled)

**Files Modified:**
- `src/CMakeLists.txt` - Added `OUTPUT_NAME` properties for all binaries
- `src/qt/CMakeLists.txt` - Added `OUTPUT_NAME` property for bitcoin-qt

### 2. Data Directory Renaming (.bitcoin → .byze)

Default data directory changed from `~/.bitcoin` to `~/.byze`:

**Files Modified:**
- `src/common/args.cpp` - Updated `GetDefaultDataDir()` function:
  - Unix-like: `~/.bitcoin` → `~/.byze`
  - macOS: `~/Library/Application Support/Bitcoin` → `~/Library/Application Support/Byze`
  - Windows: `%LOCALAPPDATA%\Bitcoin` → `%LOCALAPPDATA%\Byze`

### 3. Script Updates

All shell scripts updated to use new binary names and data directory:

**Scripts Updated:**
- `test-regtest-mining.sh`
- `test-mining-simple.sh`
- `test-mining-regtest.sh`
- `test-mining-monitor.sh`
- `test-mining-foreground.sh`
- `test-regtest.sh`
- `mine-block.sh`
- `wait-for-node.sh`
- `fix-genesis.sh`

### 4. Documentation Updates

**Documentation Updated:**
- `README.md`
- `MINING_GUIDE.md`
- `QUICK_MINING_TEST.md`

All references to `bitcoin-*` binaries and `~/.bitcoin` directory updated.

### 5. Test Framework Updates

**Files Modified:**
- `test/functional/test_framework/util.py` - Updated binary name mappings:
  - `"bitcoin"` → `"byze"`
  - `"bitcoind"` → `"byzed"`
  - `"bitcoin-cli"` → `"byze-cli"`
  - etc.

## Verification

### No Consensus Code Modified

✅ Verified that no consensus-related code was touched:
- `src/pow.cpp` - Not modified
- `src/crypto/randomx_hash.cpp` - Not modified
- `src/validation.cpp` - Not modified
- Consensus logic remains frozen

The only file in `src/crypto/` that appears in git diff is `quantum_safe.cpp`, which was modified in a previous task (ProcRand bug fix), not in this renaming task.

### Build Verification

After rebuilding, binaries will be named:
- `build/bin/byzed`
- `build/bin/byze-cli`
- `build/bin/byze-tx`
- `build/bin/byze-util`
- `build/bin/byze-wallet`
- `build/bin/byze-qt` (if GUI enabled)

### Data Directory Isolation

New installations will use:
- `~/.byze/` (Unix-like)
- `~/Library/Application Support/Byze/` (macOS)
- `%LOCALAPPDATA%\Byze\` (Windows)

This provides complete isolation from Bitcoin Core data directories.

## Next Steps

1. **Rebuild** the project to generate new binary names:
   ```bash
   cd build
   cmake ..
   make -j$(nproc)
   ```

2. **Test** that binaries work with new names:
   ```bash
   ./build/bin/byzed -regtest -daemon
   ./build/bin/byze-cli -regtest getblockchaininfo
   ```

3. **Verify** data directory isolation:
   ```bash
   ls ~/.byze/  # Should be empty or new
   ```

## Notes

- Internal target names remain as `bitcoin*` for compatibility with existing code
- Only the output binary names are changed via `OUTPUT_NAME` property
- This allows gradual migration without breaking internal references
- Test framework environment variables remain the same (BITCOIND, BITCOINCLI, etc.) for compatibility

---

**Status**: ✅ Binary and data directory renaming complete
**Consensus Code**: ✅ Not modified (frozen as requested)


