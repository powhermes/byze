#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mempool relay: valid quantum spend propagates A → B → C and is mined at C."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class HermesQuantumMempoolRelayTest(BitcoinTestFramework):
    def tx_confirmed_in_chain(self, node, txid, max_blocks=16):
        """Return True if txid appears in active-chain blocks walking back from tip."""
        h = node.getbestblockhash()
        for _ in range(max_blocks):
            block = node.getblock(h, 1)
            if txid in block["tx"]:
                return True
            h = block.get("previousblockhash")
            if not h:
                break
        return False

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [["-enforcequantumblocksigs=1", "-fallbackfee=0.001", "-logratelimit=0"]] * self.num_nodes
        # RandomX + quantum block validation during startup/generate can exceed default RPC budgets.
        self.rpc_timeout = 240
        self.sync_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def sync_all_quantum(self, nodes=None):
        n = nodes or self.nodes
        self.sync_blocks(n, timeout=self.sync_timeout)
        self.sync_mempools(n, timeout=self.sync_timeout)

    def run_test(self):
        n0, n1, n2 = self.nodes
        self.log.info("Fund chain and connect linear topology 0 — 1 — 2")
        self.generatetoaddress(n0, 110, n0.getnewaddress(), sync_fun=self.no_op)
        self.connect_nodes(0, 1)
        self.connect_nodes(1, 2)
        self.sync_all_quantum()

        self.log.info("Node 0 sends valid quantum tx toward node 2; mempools match on all peers")
        dest = n2.getnewaddress()
        txid = n0.sendtoaddress(dest, 1.0)
        self.sync_mempools(timeout=self.sync_timeout)
        m0 = n0.getrawmempool()
        m1 = n1.getrawmempool()
        m2 = n2.getrawmempool()
        assert txid in m0
        assert_equal(set(m0), set(m1))
        assert_equal(set(m1), set(m2))

        self.log.info("Node 2 mines and confirms the tx; chain and mempools stay consistent")
        self.generatetoaddress(n2, 2, n2.getnewaddress())
        self.sync_all_quantum()
        assert self.tx_confirmed_in_chain(n2, txid), "relay tx was not confirmed on node 2 active chain"
        assert_equal(n0.getrawmempool(), [])
        assert_equal(n1.getrawmempool(), [])
        assert_equal(n2.getrawmempool(), [])


if __name__ == "__main__":
    HermesQuantumMempoolRelayTest(__file__).main()
