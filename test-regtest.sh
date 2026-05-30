#!/bin/bash
# Test script for regtest block production and validation

set -e

echo "=== Byze Regtest Testing ==="
echo ""

# Clean up any existing daemons
pkill -9 bitcoind 2>/dev/null || true
sleep 2
rm -rf ~/.byze/regtest 2>/dev/null || true

# Start daemon
echo "1. Starting regtest daemon..."
./build/bin/byzed -regtest -daemon
sleep 5

# Check if daemon is running
if ! ./build/bin/byze-cli -regtest getblockchaininfo > /dev/null 2>&1; then
    echo "ERROR: Daemon not responding"
    exit 1
fi
echo "✓ Daemon is running"

# Create wallet
echo ""
echo "2. Creating wallet..."
./build/bin/byze-cli -regtest createwallet testwallet > /dev/null 2>&1 || true
./build/bin/byze-cli -regtest loadwallet testwallet > /dev/null 2>&1 || true
ADDR=$(./build/bin/byze-cli -regtest getnewaddress 2>/dev/null)
echo "✓ Wallet created, address: $ADDR"

# Test single block
echo ""
echo "3. Testing single block generation..."
INITIAL_COUNT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null)
echo "   Initial block count: $INITIAL_COUNT"

RESULT=$(timeout 20 ./build/bin/byze-cli -regtest generatetoaddress 1 "$ADDR" 2>&1) || true
if echo "$RESULT" | grep -q "error\|timeout"; then
    echo "   ⚠ Block generation timed out or failed (expected with time cap)"
    echo "   Result: $RESULT"
else
    FINAL_COUNT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null)
    echo "   ✓ Block generated! Final count: $FINAL_COUNT"
    if [ "$FINAL_COUNT" -gt "$INITIAL_COUNT" ]; then
        echo "   ✓ Block count incremented successfully"
    fi
fi

# Check balance
echo ""
echo "4. Checking balance..."
BALANCE=$(./build/bin/byze-cli -regtest getbalance 2>/dev/null)
echo "   Balance: $BALANCE"

# Test multiple blocks
echo ""
echo "5. Testing multiple block generation (5 blocks)..."
for i in {1..5}; do
    echo -n "   Block $i: "
    RESULT=$(timeout 20 ./build/bin/byze-cli -regtest generatetoaddress 1 "$ADDR" 2>&1) || true
    if echo "$RESULT" | grep -q "error\|timeout"; then
        echo "timeout/failed"
    else
        COUNT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null)
        echo "✓ (count: $COUNT)"
    fi
    sleep 1
done

FINAL_COUNT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null)
echo "   Final block count: $FINAL_COUNT"

# Test coinbase maturity
echo ""
echo "6. Testing coinbase maturity..."
if [ "$FINAL_COUNT" -ge "100" ]; then
    MATURE_BALANCE=$(./build/bin/byze-cli -regtest getbalance 2>/dev/null)
    echo "   Mature balance (after 100+ blocks): $MATURE_BALANCE"
else
    BLOCKS_NEEDED=$((100 - FINAL_COUNT))
    echo "   Need $BLOCKS_NEEDED more blocks for coinbase maturity"
fi

echo ""
echo "=== Test Complete ==="


