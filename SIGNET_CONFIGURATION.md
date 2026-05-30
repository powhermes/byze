# Byze Signet Configuration

## Overview

Byze signet has been configured to be completely unique and isolated from Bitcoin's signet network. This ensures that Byze signet nodes will not connect to Bitcoin signet nodes.

## Unique Configuration Parameters

### 1. Signet Challenge Script

**Bitcoin Signet Challenge:**
```
512103ad5e0edad18cb1f0fc0d28a3d4f1f3e445640337489abb10404f2d1e086be430210359ef5021964fe22d6f8e05b2463c9540ce96883fe3b278760f048f5189f2e6c452ae
```

**Byze Signet Challenge:**
```
5121024c7b7fb6c310fccf1ba33b082519d82964ea93868d676662d4a59ad548df0e7d2102f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f952ae
```

This is a 2-of-2 multisig script unique to Byze signet. The challenge script determines which blocks are valid on the signet network.

**Note:** These are placeholder keys. For production use, generate proper Byze signet authority keys.

### 2. Message Start Characters

Message start characters are automatically derived from the signet challenge script (first 4 bytes of SHA256 of the challenge). Since the challenge is unique, the message start will be unique, ensuring network isolation.

### 3. Network Port

- **Bitcoin Signet:** 38333
- **Byze Signet:** 38833

This ensures port-level isolation from Bitcoin signet.

### 4. Bech32 HRP (Human-Readable Part)

- **Bitcoin Signet:** `tb` (e.g., `tb1q...`)
- **Byze Signet:** `byz` (e.g., `byz1q...`)

This makes addresses clearly identifiable as Byze signet addresses.

### 5. Seed Nodes

**Bitcoin Signet Seeds (Removed):**
- `seed.signet.bitcoin.sprovoost.nl.`
- `seed.signet.achownodes.xyz.`

**Byze Signet Seeds:**
- Currently empty (no seed nodes configured)
- TODO: Add Byze signet seed nodes when available

### 6. Chain Work and Assume Valid

- **Bitcoin Signet:** Has chain work and assume valid block from Bitcoin signet
- **Byze Signet:** Reset to empty (fresh start)

This ensures Byze signet starts from genesis without assuming Bitcoin signet state.

### 7. AssumeUTXO Data

- **Bitcoin Signet:** Has assumeutxo data at height 160,000
- **Byze Signet:** Empty (no assumeutxo data)

Byze signet starts fresh without Bitcoin signet's assumeutxo snapshots.

## Usage

### Starting Byze Signet

```bash
# Start Byze signet daemon
./build/bin/byzed -signet -daemon

# Connect to Byze signet
./build/bin/byze-cli -signet getblockchaininfo
```

### Custom Signet Configuration

You can also create a custom signet with your own challenge:

```bash
# Create custom signet with specific challenge
./build/bin/byzed -signet -signetchallenge=<your_challenge_hex> -daemon

# Add custom seed nodes
./build/bin/byzed -signet -signetseednode=your.node.com:38833 -daemon
```

## Network Isolation

The following parameters ensure complete network isolation from Bitcoin signet:

1. **Unique Challenge Script** - Different challenge means different message start
2. **Different Port** - 38833 vs 38333 prevents accidental connections
3. **Different Bech32 HRP** - `byz` vs `tb` for address differentiation
4. **No Bitcoin Seeds** - Removed all Bitcoin signet seed nodes
5. **Fresh Chain State** - No Bitcoin signet chain work or assumeutxo data

## Next Steps

1. **Generate Authority Keys** - Create proper signet authority keys for production
2. **Deploy Seed Nodes** - Set up Byze signet seed nodes
3. **Mine Genesis Block** - Mine the signet genesis block with RandomX
4. **Documentation** - Update user documentation with signet usage

## Testing

To test that signet is properly isolated:

```bash
# Start Byze signet
./build/bin/byzed -signet -daemon

# Check it's using the correct port
netstat -tuln | grep 38833

# Verify bech32 addresses use 'byz' prefix
./build/bin/byze-cli -signet getnewaddress
# Should return address starting with 'byz1...'
```

---

**Status:** ✅ Byze signet configured and isolated from Bitcoin signet
**Consensus:** ✅ No consensus changes - signet uses same RandomX + XMSS/SPHINCS as other networks


