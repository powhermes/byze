// Copyright (c) 2026 Byze developers
// Distributed under the MIT software license
//
// Byze v0.3 consensus: witness-v2 stateless outputs (ML-DSA-44 + SPHINCS+) and the one-way
// SPHINCS+-only migration spend of legacy witness-v1 outputs.

#include <crypto/quantum_safe.h>
#include <crypto/quantum_safe_config.h>
#include <crypto/sha256.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {

struct StatelessKeys {
    std::vector<uint8_t> mldsa_pk, mldsa_sk;
    crypto::sphincs_private_key sphincs;
    std::vector<uint8_t> sphincs_pk;
    std::vector<uint8_t> bundle; // mldsa_pk || sphincs_pk
};

StatelessKeys MakeStatelessKeys(uint8_t tag)
{
    StatelessKeys k;
    std::vector<uint8_t> seed(32, tag);
    BOOST_REQUIRE(crypto::mldsa_keypair_from_seed(seed, k.mldsa_pk, k.mldsa_sk));
    BOOST_REQUIRE(k.sphincs.generate());
    k.sphincs_pk = k.sphincs.get_public_key().save();
    k.bundle = k.mldsa_pk;
    k.bundle.insert(k.bundle.end(), k.sphincs_pk.begin(), k.sphincs_pk.end());
    BOOST_REQUIRE_EQUAL(k.bundle.size(), size_t{BYZE_STATELESS_PUBKEY_BUNDLE_SIZE});
    return k;
}

std::vector<unsigned char> Program(const std::vector<uint8_t>& bundle)
{
    std::vector<unsigned char> h(32);
    CSHA256().Write(bundle.data(), bundle.size()).Finalize(h.data());
    return h;
}

CScript V2Script(const std::vector<uint8_t>& bundle) { return CScript() << OP_2 << Program(bundle); }
CScript V1Script(const std::vector<uint8_t>& bundle) { return CScript() << OP_1 << Program(bundle); }

CMutableTransaction SpendTx(const std::vector<CScript>& outputs)
{
    CMutableTransaction tx;
    tx.version = 2;
    tx.vin.emplace_back(COutPoint(Txid::FromUint256(uint256::ONE), 0));
    for (const auto& spk : outputs) tx.vout.emplace_back(1000, spk);
    return tx;
}

uint256 Sighash(const CMutableTransaction& tx, const CTxOut& spent, PrecomputedTransactionData& txdata)
{
    txdata.Init(tx, std::vector<CTxOut>{spent}, /*force=*/true);
    ScriptExecutionData ed;
    ed.m_annex_init = true;
    ed.m_annex_present = false;
    uint256 h;
    BOOST_REQUIRE(SignatureHashSchnorr(h, ed, tx, 0, SIGHASH_DEFAULT, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL));
    return h;
}

bool Verify(const CMutableTransaction& tx, const CTxOut& spent, const PrecomputedTransactionData& txdata,
            const CScriptWitness& wit, script_verify_flags flags, ScriptError& err)
{
    MutableTransactionSignatureChecker checker(&tx, 0, spent.nValue, txdata, MissingDataBehavior::FAIL);
    return VerifyScript(CScript(), spent.scriptPubKey, &wit, flags, checker, &err);
}

const script_verify_flags BASE_FLAGS{SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_TAPROOT};
const script_verify_flags V03_FLAGS{BASE_FLAGS | SCRIPT_VERIFY_STATELESS_PQ};

} // namespace

BOOST_AUTO_TEST_SUITE(stateless_pq_tests)

BOOST_AUTO_TEST_CASE(mldsa_deterministic_keys_and_signatures)
{
    std::vector<uint8_t> pk1, sk1, pk2, sk2, pk3, sk3;
    BOOST_REQUIRE(crypto::mldsa_keypair_from_seed(std::vector<uint8_t>(32, 7), pk1, sk1));
    BOOST_REQUIRE(crypto::mldsa_keypair_from_seed(std::vector<uint8_t>(32, 7), pk2, sk2));
    BOOST_REQUIRE(crypto::mldsa_keypair_from_seed(std::vector<uint8_t>(32, 8), pk3, sk3));
    BOOST_CHECK(pk1 == pk2);
    BOOST_CHECK(sk1 == sk2);
    BOOST_CHECK(pk1 != pk3);
    BOOST_CHECK(!crypto::mldsa_keypair_from_seed(std::vector<uint8_t>(31, 7), pk1, sk1));

    // Same key signs many times: no state, no limit (sample well past XMSS's 1,024).
    BOOST_REQUIRE(crypto::mldsa_keypair_from_seed(std::vector<uint8_t>(32, 7), pk1, sk1));
    for (int i = 0; i < 1100; ++i) {
        uint256 msg;
        msg.data()[0] = uint8_t(i);
        msg.data()[1] = uint8_t(i >> 8);
        const auto sig = crypto::mldsa_sign(sk1, msg);
        BOOST_REQUIRE_EQUAL(sig.size(), size_t{BYZE_MLDSA_SIGNATURE_SIZE});
        BOOST_REQUIRE(crypto::mldsa_verify(pk1, sig, msg));
        if (i == 0) {
            auto bad = sig;
            bad[10] ^= 1;
            BOOST_CHECK(!crypto::mldsa_verify(pk1, bad, msg));
            BOOST_CHECK(!crypto::mldsa_verify(pk3, sig, msg));
        }
    }
}

