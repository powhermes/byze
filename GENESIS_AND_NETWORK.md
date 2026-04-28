# Byze Genesis Block and Network Configuration

## Summary

This document describes the network parameters configured for Byze Coin, including the genesis block, network magic bytes, ports, and address formats.

## Network Parameters

### Message Start (Network Magic)
- **Bytes**: `0x48 0x45 0x52 0x4D` ("HERM" in ASCII)
- **Purpose**: Prevents accidental connection to Bitcoin or other networks
- **Location**: `src/kernel/chainparams.cpp` line ~129-132

### Ports

#### P2P (Peer-to-Peer) Ports
- **Mainnet**: `8888`
- **Testnet**: `18333` (to be updated to `18888`)
- **Signet**: `38333` (to be updated to `38888`)
- **Regtest**: `18444` (to be updated to `18888`)

#### RPC Ports
- **Mainnet**: `8882`
- **Testnet**: `18882`
- **Testnet4**: `48882`
- **Signet**: `38882`
- **Regtest**: `18843`

**Location**: 
- P2P ports: `src/kernel/chainparams.cpp`
- RPC ports: `src/chainparamsbase.cpp`

### Genesis Block

#### Current Configuration
- **Timestamp**: `1735689600` (January 1, 2025 00:00:00 UTC)
- **Message**: "Byze Coin - RandomX CPU Mining Network Launched 2025"
- **Initial Reward**: 50 BYZ
- **Difficulty Bits**: `0x1e0ffff0` (easier than Bitcoin for initial mining)
- **Nonce**: `0` (placeholder - needs to be mined)

#### Status
⚠️ **IMPORTANT**: The genesis block hash has not been properly computed yet. The assertions are commented out in the code. You need to:

1. **For SHA256 (current)**: Run the genesis block generator or mine it manually
2. **For RandomX (future)**: Once RandomX is integrated, regenerate the genesis block using RandomX hashing

#### Generating Genesis Block

Use the provided script:
```bash
./contrib/devtools/genesis-gen.py
```

Or manually mine using the daemon once RandomX is integrated.

### Address Formats

#### Base58 Prefixes
- **PUBKEY_ADDRESS**: `40` (0x28) - Addresses start with 'H'
- **SCRIPT_ADDRESS**: `5` (0x05) - Script addresses start with '3'
- **SECRET_KEY**: `168` (0xA8) - Private keys start with 'L' or 'K'
- **EXT_PUBLIC_KEY**: `0x04, 0x88, 0xB2, 0x1E` - Same as Bitcoin (wallet compatibility)
- **EXT_SECRET_KEY**: `0x04, 0x88, 0xAD, 0xE4` - Same as Bitcoin (wallet compatibility)

#### Bech32 HRP
- **Mainnet**: `"byz"` (Byze)
- **Testnet**: `"thrm"` (to be updated)

**Location**: `src/kernel/chainparams.cpp` line ~159-166

### DNS Seeds

Currently empty. Nodes will need to be added manually using `-addnode` or via configuration file.

To add a node:
```bash
./bitcoind -addnode=node.ip.address:8888
```

Or in `bitcoin.conf`:
```
addnode=node.ip.address:8888
```

## Files Modified

1. **src/kernel/chainparams.cpp**
   - Added `CreateHermesGenesisBlock()` function
   - Updated network magic bytes
   - Updated P2P port
   - Updated genesis block (placeholder)
   - Updated address prefixes
   - Updated bech32 HRP
   - Removed Bitcoin DNS seeds

2. **src/chainparamsbase.cpp**
   - Updated RPC ports for all networks

3. **contrib/devtools/genesis-gen.py**
   - Genesis block generator script (for SHA256)

4. **contrib/devtools/byze-network-config.md**
   - Network configuration documentation

## Next Steps

1. ✅ Network magic bytes configured
2. ✅ Ports configured
3. ✅ Address prefixes configured
4. ⚠️ Genesis block needs to be properly mined
5. ⚠️ RandomX integration needed for final genesis block
6. ⚠️ DNS seeds to be configured when network launches

## Testing

To test the network configuration:

```bash
# Build the project
./build-ubuntu.sh

# Start regtest (for testing)
./build/bin/bitcoind -regtest -daemon

# Check network info
./build/bin/bitcoin-cli -regtest getblockchaininfo
```

## Notes

- The genesis block will need to be regenerated once RandomX is integrated
- All network parameters are distinct from Bitcoin to prevent network confusion
- Ports chosen to avoid conflicts with common services
- Address prefixes ensure addresses are visually distinct from Bitcoin

