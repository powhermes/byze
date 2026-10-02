#!/usr/bin/env python3
# Copyright (c) 2026 The Byze developers
# Distributed under the MIT software license.

"""getaddressinfo must describe this wallet's quantum addresses as quantum_program(...),
never as rawtr(...), even when the 32-byte program happens to be a valid x-only point."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

SECP256K1_P = 2**256 - 2**32 - 977


def is_valid_xonly(program_hex):
    x = int(program_hex, 16)
    if x >= SECP256K1_P:
        return False
    y2 = (pow(x, 3, SECP256K1_P) + 7) % SECP256K1_P
    return pow(y2, (SECP256K1_P - 1) // 2, SECP256K1_P) == 1


class WalletQuantumGetAddressInfoTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def check(self, w, addr):
        info = w.getaddressinfo(addr)
        assert info["ismine"]
        assert info["solvable"]
        assert not info["desc"].startswith("rawtr("), info["desc"]
        assert_equal(info["desc"], f"quantum_program({info['witness_program']})")
        return info

    def run_test(self):
        node = self.nodes[0]
        node.createwallet("q")
        w = node.get_wallet_rpc("q")

        # About half of all programs are valid x-only points; keep going until we have hit one,
        # which is exactly the case that used to be reported as rawtr(...).
        hit_xonly = False
        for i in range(24):
            addr = w.getnewaddress(f"l{i}", "bech32m")
            info = self.check(w, addr)
            hit_xonly |= is_valid_xonly(info["witness_program"])
            if hit_xonly and i >= 3:
                break
        assert hit_xonly, "no program was a valid x-only point; cannot exercise the rawtr case"

        # Same answer when the wallet is encrypted and locked (no signing keys available).
        w.encryptwallet("pass")
        self.check(w, addr)


if __name__ == "__main__":
    WalletQuantumGetAddressInfoTest(__file__).main()
