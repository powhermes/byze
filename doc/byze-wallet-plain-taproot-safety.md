# Wallet safety: plain taproot outputs on Byze

## Problem

Byze consensus lets a witness-v1 32-byte output be spent only with a 3-item
witness `[xmss_sig, sphincs_sig, dual_pubkey_bundle]` where
`SHA256(dual_pubkey_bundle) == program` (`src/script/interpreter.cpp`,
`VerifyWitnessProgram`). There is no BIP341 key-path or script-path spend.

The wallet's `tr()` descriptors still expand to ordinary BIP86 taproot scripts
("plain tr()"). `getnewaddress` correctly replaces these with the quantum
program, but the plain scripts are still registered in the descriptor cache. As a result:

- `getaddressinfo` labels about half of all valid quantum addresses as `rawtr(...)`, namely those whose
  program happens to be a valid x-only point;
- outputs paid to a plain tr() address the wallet derived (for example via
  `deriveaddresses`) were counted as `ismine`/spendable and included in the
  balance, even though consensus can never spend them;
- the mining RPCs accepted such an address as a coinbase payout.

## Change

- `getaddressinfo` reports `quantum_program(<hex>)` and `quantum_hd_index` for
  wallet quantum programs. For plain tr() scripts it reports
  `solvable: false, unspendable: true`.
- Outputs to wallet-derived plain tr() scripts go into a separate
  `m_unspendable_txos` set. They are excluded from balances, `listunspent`
  and coin selection, but stay in the transaction history.
  `getbalances.mine.unspendable` reports their total when it is non-zero.
- `startmining`, `generatetoaddress`, `generatetodescriptor` and
  `generateblock` reject a payout script that any loaded wallet recognises as
  one of its plain tr() derivations (RPC error -5).

## Limitation: this is not a consensus-wide address validator

Plain tr() detection works **only for scripts derived by a wallet that
is loaded in the same node**. The check asks the wallet's own descriptors
whether the script is their plain BIP86 expansion.

For an arbitrary external witness-v1 address the node cannot tell a valid quantum program
(`SHA256` of a key bundle) from a plain taproot output key.
About half of all SHA256 outputs are valid x-only curve points, so the curve-point test is
not decisive either. Consequently:

- external addresses are accepted unchanged by the mining RPCs;
- a node run with `-disablewallet`, or with the deriving wallet not loaded,
  gets no protection;
- plain tr() scripts beyond the wallet's registered keypool range are not
  recognised.

Users must still obtain receiving and mining addresses from `getnewaddress`,
and should test-spend a small amount before relying on a new address.

## Out of scope (tracked separately)

- receive/change quantum-program index collision;
- `IsQuantumMine()` returning true for plain tr() scripts after reload;
- CRLF normalization of the repository;
- deployment of the v0.2.5-rc2 (PR #8) lockout fix.

## Test evidence

New functional tests: `wallet_quantum_plain_taproot.py`,
`wallet_quantum_getaddressinfo.py`, plus a unit test in `ismine_tests.cpp`.
Before and after logs are kept outside the repository in the operator's
evidence archive (`byze-evidence/2026-10-02/wallet-safety-test-logs/`).
`wallet_balance`, `wallet_basic`, `rpc_generate`, `mining_basic`,
`wallet_orphanedreward` and `wallet_mnemonic_restore` already fail on
`4ad7e5e` with identical errors before and after this change.
