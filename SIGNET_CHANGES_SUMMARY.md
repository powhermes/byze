# Byze Signet Configuration Summary

## Changes Made

All signet parameters have been updated to make Byze signet unique and isolated from Bitcoin signet.

### ✅ Completed Changes

1. **Unique Signet Challenge Script**
   - Changed from Bitcoin's challenge to Byze-specific challenge
   - New challenge: `5121024c7b7fb6c310fccf1ba33b082519d82964ea93868d676662d4a59ad548df0e7d2102f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f952ae`
   - This is a 2-of-2 multisig script (placeholder keys for now)

2. **Removed Bitcoin Seed Nodes**
   - Removed `seed.signet.bitcoin.sprovoost.nl.`
   - Removed `seed.signet.achownodes.xyz.`
   - Cleared fixed seeds from Bitcoin signet
   - Added TODO for Byze signet seed nodes

3. **Unique Network Port**
   - Changed from `38333` (Bitcoin signet) to `38833` (Byze signet)
   - Ensures port-level isolation

4. **Unique Bech32 HRP**
   - Changed from `"tb"` (Bitcoin signet) to `"byz"` (Byze signet)
   - Addresses will now use `byz1q...` format instead of `tb1q...`

5. **Reset Chain State**
   - Cleared `nMinimumChainWork` (was Bitcoin signet value)
   - Cleared `defaultAssumeValid` (was Bitcoin signet block)
   - Reset blockchain/chainstate size assumptions
   - Cleared chain transaction data

6. **Removed AssumeUTXO Data**
   - Removed Bitcoin signet assumeutxo snapshot at height 160,000
   - Byze signet starts fresh

## Network Isolation Guarantees

The following ensure complete isolation from Bitcoin signet:

1. **Message Start Characters** - Automatically derived from unique challenge (SHA256 first 4 bytes)
2. **Different Port** - 38833 vs 38333 prevents accidental connections
3. **Different Challenge** - Different challenge means different valid blocks
4. **No Bitcoin Seeds** - Removed all Bitcoin signet seed nodes
5. **Different Bech32 HRP** - `byz` vs `tb` for address differentiation
6. **Fresh Chain State** - No Bitcoin signet chain work or assumeutxo data

## Testing

To verify signet is properly configured:

```bash
# Build with changes
cd build && cmake .. && make -j$(nproc)

# Start Byze signet
./build/bin/byzed -signet -daemon

# Check network info
./build/bin/byze-cli -signet getblockchaininfo

# Verify bech32 addresses use 'byz' prefix
./build/bin/byze-cli -signet getnewaddress
# Should return: byz1q...

# Check port
netstat -tuln | grep 38833
# Should show byzed listening on 38833
```

## Next Steps

1. **Generate Authority Keys** - Create proper signet authority keys for production use
2. **Deploy Seed Nodes** - Set up Byze signet seed nodes
3. **Mine Genesis Block** - Mine the signet genesis block with RandomX
4. **Test Network** - Test signet network connectivity and block production

## Files Modified

- `src/kernel/chainparams.cpp` - Updated SigNetParams class with Byze-specific values

## Notes

- **Consensus Code:** No consensus changes - signet still uses RandomX + XMSS/SPHINCS
- **Challenge Keys:** Current keys are placeholders - replace with production keys
- **Seed Nodes:** Currently empty - add when Byze signet infrastructure is ready
- **Genesis Block:** Uses HermesGenesisBlock - may need to be re-mined for signet

---

**Status:** ✅ Byze signet configured and isolated from Bitcoin signet
**Consensus:** ✅ No consensus changes - RandomX + XMSS/SPHINCS unchanged


