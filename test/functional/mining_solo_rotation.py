#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""In-process solo miner: payout-address rotation and start/stop serialisation.

A Byze XMSS key signs at most 1,024 inputs, so a mining address must not collect more
coinbase outputs than that. startmining takes several comma-separated addresses and pays
blocks_per_address found blocks to each in turn.

A second startmining sent while the first is still building its RandomX dataset used to
create a second mining context and crash the node (seen on mainnet 2026-11-16). It must be
refused and the node must keep running.
"""

import threading

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, get_rpc_proxy

ALREADY = "Mining is already active or starting"


class MiningSoloRotationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        a = node.getnewaddress()
        b = node.getnewaddress()

        self.log.info("blocks_per_address bounds")
        assert_raises_rpc_error(-8, "between 1 and 1000", node.startmining, a, 1, 0)
        assert_raises_rpc_error(-8, "between 1 and 1000", node.startmining, a, 1, 1001)
        assert_raises_rpc_error(-5, "Invalid mining payout address", node.startmining, "notanaddress", 1)
        assert_raises_rpc_error(-5, "Invalid mining payout address", node.startmining, f"{a},notanaddress", 1)

        self.log.info("Two concurrent startmining calls: one starts, the other is refused, node survives")
        results = []

        def call():
            proxy = get_rpc_proxy(node.url, 0, timeout=600, coveragedir=self.options.coveragedir)
            try:
                results.append(("ok", proxy.startmining(f"{a},{b}", 1, 2)))
            except Exception as e:  # JSONRPCException
                results.append(("err", str(e)))

        threads = [threading.Thread(target=call) for _ in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        oks = [r for r in results if r[0] == "ok"]
        errs = [r for r in results if r[0] == "err"]
        assert_equal(len(oks), 1)
        assert_equal(len(errs), 1)
        assert ALREADY in errs[0][1], errs[0][1]
        assert_equal(oks[0][1]["addresses"], 2)
        assert_equal(oks[0][1]["blocks_per_address"], 2)
        assert_raises_rpc_error(-1, ALREADY, node.startmining, a, 1)

        self.log.info("Rotation: 2 blocks to the first address, then the second")
        self.wait_until(lambda: node.getmininginfo().get("blocks_found", 0) >= 4, timeout=600)
        info = node.getmininginfo()
        assert_equal(info["mining_address"], b)
        assert_equal(info["addresses_remaining"], 0)
        assert_equal(info["blocks_per_address"], 2)
        node.stopmining()
        assert_equal(node.getmininginfo()["mining_active"], False)

        paid = {}
        for h in range(1, node.getblockcount() + 1):
            cb = node.getblock(node.getblockhash(h), 2)["tx"][0]
            for out in cb["vout"]:
                addr = out["scriptPubKey"].get("address")
                if addr and out["value"] > 0:
                    paid[addr] = paid.get(addr, 0) + 1
        self.log.info(f"coinbase outputs per address: {paid}")
        # A block found on stale work in the instant of rotation may still pay the old address.
        assert 2 <= paid.get(a, 0) <= 3, paid
        assert paid.get(b, 0) >= 1, paid

        self.log.info("stopmining then startmining again works")
        node.startmining(b, 1)
        assert_equal(node.getmininginfo()["mining_active"], True)
        node.stopmining()


if __name__ == "__main__":
    MiningSoloRotationTest(__file__).main()
