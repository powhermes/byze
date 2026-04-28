#!/bin/bash
# Simple script to mine blocks in Byze Coin

set -e

CHAIN=${1:-regtest}
WALLET=${2:-default}

echo "Byze Coin Block Miner"
echo "======================="
echo "Chain: $CHAIN"
echo "Wallet: $WALLET"
echo ""

# Wait for node to be ready
echo "Waiting for node to be ready..."
MAX_WAIT=60
ELAPSED=0
while [ $ELAPSED -lt $MAX_WAIT ]; do
    if ./build/bin/byze-cli -$CHAIN getblockchaininfo > /dev/null 2>&1; then
        echo "✓ Node is ready!"
        break
    fi
    echo -n "."
    sleep 2
    ELAPSED=$((ELAPSED + 2))
done

if [ $ELAPSED -ge $MAX_WAIT ]; then
    echo ""
    echo "Error: Node is not responding. Is bitcoind running?"
    exit 1
fi

echo ""

# Check if wallet exists, create if not
if ! ./build/bin/byze-cli -$CHAIN listwallets | grep -q "\"$WALLET\""; then
    echo "Creating wallet: $WALLET"
    ./build/bin/byze-cli -$CHAIN createwallet "$WALLET" > /dev/null
fi

# Get or create address
ADDRESS=$(./build/bin/byze-cli -$CHAIN -rpcwallet=$WALLET getnewaddress 2>/dev/null || \
          ./build/bin/byze-cli -$CHAIN getnewaddress 2>/dev/null)

if [ -z "$ADDRESS" ]; then
    echo "Error: Could not get address"
    exit 1
fi

echo "Mining address: $ADDRESS"
echo ""

# Mine blocks
BLOCKS=${3:-1}
echo "Mining $BLOCKS block(s)..."
echo ""

if [ "$CHAIN" = "regtest" ]; then
    # Regtest mines instantly
    RESULT=$(./build/bin/byze-cli -$CHAIN -rpcwallet=$WALLET generatetoaddress $BLOCKS $ADDRESS 2>&1)
else
    # Mainnet/testnet - will take time
    echo "Note: Mining with RandomX may take some time..."
    RESULT=$(./build/bin/byze-cli -$CHAIN -rpcwallet=$WALLET generatetoaddress $BLOCKS $ADDRESS 2>&1)
fi

if [ $? -eq 0 ]; then
    echo "✓ Successfully mined block(s)!"
    echo "$RESULT"
    echo ""
    
    # Show balance
    BALANCE=$(./build/bin/byze-cli -$CHAIN -rpcwallet=$WALLET getbalance 2>/dev/null || \
              ./build/bin/byze-cli -$CHAIN getbalance 2>/dev/null)
    echo "Current balance: $BALANCE BYZ"
    
    # Show block count
    COUNT=$(./build/bin/byze-cli -$CHAIN getblockcount)
    echo "Block height: $COUNT"
else
    echo "✗ Mining failed:"
    echo "$RESULT"
    exit 1
fi

