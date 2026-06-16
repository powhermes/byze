# Pre-genesis checklist (v0.2.0)

**Purpose:** Gate before genesis mining, datadir wipe, or cutover.  
**Last updated:** 2026-06-15  
**Operator:** Review each box; do not check PASS items without evidence.

Legend: `[x]` = PASS (verified) · `[ ]` = FAIL or pending

---

## A. Launch governance

- [ ] Explicit launch / genesis approval recorded (ticket, email, or change record)
- [ ] Maintenance window scheduled and operators assigned (primary + secondary)
- [ ] `SECURITY_ADVISORY.md` (BYZE-SA-2026-001) approved for public release timing
- [x] `RELAUNCH_RUNBOOK.md` v1.1 committed on `release/v0.2.0` (`8508f5f`)

---

## B. Core validation gate (`RELAUNCH.md`)

- [x] Quantum unit tests (3) — green on `release/v0.2.0`
- [x] `feature_quantum_*` functional tests (6) — green
- [x] `signpoolblock` smoke — pass
- [x] `submitblock` rejects forged/invalid quantum block sigs — pass
- [x] Wallet quantum spend scenarios — pass (multinode, reorg, mempool, IBD)
- [x] v0.1 / wrong witness sizes rejected — pass
- [x] Core Release build — pass
- [x] Archived mainnet datadir `~/.byze` — not modified during relaunch prep

---

## C. Browser wallet (byze-web)

- [x] HMAC signing removed; watch-only mode (`c31f97f`)
- [x] `wallet-crypto` watch-only tests — 3/3 pass (re-run 2026-06-15)
- [x] Explorer footer/nav launch polish committed on `release/v0.2.0`
- [ ] `byze-web` `release/v0.2.0` pushed to remote (verify before deploy)
- [ ] Post-cutover `BYZE_CONF` / RPC env for v0.2 datadir documented in runbook §3.4 (deploy step remains)

---

## D. Release artifacts (Linux x86_64)

- [x] Binaries staged at `release/v0.2.0/linux-x86_64/` (five binaries)
- [x] `SHA256SUMS` generated locally (2026-06-15)
- [ ] `SHA256SUMS` committed or published with frozen binary set (binaries still untracked in git)
- [ ] Genesis constants frozen in `chainparams.cpp` and binaries **rebuilt** after genesis mine
- [ ] `SHA256SUMS` regenerated after final post-genesis binary freeze
- [ ] Windows / macOS Guix binaries built and checksummed

**Current `SHA256SUMS` (pre-genesis staging — not final until genesis commit):**

```
317971f24dc319f13d303edce56cd4da6d1cdc8d13cd561c22f6a2af3e6ddb5d  byze-cli
a33cc20013cafb238eafede1c80dee1ba944cceac11ebd7eb884b6db150a3bb7  byzed
da54c83abb24664af95c05bf0515b279ca64f768aa1ac9b3f4d0af014b845041  byze-tx
5af77822f57cc5cc84cd7300d9afe0ba38df0f0860b5a06fb184540706b81a78  byze-util
7046b7cfcadba1f46a0589eef67afea4949c5aeb341f87d995b5c482c9ae37f8  byze-wallet
```

---

## E. Git / branch hygiene

- [x] `byze` on branch `release/v0.2.0`
- [x] `byze` working tree clean except untracked `release/v0.2.0/linux-x86_64/` (expected)
- [x] `byze-web` on branch `release/v0.2.0`, explorer changes committed
- [ ] `byze-pool` revision pinned in backup manifest (`main` — no `release/v0.2.0` branch)

---

## F. Server paths & runbook accuracy (read-only review 2026-06-15)

- [x] v0.1 datadir `/home/byze/.byze` exists; pool wallet at `poolwallet/` (not `wallets/`)
- [x] Pool DB `/home/byze/byze-pool/data/pool.db` path matches `.env`
- [x] All systemd unit names in runbook exist on host
- [x] nginx sites `explorer.byze.org`, `wallet.byze.org`, `pool.byze.org` exist
- [x] v0.2 datadir `/home/byze/.byze-v0.2` not created yet (correct pre-cutover)
- [x] Runbook v1.1 fixes applied (wallet backup path, web RPC §3.4, pool on `main`)

---

## G. Backup capacity (pool.db)

Measured 2026-06-15:

| Metric | Value |
|--------|-------|
| Filesystem `/` | 169G total, **133G avail** (18% used) |
| `pool.db` size | **2.3G** |
| `~/.byze` size | **58M** |
| Estimated cutover backup need | ~2.4G (`pool.db` copy) + ~60M datadir + ~35M release binaries ≈ **2.5G minimum** |
| Headroom | **PASS** — sufficient for single `cp` + tarball without pool.db duplication in same FS |

- [x] Disk space adequate for `pool.db` backup before reset
- [ ] `${BACKUP_ROOT}` created and `SHA256SUMS` of backup manifest verified at cutover

---

## H. Genesis & cutover (blocked — do not proceed)

- [ ] New genesis mined and `verify-genesis` pass
- [ ] `chainparams.cpp` updated with genesis hash / merkle assertions
- [ ] Post-genesis binaries built and checksummed
- [ ] v0.2 seed datadir bootstrapped (`~/.byze-v0.2`)
- [ ] Pool `pool.db` reset (after backup only)
- [ ] Production services restarted for v0.2
- [ ] Public announcement published

---

## I. Quick re-verify commands (optional, non-destructive)

```bash
# Core quantum unit tests
cd /home/byze/byze && ./build/bin/test_bitcoin --run_test=quantum_*

# Web watch-only tests
cd /home/byze/byze-web && npm run test:wallet-crypto

# Binary version
/home/byze/byze/release/v0.2.0/linux-x86_64/byzed --version

# Checksums
cd /home/byze/byze/release/v0.2.0/linux-x86_64 && sha256sum -c SHA256SUMS
```

---

## Summary

| Area | Status |
|------|--------|
| Validation gate | **PASS** |
| Documentation / runbook | **PASS** |
| Linux binaries staged + checksums | **PASS** (pre-genesis; not final) |
| Git hygiene | **PASS** (Core + Web); pool pin pending |
| Disk for backup | **PASS** |
| Launch approval & genesis | **PENDING** |

**Do not mine genesis or execute cutover until all items in sections A and H are PASS.**
