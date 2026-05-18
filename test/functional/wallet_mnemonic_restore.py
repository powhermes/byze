#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Mnemonic restore reproduces taproot addresses and quantum HD state."""

import shutil
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

    def descriptor_private_strings(self, wallet_rpc):
        descs = wallet_rpc.listdescriptors(private=True)
        return sorted(d["desc"] for d in descs["descriptors"] if d.get("active"))

    def run_test(self):
        miner, peer = self.nodes[0], self.nodes[1]
        w_orig = miner.get_wallet_rpc("default_wallet")

        self.log.info("createwallet -> three addresses -> mnemonic -> restore reproduces exact sequence")
        seq_wallet = miner.createwallet(wallet_name="seq_test")
        assert "mnemonic" in seq_wallet
        w_seq = miner.get_wallet_rpc("seq_test")
        addrs = [w_seq.getnewaddress() for _ in range(3)]
        seq_mnemonic = w_seq.getrecoveryphrase()["mnemonic"]
        seq_xpubs = self.descriptor_xpubs(w_seq)
        miner.unloadwallet("seq_test")
        miner.restorefrommnemonic("seq_restored", seq_mnemonic)
        w_seq_r = miner.get_wallet_rpc("seq_restored")
        assert_equal([w_seq_r.getnewaddress() for _ in range(3)], addrs)
        assert_equal(self.descriptor_xpubs(w_seq_r), seq_xpubs)
        info_seq = w_seq_r.getwalletinfo()
        assert_equal(info_seq["mnemonic_matches_descriptors"], True)
        miner.unloadwallet("seq_restored")

        self.log.info("encryptwallet on funded wallet must not rotate descriptor root or mnemonic")
        miner.createwallet(wallet_name="enc_after_create")
        w_encfix = miner.get_wallet_rpc("enc_after_create")
        addr0 = w_encfix.getnewaddress()
        mnemonic_before = w_encfix.getrecoveryphrase()["mnemonic"]
        priv_before = self.descriptor_private_strings(w_encfix)
        w_encfix.encryptwallet("EncAfterCreatePass")
        w_encfix.walletpassphrase("EncAfterCreatePass", 60)
        assert_equal(w_encfix.getrecoveryphrase()["mnemonic"], mnemonic_before)
        assert_equal(self.descriptor_private_strings(w_encfix), priv_before)
        assert_equal(w_encfix.getwalletinfo()["mnemonic_matches_descriptors"], True)
        miner.unloadwallet("enc_after_create")
        miner.restorefrommnemonic("enc_after_restored", mnemonic_before, "EncAfterCreatePass")
        w_encfix_r = miner.get_wallet_rpc("enc_after_restored")
        with WalletUnlock(w_encfix_r, "EncAfterCreatePass"):
            assert_equal(w_encfix_r.getnewaddress(), addr0)
        miner.unloadwallet("enc_after_restored")

        self.log.info("Create funded wallet and capture mnemonic + descriptors")
        self.generate(miner, 110)
        addr_orig = w_orig.getnewaddress()
        info_orig = w_orig.getwalletinfo()
        assert_equal(info_orig["has_mnemonic"], True)
        assert_equal(info_orig["quantum_hd_derived"], True)
        assert_equal(info_orig["quantum_record_format"], 2)
        xpubs_orig = self.descriptor_xpubs(w_orig)

        self.log.info("createwallet returns mnemonic when unencrypted; encrypted creation references getrecoveryphrase")
        cw_plain = miner.createwallet(wallet_name="cw_plain_show")
        assert "mnemonic" in cw_plain
        assert_equal(len(cw_plain["mnemonic"].split()), 24)
        miner.unloadwallet("cw_plain_show")
        cw_enc = miner.createwallet(wallet_name="cw_enc_show", passphrase="CwEncPass123")
        assert "mnemonic_note" in cw_enc
        miner.unloadwallet("cw_enc_show")

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

        self.log.info("restorefrommnemonic(..., passphrase) must not reinitialize descriptors")
        restore_pass = "RestoreEncNoReinitPass"
        miner.createwallet(wallet_name="solo_enc_src")
        w_solo = miner.get_wallet_rpc("solo_enc_src")
        self.generate(miner, 110)
        solo_addrs = [w_solo.getnewaddress() for _ in range(3)]
        solo_mnemonic = w_solo.getrecoveryphrase()["mnemonic"]
        solo_xpubs = self.descriptor_xpubs(w_solo)
        w_orig.sendtoaddress(solo_addrs[0], Decimal("1"))
        self.generate(miner, 1)
        assert w_solo.getbalance() > 0
        miner.unloadwallet("solo_enc_src")

        with miner.assert_debug_log(
            unexpected_msgs=[
                "SetupDescriptorScriptPubKeyMansFromEntropy: wallet already has keys",
            ],
        ):
            miner.restorefrommnemonic("solo_enc_restored", solo_mnemonic, restore_pass)
        w_solo_r = miner.get_wallet_rpc("solo_enc_restored")
        with WalletUnlock(w_solo_r, restore_pass):
            assert_equal([w_solo_r.getnewaddress() for _ in range(3)], solo_addrs)
            assert_equal(w_solo_r.getrecoveryphrase()["mnemonic"], solo_mnemonic)
        assert_equal(self.descriptor_xpubs(w_solo_r), solo_xpubs)
        assert_equal(w_solo_r.getwalletinfo()["mnemonic_matches_descriptors"], True)
        assert w_solo_r.getbalance() > 0
        miner.unloadwallet("solo_enc_restored")

        self.log.info("encryptwallet reports Byze-appropriate success message")
        miner.createwallet(wallet_name="enc_msg_test")
        w_msg = miner.get_wallet_rpc("enc_msg_test")
        w_msg.getnewaddress()
        msg = w_msg.encryptwallet("EncMsgTestPass")
        assert "recovery phrase remains valid" in msg
        assert "keypool" not in msg.lower()
        assert "hd seed" not in msg.lower()
        miner.unloadwallet("enc_msg_test")

        self.log.info("After deleting wallet.dat directory, restorefrommnemonic recreates the same wallet")
        miner.createwallet(wallet_name="lost_wallet")
        w_lost = miner.get_wallet_rpc("lost_wallet")
        mnemonic_lost = w_lost.getrecoveryphrase()["mnemonic"]
        addr_lost = w_lost.getnewaddress()
        xpubs_lost = self.descriptor_xpubs(w_lost)
        w_orig.sendtoaddress(addr_lost, Decimal("0.5"))
        self.generate(miner, 1)
        miner.unloadwallet("lost_wallet")
        lost_dir = miner.wallets_path / "lost_wallet"
        assert lost_dir.exists()
        shutil.rmtree(lost_dir)
        assert not lost_dir.exists()
        res_lost = miner.restorefrommnemonic("lost_wallet", mnemonic_lost)
        assert_equal(res_lost["name"], "lost_wallet")
        w_back = miner.get_wallet_rpc("lost_wallet")
        assert_equal(w_back.getnewaddress(), addr_lost)
        assert_equal(self.descriptor_xpubs(w_back), xpubs_lost)
        assert w_back.getbalance() > 0

        self.log.info("Unencrypted restore after deleting restored wallet files")
        peer.unloadwallet("restored")
        shutil.rmtree(peer.wallets_path / "restored")
        peer.restorefrommnemonic("restored", mnemonic)
        w_rest2 = peer.get_wallet_rpc("restored")
        assert_equal(w_rest2.getnewaddress(), addr_orig)

        self.log.info("startmining/stopmining solo RPC (distinct from pool / Byze Miner workflows)")
        miner.stopmining()
        sm = miner.startmining(addr_orig, 1)
        assert_equal(sm["success"], True)
        miner.stopmining()

        self.log.info("help lists mnemonic and solo mining RPCs without internal error")
        help_text = miner.help()
        assert "getrecoveryphrase" in help_text
        assert "restorefrommnemonic" in help_text
        assert "createwallet" in help_text
        assert "startmining" in help_text

        hw = miner.helpwallet()
        assert isinstance(hw, str) and len(hw) > 0
        assert "restorefrommnemonic" in hw
        assert "createwallet" in hw

        assert "BIP39" in miner.help("createwallet")
        assert "24-word" in miner.help("restorefrommnemonic")
        assert "getrecoveryphrase" in miner.help("getrecoveryphrase")


if __name__ == "__main__":
    WalletMnemonicRestoreTest(__file__).main()
