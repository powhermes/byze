#!/bin/bash
# Wait for bitcoind to finish initializing

echo "Waiting for Byze node to finish initializing..."
echo "This may take a minute on first startup..."

CHAIN=${1:-regtest}
MAX_WAIT=120  # Maximum wait time in seconds
ELAPSED=0

while [ $ELAPSED -lt $MAX_WAIT ]; do
    # Try to get blockchain info
    if ./build/bin/byze-cli -$CHAIN getblockchaininfo > /dev/null 2>&1; then
        echo "✓ Node is ready!"
        ./build/bin/byze-cli -$CHAIN getblockchaininfo | grep -E "chain|blocks|verificationprogress"
        exit 0
    fi
    
    # Check if it's still starting
    if ./build/bin/byze-cli -$CHAIN getblockchaininfo 2>&1 | grep -q "Verifying blocks"; then
        echo -n "."
        sleep 2
        ELAPSED=$((ELAPSED + 2))
    else
        # Some other error
        echo ""
        echo "Error: Node may not be running or there's an issue"
        ./build/bin/byze-cli -$CHAIN getblockchaininfo 2>&1
        exit 1
    fi
done

echo ""
echo "Timeout: Node took too long to initialize"
exit 1

