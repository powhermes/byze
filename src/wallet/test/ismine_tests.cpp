// Copyright (c) 2017-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>
#include <key_io.h>
#include <node/context.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/solver.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <wallet/types.h>
#include <wallet/wallet.h>
#include <wallet/test/util.h>

#include <boost/test/unit_test.hpp>

using namespace util::hex_literals;

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(ismine_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(ismine_standard)
{
    CKey keys[2];
    CPubKey pubkeys[2];
    for (int i = 0; i < 2; i++) {
        keys[i].MakeNewKey(true);
        pubkeys[i] = keys[i].GetPubKey();
    }

    CKey uncompressedKey = GenerateRandomKey(/*compressed=*/false);
    CPubKey uncompressedPubkey = uncompressedKey.GetPubKey();
    std::unique_ptr<interfaces::Chain>& chain = m_node.chain;

    CScript scriptPubKey;
    bool result;

    // P2PK compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pk(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForRawPubKey(pubkeys[0]);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2PK uncompressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pk(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForRawPubKey(uncompressedPubkey);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2PKH compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pkh(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(PKHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2PKH uncompressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "pkh(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(PKHash(uncompressedPubkey));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2SH - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "sh(pkh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForDestination(PKHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // (P2PKH inside) P2SH inside P2SH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "sh(sh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // (P2PKH inside) P2SH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(sh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WPKH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(wpkh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // (P2PKH inside) P2WSH inside P2WSH (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wsh(wsh(" + EncodeSecret(keys[0]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WPKH compressed - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wpkh(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2WPKH uncompressed (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "wpkh(" + EncodeSecret(uncompressedKey) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // scriptPubKey multisig - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());
        std::string desc_str = "multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        scriptPubKey = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2SH multisig - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "sh(multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForMultisig(2, {uncompressedPubkey, pubkeys[1]});
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2WSH multisig with compressed keys - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "wsh(multi(2," + EncodeSecret(keys[0]) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript redeemScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        scriptPubKey = GetScriptForDestination(WitnessV0ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // P2WSH multisig with uncompressed key (invalid) - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "wsh(multi(2," + EncodeSecret(uncompressedKey) + "," + EncodeSecret(keys[1]) + "))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, false);
        BOOST_CHECK_EQUAL(spk_manager, nullptr);
    }

    // P2WSH multisig wrapped in P2SH - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "sh(wsh(multi(2," + EncodeSecret(keys[0]) + "," + EncodeSecret(keys[1]) + ")))";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        CScript witnessScript = GetScriptForMultisig(2, {pubkeys[0], pubkeys[1]});
        CScript redeemScript = GetScriptForDestination(WitnessV0ScriptHash(witnessScript));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }

    // Combo - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "combo(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        // Test P2PK
        result = spk_manager->IsMine(GetScriptForRawPubKey(pubkeys[0]));
        BOOST_CHECK(result);

        // Test P2PKH
        result = spk_manager->IsMine(GetScriptForDestination(PKHash(pubkeys[0])));
        BOOST_CHECK(result);

        // Test P2SH (combo descriptor does not describe P2SH)
        CScript redeemScript = GetScriptForDestination(PKHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(!result);

        // Test P2WPKH
        scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);

        // P2SH-P2WPKH output
        redeemScript = GetScriptForDestination(WitnessV0KeyHash(pubkeys[0]));
        scriptPubKey = GetScriptForDestination(ScriptHash(redeemScript));
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);

        // Test P2TR (combo descriptor does not describe P2TR)
        XOnlyPubKey xpk(pubkeys[0]);
        Assert(xpk.IsFullyValid());
        TaprootBuilder builder;
        builder.Finalize(xpk);
        WitnessV1Taproot output = builder.GetOutput();
        scriptPubKey = GetScriptForDestination(output);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(!result);
    }

    // Taproot - Descriptor
    {
        CWallet keystore(chain.get(), "", CreateMockableWalletDatabase());

        std::string desc_str = "tr(" + EncodeSecret(keys[0]) + ")";

        auto spk_manager = CreateDescriptor(keystore, desc_str, true);

        XOnlyPubKey xpk(pubkeys[0]);
        Assert(xpk.IsFullyValid());
        TaprootBuilder builder;
        builder.Finalize(xpk);
        WitnessV1Taproot output = builder.GetOutput();
        scriptPubKey = GetScriptForDestination(output);
        result = spk_manager->IsMine(scriptPubKey);
        BOOST_CHECK(result);
    }
}

