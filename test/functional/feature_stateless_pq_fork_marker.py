#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Byze v0.3 fork marker.

After statelesspq activation every block must set version bit 4. A node still on
pre-v0.3 rules (simulated: activation far away, -blockversion without the bit) can mine
a longer chain, but upgraded nodes reject it and keep their own chain.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

ACTIVATION = 120
MARKER = 1 << 4
NEW = [f"-testactivationheight=statelesspq@{ACTIVATION}"]
OLD = ["-testactivationheight=statelesspq@100000", "-blockversion=536870912"]  # 0x20000000, no marker


class StatelessPQForkMarkerTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.extra_args = [NEW, NEW, OLD]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        n0, n1, old = self.nodes
        a0 = n0.getnewaddress()
        aold = old.getnewaddress()

        self.log.info("Before activation, marker-less blocks are accepted by upgraded nodes")
        self.generatetoaddress(n0, 110, a0)
        self.generatetoaddress(old, 5, aold)
        assert_equal(n0.getblockcount(), 115)
        hdr = n0.getblockheader(n0.getbestblockhash())
        assert_equal(hdr["version"] & MARKER, 0)

        self.log.info("Split: upgraded nodes mine past activation, the old node mines a longer chain alone")
        self.disconnect_nodes(1, 2)
        self.generatetoaddress(n0, 15, a0, sync_fun=lambda: self.sync_blocks([n0, n1]))
        tip = n0.getbestblockhash()
        assert_equal(n0.getblockcount(), 130)
        assert n0.getblockheader(tip)["version"] & MARKER, "upgraded miner must set the marker after activation"
        self.generatetoaddress(old, 40, aold, sync_fun=self.no_op)
        assert_greater_than(old.getblockcount(), n0.getblockcount())

        self.log.info("A marker-less block after activation is rejected")
        for h in range(116, ACTIVATION):  # old node's pre-activation blocks are still valid
            assert_equal(n0.submitblock(old.getblock(old.getblockhash(h), 0)), "inconclusive")
        res = n0.submitblock(old.getblock(old.getblockhash(ACTIVATION), 0))
        assert res and res.startswith("bad-version-statelesspq"), res

        self.log.info("Reconnect: upgraded nodes do not reorg onto the heavier old-software chain")
        self.connect_nodes(1, 2)
        self.generatetoaddress(n0, 1, a0, sync_fun=lambda: self.sync_blocks([n0, n1]))
        assert_equal(n0.getblockcount(), 131)
        assert_equal(n1.getbestblockhash(), n0.getbestblockhash())
        assert_equal(n0.getblockhash(130), tip)
        assert old.getbestblockhash() != n0.getbestblockhash()


if __name__ == "__main__":
    StatelessPQForkMarkerTest(__file__).main()
