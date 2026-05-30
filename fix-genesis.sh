#!/bin/bash
# Script to fix the "Failed to read block" error by clearing the database

set -e

echo "Byze Coin - Genesis Block Database Fix"
echo "=========================================="
echo ""

# Find the datadir
if [ -d "$HOME/.bitcoin" ]; then
    DATADIR="$HOME/.bitcoin"
elif [ -n "$BITCOIN_DATADIR" ]; then
    DATADIR="$BITCOIN_DATADIR"
else
    DATADIR="$HOME/.bitcoin"
fi

echo "Using datadir: $DATADIR"
echo ""

# Check if datadir exists
if [ ! -d "$DATADIR" ]; then
    echo "Datadir $DATADIR does not exist. This is a fresh install."
    echo "The error might be due to a different issue."
    echo ""
    echo "Try running with -reindex:"
    echo "  ./build/bin/byze-qt -reindex"
    echo "  or"
    echo "  ./build/bin/byzed -reindex"
    exit 0
fi

# Ask for confirmation
echo "This will delete the following directories:"
echo "  - $DATADIR/blocks"
echo "  - $DATADIR/chainstate"
echo "  - $DATADIR/blocks/index"
echo ""
read -p "Do you want to continue? (y/N): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

# Backup warning
echo ""
echo "WARNING: This will delete all blockchain data!"
echo "Make sure you have a backup if needed."
echo ""
read -p "Are you sure? (yes/no): " -r
echo ""

if [[ ! $REPLY == "yes" ]]; then
    echo "Cancelled."
    exit 0
fi

# Delete the directories
echo ""
echo "Deleting database directories..."
rm -rf "$DATADIR/blocks" "$DATADIR/chainstate" "$DATADIR/blocks/index" 2>/dev/null || true

echo ""
echo "✓ Database cleared!"
echo ""
echo "Now you can run:"
echo "  ./build/bin/byze-qt"
echo "  or"
echo "  ./build/bin/byzed"
echo ""
echo "The genesis block will be recreated automatically."

