#!/usr/bin/env python3
# Copyright (c) 2026 Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Consensus rejection tests for Byze quantum block signatures via submitblock."""

from io import BytesIO

from test_framework.messages import CBlock, ser_string
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# Keep these in sync with src/crypto/quantum_safe_config.h
BYZE_XMSS_SIGNATURE_SIZE = 2500
BYZE_SPHINCS_SIGNATURE_SIZE = 7856
BYZE_DUAL_PUBKEY_BUNDLE_SIZE = 100


def ser_quantum_sigdata(*, xmss: bytes, sphincs: bytes, dual: bytes) -> bytes:
    # Matches C++ quantum_signature_data serialization:
    # READWRITE(xmss_signature, sphincs_signature, dual_public_key)
    return ser_string(xmss) + ser_string(sphincs) + ser_string(dual)


def replace_quantum_sig_tail(block_hex: str, *, xmss: bytes, sphincs: bytes, dual: bytes) -> str:
    """
    Byze serializes CBlock as:
      [80-byte header][vtx vector][quantum_signature_data]

    The Python test framework only understands header+vtx, so deserialize that
    part, keep the prefix, and overwrite the Byze tail.
    """
    raw = bytes.fromhex(block_hex)
    f = BytesIO(raw)

    block = CBlock()
    block.deserialize(f)
    tail = f.read()

    # With -enforcequantumblocksigs=1 enabled, block production should attach
    # signatures, so the Byze tail should not be 3 empty vectors.
    assert tail != b"\x00\x00\x00", "expected a signed Byze block tail"

    prefix = raw[:-len(tail)]
    mutated = prefix + ser_quantum_sigdata(xmss=xmss, sphincs=sphincs, dual=dual)
    return mutated.hex()


class QuantumBlockSigRejectTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-enforcequantumblocksigs=1", "-logratelimit=0"]] * self.num_nodes
        # RandomX FULL_MEM dataset initialization can exceed the default 60s RPC timeout on slower CI.
        self.rpc_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_reject_everywhere(self, name: str, block_hex: str, reject_reason: str):
        for node in self.nodes:
            before_height = node.getblockcount()
            before_tip = node.getbestblockhash()

            result = node.submitblock(block_hex)

            assert result is not None, f"{name}: block unexpectedly accepted"
            assert_equal(result, reject_reason)
            assert_equal(node.getblockcount(), before_height)
            assert_equal(node.getbestblockhash(), before_tip)

    def fresh_signed_block_hex(self):
        """Mine a valid signed block without submitting (unique hash per call)."""
        n0 = self.nodes[0]
        if not hasattr(self, "_mine_addr"):
            self._mine_addr = n0.getnewaddress()
        return n0.__getattr__("generateblock")(self._mine_addr, [], False)["hex"]

    def run_test(self):
        n0 = self.nodes[0]
        before_height = n0.getblockcount()
        before_tip = n0.getbestblockhash()

        self.log.info("Submit block with quantum signatures removed entirely")
        block_hex = self.fresh_signed_block_hex()
        missing_hex = replace_quantum_sig_tail(block_hex, xmss=b"", sphincs=b"", dual=b"")
        self.assert_reject_everywhere("missing_quantum_signatures", missing_hex, "bad-quantum-sig-missing")

        # Enforced regtest: empty xmss or empty sphincs -> bad-quantum-sig-missing; both lengths
        # present but VerifyBlockQuantumSignatures fails -> bad-quantum-sig.

        self.log.info("Submit block with only XMSS signature present (SPHINCS missing -> bad-quantum-sig-missing)")
        block_hex = self.fresh_signed_block_hex()
        xmss_only_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=b"\x11" * BYZE_XMSS_SIGNATURE_SIZE,
            sphincs=b"",
            dual=b"",
        )
        self.assert_reject_everywhere("partial_xmss_only", xmss_only_hex, "bad-quantum-sig-missing")

        self.log.info("Submit block with only SPHINCS signature present (XMSS missing -> bad-quantum-sig-missing)")
        block_hex = self.fresh_signed_block_hex()
        sphincs_only_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=b"",
            sphincs=b"\x22" * BYZE_SPHINCS_SIGNATURE_SIZE,
            dual=b"",
        )
        self.assert_reject_everywhere("partial_sphincs_only", sphincs_only_hex, "bad-quantum-sig-missing")

        self.log.info("XMSS signature corrupted")
        block_hex = self.fresh_signed_block_hex()
        xmss_corrupt_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=bytes([0xAB]) * BYZE_XMSS_SIGNATURE_SIZE,
            sphincs=bytes([0xBB]) * BYZE_SPHINCS_SIGNATURE_SIZE,
            dual=bytes([0xCC]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
        )
        self.assert_reject_everywhere("xmss_corrupted", xmss_corrupt_hex, "bad-quantum-sig")

        self.log.info("SPHINCS signature corrupted")
        block_hex = self.fresh_signed_block_hex()
        sphincs_corrupt_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=bytes([0xAA]) * BYZE_XMSS_SIGNATURE_SIZE,
            sphincs=bytes([0xBC]) * BYZE_SPHINCS_SIGNATURE_SIZE,
            dual=bytes([0xCC]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
        )
        self.assert_reject_everywhere("sphincs_corrupted", sphincs_corrupt_hex, "bad-quantum-sig")

        self.log.info("Dual public key bundle corrupted")
        block_hex = self.fresh_signed_block_hex()
        dual_corrupt_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=bytes([0xAA]) * BYZE_XMSS_SIGNATURE_SIZE,
            sphincs=bytes([0xBB]) * BYZE_SPHINCS_SIGNATURE_SIZE,
            dual=bytes([0xCD]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
        )
        self.assert_reject_everywhere("dual_bundle_corrupted", dual_corrupt_hex, "bad-quantum-sig")

        self.log.info("Correct sizes but invalid cryptographic signatures")
        block_hex = self.fresh_signed_block_hex()
        invalid_crypto_hex = replace_quantum_sig_tail(
            block_hex,
            xmss=bytes([0x5A]) * BYZE_XMSS_SIGNATURE_SIZE,
            sphincs=bytes([0xA5]) * BYZE_SPHINCS_SIGNATURE_SIZE,
            dual=bytes([0x3C]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
        )
        self.assert_reject_everywhere("invalid_crypto_signatures", invalid_crypto_hex, "bad-quantum-sig")

        self.log.info("Verify chain height and best block hash are unchanged everywhere")
        self.sync_all()
        for node in self.nodes:
            assert_equal(node.getblockcount(), before_height)
            assert_equal(node.getbestblockhash(), before_tip)


if __name__ == "__main__":
    QuantumBlockSigRejectTest(__file__).main()

