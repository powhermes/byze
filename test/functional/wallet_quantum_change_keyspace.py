#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Change addresses use a quantum key space separate from receive addresses.

Up to v0.2.5 the receive and change descriptors both derived the quantum program of
descriptor index i from (HD master, i), so change index i was the same address,
dual key and 1024-leaf XMSS tree as receive index i: getrawchangeaddress returned
an address already handed out by getnewaddress, change was paid to addresses given
to payers, and both shared one signature budget.

Part 1 (always): a new wallet's change addresses are distinct from its receive
addresses, are reported as change, and use their own XMSS tree.

Part 2 (needs --legacy-bindir pointing at a v0.2.5 build): a wallet created by the
old release, with collided receive/change outputs (some already spent), is loaded by
this release and restored from its mnemonic. Every output stays visible and
spendable, XMSS leaves continue from where the old release stopped, the collided
address is unchanged, new change goes to the separate key space, and the old
release then refuses to open the wallet instead of showing a short balance.
"""

import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

XMSS_SIGNATURE_SIZE = 2500


class WalletQuantumChangeKeyspaceTest(BitcoinTestFramework):
    def add_options(self, parser):
        parser.add_argument("--legacy-bindir", dest="legacy_bindir", default=os.getenv("BYZE_LEGACY_BINDIR"),
                            help="bin directory of a v0.2.5 (shared receive/change key space) build for the upgrade part")

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        # A small lookahead still covers every gap this test creates; restore relies on it.
        self.extra_args = [["-fallbackfee=0.001", "-txindex", "-keypool=4"]] * 2
        self.rpc_timeout = 3600
        self.uses_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def use_binary(self, node, bin_dir):
        """Point node at bin_dir's daemon (None = this build) for its next start."""
        own = node.binaries.node_argv()
        node.args = (self.get_binaries(bin_dir).node_argv() if bin_dir else self.get_binaries().node_argv()) + node.args[len(own):]
        node.binaries = self.get_binaries(bin_dir) if bin_dir else self.get_binaries()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        if self.options.legacy_bindir:
            # Node 0 starts on the old release so that its default wallet is an old-release wallet too.
            self.use_binary(self.nodes[0], self.options.legacy_bindir)
        self.start_nodes()
        for node in self.nodes:
            self.init_wallet(node=node.index)

    # --- helpers -----------------------------------------------------------------
    def leaves(self, node, txid):
        """(prevout address, XMSS leaf index) for every input of txid."""
        out = []
        for vin in node.getrawtransaction(txid, True)["vin"]:
            prev = node.getrawtransaction(vin["txid"], True)["vout"][vin["vout"]]
            sig = bytes.fromhex(vin["txinwitness"][0])
            assert_equal(len(sig), XMSS_SIGNATURE_SIZE)
            out.append((prev["scriptPubKey"]["address"], int.from_bytes(sig[:4], "big")))
        return out

    def spend(self, node, w, utxos, dest, amount, **kwargs):
        txid = w.send(outputs=[{dest: amount}], inputs=[{"txid": u["txid"], "vout": u["vout"]} for u in utxos],
                      add_inputs=False, **kwargs)["txid"]
        self.generate(node, 1)
        return txid

    def change_of(self, w, txid, dest):
        outs = [o["scriptPubKey"]["address"] for o in w.gettransaction(txid, True, True)["decoded"]["vout"]
                if o["scriptPubKey"]["address"] != dest]
        assert_equal(len(outs), 1)
        return outs[0]

    def remaining(self, w, addr):
        return w.getaddressinfo(addr)["quantum_sigs_remaining"]

    def utxo_set(self, w):
        return sorted((u["txid"], u["vout"], u["amount"]) for u in w.listunspent(0))

    # --- tests ---------------------------------------------------------------------
    def run_test(self):
        if self.options.legacy_bindir:
            self.test_upgrade_from_shared_keyspace()
        else:
            self.log.info("Skipping the upgrade part: no --legacy-bindir given")
        self.test_new_wallet()

    def test_new_wallet(self):
        node = self.nodes[0]
        miner = node.get_wallet_rpc(self.default_wallet_name)
        if node.getblockcount() < 110:
            self.generatetoaddress(node, 110, miner.getnewaddress())
        sink = miner.getnewaddress()

        self.log.info("New wallet: receive and change addresses never coincide")
        node.createwallet("fresh")
        w = node.get_wallet_rpc("fresh")
        recv = [w.getnewaddress() for _ in range(3)]
        chg = [w.getrawchangeaddress() for _ in range(3)]
        assert_equal(len(set(recv) | set(chg)), 6)
        for i, (r, c) in enumerate(zip(recv, chg)):
            ri, ci = w.getaddressinfo(r), w.getaddressinfo(c)
            assert_equal((ri["quantum_hd_index"], ri["quantum_key_space"], ri["ischange"]), (i, "receive", False))
            assert_equal((ci["quantum_hd_index"], ci["quantum_key_space"], ci["ischange"]), (i, "change", True))
            assert_equal(ci["solvable"], True)
        assert "quantum_change_domain" in w.getwalletinfo()["flags"]

        self.log.info("Change from a spend lands in the change key space, never on a receive address")
        miner.sendtoaddress(recv[0], 10)
        miner.sendtoaddress(recv[0], 10)
        self.generate(node, 1)
        full = self.remaining(w, recv[0])
        u = [x for x in w.listunspent() if x["address"] == recv[0]][0]
        t1 = self.spend(node, w, [u], sink, 3)
        change = self.change_of(w, t1, sink)
        assert change not in recv
        assert_equal(w.getaddressinfo(change)["quantum_key_space"], "change")
        assert_equal(self.leaves(node, t1), [(recv[0], 0)])
        assert_equal(self.remaining(w, recv[0]), full - 1)
        assert_equal([e["category"] for e in w.listtransactions("*", 100) if e["txid"] == t1], ["send"])
        next_recv = w.getnewaddress()
        assert next_recv != change

        self.log.info("Spending change uses the change address's own XMSS tree, not the receive budget")
        cu = [x for x in w.listunspent() if x["address"] == change][0]
        assert_equal(self.remaining(w, change), full)
        t2 = self.spend(node, w, [cu], sink, 1)
        assert_equal(self.leaves(node, t2), [(change, 0)])
        assert_equal(self.remaining(w, change), full - 1)
        assert_equal(self.remaining(w, recv[0]), full - 1)

        self.log.info("After a reload receive and change still never coincide, and everything is spendable")
        bal = w.getbalance()
        node.unloadwallet("fresh")
        node.loadwallet("fresh")
        assert_equal(w.getbalance(), bal)
        r2, c2 = w.getnewaddress(), w.getrawchangeaddress()
        assert r2 != c2
        assert_equal(w.getaddressinfo(c2)["quantum_key_space"], "change")
        assert_equal(w.getaddressinfo(change)["quantum_key_space"], "change")

        self.log.info("Mnemonic restore finds receive and change-key-space funds")
        mnemonic = w.getrecoveryphrase()["mnemonic"]
        self.sync_all()
        self.nodes[1].restorefrommnemonic("fresh_restored", mnemonic)
        wr = self.nodes[1].get_wallet_rpc("fresh_restored")
        assert_equal(self.utxo_set(wr), self.utxo_set(w))
        # The restore rescan marks the used receive index, so it is not handed out again.
        assert wr.getnewaddress() != recv[0]

        w.sendall([sink])
        self.generate(node, 1)
        assert_equal(w.getbalance(), 0)

    def test_upgrade_from_shared_keyspace(self):
        node = self.nodes[0]
        legacy = self.options.legacy_bindir
        miner = node.get_wallet_rpc(self.default_wallet_name)
        self.generatetoaddress(node, 110, miner.getnewaddress())
        sink = miner.getnewaddress()

        self.log.info("Old release: build a wallet with collided receive/change outputs")
        node.createwallet("legacy")
        w = node.get_wallet_rpc("legacy")
        r0 = w.getnewaddress()
        c0 = w.getrawchangeaddress()
        assert_equal(r0, c0)  # the old release's collision
        r1 = w.getnewaddress()
        for a in (r0, r0, r0, r1):
            miner.sendtoaddress(a, 10)
        self.generate(node, 1)
        # Natural change, then explicit change to c0 (== r0): r0 now holds receive AND change outputs.
        u = [x for x in w.listunspent() if x["address"] == r0]
        t1 = self.spend(node, w, u[:1], sink, 2)
        t2 = self.spend(node, w, u[1:2], sink, 1, change_address=c0)
        old_leaves = self.leaves(node, t1) + self.leaves(node, t2)
        assert_equal(old_leaves, [(r0, 0), (r0, 1)])
        on_r0 = [x for x in w.listunspent() if x["address"] == r0]
        assert_equal(len(on_r0), 2)  # one receive, one change, same address
        old_change = self.change_of(w, t1, sink)
        old_utxos = self.utxo_set(w)
        old_balance = w.getbalance()
        old_remaining = self.remaining(w, r0)
        mnemonic = w.getrecoveryphrase()["mnemonic"]
        self.sync_all()

        self.log.info("Upgrade: reload under this release")
        self.stop_node(0)
        self.use_binary(node, None)
        self.start_node(0)  # the default wallet loads on startup
        self.connect_nodes(0, 1)
        node.loadwallet("legacy")
        miner = node.get_wallet_rpc(self.default_wallet_name)
        w = node.get_wallet_rpc("legacy")
        assert_equal(w.getbalance(), old_balance)
        assert_equal(self.utxo_set(w), old_utxos)
        for a in (r0, r1, old_change):
            info = w.getaddressinfo(a)
            assert_equal((info["ismine"], info["solvable"], info["quantum_key_space"]), (True, True, "receive"))
        assert_equal(w.getaddressinfo(r0)["quantum_hd_index"], 0)
        assert_equal(self.remaining(w, r0), old_remaining)
        assert "quantum_change_domain" not in w.getwalletinfo()["flags"]

        self.log.info("Upgrade: new change goes to the separate key space")
        receive_seen = {r0, r1, old_change}
        new_c = w.getrawchangeaddress()
        assert new_c not in receive_seen
        assert_equal((w.getaddressinfo(new_c)["quantum_key_space"], w.getaddressinfo(new_c)["quantum_hd_index"]), ("change", 0))
        assert "quantum_change_domain" in w.getwalletinfo()["flags"]
        new_r = w.getnewaddress()
        assert new_r != new_c

        self.log.info("Upgrade: the collided address's XMSS leaves continue, no leaf is reused")
        t3 = self.spend(node, w, on_r0, sink, 3)
        assert_equal(sorted(self.leaves(node, t3)), [(r0, 2), (r0, 3)])
        assert_equal(self.remaining(w, r0), old_remaining - 2)
        change3 = self.change_of(w, t3, sink)
        assert change3 not in receive_seen | {new_r}
        assert_equal(w.getaddressinfo(change3)["quantum_key_space"], "change")
        assert_equal(self.leaves(node, self.spend(node, w, [x for x in w.listunspent() if x["address"] == change3], sink, 1)), [(change3, 0)])

        self.log.info("Upgrade: reload again, then mnemonic restore sees old and new outputs")
        node.unloadwallet("legacy")
        node.loadwallet("legacy")
        upgraded_utxos = self.utxo_set(w)
        assert_greater_than(len(upgraded_utxos), 0)
        self.sync_all()
        self.nodes[1].restorefrommnemonic("legacy_restored", mnemonic)
        wr = self.nodes[1].get_wallet_rpc("legacy_restored")
        assert_equal(self.utxo_set(wr), upgraded_utxos)
        assert_equal(wr.getbalance(), w.getbalance())
        # The restored wallet can sign for every one of those outputs. (Not broadcast: its XMSS
        # state restarts at leaf 0, a separate pre-existing restore hazard this test is not about.)
        swept = wr.sendall(recipients=[sink], add_to_wallet=False)
        assert_equal(len(self.nodes[1].decoderawtransaction(swept["hex"])["vin"]), len(upgraded_utxos))
        assert_equal(self.nodes[1].testmempoolaccept([swept["hex"]])[0]["allowed"], True)

        self.log.info("Upgrade: every output is spendable after reload")
        w.sendall([sink])
        self.generate(node, 1)
        assert_equal(w.getbalance(), 0)
        self.sync_all()
        assert_equal(wr.getbalance(), 0)

        self.log.info("Downgrade: the old release refuses the wallet instead of hiding change")
        node.unloadwallet("legacy")
        # The default wallet has made change under this release too, so keep the old release
        # from trying to open it at startup.
        node.unloadwallet(self.default_wallet_name, load_on_startup=False)
        self.stop_node(0)
        self.use_binary(node, legacy)
        self.start_node(0)
        assert_raises_rpc_error(-4, "requires newer version", node.loadwallet, "legacy")
        self.stop_node(0)
        self.use_binary(node, None)
        self.start_node(0)
        self.connect_nodes(0, 1)
        node.loadwallet(self.default_wallet_name, load_on_startup=True)
        assert_greater_than(node.get_wallet_rpc(self.default_wallet_name).getbalance(), 0)


if __name__ == '__main__':
    WalletQuantumChangeKeyspaceTest(__file__).main()
