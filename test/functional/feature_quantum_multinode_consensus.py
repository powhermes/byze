#!/usr/bin/env python3
# Copyright (c) 2026 Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Deterministic multi-node quantum consensus agreement checks."""

from io import BytesIO
import hashlib
import struct

from test_framework.messages import CBlock, ser_string, tx_from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

# Keep these in sync with src/crypto/quantum_safe_config.h
BYZE_XMSS_SIGNATURE_SIZE = 1028
BYZE_SPHINCS_SIGNATURE_SIZE = 1024
BYZE_DUAL_PUBKEY_BUNDLE_SIZE = 192


def ser_quantum_sigdata(*, xmss: bytes, sphincs: bytes, dual: bytes) -> bytes:
    return ser_string(xmss) + ser_string(sphincs) + ser_string(dual)


def replace_quantum_sig_tail(block_hex: str, *, xmss: bytes, sphincs: bytes, dual: bytes) -> str:
    raw = bytes.fromhex(block_hex)
    f = BytesIO(raw)
    block = CBlock()
    block.deserialize(f)
    tail = f.read()
    assert tail != b"\x00\x00\x00", "expected a signed Byze block tail"
    prefix = raw[:-len(tail)]
    return (prefix + ser_quantum_sigdata(xmss=xmss, sphincs=sphincs, dual=dual)).hex()


def read_xmss_index(key_path):
    data = key_path.read_bytes()
    # Byze uses `crypto::quantum_safe_manager::save_dual_keys()` format:
    #   uint32 magic = 0x5146534B ("QSFK" as integer)
    #   uint8  version = 2
    #   uint8  algo = 2 (DUAL)
    #   uint32 xmss_priv_size; xmss_priv_bytes (seed[32] || index[4])
    #   uint32 xmss_pub_size;  xmss_pub_bytes
    #   uint32 sphincs_priv_size; sphincs_priv_bytes
    #   uint32 sphincs_pub_size;  sphincs_pub_bytes
    #
    assert len(data) >= 6, "invalid quantum key file length"
    magic, = struct.unpack("<I", data[0:4])
    assert magic == 0x5146534B, "invalid quantum key file magic"
    version = data[4]
    algo = data[5]
    assert version >= 2, "invalid quantum key file version"
    assert algo == 2, "unexpected quantum key algorithm (expected DUAL)"

    off = 6
    xmss_priv_len, = struct.unpack("<I", data[off:off + 4])
    off += 4
    xmss_priv = data[off:off + xmss_priv_len]
    assert len(xmss_priv) >= 36, "invalid XMSS private blob length"
    return struct.unpack("<I", xmss_priv[32:36])[0]


class QuantumMultinodeConsensusTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-enforcequantumblocksigs=1", "-fallbackfee=0.001", "-logratelimit=0"]] * self.num_nodes
        # Node RPC proxies use rpc_timeout // 2 for individual calls; createwallet can
        # exceed 120s on slower runs, so keep a larger test-level timeout budget.
        self.rpc_timeout = 480
        # Block/mempool sync: peers can lag the miner while validating RandomX + quantum block data.
        self.sync_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def sync_all_quantum(self, nodes=None):
        n = nodes or self.nodes
        self.sync_blocks(n, timeout=self.sync_timeout)
        self.sync_mempools(n, timeout=self.sync_timeout)

    def mine_blocks(self, node, num_blocks):
        # IMPORTANT: `generateblock` mines exactly the provided transactions and is implemented
        # with `use_mempool=false` in this codebase. For "mine whatever is in the mempool" semantics
        # (needed by this test), use the framework wrapper around `generatetoaddress`.
        for _ in range(num_blocks):
            self.generatetoaddress(
                node,
                nblocks=1,
                address=node.getnewaddress(),
            )

    def sync_and_assert_same_tip(self, stage: str):
        self.sync_all_quantum()
        tips = [n.getbestblockhash() for n in self.nodes]
        assert_equal(len(set(tips)), 1)
        heights = [n.getblockcount() for n in self.nodes]
        assert_equal(len(set(heights)), 1)
        self.log.info("%s: all nodes share identical tip %s at height %d", stage, tips[0], heights[0])

    def assert_testmempool_reject_identical(self, tx_hex: str, stage: str):
        outcomes = []
        for node in self.nodes:
            res = node.testmempoolaccept([tx_hex])[0]
            assert_equal(res["allowed"], False)
            reject_reason = res.get("reject-reason", "")
            assert reject_reason, f"{stage}: missing reject-reason"
            outcomes.append(reject_reason)
        assert_equal(len(set(outcomes)), 1)
        self.log.info("%s: identical mempool reject reason on all nodes: %s", stage, outcomes[0])

    def assert_generateblock_reject_identical(self, tx_hex: str, stage: str):
        for node in self.nodes:
            before_height = node.getblockcount()
            before_tip = node.getbestblockhash()
            assert_raises_rpc_error(
                -25,
                "TestBlockValidity failed",
                self.generateblock,
                node,
                sync_fun=self.no_op,
                output=node.getnewaddress(),
                transactions=[tx_hex],
            )
            assert_equal(node.getblockcount(), before_height)
            assert_equal(node.getbestblockhash(), before_tip)
        self.sync_and_assert_same_tip(stage)

    def assert_submitblock_reject_identical(self, block_hex: str, expected_reason: str, stage: str):
        outcomes = []
        before_height = self.nodes[0].getblockcount()
        before_tip = self.nodes[0].getbestblockhash()
        for node in self.nodes:
            result = node.submitblock(block_hex)
            assert result is not None, f"{stage}: block unexpectedly accepted"
            outcomes.append(result)
            assert_equal(node.getblockcount(), before_height)
            assert_equal(node.getbestblockhash(), before_tip)
        assert_equal(len(set(outcomes)), 1)
        assert_equal(outcomes[0], expected_reason)
        self.log.info("%s: identical submitblock reject reason on all nodes: %s", stage, outcomes[0])
        self.sync_and_assert_same_tip(stage)

    def build_malformed_witness_variants(self, signed_hex: str):
        good_tx = tx_from_hex(signed_hex)
        assert len(good_tx.wit.vtxinwit) > 0
        original_stack = list(good_tx.wit.vtxinwit[0].scriptWitness.stack)
        assert_equal(len(original_stack), 3)

        # Variant 1: missing one witness item (2-item stack).
        tx_2_items = tx_from_hex(signed_hex)
        tx_2_items.wit.vtxinwit[0].scriptWitness.stack = original_stack[:2]

        # Variant 2: malformed shape with 4 items.
        tx_4_items = tx_from_hex(signed_hex)
        tx_4_items.wit.vtxinwit[0].scriptWitness.stack = original_stack + [b"\x99"]

        # Variant 3: 3-item stack but empty XMSS signature.
        tx_empty_xmss = tx_from_hex(signed_hex)
        tx_empty_xmss.wit.vtxinwit[0].scriptWitness.stack = [b"", original_stack[1], original_stack[2]]

        return {
            "witness_stack_len_2": tx_2_items.serialize().hex(),
            "witness_stack_len_4": tx_4_items.serialize().hex(),
            "empty_xmss_signature": tx_empty_xmss.serialize().hex(),
        }

    def build_signed_quantum_tx(self, sender, recipient, amount):
        # Spend only an input that can be fully signed with a valid quantum witness stack.
        # Older wallet UTXOs can remain spendable in listunspent() but fail quantum signing
        # if their witness program does not match the current dual key bundle.
        for utxo in sender.listunspent():
            if not utxo["spendable"]:
                continue
            raw = sender.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], {recipient: amount})
            funded = sender.fundrawtransaction(raw)
            signed = sender.signrawtransactionwithwallet(funded["hex"])
            if not signed.get("complete", False):
                continue
            signed_hex = signed["hex"]
            tx = tx_from_hex(signed_hex)
            if len(tx.wit.vtxinwit) == 0:
                continue
            stack = list(tx.wit.vtxinwit[0].scriptWitness.stack)
            if (
                len(stack) == 3
                and len(stack[0]) == BYZE_XMSS_SIGNATURE_SIZE
                and len(stack[1]) == BYZE_SPHINCS_SIGNATURE_SIZE
                and len(stack[2]) == BYZE_DUAL_PUBKEY_BUNDLE_SIZE
            ):
                prevout_spk = utxo.get("scriptPubKey", "")
                if not isinstance(prevout_spk, str) or not prevout_spk.startswith("5120") or len(prevout_spk) != 68:
                    continue
                prevout_program = prevout_spk[4:]
                witness_program = hashlib.sha256(stack[2]).hexdigest()
                if witness_program != prevout_program:
                    continue
                return signed_hex
        raise AssertionError("Could not build a fully signed quantum transaction")

    def build_quantum_funded_tx(self, sender, recipient, amount):
        # Probe candidate UTXOs until we find an unsigned tx that signs with a 3-item
        # quantum witness stack (XMSS, SPHINCS+, dual pubkey bundle).
        for utxo in sender.listunspent():
            if not utxo["spendable"] or utxo.get("coinbase", False):
                continue
            raw = sender.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], {recipient: amount})
            funded = sender.fundrawtransaction(raw)
            probe_signed = sender.signrawtransactionwithwallet(funded["hex"])["hex"]
            probe_tx = tx_from_hex(probe_signed)
            if len(probe_tx.wit.vtxinwit) == 0:
                continue
            stack = list(probe_tx.wit.vtxinwit[0].scriptWitness.stack)
            if (
                len(stack) == 3
                and len(stack[0]) == BYZE_XMSS_SIGNATURE_SIZE
                and len(stack[1]) == BYZE_SPHINCS_SIGNATURE_SIZE
                and len(stack[2]) == BYZE_DUAL_PUBKEY_BUNDLE_SIZE
            ):
                return funded["hex"], utxo
        raise AssertionError("Could not construct a funded transaction that uses quantum witness signing")

    def get_witness_stack(self, tx_hex: str):
        tx = tx_from_hex(tx_hex)
        assert len(tx.wit.vtxinwit) > 0
        stack = list(tx.wit.vtxinwit[0].scriptWitness.stack)
        assert_equal(len(stack), 3)
        return stack

    def assert_valid_quantum_tx_edge_case(self, tx_hex: str, stage: str):
        tx = tx_from_hex(tx_hex)
        stack = tx.wit.vtxinwit[0].scriptWitness.stack
        assert_equal(len(stack), 3)
        assert_equal(len(stack[0]), BYZE_XMSS_SIGNATURE_SIZE)
        assert_equal(len(stack[1]), BYZE_SPHINCS_SIGNATURE_SIZE)
        assert_equal(len(stack[2]), BYZE_DUAL_PUBKEY_BUNDLE_SIZE)

        decoded_ids = []
        for node in self.nodes:
            decoded_ids.append(node.decoderawtransaction(tx_hex)["txid"])
            res = node.testmempoolaccept([tx_hex])[0]
            assert_equal(res["allowed"], True)
            assert_equal(res["txid"], decoded_ids[-1])
        assert_equal(len(set(decoded_ids)), 1)
        expected_txid = decoded_ids[0]

        # Relay one canonical hex; all nodes should converge on the same txid.
        relay_txid = self.nodes[0].sendrawtransaction(tx_hex)
        assert_equal(relay_txid, expected_txid)
        self.sync_mempools()
        for node in self.nodes:
            assert expected_txid in node.getrawmempool()
        self.log.info("%s: valid edge tx accepted with identical txid %s", stage, expected_txid)
        return expected_txid

    def assert_valid_block_submitblock_acceptance(self, miner_node, stage: str):
        base_height = miner_node.getblockcount()
        self.generateblock(
            miner_node,
            output=miner_node.getnewaddress(),
            transactions=[],
            sync_fun=self.no_op,
        )
        block_hash = miner_node.getbestblockhash()
        block_hex = miner_node.getblock(block_hash, 0)
        assert_equal(miner_node.getblockcount(), base_height + 1)

        accepted_outcomes = []
        for node in self.nodes:
            if node.index == miner_node.index:
                continue
            before_height = node.getblockcount()
            before_tip = node.getbestblockhash()
            result = node.submitblock(block_hex)
            accepted_outcomes.append(result)
            assert result is None, f"{stage}: expected block acceptance on node {node.index}, got {result}"
            assert_equal(node.getblockcount(), before_height + 1)
            assert_equal(node.getbestblockhash(), block_hash)
            assert before_tip != block_hash
        assert_equal(len(set(accepted_outcomes)), 1)

        self.sync_and_assert_same_tip(stage)
        for node in self.nodes:
            assert_equal(node.getbestblockhash(), block_hash)
        self.log.info("%s: valid submitblock accepted everywhere with hash %s", stage, block_hash)
        return block_hash

    def run_test(self):
        n0, n1, n2 = self.nodes

        self.log.info("Initial chain setup and deterministic baseline")
        self.mine_blocks(n0, 110)
        self.sync_and_assert_same_tip("baseline_sync")

        self.log.info("Valid edge-case quantum tx: boundary witness sizes accepted deterministically")
        edge_tx_hex = self.build_signed_quantum_tx(n0, n1.getnewaddress(), 0.35)
        edge_txid = self.assert_valid_quantum_tx_edge_case(edge_tx_hex, "valid_edge_tx_acceptance")
        self.mine_blocks(n2, 1)
        self.sync_and_assert_same_tip("valid_edge_tx_mined")
        # Block sync can complete before mempool/validation-interface cleanup is fully processed.
        # Ensure each node processes mempool removal notifications before asserting.
        for node in self.nodes:
            node.syncwithvalidationinterfacequeue()
        self.sync_mempools(timeout=self.sync_timeout)
        for node in self.nodes:
            self.wait_until(lambda n=node: edge_txid not in n.getrawmempool(), timeout=30)

        self.log.info("XMSS statefulness: same unsigned tx signed twice must advance index and differ")
        reusable_funded_hex, reusable_utxo = self.build_quantum_funded_tx(n0, n1.getnewaddress(), 0.11)
        self.log.info(
            "XMSS statefulness selected quantum utxo txid=%s vout=%d coinbase=%s address=%s",
            reusable_utxo["txid"],
            reusable_utxo["vout"],
            reusable_utxo.get("coinbase", False),
            reusable_utxo.get("address", ""),
        )
        key_path = n0.chain_path / "quantum_wallet.keys"
        self.log.info("XMSS statefulness key file path used by test helper: %s", key_path)
        xmss_idx_before = read_xmss_index(key_path)
        self.log.info("XMSS statefulness index before first sign: %d", xmss_idx_before)

        signed_once = n0.signrawtransactionwithwallet(reusable_funded_hex)["hex"]
        xmss_idx_after_first = read_xmss_index(key_path)
        self.log.info("XMSS statefulness index after first sign: %d", xmss_idx_after_first)
        signed_twice = n0.signrawtransactionwithwallet(reusable_funded_hex)["hex"]
        xmss_idx_after_second = read_xmss_index(key_path)
        self.log.info("XMSS statefulness index after second sign: %d", xmss_idx_after_second)

        assert_equal(xmss_idx_after_first, xmss_idx_before + 1)
        assert_equal(xmss_idx_after_second, xmss_idx_after_first + 1)

        stack_once = self.get_witness_stack(signed_once)
        stack_twice = self.get_witness_stack(signed_twice)
        assert stack_once[0] != stack_twice[0], "XMSS signatures should differ across signing index use"
        assert signed_once != signed_twice, "Signed tx encodings should differ across XMSS state"

        for node in self.nodes:
            res_once = node.testmempoolaccept([signed_once])[0]
            res_twice = node.testmempoolaccept([signed_twice])[0]
            assert_equal(res_once["allowed"], True)
            assert_equal(res_twice["allowed"], True)
            # txid excludes witness for segwit/taproot spends; witness changes should
            # produce distinct wtxids while txid may remain the same.
            assert_equal(res_once["txid"], res_twice["txid"])
            assert res_once["wtxid"] != res_twice["wtxid"]

        self.log.info("SPHINCS deterministic verification: repeated checks agree across nodes")
        sphincs_probe = signed_once
        deterministic_results = []
        for _ in range(3):
            round_results = []
            for node in self.nodes:
                round_results.append(node.testmempoolaccept([sphincs_probe])[0]["allowed"])
            deterministic_results.append(tuple(round_results))
        assert_equal(len(set(deterministic_results)), 1)
        assert_equal(deterministic_results[0], (True, True, True))

        self.log.info("Valid block submitblock determinism across nodes")
        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(0, 2)
        valid_block_hash = self.assert_valid_block_submitblock_acceptance(n0, "valid_block_submitblock")
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.sync_and_assert_same_tip("valid_block_post_reconnect")
        for node in self.nodes:
            assert_equal(node.getbestblockhash(), valid_block_hash)

        self.log.info("Quantum-only relay flow: valid tx relays and confirms deterministically")
        recv = n1.getnewaddress()
        relay_hex = self.build_signed_quantum_tx(n0, recv, 1.0)
        txid = n0.sendrawtransaction(relay_hex)
        self.sync_mempools(timeout=self.sync_timeout)
        for node in self.nodes:
            assert txid in node.getrawmempool()
        self.mine_blocks(n2, 1)
        self.sync_and_assert_same_tip("valid_quantum_tx_mined")
        for node in self.nodes:
            assert txid not in node.getrawmempool()

        self.log.info("Build signed quantum tx and check malformed witness variants")
        signed_hex = self.build_signed_quantum_tx(n0, n2.getnewaddress(), 0.2)
        bad_variants = self.build_malformed_witness_variants(signed_hex)

        for name, bad_hex in bad_variants.items():
            self.assert_testmempool_reject_identical(bad_hex, name)
            self.assert_generateblock_reject_identical(bad_hex, f"{name}_consensus")

        self.log.info("XMSS one-time use enforcement: reused XMSS signature must be rejected")
        fresh_valid_hex = self.build_signed_quantum_tx(n0, n2.getnewaddress(), 0.17)
        fresh_stack = self.get_witness_stack(fresh_valid_hex)
        reused_xmss_tx = tx_from_hex(fresh_valid_hex)
        reused_xmss_tx.wit.vtxinwit[0].scriptWitness.stack = [stack_once[0], fresh_stack[1], fresh_stack[2]]
        reused_xmss_hex = reused_xmss_tx.serialize().hex()
        self.assert_testmempool_reject_identical(reused_xmss_hex, "reused_xmss_signature")
        self.assert_generateblock_reject_identical(reused_xmss_hex, "reused_xmss_signature_consensus")

        self.log.info("Build malformed block quantum signature variants (submitblock path)")
        tip_hash = n0.getbestblockhash()
        tip_block_hex = n0.getblock(tip_hash, 0)
        malformed_blocks = [
            (
                "missing_all_quantum_block_sigs",
                replace_quantum_sig_tail(tip_block_hex, xmss=b"", sphincs=b"", dual=b""),
                "bad-quantum-sig-missing",
            ),
            (
                "invalid_xmss_block_sig",
                replace_quantum_sig_tail(
                    tip_block_hex,
                    xmss=bytes([0xAB]) * BYZE_XMSS_SIGNATURE_SIZE,
                    sphincs=bytes([0xBB]) * BYZE_SPHINCS_SIGNATURE_SIZE,
                    dual=bytes([0xCC]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
                ),
                "bad-quantum-sig",
            ),
            (
                "invalid_sphincs_block_sig",
                replace_quantum_sig_tail(
                    tip_block_hex,
                    xmss=bytes([0xAA]) * BYZE_XMSS_SIGNATURE_SIZE,
                    sphincs=bytes([0xBC]) * BYZE_SPHINCS_SIGNATURE_SIZE,
                    dual=bytes([0xCC]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
                ),
                "bad-quantum-sig",
            ),
            (
                "invalid_dual_bundle",
                replace_quantum_sig_tail(
                    tip_block_hex,
                    xmss=bytes([0xAA]) * BYZE_XMSS_SIGNATURE_SIZE,
                    sphincs=bytes([0xBB]) * BYZE_SPHINCS_SIGNATURE_SIZE,
                    dual=bytes([0xCD]) * BYZE_DUAL_PUBKEY_BUNDLE_SIZE,
                ),
                "bad-quantum-sig",
            ),
        ]

        for name, bad_block_hex, reject_reason in malformed_blocks:
            self.assert_submitblock_reject_identical(bad_block_hex, reject_reason, name)

        self.log.info("Mixed sequence validity: valid tx -> invalid tx -> valid tx")
        mixed_valid_1 = self.build_signed_quantum_tx(n0, n2.getnewaddress(), 0.21)
        mixed_valid_1_txid = self.assert_valid_quantum_tx_edge_case(mixed_valid_1, "mixed_valid_tx_1")
        self.mine_blocks(n1, 1)
        self.sync_and_assert_same_tip("mixed_valid_tx_1_mined")
        for node in self.nodes:
            assert mixed_valid_1_txid not in node.getrawmempool()

        mixed_invalid = bad_variants["witness_stack_len_2"]
        self.assert_testmempool_reject_identical(mixed_invalid, "mixed_invalid_tx")
        self.assert_generateblock_reject_identical(mixed_invalid, "mixed_invalid_tx_consensus")

        mixed_valid_2 = self.build_signed_quantum_tx(n1, n0.getnewaddress(), 0.19)
        mixed_valid_2_txid = self.assert_valid_quantum_tx_edge_case(mixed_valid_2, "mixed_valid_tx_2")
        self.mine_blocks(n2, 1)
        self.sync_and_assert_same_tip("mixed_valid_tx_2_mined")
        for node in self.nodes:
            assert mixed_valid_2_txid not in node.getrawmempool()

        self.log.info("Reconnect/restart safety: rejected blocks never become active")
        expected_tip = n0.getbestblockhash()
        expected_height = n0.getblockcount()

        self.disconnect_nodes(0, 1)
        self.disconnect_nodes(1, 2)
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_and_assert_same_tip("post_reconnect")

        for i in range(self.num_nodes):
            self.restart_node(i)
        self.sync_all_quantum()
        for node in self.nodes:
            assert_equal(node.getbestblockhash(), expected_tip)
            assert_equal(node.getblockcount(), expected_height)


if __name__ == "__main__":
    QuantumMultinodeConsensusTest(__file__).main()
