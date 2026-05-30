#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Quantum-only wallet invariant checks."""

from test_framework.messages import tx_from_hex
from test_framework.descriptors import descsum_create
from test_framework.script import CScript, OP_0
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


NON_QUANTUM_WPKH_DESCRIPTOR = descsum_create("wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)")


class WalletQuantumInvariantsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-fallbackfee=0.001"]]
        self.rpc_timeout = 3600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        addr = node.getnewaddress()
        for _ in range(110):
            self.generatetoaddress(node, 1, addr)

        self.log.info("Reject funding raw tx with valid non-quantum witness v0 output")
        raw = node.createrawtransaction([], {node.getnewaddress(): 1.0})
        tx = tx_from_hex(raw)
        tx.vout[0].scriptPubKey = CScript([OP_0, bytes.fromhex("11" * 20)])
        assert_raises_rpc_error(
            -5,
            "Byze wallet sends support only bech32m witness v1 destinations",
            node.fundrawtransaction,
            tx.serialize().hex(),
        )

        self.log.info("Reject non-quantum descriptors being activated")
        node.createwallet(wallet_name="watch", disable_private_keys=True)
        watch = node.get_wallet_rpc("watch")
        res = watch.importdescriptors([{
            "desc": NON_QUANTUM_WPKH_DESCRIPTOR,
            "active": True,
            "timestamp": "now",
        }])
        assert_equal(res[0]["success"], False)
        assert "error" in res[0]
        err = res[0]["error"]
        msg = err["message"] if isinstance(err, dict) else str(err)
        assert msg
        assert "bech32m" in msg.lower(), f"expected 'bech32m' in importdescriptors error, got: {msg}"


if __name__ == "__main__":
    WalletQuantumInvariantsTest(__file__).main()