// Byze: plain (non-quantum) witness-v1 scripts derived by the wallet's own descriptors are
// ismine but permanently unspendable; nothing else may be classified that way.
BOOST_AUTO_TEST_CASE(byze_unspendable_descriptor_taproot)
{
    const CKey tr_key = GenerateRandomKey();
    const CKey wpkh_key = GenerateRandomKey();
    const CKey foreign_key = GenerateRandomKey();
    CExtKey ext_key;
    ext_key.SetSeed(std::as_bytes(std::span{"byze plain tr seed"}));

    CWallet keystore(m_node.chain.get(), "", CreateMockableWalletDatabase());
    BOOST_REQUIRE(CreateDescriptor(keystore, "rawtr(" + EncodeSecret(tr_key) + ")", true));
    BOOST_REQUIRE(CreateDescriptor(keystore, "wpkh(" + EncodeSecret(wpkh_key) + ")", true));
    const std::string ranged_desc{"tr(" + EncodeExtKey(ext_key) + "/86h/1h/0h/0/*)"};
    BOOST_REQUIRE(CreateDescriptor(keystore, ranged_desc, true));

    LOCK(keystore.cs_wallet);

    const CScript plain = GetScriptForDestination(WitnessV1Taproot{XOnlyPubKey{tr_key.GetPubKey()}});
    BOOST_CHECK(keystore.IsMine(plain));
    BOOST_CHECK(keystore.IsUnspendableDescriptorTaproot(plain));
    // ... and never a quantum program (FindReceiveIndexForQuantumProgram scans the same maps).
    BOOST_CHECK(!keystore.IsQuantumMine(plain));
    BOOST_CHECK(!keystore.FindReceiveIndexForQuantumProgram(std::span{plain}.subspan(2)));

    // Index 0 of a ranged tr() descriptor (no quantum root available: not active).
    FlatSigningProvider desc_keys, out_keys;
    std::string error;
    const auto parsed{Parse(ranged_desc, desc_keys, error, /*require_checksum=*/false)};
    BOOST_REQUIRE_EQUAL(parsed.size(), 1U);
    std::vector<CScript> expanded;
    BOOST_REQUIRE(parsed.at(0)->Expand(0, desc_keys, expanded, out_keys));
    BOOST_REQUIRE_EQUAL(expanded.size(), 1U);
    const CScript& ranged_plain = expanded.at(0);
    BOOST_CHECK(keystore.IsMine(ranged_plain));
    BOOST_CHECK(keystore.IsUnspendableDescriptorTaproot(ranged_plain));
    BOOST_CHECK(!keystore.IsQuantumMine(ranged_plain));
    BOOST_CHECK(!keystore.FindReceiveIndexForQuantumProgram(std::span{ranged_plain}.subspan(2)));

    // Not witness v1: never classified, even though it is ours.
    const CScript wpkh = GetScriptForDestination(WitnessV0KeyHash{wpkh_key.GetPubKey()});
    BOOST_CHECK(keystore.IsMine(wpkh));
    BOOST_CHECK(!keystore.IsUnspendableDescriptorTaproot(wpkh));

    // A witness-v1 output the wallet does not derive (could be someone's quantum program).
    const CScript foreign = GetScriptForDestination(WitnessV1Taproot{XOnlyPubKey{foreign_key.GetPubKey()}});
    BOOST_CHECK(!keystore.IsMine(foreign));
    BOOST_CHECK(!keystore.IsUnspendableDescriptorTaproot(foreign));
    // Mining guard: own plain expansions are refused, foreign and non-v1 scripts are not.
    BOOST_CHECK(keystore.IsWalletDerivedPlainTaproot(plain));
    BOOST_CHECK(keystore.IsWalletDerivedPlainTaproot(ranged_plain));
    BOOST_CHECK(!keystore.IsWalletDerivedPlainTaproot(wpkh));
    BOOST_CHECK(!keystore.IsWalletDerivedPlainTaproot(foreign));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
