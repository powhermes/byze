#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""P2P block sync when Byze quantum block signatures are policy-enforced.

BIP152 compact blocks carry only the header and short transaction IDs; the Byze
quantum signature tail is serialized after vtx on the wire and is not present in
cmpctblock reconstruction. With -enforcequantumblocksigs, the node must fall
back to a full block (MSG_BLOCK) instead of accepting a reconstructed block
without the quantum tail. This test ensures two connected peers stay in sync
under that policy (headers + near-tip block relay).
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class HermesQuantumP2PCompactBlockSyncTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [
            ["-enforcequantumblocksigs=1", "-fallbackfee=0.001"],
            ["-enforcequantumblocksigs=1", "-fallbackfee=0.001"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        miner = self.nodes[0]
        self.log.info("Mine past IBD and connect second peer")
        self.generatetoaddress(miner, nblocks=110, address=miner.getnewaddress())
        self.connect_nodes(0, 1)
        self.sync_all()

        self.log.info("Mine additional blocks and verify P2P sync with quantum block policy")
        tip = miner.getblockcount()
        self.generatetoaddress(miner, nblocks=5, address=miner.getnewaddress())
        self.sync_all()
        assert_equal(self.nodes[1].getblockcount(), tip + 5)


if __name__ == "__main__":
    HermesQuantumP2PCompactBlockSyncTest(__file__).main()
