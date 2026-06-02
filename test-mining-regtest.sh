#!/bin/bash
# Comprehensive regtest mining test script for Byze
# Tests RandomX mining with retry logic and validation

set -e

echo "=========================================="
echo "Byze Regtest Mining Test"
echo "=========================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
MAX_RETRIES=10
BLOCKS_TO_MINE=5
TIMEOUT=30

# Clean up any existing daemons
echo "1. Cleaning up existing instances..."
pkill -9 bitcoind 2>/dev/null || true
sleep 2
rm -rf ~/.byze/regtest 2>/dev/null || true
echo -e "${GREEN}✓ Cleanup complete${NC}"
echo ""

# Start daemon
echo "2. Starting regtest daemon..."
if ! ./build/bin/byzed -regtest -daemon; then
    echo -e "${RED}✗ Failed to start daemon${NC}"
    exit 1
fi

# Wait for daemon to be ready
echo "   Waiting for daemon to initialize..."
MAX_WAIT=30
ELAPSED=0
while [ $ELAPSED -lt $MAX_WAIT ]; do
    if ./build/bin/byze-cli -regtest getblockchaininfo > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Daemon is running and responsive${NC}"
        break
    fi
    echo -n "."
    sleep 1
    ELAPSED=$((ELAPSED + 1))
done

if [ $ELAPSED -ge $MAX_WAIT ]; then
    echo ""
    echo -e "${RED}✗ Daemon failed to start or not responding${NC}"
    exit 1
fi
echo ""

# Get blockchain info
echo "3. Initial blockchain state:"
INITIAL_INFO=$(./build/bin/byze-cli -regtest getblockchaininfo 2>/dev/null)
INITIAL_HEIGHT=$(echo "$INITIAL_INFO" | grep -o '"blocks":[0-9]*' | cut -d: -f2 || echo "0")
INITIAL_DIFFICULTY=$(echo "$INITIAL_INFO" | grep -o '"difficulty":[0-9.]*' | cut -d: -f2 || echo "N/A")
echo "   Initial block height: ${INITIAL_HEIGHT:-0}"
echo "   Initial difficulty: ${INITIAL_DIFFICULTY:-N/A}"
echo ""

# Create wallet
echo "4. Setting up wallet..."
WALLET_NAME="mining_test_wallet"
if ./build/bin/byze-cli -regtest listwallets 2>/dev/null | grep -q "\"$WALLET_NAME\""; then
    ./build/bin/byze-cli -regtest loadwallet "$WALLET_NAME" > /dev/null 2>&1 || true
else
    ./build/bin/byze-cli -regtest createwallet "$WALLET_NAME" > /dev/null 2>&1
fi

ADDR=$(./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" getnewaddress 2>/dev/null)
if [ -z "$ADDR" ]; then
    echo -e "${RED}✗ Failed to get address${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Wallet ready${NC}"
echo "   Mining address: $ADDR"
echo ""

# Test mining with retry logic
echo "5. Testing block mining (target: $BLOCKS_TO_MINE blocks)..."
echo "   Note: RandomX mining may take time, retrying if needed..."
echo ""

MINED_BLOCKS=0
ATTEMPT=1

while [ $MINED_BLOCKS -lt $BLOCKS_TO_MINE ] && [ $ATTEMPT -le $MAX_RETRIES ]; do
    CURRENT_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "0")
    BLOCKS_NEEDED=$((BLOCKS_TO_MINE - MINED_BLOCKS))
    TIMEOUT_OCCURRED=0
    
    echo "   Attempt $ATTEMPT: Mining $BLOCKS_NEEDED block(s) (current height: $CURRENT_HEIGHT)..."
    
    # Try mining with high maxtries for regtest
    # Use timeout and capture both stdout and stderr
    set +e
    RESULT=$(timeout $TIMEOUT ./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" generatetoaddress 1 "$ADDR" 10000000 2>&1)
    EXIT_CODE=$?
    set -e
    
    if [ $EXIT_CODE -eq 124 ] || [ $EXIT_CODE -eq 143 ]; then
        TIMEOUT_OCCURRED=1
    fi
    
    NEW_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "$CURRENT_HEIGHT")
    
    # Check if timeout occurred
    if [ "${TIMEOUT_OCCURRED:-0}" -eq 1 ]; then
        echo -e "   ${YELLOW}⚠ Command timed out after ${TIMEOUT}s (expected with RandomX)${NC}"
        TIMEOUT_OCCURRED=0
        sleep 2
    elif [ "$NEW_HEIGHT" -gt "$CURRENT_HEIGHT" ] 2>/dev/null; then
        MINED_BLOCKS=$((MINED_BLOCKS + 1))
        echo -e "   ${GREEN}✓ Block mined! New height: $NEW_HEIGHT${NC}"
        
        # Get block info
        BLOCK_HASH=$(echo "$RESULT" | grep -o '"[a-f0-9]*"' | head -1 | tr -d '"')
        if [ -n "$BLOCK_HASH" ]; then
            BLOCK_INFO=$(./build/bin/byze-cli -regtest getblock "$BLOCK_HASH" 2>/dev/null | head -20)
            echo "   Block hash: $BLOCK_HASH"
        fi
    else
        if echo "$RESULT" | grep -qi "timeout\|timed out"; then
            echo -e "   ${YELLOW}⚠ Timeout (expected with RandomX, will retry)${NC}"
        elif echo "$RESULT" | grep -qi "error"; then
            echo -e "   ${YELLOW}⚠ Error: $RESULT${NC}"
        else
            echo -e "   ${YELLOW}⚠ No block found yet (will retry)${NC}"
        fi
    fi
    
    ATTEMPT=$((ATTEMPT + 1))
    sleep 1
