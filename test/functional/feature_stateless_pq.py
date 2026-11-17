#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Byze v0.3 stateless addresses (witness v2, ML-DSA-44 + SPHINCS+) and legacy coin migration.

- Before activation, witness-v2 outputs and migration spends are not accepted.
- After activation a wallet can receive at a stateless address, spend from it many times
  (no XMSS signature counter), and send change back to it.
- migratequantumcoins moves legacy (XMSS) coins with SPHINCS+ only, into a stateless address.
- Stateless addresses survive a restart and are found again after a recovery-phrase restore.
"""

from decimal import Decimal

from test_framework.address import address_to_scriptpubkey
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than

ACTIVATION = 200
ARGS = [f"-testactivationheight=statelesspq@{ACTIVATION}", "-fallbackfee=0.0002"]


class StatelessPQTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [ARGS, ARGS]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        n0, n1 = self.nodes
        legacy = [n0.getnewaddress() for _ in range(3)]
        for a in legacy:
            self.generatetoaddress(n0, 50, a)  # height 150, 50 mature coinbases

        z = n0.getnewstatelessaddress("z")
        self.log.info(f"stateless address {z}")
        assert z.startswith("bcrt1z"), z
        assert n0.getaddressinfo(z)["ismine"]
        z_spk = address_to_scriptpubkey(z)

        self.log.info("Before activation: no witness-v2 outputs, no migration spends")
        assert_greater_than(ACTIVATION - 1, n0.getblockcount())
        pre = n0.send(outputs=[{z: 1}], options={"add_to_wallet": False})
        res = n0.testmempoolaccept([pre["hex"]])[0]
        assert not res["allowed"]
        mig = n0.migratequantumcoins("", 2, 2, False)
        res = n0.testmempoolaccept([mig["hex"]])[0]
        assert not res["allowed"], res

        self.log.info("Activate statelesspq")
        self.generatetoaddress(n0, ACTIVATION - 1 - n0.getblockcount(), legacy[0])

        self.log.info("Receive at the stateless address")
        txid = n0.sendtoaddress(z, 10)
        assert txid in n0.getrawmempool()
        self.generatetoaddress(n0, 1, legacy[0])
        assert_equal(n0.getreceivedbyaddress(z), Decimal("10"))
        u = [x for x in n0.listunspent(addresses=[z])]
        assert_equal(len(u), 1)
        assert u[0]["spendable"] and u[0]["solvable"], u[0]

        self.log.info("Fan out to 30 outputs at the same stateless address (signed by the wallet)")
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(u[0]["txid"], 16), u[0]["vout"]))]
        tx.vout = [CTxOut(int(Decimal("0.3") * COIN), z_spk) for _ in range(30)]
        tx.vout.append(CTxOut(int(Decimal("0.99") * COIN), z_spk))
        signed = n0.signrawtransactionwithwallet(tx.serialize().hex())
        assert signed["complete"], signed
        n0.sendrawtransaction(signed["hex"])
        self.generatetoaddress(n0, 1, legacy[0])
        assert_equal(len(n0.listunspent(addresses=[z])), 31)

        self.log.info("Spend all 31 coins of one stateless address, change back to the same address")
        dest = n1.getnewaddress()
        utxos = n0.listunspent(addresses=[z])
        for i in range(0, len(utxos), 10):
            batch = utxos[i:i + 10]
            total = sum(x["amount"] for x in batch)
            res = n0.send(outputs=[{dest: total / 2}],
                          options={"inputs": [{"txid": x["txid"], "vout": x["vout"]} for x in batch],
                                   "add_inputs": False, "change_address": z})
            assert res["complete"], res
            self.generatetoaddress(n0, 1, legacy[0])
        assert_equal(len(n0.listunspent(addresses=[z])), 4)  # one change output per batch
        self.sync_all()
        assert_greater_than(n1.getbalance(), Decimal("4"))

        self.log.info("Migrate legacy XMSS coins with SPHINCS+ only, into a stateless address")
        legacy_before = len([x for x in n0.listunspent() if x["address"] in legacy])
        m = n0.migratequantumcoins("", 5)
        assert_equal(m["inputs"], 5)
        assert m["destination"].startswith("bcrt1z")
        self.generatetoaddress(n0, 1, legacy[1])
        mtx = n0.decoderawtransaction(n0.gettransaction(m["txid"])["hex"])
        assert_equal(len(mtx["vout"]), 1)
        assert_equal(mtx["vout"][0]["scriptPubKey"]["address"], m["destination"])
        for vin in mtx["vin"]:
            assert_equal(vin["txinwitness"][0], "")  # no XMSS signature used
        assert_equal(n0.getreceivedbyaddress(m["destination"]), Decimal(m["amount"]))
        moved = {(v["txid"], v["vout"]) for v in mtx["vin"]}
        still = {(x["txid"], x["vout"]) for x in n0.listunspent()}
        assert not (moved & still), "migrated coins are still unspent"
        assert_greater_than(legacy_before, 5)
        assert_greater_than(m["remaining"], 0)  # all other legacy coins (incl. legacy change) stay

        self.log.info("Stateless addresses persist across a restart")
        z_balance = sum(x["amount"] for x in n0.listunspent(addresses=[z]))
        self.restart_node(0, extra_args=ARGS)
        n0 = self.nodes[0]
        assert n0.getaddressinfo(z)["ismine"]
        assert_equal(sum(x["amount"] for x in n0.listunspent(addresses=[z])), z_balance)

        self.log.info("A recovery-phrase restore finds the stateless coins")
        phrase = n0.getrecoveryphrase()["mnemonic"]
        self.connect_nodes(0, 1)
        self.sync_all()
        n1.restorefrommnemonic("restored", phrase)
        w = n1.get_wallet_rpc("restored")
        assert_equal(sum(x["amount"] for x in w.listunspent(addresses=[z])), z_balance)
        assert w.getaddressinfo(m["destination"])["ismine"]


if __name__ == "__main__":
    StatelessPQTest(__file__).main()
