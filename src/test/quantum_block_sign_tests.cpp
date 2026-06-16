// Copyright (c) 2026 Byze developers
// Distributed under the MIT software license

#include <crypto/quantum_block_sign.h>
#include <crypto/quantum_safe.h>
#include <crypto/quantum_safe_config.h>
#include <primitives/block.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(quantum_manager_dual_sign_smoke)
{
    crypto::quantum_safe_manager mgr;
    unsigned char seed[32]{};
    seed[0] = 0x42;
    BOOST_REQUIRE(mgr.generate_dual_keys_from_entropy_ikm(seed, sizeof(seed)));
    const uint256 msg;
    const auto xmss = mgr.sign(msg, crypto::quantum_algorithm::XMSS);
    const auto sph = mgr.sign(msg, crypto::quantum_algorithm::SPHINCS_PLUS);
    BOOST_CHECK_EQUAL(xmss.size(), BYZE_XMSS_SIGNATURE_SIZE);
    BOOST_CHECK_EQUAL(sph.size(), BYZE_SPHINCS_SIGNATURE_SIZE);
}

BOOST_AUTO_TEST_CASE(quantum_block_sign_smoke)
{
    CBlock block;
    block.nVersion = 4;
    block.nTime = 123;
    block.nBits = 0x207fffff;
    block.nNonce = 42;

    BOOST_CHECK(crypto::AttachQuantumBlockSignatures(block, true, false));
    BOOST_CHECK_EQUAL(block.quantum_signatures.xmss_signature.size(), BYZE_XMSS_SIGNATURE_SIZE);
    BOOST_CHECK_EQUAL(block.quantum_signatures.sphincs_signature.size(), BYZE_SPHINCS_SIGNATURE_SIZE);
    BOOST_CHECK_EQUAL(block.quantum_signatures.dual_public_key.size(), BYZE_DUAL_PUBKEY_BUNDLE_SIZE);
}

BOOST_AUTO_TEST_CASE(quantum_block_sign_skips_genesis)
{
    CBlock block;
    block.nVersion = 4;
    BOOST_CHECK(crypto::AttachQuantumBlockSignatures(block, true, true));
    BOOST_CHECK(block.quantum_signatures.xmss_signature.empty());
}
