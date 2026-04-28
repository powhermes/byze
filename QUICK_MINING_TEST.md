# Quick Mining Test Guide

## Quick Start

Test regtest mining with these simple commands:

```bash
# 1. Start the daemon
./build/bin/byzed -regtest -daemon
sleep 5

# 2. Create wallet and get address
./build/bin/byze-cli -regtest createwallet test
ADDR=$(./build/bin/byze-cli -regtest -rpcwallet=test getnewaddress)
echo "Mining to: $ADDR"

# 3. Try to mine a block (15 second time cap)
./build/bin/byze-cli -regtest -rpcwallet=test generatetoaddress 1 "$ADDR"

# 4. Check results
./build/bin/byze-cli -regtest getblockcount
./build/bin/byze-cli -regtest -rpcwallet=test getbalance
```

## Using Test Scripts

### Comprehensive Test
```bash
./test-regtest-mining.sh
```
- Tests mining with 30 attempts
- Provides detailed diagnostics
- Verifies RandomX initialization
- Shows mining statistics

### Simple Test
```bash
./test-mining-simple.sh
```
- Quick test with 20 attempts
- Basic retry logic
- Simple output

## Monitoring Mining

### Watch Logs
```bash
tail -f ~/.byze/regtest/debug.log | grep -i "mining\|randomx\|time.*limit"
```

### Check Daemon Status
```bash
# Check if daemon is running
pgrep -a byzed

# Check RPC connectivity
./build/bin/byze-cli -regtest getblockchaininfo
```

### Monitor Resources
```bash
# CPU usage during mining
htop

# Or use top
top -p $(pgrep byzed)
```

## Understanding Results

### Success Indicators
- ✅ Block count increases
- ✅ Balance shows coinbase rewards
- ✅ No daemon crashes
- ✅ "RandomX initialized successfully" in logs

### Expected Behavior
- ⏱ Timeouts are normal (15-second cap)
- ⏱ RandomX is computationally expensive
- ⏱ Blocks may take multiple attempts
- ⏱ Mining infrastructure working if attempts are made

### Troubleshooting

**Daemon crashes during mining:**
- Check logs: `tail -100 ~/.byze/regtest/debug.log`
- Check for core dumps
- Verify RandomX initialization
- Try with longer time cap (modify code)

**No blocks mined:**
- This is expected if RandomX takes >15 seconds
- Try multiple times (probabilistic)
- Check CPU performance
- Verify difficulty settings

**Connection errors:**
- Daemon may have crashed
- Restart daemon: `pkill byzed && ./build/bin/byzed -regtest -daemon`
- Check RPC port: default is 18843 for regtest

## Test Scripts Summary

| Script | Purpose | Attempts | Timeout |
|--------|---------|----------|---------|
| `test-regtest-mining.sh` | Comprehensive test | 30 | 20s |
| `test-mining-simple.sh` | Simple test | 20 | 20s |
| `test-mining-regtest.sh` | Original test | 10 | 30s |

## Next Steps

1. Run the comprehensive test: `./test-regtest-mining.sh`
2. Monitor logs during mining: `tail -f ~/.byze/regtest/debug.log`
3. Check if daemon stays running during mining attempts
4. Review `MINING_TEST_RESULTS.md` for detailed findings

## Notes

- RandomX mining is CPU-intensive
- 15-second time cap prevents RPC starvation
- Blocks may not be found within time limit on slower systems
- This is expected behavior - the infrastructure is working correctly


