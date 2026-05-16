#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mnemonic restore reproduces taproot addresses and quantum HD state."""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.wallet_util import WalletUnlock


class WalletMnemonicRestoreTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-fallbackfee=0.001"], ["-fallbackfee=0.001"]]
        self.rpc_timeout = 3600
        self.wallet_names = ["default_wallet", False]
        self.uses_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def descriptor_xpubs(self, wallet_rpc):
        descs = wallet_rpc.listdescriptors()
        return sorted(d["desc"] for d in descs["descriptors"] if d.get("active"))

    def run_test(self):
        miner, peer = self.nodes[0], self.nodes[1]
        w_orig = miner.get_wallet_rpc("default_wallet")

        self.log.info("Create funded wallet and capture mnemonic + descriptors")
        for _ in range(110):
            self.generate(miner, 1)
        addr_orig = w_orig.getnewaddress()
        info_orig = w_orig.getwalletinfo()
        assert_equal(info_orig["has_mnemonic"], True)
        assert_equal(info_orig["quantum_hd_derived"], True)
        assert_equal(info_orig["quantum_record_format"], 2)
        xpubs_orig = self.descriptor_xpubs(w_orig)

        mnemonic = w_orig.getrecoveryphrase()["mnemonic"]
        assert_equal(len(mnemonic.split()), 24)

        w_orig.sendtoaddress(addr_orig, Decimal("2"))
        self.generate(miner, 1)
        self.sync_all()

        self.log.info("Restore on second node from mnemonic; addresses and descriptors match")
        self.sync_all()
        res = peer.restorefrommnemonic("restored", mnemonic)
        assert_equal(res["name"], "restored")
        assert_equal(res["quantum_hd_derived"], True)
        assert_equal(res["has_mnemonic"], True)

        w_rest = peer.get_wallet_rpc("restored")
        assert_equal(w_rest.getnewaddress(), addr_orig)
        assert_equal(self.descriptor_xpubs(w_rest), xpubs_orig)
        info_rest = w_rest.getwalletinfo()
        assert_equal(info_rest["quantum_hd_derived"], True)
        assert_equal(info_rest["quantum_record_format"], 2)
        assert w_rest.getbalance() > 0

        self.log.info("Encrypted mnemonic restore reproduces state after unlock")
        enc_pass = "MnemonicRestoreEncTest"
        peer.createwallet(wallet_name="enc_src", passphrase=enc_pass)
        w_enc = peer.get_wallet_rpc("enc_src")
        with WalletUnlock(w_enc, enc_pass):
            enc_mnemonic = w_enc.getrecoveryphrase()["mnemonic"]
            enc_addr = w_enc.getnewaddress()
            enc_xpubs = self.descriptor_xpubs(w_enc)
        peer.unloadwallet("enc_src")

        peer.restorefrommnemonic("enc_restored", enc_mnemonic, enc_pass)
        w_enc_r = peer.get_wallet_rpc("enc_restored")
        with WalletUnlock(w_enc_r, enc_pass):
            assert_equal(w_enc_r.getrecoveryphrase()["mnemonic"], enc_mnemonic)
            assert_equal(w_enc_r.getnewaddress(), enc_addr)
            assert_equal(self.descriptor_xpubs(w_enc_r), enc_xpubs)
        info_enc = w_enc_r.getwalletinfo()
        assert_equal(info_enc["quantum_secrets_encrypted"], True)
        assert_equal(info_enc["quantum_hd_derived"], True)
        assert_equal(info_enc["has_mnemonic"], True)

        self.log.info("help lists mnemonic RPCs without internal error")
        help_text = miner.help()
        assert "getrecoveryphrase" in help_text
        assert "restorefrommnemonic" in help_text


if __name__ == "__main__":
    WalletMnemonicRestoreTest(__file__).main()
