#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mine a block that confirms a Taproot/bech32m spend from mempool (SegWit commitment path)."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class WalletTaprootMineTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-fallbackfee=0.001"]] * self.num_nodes
        self.rpc_timeout = 240

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        sender = self.nodes[0]
        receiver = self.nodes[1]
        self.connect_nodes(0, 1)

        sender.createwallet("sender")
        receiver.createwallet("receiver")
        sw = sender.get_wallet_rpc("sender")
        rw = receiver.get_wallet_rpc("receiver")

        fund_addr = sw.getnewaddress()
        self.generatetoaddress(sender, 110, fund_addr, sync_fun=self.no_op)
        self.sync_all()

        dest = rw.getnewaddress()
        assert dest.startswith("byz1") or dest.startswith("bcrt1") or dest.startswith("tb1")

        txid = sw.sendtoaddress(dest, 1.0)
        assert txid in sender.getrawmempool()

        miner_addr = sw.getnewaddress()
        self.generatetoaddress(sender, 1, miner_addr)
        self.sync_all()

        assert self.tx_confirmed(sender, txid)
        assert_equal(rw.getbalances()["mine"]["untrusted_pending"], 0)
        assert rw.getbalances()["mine"]["trusted"] >= 1.0

    def tx_confirmed(self, node, txid):
        for h in range(node.getblockcount(), max(-1, node.getblockcount() - 32), -1):
            if txid in node.getblock(node.getblockhash(h), 1)["tx"]:
                return True
        return False


if __name__ == "__main__":
    WalletTaprootMineTest(__file__).main()
