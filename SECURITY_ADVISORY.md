# Security Advisory: Byze v0.1 quantum signatures (HMAC-Keccak)

**Advisory ID:** BYZE-SA-2026-001  
**Date:** 2026-06-15  
**Severity:** Critical  
**Affected:** Byze Core **v0.1.x** (mainnet through archived height ~3,751)  
**Fixed in:** Byze Core **v0.2.0** (requires chain reset; not yet live)

## Summary

Byze v0.1 advertised XMSS and SPHINCS+ quantum-safe signatures, but the implementation in `src/crypto/quantum_safe.cpp` was a **custom HMAC-Keccak + SHA3-256 construction**, not liboqs XMSS/SPHINCS+. The verifier relied on a **public `verification_token`**, allowing an attacker to forge valid signatures without private keys. On-chain v0.1 “quantum” spends and block tails were **not cryptographically bound** to legitimate key material.

v0.2.0 replaces this with real **liboqs** algorithms and new wire sizes. **v0.1 and v0.2 chains are incompatible**; v0.1 mainnet is archived and will not resume.

## Impact

| Area | Impact |
|------|--------|
| Transaction signatures | Any v0.1 quantum taproot input could be forged in principle |
| Block quantum tails | Block-level XMSS/SPHINCS+ fields were not real PQ signatures |
| Funds on archived chain | No confirmed in-the-wild forgery observed; **cannot assume spend authority was ever meaningful** |
| Browser wallet (v0.1) | Same forgeable JS HMAC port — **removed in v0.2.0** (watch-only only) |

## Technical details

### v0.1 (vulnerable)

- Signature “verification” used HMAC with a **derivable/public** token
- Wire sizes inconsistent with real XMSS/SPHINCS+ (e.g. XMSS 1028 bytes vs liboqs 2500)
- HKDF label `byze_quantum_hd_v1` in browser and Core wallet derivation
- Introduced in initial Core commit (2026-04-28); never corrected on mainnet

### v0.2.0 (remediation)

- **XMSS-SHA2_10_256** (STFL) and **SPHINCS+-SHA2-128s-simple** via liboqs 0.14.0
- Fixed sizes: XMSS pubkey 68 B, SPHINCS+ pubkey 32 B, XMSS sig 2500 B, SPHINCS+ sig 7856 B, bundle 100 B
- Wallet format version **3**, HKDF `byze_quantum_hd_v2`
- Browser wallet: **no signing**; Core-only key custody

## Proof of concept (conceptual)

Given a published pubkey bundle and message `m`, an attacker chooses nonce `n ≠ H(commitment ‖ m ‖ index)` and computes HMAC-Keccak with the public verification token. The v0.1 verifier accepted such tuples. Core comments in v0.1 `quantum_safe.cpp` acknowledged this weakness.

## Recommendations

### Operators

1. **Do not** treat v0.1 mainnet balances as secured by PQ cryptography.
2. **Do not** resume the archived datadir (`~/.byze`) for production — it remains a forensic archive only.
3. Deploy **v0.2.0** only on a **new datadir** after published genesis and verified binaries.
4. Rotate any credentials that were used on v0.1 infrastructure.

### Users

1. Use **Byze Core v0.2.0** for wallet creation and signing.
2. Use the **watch-only** browser wallet only for balance viewing and broadcast relay.
3. Do not import v0.1 browser wallet mnemonics expecting v0.2 address compatibility without Core migration tools (v0.2 uses new derivation and algorithms).

### Developers

1. Merge only from `release/v0.2.0` for relaunch work.
2. Confirm browser HMAC removal via `byze-web/doc/HMAC_SIGNING_REMOVAL_CHECKLIST.md`.
3. Run the quantum functional suite on regtest before genesis.

## Timeline

| Date | Event |
|------|--------|
| 2026-04-28 | Forgeable scheme introduced (initial commit) |
| 2026-06 | Security audit confirms universal forgeability |
| 2026-06-15 | `release/v0.2.0` branch: liboqs migration, browser watch-only, relaunch docs |
| TBD | Genesis + v0.2.0 mainnet launch (pending approval) |

## References

- Core fix: `release/v0.2.0` — `cmake/liboqs.cmake`, `src/crypto/quantum_safe.cpp`
- Browser remediation: `byze-web` `release/v0.2.0`
- Relaunch plan: `RELAUNCH.md`
- HMAC removal checklist: `byze-web/doc/HMAC_SIGNING_REMOVAL_CHECKLIST.md`

## CVE

No CVE assigned at publication time.

---

**Contact:** Report issues at https://github.com/powhermes/byze/issues

*This advisory is intended for public distribution alongside the v0.2.0 relaunch.*
