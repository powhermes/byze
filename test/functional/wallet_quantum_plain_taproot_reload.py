#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Plain tr() safety across the wallet lifecycle: fresh, importdescriptors, reload, restart, rescan.

Byze consensus can never spend a witness-v1 output whose program is a plain BIP86 key, so the
wallet's own plain tr() expansions (e.g. deriveaddresses on its tr() descriptors) must at every
stage:
  1. be refused by startmining, generatetoaddress, generatetodescriptor and generateblock
     (rc3 only refused them once a wallet LOAD had registered them, so a wallet created or
     re-imported in this session was unprotected);
  2. leave quantum (getnewaddress) and external witness-v1 addresses minable;
  3. stay out of balances, listunspent and coin selection, also as a forced input;
  4. never be signed: the wallet signer refuses them as non-quantum inputs (IsQuantumMine()
     used to match them after a reload).
Also checked: keypool growth (keypoolrefill, getnewaddress
beyond the lookahead), an encrypted wallet locked across a reload, top-up while locked (larger
-keypool at load) and top-up after unlocking, always including the plain expansion at the end
of each descriptor's topped-up range. Every accepted mining call is rolled back with
invalidateblock so the chain (and so balances) evolve identically on builds with and without
the guard; a JSON dump of balances, address sequences and ismine/getaddressinfo results is
written to <tmpdir>/observations.json for comparing builds.
"""

import hashlib
import json
import time

from test_framework.authproxy import JSONRPCException
from test_framework.segwit_addr import encode_segwit_address
from test_framework.test_framework import BitcoinTestFramework

PLAIN_TR_ERROR = "plain (non-quantum) taproot key"
NON_QUANTUM_SIGN_ERROR = "refuses to sign non-quantum inputs"
SECP256K1_P = 2**256 - 2**32 - 977
# BIP39 test vectors (entropy 0x7f.. and 0x80..)
MNEMONIC_D = ("legal winner thank year wave sausage worth useful legal winner thank year "
              "wave sausage worth useful legal winner thank year wave sausage worth title")
MNEMONIC_E = ("letter advice cage absurd amount doctor acoustic avoid letter advice cage absurd "
              "amount doctor acoustic avoid letter advice cage absurd amount doctor acoustic bless")
MINING_RPCS = ("startmining", "generatetoaddress", "generatetodescriptor", "generateblock")


def is_on_curve(program):
    x = int.from_bytes(program, "big")
    if x >= SECP256K1_P:
        return False
    y2 = (pow(x, 3, SECP256K1_P) + 7) % SECP256K1_P
    return pow(y2, (SECP256K1_P - 1) // 2, SECP256K1_P) == 1


def deterministic_program(tag, on_curve):
    i = 0
    while True:
        program = hashlib.sha256(f"{tag}{i}".encode()).digest()
        if is_on_curve(program) == on_curve:
            return program
        i += 1


def err(e):
    return f"ERR {e.error['code']}: {e.error['message']}"


class WalletQuantumPlainTaprootReloadTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [["-fallbackfee=0.001"]]
        self.rpc_timeout = 600

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def call(self, fn, *args, **kwargs):
        try:
            return fn(*args, **kwargs)
        except JSONRPCException as e:
            return err(e)

    def mine(self, addr, startmining=True):
        """Try every mining RPC; roll back any block it mined so all builds see the same chain."""
        node = self.nodes[0]
        res = {}
        for rpc in MINING_RPCS:
            if rpc == "startmining" and not startmining:
                continue
            h0 = node.getblockcount()
            # A fresh timestamp per attempt: a rolled-back block must not be rebuilt bit-identical
            # (it would be rejected as already-invalid).
            self.bump_time()
            try:
                if rpc == "generatetoaddress":
                    self.generatetoaddress(node, 1, addr, sync_fun=self.no_op)
                elif rpc == "generatetodescriptor":
                    self.generatetodescriptor(node, 1, f"addr({addr})", sync_fun=self.no_op)
                elif rpc == "generateblock":
                    self.generateblock(node, addr, [], sync_fun=self.no_op)
                else:
                    # Accepted startmining initialises the miner (slow); stop it at once.
                    node.startmining(addr, 1)
                    node.stopmining()
                res[rpc] = "accepted"
            except JSONRPCException as e:
                res[rpc] = "refused" if PLAIN_TR_ERROR in e.error["message"] else err(e)
            if node.getblockcount() > h0:
                node.invalidateblock(node.getblockhash(h0 + 1))
            assert not node.getminingstatus()["active"]
        return res

    def bump_time(self):
        node = self.nodes[0]
        # Strictly increasing and above the tip, so the block time (max(MTP + 1, now)) is new.
        self.now = max(self.now, node.getblockheader(node.getbestblockhash())["time"]) + 1
        node.setmocktime(self.now)

    def plain_addrs(self, w):
        descs = w.listdescriptors()["descriptors"]
        ext = [d["desc"] for d in descs if d["active"] and not d["internal"] and d["desc"].startswith("tr(")][0]
        intl = [d["desc"] for d in descs if d["active"] and d["internal"] and d["desc"].startswith("tr(")][0]
        node = self.nodes[0]
        e = node.deriveaddresses(ext, [0, 3])
        i = node.deriveaddresses(intl, [0, 3])
        return {"plain_ext0": e[0], "plain_ext1": e[1], "plain_int0": i[0]}, e, i

    def mining_matrix(self, plain, quantum, externals_startmining):
        m = {}
        for n, a in plain.items():
            m[n] = self.mine(a)
        m["quantum"] = self.mine(quantum)
        for n, a in self.externals.items():
            m[n] = self.mine(a, startmining=externals_startmining)
        return m

    def wallet_probe(self, w, locked=False):
        """Balances, listunspent, coin selection, forced inputs and signing for wallet d."""
        node = self.nodes[0]
        o = {}
        bal = w.getbalances()["mine"]
        o["getbalances"] = {k: str(v) for k, v in sorted(bal.items())}
        o["getbalance"] = str(w.getbalance())
        utxos = w.listunspent(0)
        o["listunspent"] = sorted((self.names.get(u["address"], "other"), str(u["amount"])) for u in utxos)
        o["listunspent_total_matches"] = sum(u["amount"] for u in utxos) == bal["trusted"] + bal["untrusted_pending"]
        T = bal["trusted"] + bal["untrusted_pending"]
        sink = self.sink
        o["walletcreatefundedpsbt(T+1)"] = self.call(w.walletcreatefundedpsbt, [], {sink: T + 1})
        if not locked:
            o["sendtoaddress(T+1)"] = self.call(w.sendtoaddress, sink, T + 1)
            o["send(T+1)"] = self.call(w.send, {sink: T + 1}, options={"add_to_wallet": False})
            r = self.call(w.sendall, [sink], options={"add_to_wallet": False})
            if isinstance(r, dict):
                vin = {(i["txid"], i["vout"]) for i in node.decoderawtransaction(r["hex"])["vin"]}
                o["sendall_uses_plain"] = any((p["txid"], p["vout"]) in vin for p in self.plain_ops.values())
            else:
                o["sendall_uses_plain"] = r
        for pn, p in self.plain_ops.items():
            op = [{"txid": p["txid"], "vout": p["vout"]}]
            amt = p["amount"] / 2  # well within the input, so only refusal can fail it
            o[f"walletcreatefundedpsbt(preset {pn})"] = self.call(w.walletcreatefundedpsbt, op, {sink: amt}, 0, {"add_inputs": False})
            raw = node.createrawtransaction(op, {sink: amt})
            o[f"fundrawtransaction(preset {pn})"] = self.call(w.fundrawtransaction, raw, {"add_inputs": False})
            if locked:
                continue
            o[f"send(preset {pn})"] = self.call(w.send, {sink: amt}, options={"inputs": op, "add_inputs": False, "add_to_wallet": False})
            prev = [{"txid": p["txid"], "vout": p["vout"], "scriptPubKey": p["spk"], "amount": p["amount"]}]
            r = self.call(w.signrawtransactionwithwallet, raw, prev)
            if isinstance(r, dict):
                tma = node.testmempoolaccept([r["hex"]])[0]
                o[f"signraw({pn})"] = {"complete": r["complete"], "errors": sorted({e["error"] for e in r.get("errors", [])}),
                                       "mempool_allowed": tma["allowed"]}
            else:
                o[f"signraw({pn})"] = r
        return o

    def ismine_dump(self, w):
        """Every address the collision behaviour could touch: receive/change sequence and plain expansions."""
        dump = {}
        for a in self.receive_seq + self.change_seq + self.plain_ext_all + self.plain_int_all:
            info = w.getaddressinfo(a)
            d = {k: info.get(k) for k in ("ismine", "solvable", "unspendable", "iswatchonly", "ischange", "quantum_hd_index", "parent_desc")}
            d["desc"] = info["desc"].split("(")[0] if "desc" in info else None
            dump[a] = d
        dump["descriptors"] = sorted((d["desc"], d["active"], d.get("internal"), tuple(d.get("range", [])), d.get("next_index", d.get("next")))
                                     for d in w.listdescriptors()["descriptors"])
        return dump

    def stage(self, name, extra_wallets=(), externals_startmining=False, locked=False):
        node = self.nodes[0]
        w = node.get_wallet_rpc("d")
        self.log.info(f"=== stage {name} ===")
        # Also the plain expansions at the END of each descriptor's current topped-up range.
        plain = dict(self.d_plain)
        ranges = {}
        for x in w.listdescriptors()["descriptors"]:
            if x["active"] and x["desc"].startswith("tr("):
                kind = "int" if x["internal"] else "ext"
                plain[f"plain_{kind}_last"] = node.deriveaddresses(x["desc"], [x["range"][1], x["range"][1]])[0]
                ranges[kind] = x["range"]
        s = {"ranges": ranges, "mining": {"d": self.mining_matrix(plain, self.d_quantum, externals_startmining)}}
        for wn, plain, quantum in extra_wallets:
            s["mining"][wn] = self.mining_matrix(plain, quantum, False)
        s["wallet"] = self.wallet_probe(w, locked=locked)
        s["ismine"] = self.ismine_dump(w)
        if not locked:
            # Address generation continues identically at every stage.
            r, c = w.getnewaddress(), w.getrawchangeaddress()
            self.receive_seq.append(r)
            self.change_seq.append(c)
            s["new_addresses"] = [r, c]
        self.obs[name] = s
        self.log.info(f"[{name}] " + json.dumps({k: s[k] for k in ("mining", "wallet")}, default=str))

    def check(self, name):
        """Per-stage verdict for checks 1-4, 7, 8."""
        s = self.obs[name]
        res = {}
        bad1 = [f"{wn}.{n}.{r}={v}" for wn, m in s["mining"].items() for n, rr in m.items() if n.startswith("plain") for r, v in rr.items() if v != "refused"]
        bad2 = [f"{wn}.quantum.{r}={v}" for wn, m in s["mining"].items() for r, v in m["quantum"].items() if v != "accepted"]
        bad7 = [f"{wn}.{n}.{r}={v}" for wn, m in s["mining"].items() for n, rr in m.items() if n.startswith("external") for r, v in rr.items() if v != "accepted"]
        o = s["wallet"]
        bad3 = []
        if any(n.startswith("plain") for n, _ in o["listunspent"]):
            bad3.append("plain in listunspent")
        if not o["listunspent_total_matches"]:
            bad3.append("balance != listunspent")
        bad4 = []
        for k, v in o.items():
            if k.endswith("(T+1)") and not (isinstance(v, str) and "Insufficient funds" in v):
                bad4.append(f"{k}: {v}")
            if k == "sendall_uses_plain" and v is True:
                bad4.append("sendall selected a plain input")
            # A plain UTXO the wallet does not know is an "external" preset input, which funding may
            # wrap into an unsigned transaction (same on rc3); it must never come out signed.
            if "(preset" in k and isinstance(v, dict) and v.get("complete"):
                bad4.append(f"{k} complete")
            if k.startswith("signraw("):
                if not isinstance(v, dict) or v["complete"] or v["mempool_allowed"] or NON_QUANTUM_SIGN_ERROR not in " ".join(v["errors"]):
                    bad4.append(f"{k}: {v}")
        res["1_plain_refused_by_all_4"] = bad1
        res["2_quantum_accepted_by_all_4"] = bad2
        res["3_balances_listunspent_exclude_plain"] = bad3
        res["4_selection_forced_inputs_signing_refuse_plain"] = bad4
        res["7_external_on/off_curve_accepted"] = bad7
        return res

    def run_test(self):
        node = self.nodes[0]
        self.obs = {}
        self.now = int(time.time())
        node.setmocktime(self.now)
        node.createwallet("funder")
        funder = node.get_wallet_rpc("funder")
        self.sink = funder.getnewaddress()
        self.generatetoaddress(node, 110, self.sink)
        hrp = self.sink[:self.sink.rindex("1")]
        self.externals = {"external_oncurve": encode_segwit_address(hrp, 1, deterministic_program("byze-ext-on", True)),
                          "external_offcurve": encode_segwit_address(hrp, 1, deterministic_program("byze-ext-off", False))}

        self.log.info("Deterministic wallet d (fixed mnemonic): receive/change sequence, plain expansions")
        node.restorefrommnemonic("d", MNEMONIC_D)
        d = node.get_wallet_rpc("d")
        self.receive_seq = [d.getnewaddress() for _ in range(3)]
        self.change_seq = [d.getrawchangeaddress() for _ in range(2)]
        self.d_plain, self.plain_ext_all, self.plain_int_all = self.plain_addrs(d)
        self.d_quantum = self.receive_seq[0]
        self.names = {a: f"receive{i}" for i, a in enumerate(self.receive_seq)}
        self.names.update({a: f"change{i}" for i, a in enumerate(self.change_seq)})
        self.names.update({a: f"plain_ext{i}" for i, a in enumerate(self.plain_ext_all)})
        self.names.update({a: f"plain_int{i}" for i, a in enumerate(self.plain_int_all)})

        self.log.info("A createwallet wallet w and an encrypted (locked) restored wallet e")
        node.createwallet("w")
        w = node.get_wallet_rpc("w")
        w_quantum = w.getnewaddress()
        w.getnewaddress()
        w_plain = self.plain_addrs(w)[0]
        node.restorefrommnemonic("e", MNEMONIC_E, "pass")
        e = node.get_wallet_rpc("e")
        e.walletpassphrase("pass", 600)
        e_quantum = e.getnewaddress()
        e.getnewaddress()
        e.walletlock()
        e_plain = self.plain_addrs(e)[0]

        self.log.info("Fund d: coinbase to a quantum address, payments to quantum and plain addresses")
        self.generatetoaddress(node, 1, self.receive_seq[1])
        self.generatetoaddress(node, 101, self.sink)
        self.plain_ops = {}
        funder.sendtoaddress(self.receive_seq[2], 5)
        for n, amt in (("plain_ext0", 7), ("plain_int0", 3)):
            txid = funder.sendtoaddress(self.d_plain[n], amt)
            dec = funder.gettransaction(txid, True, True)["decoded"]
            vout = [v for v in dec["vout"] if v["scriptPubKey"].get("address") == self.d_plain[n]][0]
            self.plain_ops[n] = {"txid": txid, "vout": vout["n"], "amount": vout["value"], "spk": vout["scriptPubKey"]["hex"]}
        self.generatetoaddress(node, 1, self.sink)

        self.stage("1_fresh", extra_wallets=(("w_createwallet", w_plain, w_quantum), ("e_encrypted_locked", e_plain, e_quantum)),
                   externals_startmining=True)

        self.log.info("Re-import d's own tr() descriptors (no reload)")
        priv = [x for x in d.listdescriptors(True)["descriptors"] if x["desc"].startswith("tr(")]
        req = []
        for x in priv:
            nxt = x.get("next_index", x.get("next"))
            req.append({"desc": x["desc"], "timestamp": 0, "active": x["active"], "internal": x["internal"],
                        "range": [x["range"][0], max(x["range"][1], nxt)], "next_index": nxt})
        res = d.importdescriptors(req)
        assert all(r["success"] for r in res), res
        self.stage("2_after_importdescriptors")

        node.unloadwallet("d")
        node.loadwallet("d")
        self.stage("3_after_reload")

        self.log.info("New plain output processed after the reload")
        txid = funder.sendtoaddress(self.d_plain["plain_ext1"], 2)
        self.generatetoaddress(node, 1, self.sink)
        self.restart_node(0, extra_args=["-fallbackfee=0.001", "-wallet=funder", "-wallet=d"])
        node.setmocktime(self.now)
        self.stage("4_after_restart")
        node.get_wallet_rpc("d").rescanblockchain()
        self.stage("5_after_restart_rescan", externals_startmining=True)

        self.log.info("Keypool growth: keypoolrefill and getnewaddress beyond the lookahead")
        d = node.get_wallet_rpc("d")
        d.keypoolrefill(4)
        for _ in range(6):
            r = d.getnewaddress()
            self.receive_seq.append(r)
        self.stage("6_keypool_growth")

        self.log.info("Encrypted d, locked across a reload")
        d = node.get_wallet_rpc("d")
        d.encryptwallet("pass")
        node.unloadwallet("d")
        node.loadwallet("d")
        self.stage("7_encrypted_locked_reload", locked=True)

        self.log.info("Top-up while locked (larger -keypool at load), then unlock and top up again")
        self.restart_node(0, extra_args=["-fallbackfee=0.001", "-wallet=funder", "-wallet=d", "-keypool=8"])
        node.setmocktime(self.now)
        self.stage("8_locked_topup_larger_keypool", locked=True)
        d = node.get_wallet_rpc("d")
        d.walletpassphrase("pass", 3600)
        d.keypoolrefill(12)
        self.stage("9_unlocked_topup")
        d.walletlock()

        checks = {name: self.check(name) for name in self.obs}
        matrix = {name: {c: ("PASS" if not bad else "FAIL") for c, bad in r.items()} for name, r in checks.items()}
        cols = list(next(iter(matrix.values())).keys())
        self.log.info("STAGE MATRIX\n" + f"{'stage':28}" + "".join(f"{c[:30]:32}" for c in cols) + "\n" +
                      "\n".join(f"{name:28}" + "".join(f"{matrix[name][c]:32}" for c in cols) for name in matrix))
        failures = {name: {c: bad for c, bad in r.items() if bad} for name, r in checks.items()}
        failures = {k: v for k, v in failures.items() if v}
        with open(f"{self.options.tmpdir}/observations.json", "w") as f:
            json.dump({"receive_seq": self.receive_seq, "change_seq": self.change_seq, "plain": self.d_plain,
                       "externals": self.externals, "obs": self.obs, "matrix": matrix, "failures": failures},
                      f, indent=1, default=str)
        assert not failures, json.dumps(failures, indent=1)


if __name__ == "__main__":
    WalletQuantumPlainTaprootReloadTest(__file__).main()
