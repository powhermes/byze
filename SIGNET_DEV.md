# Byze Signet Development Guide

This guide explains how to use Byze signet for local development with block production enabled.

## Overview

Byze signet uses a 1-of-1 P2PK challenge script for local development. Blocks are authorized by satisfying the signet challenge script (BIP325), not by proof-of-work mining. This matches Bitcoin Core signet semantics exactly.

## Signet Challenge

The dev signet uses a hardcoded compressed public key:
- **Public Key (hex):** `0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798`
- **Challenge Script:** `<compressed_pubkey> OP_CHECKSIG` (P2PK format)
- **Script (hex):** `210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798ac`

## Setup

### 1. Generate the Signet Authority Keypair

You need to generate a keypair that corresponds to the hardcoded public key. The public key above is the secp256k1 generator point's compressed form, which corresponds to the private key `1` (0x01).

**Option A: Use byze-cli (recommended)**

```bash
# Create a temporary wallet
./build/bin/byze-cli -signet createwallet temp-keygen

# Generate a keypair (we'll derive the one we need)
# The private key for the dev signet pubkey is: 1 (0x01)
# Import it directly:
./build/bin/byze-cli -signet -rpcwallet=temp-keygen importprivkey "cMahea7zqjxrtgAbB7LSGbcQUr1uX1ojuat9jZodMN87JcbXMTcA" "signet-authority" false

# Verify the public key matches
./build/bin/byze-cli -signet -rpcwallet=temp-keygen getaddressinfo $(./build/bin/byze-cli -signet -rpcwallet=temp-keygen getnewaddress)
```

**Option B: Use Python/bitcoin library**

```python
from bitcoin import *
# The private key is 1 (0x01 in hex)
privkey_hex = "01"
key = PrivateKey(privkey_hex)
pubkey = key.pubkey.hex()
wif = key.to_wif()
print(f"Private key WIF: {wif}")
print(f"Public key: {pubkey}")
# Should match: 0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798
```

**Option C: Manual calculation**

The dev signet uses the secp256k1 generator point's compressed public key, which corresponds to private key `1`:
- **Private key (hex):** `01`
- **Private key (WIF, testnet/signet):** `cMahea7zqjxrtgAbB7LSGbcQUr1uX1ojuat9jZodMN87JcbXMTcA`

### 2. Start the Signet Node

```bash
./build/bin/byzed -signet -daemon
```

Or with a custom data directory:

```bash
./build/bin/byzed -signet -datadir=/path/to/signet/data -daemon
```

### 3. Import the Private Key

Import the signet authority private key into your wallet:

```bash
# Create or use existing wallet
./build/bin/byze-cli -signet createwallet miner

# Import the private key (NOTE: must specify wallet with -rpcwallet)
# WIF for dev signet (private key = 1): cMahea7zqjxrtgAbB7LSGbcQUr1uX1ojuat9jZodMN87JcbXMTcA
./build/bin/byze-cli -signet -rpcwallet=miner importprivkey "cMahea7zqjxrtgAbB7LSGbcQUr1uX1ojuat9jZodMN87JcbXMTcA" "signet-authority" false
```

**Alternative: Import using descriptor (for P2PK script)**

If you prefer to use descriptors:

```bash
# Import the descriptor with checksum
# Note: This imports the PUBLIC key only - you still need the private key for signing
./build/bin/byze-cli -signet -rpcwallet=miner importdescriptors '[{
  "desc": "pk(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)#gn28ywm7",
  "timestamp": "now",
  "active": true
}]'

# However, for signet mining you MUST import the private key, not just the descriptor
# So use importprivkey instead (see above)
```

### 4. Generate Blocks

**Important:** `generatetoaddress` does NOT work for signet blocks because signet requires a signet solution to be added to the block. You must use the signet miner script instead.

### Using the Signet Miner Script

The signet miner script automatically creates blocks with the signet solution:

```bash
# Make sure you're in the repository root (not build/bin)
cd /home/a/byze

# Generate blocks using the signet miner
# The script will automatically add the signet solution if the key is in your wallet
python3 contrib/signet/miner generate \
    --cli="./build/bin/byze-cli -signet -rpcwallet=miner" \
    --address=$(./build/bin/byze-cli -signet -rpcwallet=miner getnewaddress) \
    --nbits=1e0377ae

# Or use the network parameter (simpler)
python3 contrib/signet/miner generate \
    --cli="./build/bin/byze-cli -signet -rpcwallet=miner" \
    --network=signet \
    --address=$(./build/bin/byze-cli -signet -rpcwallet=miner getnewaddress)
```

**Note:** The signet miner script must be run from the repository root directory (where `contrib/signet/miner` exists), not from `build/bin`.

### Why generatetoaddress Returns Empty

If you try `generatetoaddress`, it will return an empty array `[]` because:
1. Signet blocks require a signet solution (BIP325 signature) in the witness commitment
2. `generatetoaddress` does not automatically add the signet solution
3. Blocks without the signet solution are rejected by validation

This is expected behavior - always use the signet miner script for signet block generation.

## Key Information

- **Challenge Script Type:** 1-of-1 P2PK (`<compressed_pubkey> OP_CHECKSIG`)
- **Public Key:** `0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798`
- **Private Key (hex):** `01`
- **Private Key (WIF):** `cMahea7zqjxrtgAbB7LSGbcQUr1uX1ojuat9jZodMN87JcbXMTcA`
- **Network:** Signet (test network)
- **Port:** 38833 (P2P), 38882 (RPC)
- **Bech32 HRP:** `sbyz`

## Troubleshooting

### Block Rejected: "bad-signet-blksig"

This means the signet solution is missing or invalid. Ensure:
1. The signet authority private key is imported into your wallet
2. The signet solution is added to the block (use contrib/signet/miner)

### Cannot Generate Blocks

Make sure:
1. The node is fully synced (or you're starting from genesis)
2. The signet authority key is imported
3. You're using the correct wallet that contains the key
4. You're using the signet miner script to add the solution

## Development Notes

- The signet challenge uses a hardcoded compressed public key (secp256k1 generator point)
- This ensures deterministic behavior across different runs
- The corresponding private key is `1` (0x01), making it easy to import
- For production signet, use a different challenge script with proper key management
- This implementation matches Bitcoin Core signet semantics exactly (BIP325)
