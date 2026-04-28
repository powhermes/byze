#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Basic end-to-end wallet flow checks for Byze quantum spends."""

import struct

from test_framework.messages import tx_from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


def read_xmss_index(key_path):
    data = key_path.read_bytes()
    # Byze uses `crypto::quantum_safe_manager::save_dual_keys()` format.
    # Keep backward compatibility with older internal test prototypes that wrote "HERMESQ1".
    if data.startswith(b"HERMESQ1"):
        off = 8

        def read_blob():
            nonlocal off
            ln = struct.unpack("<I", data[off:off + 4])[0]
            off += 4
            blob = data[off:off + ln]
            off += ln
            return blob

        xmss_priv = read_blob()
        assert len(xmss_priv) >= 36, "invalid XMSS private blob length"
        return struct.unpack("<I", xmss_priv[32:36])[0]

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

        self.log.info("XMSS index is monotonic and restart-persistent")
        key_file = n0.chain_path / "quantum_wallet.keys"
        idx_before = read_xmss_index(key_file)

        n0.sendtoaddress(n1.getnewaddress(), 0.5)
        self.generate(n0, 1)
        self.sync_all()
        idx_after = read_xmss_index(key_file)
        assert idx_after > idx_before

        self.restart_node(0)
        idx_restart = read_xmss_index(self.nodes[0].chain_path / "quantum_wallet.keys")
        assert_equal(idx_restart, idx_after)


if __name__ == "__main__":
    WalletQuantumFlowTest(__file__).main()
