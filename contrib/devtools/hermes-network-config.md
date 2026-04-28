# Byze Network Configuration

## Network Parameters

### Message Start (Network Magic)
Unique 4-byte identifier to prevent connection to other networks:
- **Byze Mainnet**: `0x48 0x45 0x52 0x4D` ("HERM" in ASCII)

### Ports
- **P2P Port**: `8888` (default for Byze mainnet)
- **RPC Port**: `8882` (default for Byze mainnet)
- **Testnet P2P**: `18888`
- **Testnet RPC**: `18882`
- **Regtest RPC**: `18843`

### Address Prefixes
- **PUBKEY_ADDRESS**: `0x28` (40 in decimal) - Addresses start with 'H'
- **SCRIPT_ADDRESS**: `0x05` (5 in decimal) - Script addresses start with '3'
- **SECRET_KEY**: `0xA8` (168 in decimal) - Private keys start with 'L' or 'K'
- **EXT_PUBLIC_KEY**: `0x04, 0x88, 0xB2, 0x1E` (same as Bitcoin for compatibility)
- **EXT_SECRET_KEY**: `0x04, 0x88, 0xAD, 0xE4` (same as Bitcoin for compatibility)

### Bech32 HRP
- **Mainnet**: `"byz"` (Byze)
- **Testnet**: `"thrm"` (test Byze)

### Genesis Block
- **Timestamp**: Current time (will be set when generating)
- **Message**: "Byze Coin - RandomX CPU Mining Network Launched 2025"
- **Initial Reward**: 50 BYZ
- **Difficulty**: Easier than Bitcoin for initial mining

## DNS Seeds
(To be configured later - leave empty for now)

## Notes
- These parameters ensure Byze cannot accidentally connect to Bitcoin network
- Ports chosen to avoid conflicts with common services
- Address prefixes chosen to be distinct from Bitcoin while maintaining compatibility

