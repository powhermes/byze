#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""wallet.dat-only recovery, encryption, and multi-wallet isolation for quantum-backed wallets."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet_util import WalletUnlock


class WalletQuantumWalletdatRecoveryTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-fallbackfee=0.001"], ["-fallbackfee=0.001"]]
        # Miner has default_wallet; second node has no wallet until restorewallet.
        self.wallet_names = ["default_wallet", False]
        self.uses_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def assert_no_datadir_quantum_keys(self, node, label):
        legacy = node.chain_path / "quantum_wallet.keys"
        assert not legacy.exists(), (
            f"{label}: datadir must not contain {legacy}; quantum material belongs in wallet.dat"
        )

    def run_test(self):
        miner, fresh = self.nodes[0], self.nodes[1]
        w_miner = miner.get_wallet_rpc("default_wallet")

        self.log.info("Regression: new wallets never create datadir quantum_wallet.keys")
        self.assert_no_datadir_quantum_keys(miner, "miner after init")

        self.log.info("Multi-wallet: additional wallets do not create quantum_wallet.keys")
        miner.createwallet(wallet_name="mw2", load_on_startup=True)
        w2 = miner.get_wallet_rpc("mw2")
        w_miner.getnewaddress()
        w2.getnewaddress()
        self.assert_no_datadir_quantum_keys(miner, "miner after mw2")
        miner.unloadwallet("mw2")

        self.log.info("Mine and fund default_wallet; backup wallet.dat only")
        self.generate(miner, 110)
        recv = w_miner.getnewaddress()
        w_miner.sendtoaddress(recv, Decimal("5"))
        self.generate(miner, 1)
        self.sync_all()

        backup_path = miner.datadir_path / "quantum_walletdat_solo.bak"
        w_miner.backupwallet(str(backup_path))
        self.assert_no_datadir_quantum_keys(miner, "miner after backup")

        self.log.info("Restore backup on a second node that only ever had chain sync (no prior wallet)")
        self.sync_all()
        assert_equal(fresh.getblockchaininfo()["blocks"], miner.getblockchaininfo()["blocks"])

        res = fresh.restorewallet("restored", str(backup_path))
        assert "name" in res
        w_fresh = fresh.get_wallet_rpc("restored")
        assert w_fresh.getbalance() > 0
        self.assert_no_datadir_quantum_keys(fresh, "fresh node after restore")

        self.log.info("Restored wallet.dat can sign and spend without quantum_wallet.keys")
        dest = w_miner.getnewaddress()
        w_fresh.sendtoaddress(dest, Decimal("1"))
        self.generate(miner, 1)
        self.sync_all()
        self.assert_no_datadir_quantum_keys(fresh, "fresh node after spend")

        self.log.info("Encrypted wallet: restart preserves XMSS-capable signing from DB")
        enc_pass = "QuantumEncTestPass"
        w_miner.encryptwallet(enc_pass)
        self.restart_node(0)
        w_miner = miner.get_wallet_rpc("default_wallet")
        self.wait_until(lambda: miner.getblockchaininfo()["blocks"] > 0)
        self.assert_no_datadir_quantum_keys(miner, "miner after encrypt+restart")

        with WalletUnlock(w_miner, enc_pass):
            peer = w_fresh.getnewaddress()
            w_miner.sendtoaddress(peer, Decimal("0.5"))

        self.generate(miner, 1)
        self.sync_all()

        self.restart_node(0)
        w_miner = miner.get_wallet_rpc("default_wallet")
        with WalletUnlock(w_miner, enc_pass):
            w_miner.sendtoaddress(w_fresh.getnewaddress(), Decimal("0.3"))

        self.generate(miner, 1)
        self.sync_all()
        self.assert_no_datadir_quantum_keys(miner, "miner after second encrypted spend cycle")


if __name__ == "__main__":
    WalletQuantumWalletdatRecoveryTest(__file__).main()
