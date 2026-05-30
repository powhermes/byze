#!/bin/bash
# Test mining with daemon in foreground to see crash output

set -e

echo "=========================================="
echo "Mining Test - Foreground Mode"
echo "=========================================="
echo ""

# Cleanup
pkill -9 bitcoind 2>/dev/null || true
sleep 2
rm -rf ~/.byze/regtest 2>/dev/null || true

# Start daemon in background but capture output
echo "1. Starting daemon in foreground mode..."
./build/bin/byzed -regtest > /tmp/bitcoind-output.log 2>&1 &
DAEMON_PID=$!
echo "   Daemon PID: $DAEMON_PID"

# Wait for daemon
echo "   Waiting for daemon to initialize..."
for i in {1..30}; do
    if ./build/bin/byze-cli -regtest getblockchaininfo > /dev/null 2>&1; then
        echo "✓ Daemon ready"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "✗ Daemon failed to start"
        cat /tmp/bitcoind-output.log
        exit 1
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

# Try mining
echo ""
echo "3. Attempting to mine..."
echo "   (Watch for daemon output below)"
echo ""

# Start monitoring daemon output
tail -f /tmp/bitcoind-output.log &
TAIL_PID=$!

# Try mining
timeout 10 ./build/bin/byze-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR" 2>&1 || true

# Stop tail
kill $TAIL_PID 2>/dev/null || true

# Check if daemon is still running
sleep 1
echo ""
echo "4. Checking daemon status..."
if ps -p $DAEMON_PID > /dev/null 2>&1; then
    echo "   ✓ Daemon is still running"
    ./build/bin/byze-cli -regtest getblockcount 2>&1 || echo "   But RPC not responding"
else
    echo "   ✗ Daemon has crashed/stopped"
    echo ""
    echo "   Last daemon output:"
    tail -50 /tmp/bitcoind-output.log
fi

echo ""
echo "=========================================="