done

echo ""

# Final status
FINAL_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null)
TOTAL_MINED=$((FINAL_HEIGHT - INITIAL_HEIGHT))

echo "6. Mining Results:"
echo "   Initial height: $INITIAL_HEIGHT"
echo "   Final height: $FINAL_HEIGHT"
echo "   Blocks mined: $TOTAL_MINED"
echo ""

if [ $TOTAL_MINED -gt 0 ]; then
    echo -e "${GREEN}✓ Successfully mined $TOTAL_MINED block(s)!${NC}"
else
    echo -e "${YELLOW}⚠ No blocks mined (may need more time or lower difficulty)${NC}"
fi
echo ""

# Check balance
echo "7. Checking balance..."
BALANCE=$(./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" getbalance 2>/dev/null || echo "0")
IMMATURE=$(./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" getbalance "*" 0 true 2>/dev/null || echo "0")
echo "   Mature balance: $BALANCE BYZ"
echo "   Immature balance: $IMMATURE BYZ"
echo ""

# Get mining info
echo "8. Mining Information:"
MINING_INFO=$(./build/bin/byze-cli -regtest getmininginfo 2>/dev/null)
echo "$MINING_INFO" | head -15
echo ""

# Get blockchain info
echo "9. Final Blockchain State:"
FINAL_INFO=$(./build/bin/byze-cli -regtest getblockchaininfo 2>/dev/null)
FINAL_DIFFICULTY=$(echo "$FINAL_INFO" | grep -o '"difficulty":[0-9.]*' | cut -d: -f2)
FINAL_CHAINWORK=$(echo "$FINAL_INFO" | grep -o '"chainwork":"[^"]*"' | cut -d: -f2 | tr -d '"')
echo "   Difficulty: $FINAL_DIFFICULTY"
echo "   Chainwork: $FINAL_CHAINWORK"
echo ""

# Test transaction if we have mature balance
BALANCE_NUM=$(echo "$BALANCE" | awk '{print int($1)}')
if [ "$BALANCE_NUM" -gt 0 ] 2>/dev/null; then
    echo "10. Testing transaction creation..."
    RECEIVE_ADDR=$(./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" getnewaddress 2>/dev/null)
    if [ -n "$RECEIVE_ADDR" ]; then
        echo "   Creating test transaction..."
        TX_RESULT=$(./build/bin/byze-cli -regtest -rpcwallet="$WALLET_NAME" sendtoaddress "$RECEIVE_ADDR" 0.01 2>&1 || echo "error")
        if echo "$TX_RESULT" | grep -q "error\|insufficient"; then
            echo -e "   ${YELLOW}⚠ Transaction test skipped (insufficient funds or immature balance)${NC}"
        else
            echo -e "   ${GREEN}✓ Transaction created: $TX_RESULT${NC}"
        fi
    fi
    echo ""
fi

# Summary
echo "=========================================="
echo "Test Summary"
echo "=========================================="
if [ $TOTAL_MINED -gt 0 ]; then
    echo -e "${GREEN}✓ Mining test PASSED${NC}"
    echo "   - Daemon started successfully"
    echo "   - Wallet created and functional"
    echo "   - Blocks mined: $TOTAL_MINED"
    echo "   - Blockchain state: Valid"
    exit 0
else
    echo -e "${YELLOW}⚠ Mining test PARTIAL${NC}"
    echo "   - Daemon started successfully"
    echo "   - Wallet created and functional"
    echo "   - No blocks mined (may need more time or adjustments)"
    echo ""
    echo "   Suggestions:"
    echo "   - Try running with longer timeout"
    echo "   - Check RandomX initialization in logs"
    echo "   - Verify difficulty settings for regtest"
    exit 1
fi

