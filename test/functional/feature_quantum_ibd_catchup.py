#!/usr/bin/env python3
# Copyright (c) 2026 Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""IBD/catch-up correctness for quantum (witness v1) blocks under load."""

from test_framework.messages import tx_from_hex
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from decimal import Decimal

# Keep these in sync with src/crypto/quantum_safe_config.h
BYZE_XMSS_SIGNATURE_SIZE = 1028
BYZE_SPHINCS_SIGNATURE_SIZE = 1024
BYZE_DUAL_PUBKEY_BUNDLE_SIZE = 192


def tx_confirmed_in_chain(node, txid, max_blocks=512):
    """Return True if txid appears in any active-chain block walking back from tip."""
    h = node.getbestblockhash()
    for _ in range(max_blocks):
        block = node.getblock(h, 1)
        if txid in block["tx"]:
            return True
        h = block.get("previousblockhash")
        if not h:
            break
    return False


class QuantumIBDCatchupTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-enforcequantumblocksigs=1", "-fallbackfee=0.001", "-logratelimit=0"]] * self.num_nodes
        self.rpc_timeout = 1200
        self.sync_timeout = 900

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def setup_network(self):
        # Keep nodes disconnected during source-chain construction so node1 starts catch-up from height 0.
        self.setup_nodes()

    def mine_blocks(self, node, num_blocks):
        for _ in range(num_blocks):
            self.generatetoaddress(node, nblocks=1, address=self.get_quantum_address(node), sync_fun=self.no_op)

    def get_quantum_address(self, node):
        # Make address type explicit so this test always builds spendable witness-v1 (quantum) UTXOs.
        return node.getnewaddress(address_type="bech32m")

    def ensure_spendable_utxos(self, node, *, min_count=1, max_extra_blocks=240):
        spendable = [u for u in node.listunspent() if u.get("spendable", False)]
        extra = 0
        while len(spendable) < min_count and extra < max_extra_blocks:
            self.mine_blocks(node, 1)
            extra += 1
            spendable = [u for u in node.listunspent() if u.get("spendable", False)]
        assert len(spendable) >= min_count, "No spendable wallet UTXOs available for quantum tx construction"
        if extra > 0:
            self.log.info("Mined %d extra block(s) to obtain spendable UTXOs", extra)

    def seed_quantum_utxo(self, node, amount=1.0):
        # Create a confirmed witness-v1 wallet UTXO so quantum signing has a deterministic candidate.
        txid = node.sendtoaddress(self.get_quantum_address(node), amount)
        self.mine_blocks(node, 1)
        return txid

    def build_signed_quantum_tx(self, sender, recipient, amount):
        """Return a signed tx spending at least one quantum (witness v1) input with a valid 3-item stack.

        Build a single-input tx from a known witness-v1 UTXO to avoid wallet coin-selection variance.
        """
        fee = Decimal("0.001")
        amount = Decimal(str(amount))
        for utxo in sender.listunspent():
            if not utxo["spendable"]:
                continue
            prevout_spk = utxo.get("scriptPubKey", "")
            if not isinstance(prevout_spk, str) or not prevout_spk.startswith("5120") or len(prevout_spk) != 68:
                continue
            utxo_amount = Decimal(str(utxo["amount"]))
            if utxo_amount <= amount + fee:
                continue
            change = utxo_amount - amount - fee
            # Keep both outputs quantum-typed for deterministic witness-v1 UTXO churn in this test.
            outputs = {
                recipient: round(float(amount), 8),
                self.get_quantum_address(sender): round(float(change), 8),
            }
            raw = sender.createrawtransaction([{"txid": utxo["txid"], "vout": utxo["vout"]}], outputs)
            signed = sender.signrawtransactionwithwallet(raw)
            if not signed.get("complete", False):
                continue
            signed_hex = signed["hex"]
            tx = tx_from_hex(signed_hex)
            if len(tx.wit.vtxinwit) > 0:
                stack = list(tx.wit.vtxinwit[0].scriptWitness.stack)
                if (
                    len(stack) == 3
                    and len(stack[0]) == BYZE_XMSS_SIGNATURE_SIZE
                    and len(stack[1]) == BYZE_SPHINCS_SIGNATURE_SIZE
                    and len(stack[2]) == BYZE_DUAL_PUBKEY_BUNDLE_SIZE
                ):
                    return signed_hex
            # Keep trying other candidate witness-v1 UTXOs.
            continue
        raise AssertionError("Could not build a fully signed quantum transaction")

    def run_test(self):
        source, ibd = self.nodes

        self.log.info("Phase 1: build a quantum-heavy source chain while node1 remains at height 0")
        self.mine_blocks(source, 110)
        self.ensure_spendable_utxos(source, min_count=1)
        self.seed_quantum_utxo(source, amount=1.0)
        confirmed_quantum_txids = []
        load_rounds = 24
        for _ in range(load_rounds):
            spend_hex = self.build_signed_quantum_tx(source, self.get_quantum_address(source), 0.15)
            # Quantum witness txs are very large; default sendrawtransaction maxfeerate (0.1 BTC/kvB)
            # can reject an otherwise-reasonable absolute fee. Test-only: disable client feerate cap.
            spend_txid = source.sendrawtransaction(spend_hex, 0)
            confirmed_quantum_txids.append(spend_txid)
            self.mine_blocks(source, 1)
        self.mine_blocks(source, 12)

        source_tip = source.getbestblockhash()
        source_height = source.getblockcount()
        source_chainwork = source.getblockheader(source_tip)["chainwork"]
        assert_equal(ibd.getblockcount(), 0)
        self.log.info("Source chain prepared: height=%d tip=%s", source_height, source_tip)

        self.log.info("Phase 2: connect node1 and perform full IBD catch-up from height 0")
        with ibd.assert_debug_log(
            [],
            unexpected_msgs=["invalid header received", "Stall started"],
            timeout=self.sync_timeout,
        ):
            self.connect_nodes(0, 1)
            self.sync_blocks(self.nodes, timeout=self.sync_timeout)
        self.sync_mempools(self.nodes, timeout=self.sync_timeout)

        self.log.info("Phase 3: verify full-sync correctness (no partial sync state)")
        tip0 = source.getbestblockhash()
        tip1 = ibd.getbestblockhash()
        assert_equal(tip1, tip0)
        assert_equal(ibd.getblockcount(), source_height)
        assert_equal(ibd.getblockheader(tip1)["chainwork"], source_chainwork)
        assert_equal(ibd.getblockchaininfo()["initialblockdownload"], False)

        # Confirm the IBD node has a fully active chain tip and no in-flight block downloads left.
        active_tips = [t for t in ibd.getchaintips() if t["status"] == "active"]
        assert_equal(len(active_tips), 1)
        assert_equal(active_tips[0]["hash"], tip1)
        self.wait_until(
            lambda: all(len(peer["inflight"]) == 0 for peer in ibd.getpeerinfo()),
            timeout=60,
        )

        # Under-load validation check: a sample of quantum spends mined on source is present on IBD node.
        sample_txids = confirmed_quantum_txids[::4]
        for txid in sample_txids:
            assert tx_confirmed_in_chain(ibd, txid, max_blocks=source_height + 4), f"IBD node missing confirmed tx {txid}"

        self.log.info("IBD quantum catch-up completed successfully at height %d", source_height)


if __name__ == "__main__":
    QuantumIBDCatchupTest(__file__).main()
