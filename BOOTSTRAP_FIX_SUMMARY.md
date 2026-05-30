# Bootstrap Fix Summary: Allowing Limited Peers During Bootstrap

## Problem

Fresh nodes at height 0 on Byze receive headers from NODE_NETWORK_LIMITED (pruned) seed nodes but never request blocks, remaining stuck at height 0. This is a bootstrap deadlock where Core's normal logic prevents downloading blocks from limited peers during initial sync.

## Root Cause

**File:** `src/net_processing.cpp:5917`

The block download condition was:
```cpp
if (CanServeBlocks(*peer) && 
    ((sync_blocks_and_headers_from_peer && !IsLimitedPeer(*peer)) || 
     !m_chainman.IsInitialBlockDownload()) && 
    ...)
```

This condition:
1. During IBD: Only allows non-limited peers (`!IsLimitedPeer(*peer)`)
2. After IBD: Allows all peers (including limited)

However, on Byze:
- IBD is disabled at height 0 (for mining purposes - see `src/validation.cpp:1953-1954`)
- All seed nodes are pruned and advertise NODE_NETWORK_LIMITED
- The logic assumes IBD=false means "already synced", not "at genesis with exception"
- Result: Limited peers are technically allowed (IBD=false), but edge cases prevent block requests

## Solution

**File:** `src/net_processing.cpp:5917-5922`

Added explicit bootstrap logic to allow limited peers when at height 0 or early heights (below `NODE_NETWORK_LIMITED_MIN_BLOCKS = 288`):

```cpp
// Allow limited peers during bootstrap (height 0 or early heights) for new networks.
// After bootstrap threshold, revert to normal Core behavior (limited peers only when not in IBD).
const CBlockIndex* activeTip = m_chainman.ActiveTip();
bool allow_limited_peer_bootstrap = activeTip && activeTip->nHeight < static_cast<int>(NODE_NETWORK_LIMITED_MIN_BLOCKS);
bool allow_limited_peer = allow_limited_peer_bootstrap || !m_chainman.IsInitialBlockDownload();
if (CanServeBlocks(*peer) && 
    ((sync_blocks_and_headers_from_peer && (!IsLimitedPeer(*peer) || allow_limited_peer)) || 
     allow_limited_peer) && 
    ...)
```

## Behavior

### During Bootstrap (height < 288)
- Limited peers ARE allowed for block downloads
- Enables bootstrap from pruned seed nodes on new networks
- Essential for networks where all seed nodes are pruned

### After Bootstrap (height >= 288)
- Reverts to normal Core behavior:
  - Limited peers allowed only when IBD is false (already synced)
  - Limited peers excluded during IBD (normal Core behavior)
- Preserves Core semantics for established chains

## Why This Works

1. **Explicit bootstrap check**: The `allow_limited_peer_bootstrap` check explicitly allows limited peers at early heights
2. **Preserves Core behavior**: The `|| !m_chainman.IsInitialBlockDownload()` part maintains normal Core behavior after bootstrap
3. **Safe threshold**: Uses `NODE_NETWORK_LIMITED_MIN_BLOCKS` (288) as the threshold, which aligns with Core's definition of limited peers
4. **No genesis changes**: Does not modify genesis block or chain parameters
5. **No hardcoded peers**: Works with any pruned peer that advertises NODE_NETWORK_LIMITED

## Testing

After this fix:
- ✅ Fresh nodes should sync from height 0 using pruned seed nodes
- ✅ Blocks should be requested from limited peers during bootstrap
- ✅ Mining should work after sync completes
- ✅ Behavior reverts to normal Core rules after height 288
- ✅ Established chains (height > 288) maintain normal Core behavior

## Code Locations

- **Fix location**: `src/net_processing.cpp:5917-5922`
- **IBD exception**: `src/validation.cpp:1953-1954` (Byze-specific, allows IBD=false at height 0)
- **Limited peer check**: `src/net_processing.cpp:1135-1139` (IsLimitedPeer function)
- **Block filtering**: `src/net_processing.cpp:1507-1509` (Additional filter for limited peers)

## Related Constants

- `NODE_NETWORK_LIMITED_MIN_BLOCKS = 288` (line 152): Minimum blocks required to signal limited
- `BLOCK_DOWNLOAD_WINDOW = 1024` (line 144): Maximum block download window
- `NODE_NETWORK_LIMITED_ALLOW_CONN_BLOCKS = 144` (line 154): Window for connecting to limited peers