BOOST_AUTO_TEST_CASE(witness_v2_stateless_spend)
{
    const StatelessKeys k = MakeStatelessKeys(1);
    const CTxOut spent(50000, V2Script(k.bundle));
    const CMutableTransaction tx = SpendTx({V2Script(k.bundle)});
    PrecomputedTransactionData txdata;
    const uint256 h = Sighash(tx, spent, txdata);

    CScriptWitness wit;
    wit.stack = {crypto::mldsa_sign(k.mldsa_sk, h), k.sphincs.sign(h).save(), k.bundle};
    ScriptError err;
    BOOST_CHECK(Verify(tx, spent, txdata, wit, V03_FLAGS, err));

    // Before activation a witness-v2 output cannot be spent.
    BOOST_CHECK(!Verify(tx, spent, txdata, wit, BASE_FLAGS, err));

    // Tampered ML-DSA signature, tampered SPHINCS+ signature, wrong bundle, wrong stack size.
    CScriptWitness bad = wit;
    bad.stack[0][5] ^= 1;
    BOOST_CHECK(!Verify(tx, spent, txdata, bad, V03_FLAGS, err));
    bad = wit;
    bad.stack[1][5] ^= 1;
    BOOST_CHECK(!Verify(tx, spent, txdata, bad, V03_FLAGS, err));
    bad = wit;
    bad.stack[2][0] ^= 1;
    BOOST_CHECK(!Verify(tx, spent, txdata, bad, V03_FLAGS, err));
    bad = wit;
    bad.stack.pop_back();
    BOOST_CHECK(!Verify(tx, spent, txdata, bad, V03_FLAGS, err));

    // A signature for a different transaction does not verify.
    const CMutableTransaction tx2 = SpendTx({V2Script(k.bundle), V2Script(k.bundle)});
    PrecomputedTransactionData txdata2;
    Sighash(tx2, spent, txdata2);
    BOOST_CHECK(!Verify(tx2, spent, txdata2, wit, V03_FLAGS, err));
}

BOOST_AUTO_TEST_CASE(legacy_migration_spend_is_one_way)
{
    // Legacy bundle: XMSS pk (never used on this path) || SPHINCS+ pk.
    crypto::sphincs_private_key sphincs;
    BOOST_REQUIRE(sphincs.generate());
    std::vector<uint8_t> legacy(BYZE_XMSS_PUBKEY_SIZE, 0x5a);
    const auto sph_pk = sphincs.get_public_key().save();
    legacy.insert(legacy.end(), sph_pk.begin(), sph_pk.end());
    const CTxOut spent(50000, V1Script(legacy));
    const StatelessKeys dest = MakeStatelessKeys(2);

    auto check = [&](const std::vector<CScript>& outs, script_verify_flags flags) {
        const CMutableTransaction tx = SpendTx(outs);
        PrecomputedTransactionData txdata;
        const uint256 h = Sighash(tx, spent, txdata);
        CScriptWitness wit;
        wit.stack = {std::vector<unsigned char>{}, sphincs.sign(h).save(), legacy};
        ScriptError err;
        return Verify(tx, spent, txdata, wit, flags, err);
    };

    const CScript op_return = CScript() << OP_RETURN << std::vector<unsigned char>{1, 2, 3};
    BOOST_CHECK(check({V2Script(dest.bundle)}, V03_FLAGS));
    BOOST_CHECK(check({V2Script(dest.bundle), op_return}, V03_FLAGS));
    // Not before activation.
    BOOST_CHECK(!check({V2Script(dest.bundle)}, BASE_FLAGS));
    // Never back to a legacy (weakened) witness-v1 address, nor to any other script.
    BOOST_CHECK(!check({V2Script(dest.bundle), V1Script(legacy)}, V03_FLAGS));
    BOOST_CHECK(!check({CScript() << OP_TRUE}, V03_FLAGS));
}

BOOST_AUTO_TEST_SUITE_END()
