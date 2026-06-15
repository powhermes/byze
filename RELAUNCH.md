# Byze v0.2.0 relaunch

**Status:** Release branch ready — **genesis not generated** (awaiting approval).  
**Branch:** `release/v0.2.0` (Core: `byze`, Web: `byze-web`)

## Summary

v0.2.0 replaces the v0.1 **forgeable HMAC-Keccak “quantum” scheme** with real **liboqs** signatures (XMSS-SHA2_10_256 + SPHINCS+-SHA2-128s-simple), resets wire formats, and requires a **new chain** (archived v0.1 data at `~/.byze` is preserved and never resumed).

| Component | v0.2.0 state |
|-----------|----------------|
| Core (`byzed`, wallet, CLI) | liboqs integrated; wallet format v3 |
| Browser wallet | **Watch-only** — HMAC signing removed |
| Pool / explorer / API | Redeploy after seed node (post-genesis) |
| Genesis | **Not generated** — blocked pending approval |

## What changed in Core

- **Algorithms:** `XMSS-SHA2_10_256` (STFL), `SPHINCS+-SHA2-128s-simple`
- **Wire sizes:** XMSS sig 2500 B, SPHINCS+ sig 7856 B, pubkey bundle 100 B
- **Wallet:** `BYZE_QUANTUM_KEY_FORMAT_VERSION = 3`, HKDF label `byze_quantum_hd_v2`
- **Build:** `cmake/liboqs.cmake` fetches liboqs 0.14.0 with STFL XMSS enabled
- **Version string:** `v0.2.0`

## Browser wallet (watch-only)

The web wallet no longer generates mnemonics or signs transactions. Users:

1. Manage keys in **Byze Core** (`byze-wallet` / `byze-cli`)
2. Add `byz1…` addresses in the watch-only UI
3. Paste Core-signed raw hex to broadcast

Verification checklist: [byze-web `doc/HMAC_SIGNING_REMOVAL_CHECKLIST.md`](https://github.com/powhermes/byze-web/blob/release/v0.2.0/doc/HMAC_SIGNING_REMOVAL_CHECKLIST.md)

## Release binaries

### Linux x86_64 (built)

Path: `release/v0.2.0/linux-x86_64/`

| Binary | Role |
|--------|------|
| `byzed` | Node daemon |
| `byze-cli` | RPC client |
| `byze-wallet` | Wallet |
| `byze-tx` | Transaction tool |
| `byze-util` | Utilities |

Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)`

### Windows / macOS (not built in this environment)

Cross-compilers and Guix were not available on the build host. Produce release artifacts with:

```bash
# See contrib/guix/README.md
./contrib/guix/guix-build 0.2.0
```

Targets: `x86_64-w64-mingw32`, `x86_64-apple-darwin`, `arm64-apple-darwin`.

## Validation

| Suite | Result |
|-------|--------|
| `wallet-crypto` watch-only tests | **Pass** (7/7) |
| Core Release build | **Pass** |
| `feature_quantum_*` functional (regtest) | **Blocked** — `generateblock` RPC crashes during liboqs block-signer init on HTTP worker threads; fix in progress on `release/v0.2.0` (use wallet IKM keygen path + large-stack signing). Wallet quantum keygen/sign paths work. |
| Archived mainnet datadir (`~/.byze`) | **Not modified** |

Re-run after block-signing fix:

```bash
python3 test/functional/feature_quantum_blocksig_reject.py
python3 test/functional/feature_quantum_multinode_consensus.py
python3 test/functional/feature_quantum_ibd_catchup.py
python3 test/functional/feature_quantum_reorg.py
python3 test/functional/feature_quantum_mempool_relay.py
python3 test/functional/feature_quantum_p2p_compact_block_sync.py
```

## Deployment order (after genesis approval)

1. Seed node — **new datadir** (genesis generation step)
2. Core peers
3. Pool — reset `pool.db`, regenerate pool wallet keys
4. Miners — point at v0.2 RPC
5. Explorer
6. Wallet API
7. Browser wallet (watch-only) — last

## Rollback

Stop v0.2 nodes; restart v0.1 binaries against the **archived** datadir only. v0.2 blocks become orphaned; archived chain remains forgeable-in-principle as documented in the security advisory.

## Pre-genesis checklist (awaiting approval)

- [x] `release/v0.2.0` branch opened; Core liboqs changes committed
- [x] Browser HMAC signing removed; watch-only mode; checklist published
- [x] Linux release binaries staged
- [ ] Windows/macOS Guix binaries (operator build)
- [ ] Functional quantum suite green (block RPC signing fix)
- [ ] **Genesis generation** — **STOP — requires explicit approval**
- [ ] New seed datadir bootstrap (separate from `~/.byze` archive)

## Commits (Core)

- `47ef541` — liboqs migration (quantum_safe, validation, functional test updates)
- `adee1ba` — generateblock pre-check + v0.2.0 version bump
- *(pending)* — block quantum signer stability fixes

## Commits (byze-web)

- `c31f97f` — watch-only wallet; HMAC removal

---

*Do not wipe `~/.byze` or any archived chain data. Genesis creation is intentionally deferred.*
