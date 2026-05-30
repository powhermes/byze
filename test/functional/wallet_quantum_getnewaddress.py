#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license.

"""Byze descriptor wallets must advance receive index and return distinct quantum addresses."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_not_equal


class WalletQuantumGetNewAddressTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        wallet_name = "quantum_recv"
        node.createwallet(wallet_name)
        w = node.get_wallet_rpc(wallet_name)

        addr0 = w.getnewaddress()
        addr1 = w.getnewaddress()
        assert_not_equal(addr0, addr1)

        descs = w.listdescriptors()
        external = next(d for d in descs["descriptors"] if not d["internal"])
        assert external["next_index"] >= 2, f"expected next_index >= 2, got {external['next_index']}"

        # Labeling a new address must not reuse the same script.
        labeled = w.getnewaddress("pool-miner")
        assert_not_equal(labeled, addr0)
        assert_not_equal(labeled, addr1)


if __name__ == "__main__":
    WalletQuantumGetNewAddressTest(__file__).main()
