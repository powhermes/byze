# Byze Coin Mining Guide

## Quick Start: Mining Your First Block

### Option 1: Using Regtest Mode (Recommended for Testing)

Regtest mode allows you to mine blocks instantly with minimal difficulty, perfect for testing:

```bash
# Start byzed in regtest mode
./build/bin/byzed -regtest -daemon

# Create a wallet and get an address
./build/bin/byze-cli -regtest createwallet "testwallet"
./build/bin/byze-cli -regtest -rpcwallet=testwallet getnewaddress

# Mine blocks (replace ADDRESS with your address)
./build/bin/byze-cli -regtest -rpcwallet=testwallet generatetoaddress 1 ADDRESS

# Check your balance
./build/bin/byze-cli -regtest -rpcwallet=testwallet getbalance
```

### Option 2: Mining on Mainnet/Testnet

For mainnet or testnet, you'll need to actually solve the RandomX proof-of-work:

```bash
# Start the daemon
./build/bin/byzed -daemon

# Create wallet and get address
./build/bin/byze-cli createwallet "mywallet"
./build/bin/byze-cli -rpcwallet=mywallet getnewaddress

# Mine blocks (this will take time with RandomX)
./build/bin/byze-cli -rpcwallet=mywallet generatetoaddress 1 ADDRESS

# Check mining info
./build/bin/byze-cli getmininginfo
```

### Option 3: Using byze-qt GUI

The GUI doesn't have a built-in mining interface, but you can:

1. **Open the Debug Console** (Help → Debug Window → Console)
2. **Use RPC commands directly**:
   ```
   createwallet "mywallet"
   -rpcwallet=mywallet getnewaddress
   -rpcwallet=mywallet generatetoaddress 1 <your-address>
   ```

## Mining with Multiple Nodes (Local Network)

To test with multiple nodes on your local machine:

### Node 1 (Miner):
```bash
mkdir -p ~/.byze/node1
./build/bin/byzed -datadir=~/.byze/node1 -port=8888 -rpcport=8882 -daemon
```

### Node 2 (Peer):
```bash
mkdir -p ~/.byze/node2
./build/bin/byzed -datadir=~/.byze/node2 -port=8889 -rpcport=8883 -addnode=127.0.0.1:8888 -daemon
```

### Connect them:
```bash
# From node2, connect to node1
./build/bin/byze-cli -datadir=~/.byze/node2 addnode 127.0.0.1:8888 add
```

## Useful RPC Commands

- `getmininginfo` - Get mining information
- `getnetworkhashps` - Get network hash rate
- `getblocktemplate` - Get block template for mining
- `submitblock` - Submit a mined block
- `generatetoaddress <nblocks> <address>` - Generate blocks to an address
- `getblockcount` - Get current block height
- `getblockchaininfo` - Get blockchain information

## Mining Performance

RandomX is CPU-friendly but still requires significant computation. On a typical CPU:
- Regtest: Blocks mine instantly (difficulty = 1)
- Mainnet: Blocks may take minutes to hours depending on difficulty and CPU

## Troubleshooting

If mining fails:
1. Check `debug.log` for errors
2. Ensure RandomX is properly initialized (check logs for "RandomX initialized successfully")
3. Verify your address is valid: `validateaddress <address>`
4. Check you have sufficient funds for fees (if sending transactions)

