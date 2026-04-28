# Release Tag Preparation: v0.1.0-regtest-stable

This document outlines the steps to create the v0.1.0-regtest-stable release tag.

## Pre-Tag Checklist

### ✅ Documentation
- [x] `RELEASE_NOTES_v0.1.0.md` created with features, fixes, limitations, and warnings
- [x] `README.md` updated with project description and regtest usage
- [x] All regtest-only optimizations marked with comments
- [x] Warnings added about regtest-only status

### ✅ Code Hygiene
- [x] All regtest-only code gated behind `is_regtest` checks
- [x] Temporary regtest optimizations marked with `REGTEST OPTIMIZATION` comments
- [x] TODO comments added for future testnet/mainnet review

### ✅ Versioning
- [x] Version set to 0.1.0 in `CMakeLists.txt`
- [x] Binary names remain as `bitcoin-cli`, `bitcoind`, etc. (as requested)

### ✅ Functionality
- [x] Regtest mining verified working
- [x] RandomX integration stable
- [x] Quantum-safe signatures functional
- [x] No consensus rule changes made

## Creating the Tag

### Step 1: Verify Current State

```bash
# Ensure you're on the correct branch/commit
git status

# Verify version in CMakeLists.txt
grep "CLIENT_VERSION" CMakeLists.txt

# Run a quick test to ensure mining works
./test-regtest-mining.sh
```

### Step 2: Create the Tag

```bash
# Create annotated tag
git tag -a v0.1.0-regtest-stable -m "Byze v0.1.0-regtest-stable

Regtest-only pre-release milestone with:
- RandomX proof-of-work integration
- XMSS/SPHINCS+ quantum-safe signatures
- Working regtest mining
- Fixed RPC crashes and ProcRand bug

⚠️ WARNING: NOT FOR MAINNET USE - REGTEST ONLY ⚠️

See RELEASE_NOTES_v0.1.0.md for full details."

# Verify tag was created
git tag -l "v0.1.0*"

# Show tag details
git show v0.1.0-regtest-stable
```

### Step 3: Push Tag (if using remote repository)

```bash
# Push tag to remote
git push origin v0.1.0-regtest-stable

# Or push all tags
git push --tags
```

## Post-Tag Verification

### Build Verification

```bash
# Clean build
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Verify version string
./bin/bitcoind --version | grep -i "byze\|0.1.0"

# Test regtest mining
./bin/bitcoind -regtest -daemon
sleep 5
./bin/bitcoin-cli -regtest createwallet test
ADDR=$(./bin/bitcoin-cli -regtest -rpcwallet=test getnewaddress)
./bin/bitcoin-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR"
./bin/bitcoin-cli -regtest getblockcount
```

### Documentation Verification

- [ ] `RELEASE_NOTES_v0.1.0.md` is present and complete
- [ ] `README.md` has proper warnings
- [ ] All test scripts are functional
- [ ] Version string shows 0.1.0

## Release Artifacts (Optional)

If creating release artifacts:

```bash
# Create source archive
git archive --format=tar.gz --prefix=byze-v0.1.0-regtest-stable/ \
    v0.1.0-regtest-stable > byze-v0.1.0-regtest-stable.tar.gz

# Verify archive
tar -tzf byze-v0.1.0-regtest-stable.tar.gz | head -20
```

## Important Notes

1. **This is a regtest-only release** - Do not use on mainnet or testnet
2. **No consensus changes** - This release freezes the current consensus behavior
3. **Future work** - Testnet support will be added in future releases
4. **Binary names** - Binaries remain as `bitcoin-*` for compatibility

## Next Steps After Tagging

1. Document any issues found during testing
2. Plan testnet implementation
3. Review regtest optimizations for testnet/mainnet applicability
4. Continue development on main branch for future releases

---

**Tag Name**: `v0.1.0-regtest-stable`  
**Release Type**: Pre-release milestone (regtest-only)  
**Status**: Stable baseline for regtest development


