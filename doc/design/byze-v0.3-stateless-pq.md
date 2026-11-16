# Byze v0.3: remove the per-address signature limit (DRAFT, not approved)

Status: design draft, 2026-11-16. Nothing implemented. Activation needs owner approval.

## Problem

Every Byze spend carries two post-quantum signatures: XMSS-SHA2_10_256 and
SPHINCS+-SHA2-128s (`src/crypto/quantum_safe_config.h`). XMSS is a *stateful*
scheme with tree height 10, so each address key can produce at most 1,024
signatures in its lifetime. Each transaction input is signed separately, so an
address can ever spend at most ~1,024 of the outputs it received.

Consequences seen on mainnet:
- The old pool's payout addresses exhausted their keys (~74.8k BYZ stranded, Sep 2026 audit).
- A solo-mining address (`byz1p4pm9…mu68mn`) has received 1,337 coinbase outputs
  (66,850 BYZ, measured 2026-11-16 at h24450); everything past ~1,024 inputs cannot be spent.
- Any exchange, pool, payment processor or merchant that reuses an address will hit it.
  Address rotation is a workaround users cannot be expected to follow.

Block-level signatures are NOT affected: blocks are signed with an ephemeral in-memory key
that auto-rotates at 950 leaves (`src/crypto/quantum_block_sign.cpp`) and is not bound to
any coins.

## Goal

Spending from an address has no signature limit and no signing state, while keeping
two independent post-quantum signatures (defence in depth) and the existing address format.

## Proposal

Add a second key-bundle type behind the same witness-v1 program commitment:

| Bundle | Contents | Program | Limit |
|---|---|---|---|
| v1-legacy (today) | XMSS-SHA2_10_256 pk (68 B) + SPHINCS+-128s pk (32 B) = 100 B | SHA256(bundle) | 1,024 sigs |
| v1-stateless (new) | ML-DSA-44 pk (1,312 B) + SPHINCS+-128s pk (32 B) = 1,344 B | SHA256(bundle) | none |

- Validation dispatches on the bundle length in the witness (100 B = legacy path, 1,344 B =
  stateless path). The scriptPubKey stays a 32-byte witness-v1 program, so addresses keep the
  `byz1p…` format; exchanges and explorers need no new address type.
- Stateless witness: `[mldsa_sig (2,420 B), sphincs_sig (7,856 B), bundle (1,344 B)]`.
  Size is about the same as legacy (2,500 + 7,856 + 100); bundle is larger, sig is smaller.
- ML-DSA (NIST FIPS 204) is already in liboqs; Byze's build currently has it switched off
  (`OQS_ENABLE_SIG_ML_DSA` undefined), so it is a build flag, not a new dependency.
- Sighash is unchanged (both signatures sign the same message).
- Wallet: after activation every new receive and change address is v1-stateless.
  Legacy addresses remain spendable with XMSS as today, within their remaining leaves.

## Why a hard fork

`VerifyScript` rejects every spend that is not a 3-item witness-v1 legacy bundle
(`src/script/interpreter.cpp`, "Byze requires witness v1 quantum-safe spends only").
Accepting a new bundle loosens consensus, so all nodes must upgrade before an activation height.
Old nodes would reject the first block containing a stateless spend.

## Option: migrate exhausted legacy coins (owner decision)

Owner asked 2026-11-16 whether v0.3 can also fix coins already stuck. Classes:

| Class | Approx. BYZ | Recoverable by a rule change? |
|---|---|---|
| Coinbase paid to bare OP_1 (blocks 5–1,437) | 28,050 (measured) | No: no key exists |
| Pre-v0.2.3 web wallet SHA3 address bug | ~22,000 (estimate) | No: keys do not match the committed program |
| XMSS keys exhausted (pool ~74.8k, MAC wallet 30.0k, solo address overflow ~16k+) | ~120,000 (estimate) | Yes: owners still hold the SPHINCS+ key |

Proposed migration rule: a legacy (XMSS+SPHINCS+) output may be spent with a SPHINCS+-only
witness IF every output of that transaction is a v1-stateless output (or OP_RETURN/fee). The coins
can only move into the new unlimited address type, so the rule cannot be used to keep spending
from weakened legacy addresses. SPHINCS+-SHA2-128s is stateless hash-based and post-quantum on its own.

Trade-offs: drops the second signature for that one migration spend; supersedes the earlier
no-fork-for-recovery decisions (2026-07-02, 2026-09-14), so it needs an explicit owner decision;
most recovered coins are pool/developer-held, so pool coins should go back to the miners they are owed to.
Optional: allow it only until a sunset height.

## Rollout

1. rc8 (no fork): fix startmining crash when mining is already active (segfault seen 2026-11-16);
   built-in miner pays each block to a fresh wallet address; wallet warns when
   an address passes 800 received outputs; `getaddressinfo` shows `quantum_sigs_remaining`.
2. v0.3.0: ML-DSA enabled in liboqs build; v1-stateless bundle in consensus behind
   `-stateless_activation_height`; wallet keygen + mnemonic derivation for ML-DSA; tests
   (functional: send/receive/spend >1,024 times from one address; mixed legacy/stateless tx).
3. Testnet/regtest soak, then pick a mainnet activation height ≥ 4 weeks after release.
4. Upgrade every known node and service before activation: pool, byze.org node, explorer,
   wallet.byze.org (web wallet needs ML-DSA in JS/WASM), Contract-Dev P2Pool, Discord bot.

## Open questions

- ML-DSA-44 (NIST level 2) vs ML-DSA-65 (level 3, 3,309 B sigs). SPHINCS+-128s is level 1, so 44 matches.
- Deterministic derivation of ML-DSA keys from the BIP39 mnemonic (ML-DSA keygen takes a 32-byte seed, so yes).
- Web wallet WASM build size and signing speed.
