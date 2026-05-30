#!/usr/bin/env python3
# Copyright (c) 2026 Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reorg and adversarial correctness for quantum (witness v1) spends and XMSS wallet state."""

import hashlib

from test_framework.messages import tx_from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

# Keep these in sync with src/crypto/quantum_safe_config.h
BYZE_XMSS_SIGNATURE_SIZE = 1028
BYZE_SPHINCS_SIGNATURE_SIZE = 1024
BYZE_DUAL_PUBKEY_BUNDLE_SIZE = 192


def tx_confirmed_in_chain(node, txid, max_blocks=64):
    """Return True if txid appears in any block on the active chain (walking back from tip)."""
    h = node.getbestblockhash()
    for _ in range(max_blocks):
        block = node.getblock(h, 1)
        if txid in block["tx"]:
            return True
        h = block.get("previousblockhash")
        if not h:
            break
    return False


class QuantumReorgTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [
            [
                "-enforcequantumblocksigs=1",
                "-fallbackfee=0.001",
                "-logratelimit=0",
            ]
        ] * self.num_nodes
        # RPC proxies use roughly rpc_timeout // 2 per call; RandomX block generation can
        # exceed 240s on slower hosts, so provide more headroom.
        self.rpc_timeout = 1200
        self.sync_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        """Use a small mesh so isolating node1 still leaves node2 connected to node0."""
        self.setup_nodes()
        self.connect_nodes(0, 1)
        self.connect_nodes(0, 2)
        self.connect_nodes(1, 2)
        self.sync_all()

    def sync_pair(self, a, b):
        self.sync_blocks([self.nodes[a], self.nodes[b]], timeout=self.sync_timeout)
        self.sync_mempools([self.nodes[a], self.nodes[b]], timeout=self.sync_timeout)

    def sync_all_quantum(self):
        self.sync_blocks(self.nodes, timeout=self.sync_timeout)
        self.sync_mempools(self.nodes, timeout=self.sync_timeout)

    def best_chainwork(self, node):
        best = node.getbestblockhash()
        return int(node.getblockheader(best)["chainwork"], 16)

    def wait_for_any_peers_all_nodes(self):
        # sync_blocks() requires every node to have at least one peer connection.
        self.wait_until(
            lambda: all(len(n.getpeerinfo()) > 0 for n in self.nodes),
            timeout=30,
        )

    def connect_with_retry(self, a, b, attempts=3):
        last_err = None
        for _ in range(attempts):
            try:
                self.connect_nodes(a, b)
                return
            except AssertionError as e:
                last_err = e
        raise last_err

    def mine_blocks(self, node, num_blocks, *, sync_fun=None):
        """Mine blocks; use sync_fun=self.no_op while the network is intentionally partitioned."""
        sync_cb = self.no_op if sync_fun is None else sync_fun
        for _ in range(num_blocks):
            self.generatetoaddress(
                node,
                nblocks=1,
                address=node.getnewaddress(),
                sync_fun=sync_cb,
            )

    def log_tips(self, stage):
        for i, n in enumerate(self.nodes):
            self.log.info(
                "%s: node %d height=%d tip=%s",
                stage,
                i,
                n.getblockcount(),
                n.getbestblockhash(),
            )

    def build_signed_quantum_tx(self, sender, recipient, amount):
        """Mirror feature_quantum_multinode_consensus: only return fully valid quantum witness spends."""
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

    def get_witness_stack(self, tx_hex: str):
        tx = tx_from_hex(tx_hex)
        assert len(tx.wit.vtxinwit) > 0
        stack = list(tx.wit.vtxinwit[0].scriptWitness.stack)
        assert_equal(len(stack), 3)
        return stack

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

    def assert_mempool_presence_identical(self, txid: str, stage: str):
        states = [txid in n.getrawmempool() for n in self.nodes]
        if len(set(states)) != 1:
            raise AssertionError(f"{stage}: mempool presence differs across nodes: {states}")
        self.log.info("%s: txid %s in mempool on all nodes: %s", stage, txid, states[0])

    def run_test(self):
        n0, n1, n2 = self.nodes

        self.log.info("Phase 1: common chain on node A (node0), sync mesh")
        self.mine_blocks(n0, 110)
        self.sync_all_quantum()
        self.log_tips("after_baseline_sync")

        self.log.info("Phase 2: isolate node B (node1) before quantum tx so fork never sees the spend")
        self.disconnect_nodes(1, 0)
        self.disconnect_nodes(1, 2)

        self.log.info("Phase 3: valid quantum tx on main branch (node0 + node2)")
        recv_addr = n2.getnewaddress()
        spent_hex = self.build_signed_quantum_tx(n0, recv_addr, 0.35)
        spent_wit = self.get_witness_stack(spent_hex)
        dec = n0.decoderawtransaction(spent_hex)
        spent_txid = dec["txid"]
        vin0 = dec["vin"][0]
        prev_txid = vin0["txid"]
        prev_vout = vin0["vout"]

        n0.sendrawtransaction(spent_hex)
        self.sync_pair(0, 2)
        assert spent_txid in n0.getrawmempool()
        assert spent_txid in n2.getrawmempool()

        self.mine_blocks(n0, 1, sync_fun=self.no_op)
        self.sync_pair(0, 2)

        assert tx_confirmed_in_chain(n0, spent_txid)
        assert tx_confirmed_in_chain(n2, spent_txid)
        assert not tx_confirmed_in_chain(n1, spent_txid)

        self.log.info("Phase 4: longer competing chain on node B without the quantum tx")
        fork_blocks = 3
        self.mine_blocks(n1, fork_blocks, sync_fun=self.no_op)
        # On this RandomX-derived chain, height alone is not a reliable proxy for best-chain selection.
        # Ensure node1's competing branch is strictly heavier before reconnect.
        while self.best_chainwork(n1) <= self.best_chainwork(n0):
            self.mine_blocks(n1, 1, sync_fun=self.no_op)
            fork_blocks += 1
        assert_equal(n0.getblockcount(), 111)
        self.log.info(
            "Competing fork prepared on node1: height=%d chainwork=%d vs node0 chainwork=%d",
            n1.getblockcount(),
            self.best_chainwork(n1),
            self.best_chainwork(n0),
        )
        self.log_tips("split_end_before_reconnect")

        self.log.info("Phase 5: reconnect; node1 wins by work/height")
        self.connect_with_retry(0, 1)
        self.connect_with_retry(0, 2)
        self.wait_for_any_peers_all_nodes()
        self.sync_blocks(self.nodes, timeout=self.sync_timeout)
        self.sync_mempools(self.nodes, timeout=self.sync_timeout)
        self.log_tips("after_reorg")

        final_height = 110 + fork_blocks
        for n in self.nodes:
            assert_equal(n.getblockcount(), final_height)

        tips = [n.getbestblockhash() for n in self.nodes]
        assert_equal(len(set(tips)), 1)

        self.log.info("Post-reorg: orphaned quantum tx must not appear on active chain")
        for i, n in enumerate(self.nodes):
            assert not tx_confirmed_in_chain(n, spent_txid), f"node {i} still has spent_tx on active chain"

        self.log.info(
            "Post-reorg: prevout unspent at tip on chain (gettxout include_mempool=false; mempool may claim it)"
        )
        for i, n in enumerate(self.nodes):
            out = n.gettxout(prev_txid, prev_vout, False)
            assert out is not None, f"node {i}: prevout missing from chain UTXO after reorg"
        out0 = n0.gettxout(prev_txid, prev_vout, False)
        for n in self.nodes[1:]:
            assert_equal(n.gettxout(prev_txid, prev_vout, False)["scriptPubKey"]["hex"], out0["scriptPubKey"]["hex"])

        self.log.info("Post-reorg: XMSS state is wallet-local (no datadir quantum_wallet.keys); UTXO set restored above")
        assert not (n0.chain_path / "quantum_wallet.keys").exists()

        self.log.info("Post-reorg: mempool handling for orphaned tx (identical across nodes)")
        for n in self.nodes:
            n.syncwithvalidationinterfacequeue()
        self.sync_mempools(self.nodes, timeout=self.sync_timeout)
        self.assert_mempool_presence_identical(spent_txid, "orphan_tx_mempool")

        self.log.info("Spliced witness: XMSS from orphaned tx on a fresh spend must be rejected identically")
        fresh_hex = self.build_signed_quantum_tx(n0, n2.getnewaddress(), 0.12)
        fresh_stack = self.get_witness_stack(fresh_hex)
        splice = tx_from_hex(fresh_hex)
        splice.wit.vtxinwit[0].scriptWitness.stack = [spent_wit[0], fresh_stack[1], fresh_stack[2]]
        self.assert_testmempool_reject_identical(splice.serialize().hex(), "spliced_xmss_from_orphan")

        self.log.info("Replay: testmempoolaccept of original orphaned tx (deterministic across nodes)")
        replay_outcomes = []
        for node in self.nodes:
            replay_outcomes.append(node.testmempoolaccept([spent_hex])[0]["allowed"])
        assert_equal(len(set(replay_outcomes)), 1)
        self.log.info(
            "Replay testmempoolaccept allowed (same on all nodes): %s",
            replay_outcomes[0],
        )


if __name__ == "__main__":
    QuantumReorgTest(__file__).main()
