# Byze: single remote peer smoke test

Manual checklist for validating behavior against one external node (no automation required). Use the same binary and network parameters as production.

## Setup

1. **Remote node** — one `byzed` (or equivalent) on the target network, reachable by IP/hostname, P2P port open.
2. **Local node** — one instance with a clean or synced datadir, same chain parameters as the remote peer.
3. **Connect** — from the local node, add the remote as a peer (e.g. `addnode <host>:<port> onetry` or `-addnode=<host>:<port>` at startup). After `addnode`, confirm the peer appears in `getpeerinfo` with a sensible `lastrecv` / `lastsend` and no immediate disconnect.

## Checks

| Step | What to verify |
|------|----------------|
| Handshake | Peers show in `getpeerinfo`; connection stable for several minutes. |
| Headers | `getblockchaininfo` headers height approaches network tip; no repeated `invalid header` / stall in the log. |
| Blocks | Tip height advances when the network produces blocks; `getblockchaininfo` `verificationprogress` reasonable. |
| Compact relay | Optional: `-debug=cmpctblock` / `-debug=net` for raw compact traffic. If quantum block policy is enforced (`mainnet` or `-enforcequantumblocksigs`), compact reconstruction does not carry the Byze quantum tail; use **`-debug=byze-p2p`** to see keyed lines (`compact_fallback=1`, `reason=missing_quantum_tail`) when the node requests a full block instead. |
| Mempool | Submit a valid tx via local wallet or `sendrawtransaction`; remote relay path can be observed if both mempools are open (not `-blocksonly` on the path you care about). |

## Logs

- **Quantum block rejection (always on `LogError`)**: grep for **`block_sig_reject`** — each line includes `hash=` and `reason=` (`missing_tail`, `partial_tail`, `wrong_sig_sizes`, `crypto_deserialize_failed`, `xmss_verify_failed`, `sphincs_verify_failed`).
- **Compact-block → full block (debug-only)**: enable **`-debug=byze-p2p`** for lines prefixed with **`[byze-p2p]`** when a compact path triggers a full-block fetch for the quantum tail.
- **Extra validation detail**: `-debug=validation` still emits optional `[quantum-cmp]` debug lines where useful.

## Failure triage

- Stuck headers: check firewall, `getpeerinfo` `synced_headers` vs `synced_blocks`, and chain work.
- Invalid blocks: inspect `debug.log` for `block_sig_reject` / `bad-quantum-sig`; ensure peers run compatible Byze rules.
