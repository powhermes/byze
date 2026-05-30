#!/bin/bash
# Test mining while monitoring daemon status

set -e

echo "=========================================="
echo "Mining Test with Daemon Monitoring"
echo "=========================================="
echo ""

# Cleanup
pkill -9 bitcoind 2>/dev/null || true
sleep 2
rm -rf ~/.byze/regtest 2>/dev/null || true

# Start daemon
echo "1. Starting daemon..."
./build/bin/byzed -regtest -daemon
sleep 5

# Wait for ready
for i in {1..30}; do
    if ./build/bin/byze-cli -regtest getblockchaininfo > /dev/null 2>&1; then
        echo "✓ Daemon ready"
        break
    fi
    sleep 1
done

# Setup
echo ""
echo "2. Setting up wallet..."
./build/bin/byze-cli -regtest createwallet test 2>/dev/null || \
    ./build/bin/byze-cli -regtest loadwallet test 2>/dev/null || true
ADDR=$(./build/bin/byze-cli -regtest -rpcwallet=test getnewaddress 2>/dev/null)
echo "   Address: $ADDR"
echo ""

# Monitor daemon in background
echo "3. Starting daemon monitor..."
DAEMON_PID=$(pgrep bitcoind)
echo "   Daemon PID: $DAEMON_PID"

# Function to check daemon
check_daemon() {
    if pgrep -P $DAEMON_PID > /dev/null 2>&1 || pgrep bitcoind > /dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Try mining with monitoring
echo ""
echo "4. Attempting to mine (monitoring daemon)..."
echo "   Watch for daemon status and log messages"
echo ""

# Start log monitoring in background
tail -f ~/.byze/regtest/debug.log 2>/dev/null | grep --line-buffered -iE "mining|time.*limit|randomx|error|fatal" &
LOG_PID=$!

# Give it a moment
sleep 1

# Try mining
echo "   Starting mining command..."
START_TIME=$(date +%s)
RESULT=$(timeout 25 ./build/bin/byze-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR" 2>&1) || EXIT_CODE=$?
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

# Stop log monitoring
kill $LOG_PID 2>/dev/null || true

echo ""
echo "5. Results:"
echo "   Elapsed time: ${ELAPSED}s"
echo "   Exit code: ${EXIT_CODE:-0}"
echo "   Result: $(echo "$RESULT" | head -3)"

# Check daemon status
echo ""
echo "6. Daemon status:"
if check_daemon; then
    echo "   ✓ Daemon is still running"
    NEW_HEIGHT=$(./build/bin/byze-cli -regtest getblockcount 2>/dev/null || echo "0")
    echo "   Block height: $NEW_HEIGHT"
else
    echo "   ✗ Daemon has stopped/crashed"
fi

# Check recent logs
echo ""
echo "7. Recent mining-related log entries:"
tail -50 ~/.byze/regtest/debug.log 2>/dev/null | grep -iE "mining|time.*limit|randomx" | tail -10 || echo "   No mining-related log entries found"

echo ""
echo "=========================================="


