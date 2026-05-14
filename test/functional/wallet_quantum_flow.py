#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Basic end-to-end wallet flow checks for Byze quantum spends."""

from test_framework.messages import tx_from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class WalletQuantumFlowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-fallbackfee=0.001"], ["-fallbackfee=0.001"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        n0, n1 = self.nodes[0], self.nodes[1]
        self.generate(n0, 110)
        self.sync_all()

        self.log.info("Quantum spend material must not use datadir quantum_wallet.keys")
        for i, node in enumerate(self.nodes):
            legacy = node.chain_path / "quantum_wallet.keys"
            assert not legacy.exists(), (
                f"node{i} must not create {legacy}; quantum keys belong in wallet.dat"
            )

        self.log.info("Reject non-quantum address type RPC paths")
        assert_raises_rpc_error(-8, "Byze requires bech32m quantum addresses only", n0.getnewaddress, "", "legacy")
        assert_raises_rpc_error(-8, "Byze requires bech32m quantum addresses only", n0.getrawchangeaddress, "legacy")

        self.log.info("Send with change and respend")
        recv = n1.getnewaddress()
        txid_1 = n0.sendtoaddress(recv, 5)
        self.generate(n0, 1)
        self.sync_all()
        assert txid_1 in [t["txid"] for t in n0.listtransactions("*", 100)]

        txid_2 = n1.sendtoaddress(n0.getnewaddress(), 1)
        self.generate(n1, 1)
        self.sync_all()
        assert txid_2 in [t["txid"] for t in n1.listtransactions("*", 100)]

        self.log.info("Reject malformed v1/32 witness from mempool policy")
        utxo = next(u for u in n0.listunspent() if u["spendable"])
        raw = n0.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], {n1.getnewaddress(): 0.2})
        funded = n0.fundrawtransaction(raw)
        signed = n0.signrawtransactionwithwallet(funded["hex"])
        good_tx = tx_from_hex(signed["hex"])
        assert len(good_tx.wit.vtxinwit) > 0
        assert len(good_tx.wit.vtxinwit[0].scriptWitness.stack) == 3
        bad_tx = tx_from_hex(signed["hex"])
        bad_tx.wit.vtxinwit[0].scriptWitness.stack = bad_tx.wit.vtxinwit[0].scriptWitness.stack[:2]
        res = n0.testmempoolaccept([bad_tx.serialize().hex()])[0]
        assert_equal(res["allowed"], False)

        self.log.info("Consensus path: malformed witness cannot be mined via generateblock")
        bad_hex = bad_tx.serialize().hex()
        h0 = n0.getblockcount()
        h1 = n1.getblockcount()
        assert_raises_rpc_error(-25, "TestBlockValidity failed", n0.generateblock, n0.getnewaddress(), [bad_hex])
        assert_raises_rpc_error(-25, "TestBlockValidity failed", n1.generateblock, n1.getnewaddress(), [bad_hex])
        assert_equal(n0.getblockcount(), h0)
        assert_equal(n1.getblockcount(), h1)
        self.sync_all()

        self.log.info("XMSS state in wallet DB: spends after restart remain valid (no quantum_wallet.keys)")
        n0.sendtoaddress(n1.getnewaddress(), 0.5)
        self.generate(n0, 1)
        self.sync_all()
        self.restart_node(0)
        assert not (n0.chain_path / "quantum_wallet.keys").exists()
        n0.sendtoaddress(n1.getnewaddress(), 0.3)
        self.generate(n0, 1)
        self.sync_all()


if __name__ == "__main__":
    WalletQuantumFlowTest(__file__).main()
