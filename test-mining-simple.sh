#!/bin/bash
# Simple regtest mining test with proper error handling

set -e

echo "=========================================="
echo "Byze Simple Regtest Mining Test"
echo "=========================================="
echo ""

# Clean up
echo "1. Cleaning up..."
pkill -9 bitcoind 2>/dev/null || true
sleep 2
rm -rf ~/.byze/regtest 2>/dev/null || true
echo "✓ Cleanup done"
echo ""

# Start daemon
echo "2. Starting daemon..."
./build/bin/byzed -regtest -daemon
sleep 5

# Wait for daemon
echo "   Waiting for daemon..."
for i in {1..30}; do
    if ./build/bin/byze-cli -regtest getblockchaininfo > /dev/null 2>&1; then
        echo "✓ Daemon ready"
        break
    fi
    sleep 1
done
echo ""

# Get initial state
echo "3. Initial state:"
INITIAL_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "0")
echo "   Block height: $INITIAL_HEIGHT"

MINING_INFO=$(./build/bin/byze-cli -regtest getmininginfo 2>/dev/null)
DIFFICULTY=$(echo "$MINING_INFO" | grep -o '"difficulty":[0-9.e-]*' | cut -d: -f2)
echo "   Difficulty: $DIFFICULTY"
echo ""

# Setup wallet
echo "4. Setting up wallet..."
./build/bin/byze-cli -regtest createwallet test 2>/dev/null || \
    ./build/bin/byze-cli -regtest loadwallet test 2>/dev/null || true
ADDR=$(./build/bin/byze-cli -regtest -rpcwallet=test getnewaddress 2>/dev/null)
echo "   Address: $ADDR"
echo ""

# Try mining with multiple attempts
echo "5. Attempting to mine blocks..."
echo "   Note: Each attempt has a 15-second time cap"
echo ""

SUCCESS_COUNT=0
MAX_ATTEMPTS=20

for i in $(seq 1 $MAX_ATTEMPTS); do
    CURRENT_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "0")
    
    echo -n "   Attempt $i: "
    
    # Try to mine (15 second cap + buffer)
    RESULT=$(timeout 20 ./build/bin/byze-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR" 2>&1) || true
    
    NEW_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "$CURRENT_HEIGHT")
    
    if [ "$NEW_HEIGHT" -gt "$CURRENT_HEIGHT" ] 2>/dev/null; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        BLOCK_HASH=$(echo "$RESULT" | grep -o '"[a-f0-9]*"' | head -1 | tr -d '"' || echo "unknown")
        echo "✓ SUCCESS! Block mined (height: $NEW_HEIGHT, hash: ${BLOCK_HASH:0:16}...)"
        
        # Show block info
        if [ -n "$BLOCK_HASH" ] && [ "$BLOCK_HASH" != "unknown" ]; then
            BLOCK_TIME=$(./build/bin/byze-cli -regtest getblock "$BLOCK_HASH" 2>/dev/null | grep -o '"time":[0-9]*' | cut -d: -f2 || echo "N/A")
            echo "      Block time: $BLOCK_TIME"
        fi
    else
        if echo "$RESULT" | grep -qi "timeout\|time limit"; then
            echo "⏱ Timeout (15s cap reached - expected)"
        elif echo "$RESULT" | grep -qi "error"; then
            echo "✗ Error: $(echo "$RESULT" | head -1)"
        else
            echo "⏱ No block found (will retry)"
        fi
    fi
    
    # Stop if we've mined enough
    if [ $SUCCESS_COUNT -ge 3 ]; then
        echo ""
        echo "   ✓ Mined $SUCCESS_COUNT blocks, stopping test"
        break
    fi
    
    sleep 1
done

echo ""

# Final status
FINAL_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "0")
TOTAL_MINED=$((FINAL_HEIGHT - INITIAL_HEIGHT))

echo "6. Results:"
echo "   Blocks mined: $TOTAL_MINED"
echo "   Success rate: $SUCCESS_COUNT successful attempts"

if [ $TOTAL_MINED -gt 0 ]; then
    echo ""
    echo "7. Checking balance..."
    BALANCE=$(./build/bin/byze-cli -regtest -rpcwallet=test getbalance 2>/dev/null || echo "0")
    echo "   Balance: $BALANCE BYZ"
    
    echo ""
    echo "8. Latest block info:"
    LATEST_HASH=$(./build/bin/byze-cli -regtest getbestblockhash 2>/dev/null || echo "")
    if [ -n "$LATEST_HASH" ]; then
        ./build/bin/byze-cli -regtest getblock "$LATEST_HASH" 2>/dev/null | head -15
    fi
fi

echo ""
echo "=========================================="
if [ $TOTAL_MINED -gt 0 ]; then
    echo "✓ TEST PASSED: Mining is working!"
    echo "   Mined $TOTAL_MINED block(s) successfully"
    exit 0
else
    echo "⚠ TEST INCONCLUSIVE: No blocks mined"
    echo "   This may be normal if RandomX takes longer than 15 seconds"
    echo "   Try running again or check logs: ~/.byze/regtest/debug.log"
    exit 1
fi


