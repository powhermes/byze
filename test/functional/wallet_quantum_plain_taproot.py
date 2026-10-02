#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Plain tr() outputs derived from the wallet's own descriptor are unspendable on Byze.

Consensus only lets a 32-byte witness-v1 output be spent with a quantum dual-key witness
whose bundle hashes to the program, so the plain BIP86 key of the wallet's tr() descriptor
(e.g. from deriveaddresses) can never be spent. The wallet must:
  - not count such outputs in trusted/untrusted/immature balances, nor list or select them,
    but report them under getbalances()["mine"]["unspendable"];
  - keep counting and spending its real (getnewaddress) quantum outputs;
  - refuse every mining payout entry point (startmining, generatetoaddress,
    generatetodescriptor, generateblock) for such an address, also while locked;
  - still accept external witness-v1 addresses it knows nothing about.
"""

import os

from test_framework.segwit_addr import encode_segwit_address
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

SECP256K1_P = 2**256 - 2**32 - 977
PLAIN_TR_ERROR = "plain (non-quantum) taproot key"


def is_valid_xonly(program):
    x = int.from_bytes(program, "big")
    if x >= SECP256K1_P:
        return False
    y2 = (pow(x, 3, SECP256K1_P) + 7) % SECP256K1_P
    return pow(y2, (SECP256K1_P - 1) // 2, SECP256K1_P) == 1


class WalletQuantumPlainTaprootTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-fallbackfee=0.001"]]
        self.rpc_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def reload(self, name):
        node = self.nodes[0]
        node.unloadwallet(name)
        node.loadwallet(name)
        return node.get_wallet_rpc(name)

    def check_balances(self, w, trusted, unspendable, plain_addrs):
        bal = w.getbalances()["mine"]
        assert_equal(bal["trusted"], trusted)
        assert_equal(bal["untrusted_pending"], 0)
        assert_equal(bal["immature"], 0)
        assert_equal(bal.get("unspendable", 0), unspendable)
        assert_equal(w.getbalance(), trusted)
        utxos = w.listunspent()
        assert not [u for u in utxos if u["address"] in plain_addrs], utxos
        assert_equal(sum(u["amount"] for u in utxos), trusted)

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("funder")
        funder = node.get_wallet_rpc("funder")
        funder_addr = funder.getnewaddress()
        self.generatetoaddress(node, 110, funder_addr)

        node.createwallet("w")
        w = node.get_wallet_rpc("w")
        quantum_addr = w.getnewaddress()

        tr_desc = [d["desc"] for d in w.listdescriptors()["descriptors"]
                   if d["active"] and not d["internal"] and d["desc"].startswith("tr(")]
        assert_equal(len(tr_desc), 1)
        plain_addrs = node.deriveaddresses(tr_desc[0], [0, 2])
        assert quantum_addr not in plain_addrs
        plain = plain_addrs[0]

        # A wallet reload rebuilds the descriptor script cache (SetCache), which registers the
        # plain tr() scripts: this is how real wallets came to report them as spendable.
        w = self.reload("w")

        self.log.info("getaddressinfo: plain tr() is unspendable, quantum address is a quantum_program")
        info = w.getaddressinfo(plain)
        assert info["ismine"]
        assert_equal(info.get("unspendable"), True)
        assert_equal(info["solvable"], False)
        assert "desc" not in info and "quantum_sigs_remaining" not in info
        qinfo = w.getaddressinfo(quantum_addr)
        assert_equal(qinfo["desc"], f"quantum_program({qinfo['witness_program']})")
        assert "unspendable" not in qinfo

        self.log.info("Payments to plain tr() are not spendable balance; quantum payments are")
        funder.sendtoaddress(quantum_addr, 5)
        funder.sendtoaddress(plain, 7)
        self.generatetoaddress(node, 1, funder_addr)
        self.check_balances(w, 5, 7, plain_addrs)

        self.log.info("Spending selects only the quantum coin")
        w.sendtoaddress(funder_addr, 4)
        self.generatetoaddress(node, 1, funder_addr)
        trusted = w.getbalances()["mine"]["trusted"]
        assert 0 < trusted < 1, trusted
        self.check_balances(w, trusted, 7, plain_addrs)

        self.log.info("Same after reload and full rescan (existing wallet.dat)")
        w = self.reload("w")
        self.check_balances(w, trusted, 7, plain_addrs)
        w.rescanblockchain()
        self.check_balances(w, trusted, 7, plain_addrs)

        self.log.info("Mining entry points refuse the wallet's plain tr() address")
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, node.startmining, plain, 1)
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, self.generatetoaddress, node, 1, plain, sync_fun=self.no_op)
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, self.generatetodescriptor, node, 1, f"addr({plain})", sync_fun=self.no_op)
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, self.generateblock, node, plain, [], sync_fun=self.no_op)
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, self.generatetoaddress, node, 1, plain_addrs[1], sync_fun=self.no_op)
        assert_equal(node.getminingstatus()["active"], False)

        self.log.info("... but accept quantum addresses and unknown external witness-v1 addresses")
        self.generatetoaddress(node, 1, quantum_addr)
        hrp = quantum_addr[:quantum_addr.rindex("1")]
        while True:
            program = os.urandom(32)
            if is_valid_xonly(program):
                break
        external = encode_segwit_address(hrp, 1, program)
        self.generatetoaddress(node, 1, external)
        res = node.startmining(quantum_addr, 1)
        assert_equal(res["success"], True)
        node.stopmining()

        self.log.info("Refusal also works while the wallet is locked")
        w.encryptwallet("pass")
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, node.startmining, plain, 1)
        assert_raises_rpc_error(-5, PLAIN_TR_ERROR, self.generatetoaddress, node, 1, plain, sync_fun=self.no_op)
        assert_equal(w.getaddressinfo(plain).get("unspendable"), True)


if __name__ == "__main__":
    WalletQuantumPlainTaprootTest(__file__).main()
