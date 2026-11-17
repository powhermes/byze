// Copyright (c) 2026 Byze developers
// Distributed under the MIT software license
//
// Byze v0.3 wallet RPCs: witness-v2 stateless (ML-DSA-44 + SPHINCS+) addresses and the one-way
// migration of legacy (XMSS + SPHINCS+) coins, including coins whose XMSS key is exhausted.

#include <consensus/amount.h>
#include <core_io.h>
#include <crypto/quantum_safe_config.h>
#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/util.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <util/translation.h>
#include <wallet/rpc/util.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <univalue.h>

namespace wallet {

RPCHelpMan getnewstatelessaddress()
{
    return RPCHelpMan{
        "getnewstatelessaddress",
        "Returns a new Byze v0.3 stateless address (witness v2, ML-DSA-44 + SPHINCS+).\n"
        "Unlike witness-v1 (XMSS) addresses it has no signature limit and can be reused.\n"
        "Coins sent to it can only be spent once the v0.3 rules (statelesspq) are active.\n"
        "Keys are derived from the wallet's HD seed, so a recovery-phrase restore finds them.\n",
        {
            {"label", RPCArg::Type::STR, RPCArg::Default{""}, "The label name for the address."},
        },
        RPCResult{RPCResult::Type::STR, "address", "The new stateless address"},
        RPCExamples{HelpExampleCli("getnewstatelessaddress", "") + HelpExampleRpc("getnewstatelessaddress", "")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;
            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);
            const std::string label{LabelFromValue(request.params[0])};
            auto dest = pwallet->GetNewStatelessDestination(label);
            if (!dest) throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(dest).original);
            return EncodeDestination(*dest);
        },
    };
}

// Witness bytes of one migration input: count + [<empty>, sphincs_sig, legacy bundle].
static constexpr int64_t MIGRATION_INPUT_WITNESS_BYTES{1 + 1 + 3 + BYZE_SPHINCS_SIGNATURE_SIZE + 1 + BYZE_DUAL_PUBKEY_BUNDLE_SIZE};

RPCHelpMan migratequantumcoins()
{
    return RPCHelpMan{
        "migratequantumcoins",
        "Moves this wallet's legacy witness-v1 (XMSS + SPHINCS+) coins to a new stateless witness-v2 address,\n"
        "signing with SPHINCS+ only. This works for coins whose XMSS key has no signatures left.\n"
        "Consensus only allows this spend into stateless outputs, and only once the v0.3 rules are active.\n"
        "Each input carries a ~7.9 kB SPHINCS+ signature, so one call moves at most max_inputs coins.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Default{""}, "Only move coins held by this legacy address (default: any legacy coin)."},
            {"max_inputs", RPCArg::Type::NUM, RPCArg::Default{40}, "Coins per transaction (1-45)."},
            {"fee_rate", RPCArg::Type::AMOUNT, RPCArg::Default{2}, "Fee rate in sat/vB."},
            {"broadcast", RPCArg::Type::BOOL, RPCArg::Default{true}, "Broadcast the transaction; false returns the signed hex only."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "Transaction id"},
                {RPCResult::Type::STR, "destination", "The new stateless address the coins were sent to"},
                {RPCResult::Type::NUM, "inputs", "Number of coins moved"},
                {RPCResult::Type::STR_AMOUNT, "amount", "Amount received at the destination"},
                {RPCResult::Type::STR_AMOUNT, "fee", "Fee paid"},
                {RPCResult::Type::NUM, "remaining", "Legacy coins still matching after this transaction"},
                {RPCResult::Type::STR_HEX, "hex", /*optional=*/true, "Signed transaction (when broadcast=false)"},
            }},
        RPCExamples{HelpExampleCli("migratequantumcoins", "") + HelpExampleCli("migratequantumcoins", "\"byz1p...\" 20")},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue {
            std::shared_ptr<CWallet> const pwallet = GetWalletForJSONRPCRequest(request);
            if (!pwallet) return UniValue::VNULL;
            pwallet->BlockUntilSyncedToCurrentChain();
            LOCK(pwallet->cs_wallet);
            EnsureWalletIsUnlocked(*pwallet);

            std::optional<CScript> only_script;
            if (!request.params[0].isNull() && !request.params[0].get_str().empty()) {
                const CTxDestination d = DecodeDestination(request.params[0].get_str());
                if (!IsValidDestination(d)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
                only_script = GetScriptForDestination(d);
            }
            const int max_inputs = request.params[1].isNull() ? 40 : request.params[1].getInt<int>();
            if (max_inputs < 1 || max_inputs > 45) throw JSONRPCError(RPC_INVALID_PARAMETER, "max_inputs must be 1-45");
            const CFeeRate fee_rate = request.params[2].isNull() ? CFeeRate(2000) : CFeeRate(AmountFromValue(request.params[2], /*decimals=*/3), 1000);
            const bool broadcast = request.params[3].isNull() || request.params[3].get_bool();

            // Legacy witness-v1 coins this wallet owns
            std::vector<COutput> legacy;
            for (const COutput& out : AvailableCoins(*pwallet).All()) {
                int ver{-1};
                std::vector<unsigned char> prog;
                if (!out.txout.scriptPubKey.IsWitnessProgram(ver, prog) || ver != 1 || prog.size() != WITNESS_V1_TAPROOT_SIZE) continue;
                if (pwallet->IsStatelessProgram(prog) || !pwallet->IsQuantumMine(out.txout.scriptPubKey)) continue;
                if (only_script && out.txout.scriptPubKey != *only_script) continue;
                legacy.push_back(out);
            }
            if (legacy.empty()) throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "No legacy quantum coins to migrate");
            std::sort(legacy.begin(), legacy.end(), [](const COutput& a, const COutput& b) { return a.txout.nValue > b.txout.nValue; });
            const size_t n = std::min<size_t>(legacy.size(), max_inputs);

            auto dest = pwallet->GetNewStatelessDestination("migration");
            if (!dest) throw JSONRPCError(RPC_WALLET_ERROR, util::ErrorString(dest).original);
            const CScript dest_script = GetScriptForDestination(*dest);

            CMutableTransaction tx;
            tx.version = 2;
            CAmount in_total{0};
            std::vector<CTxOut> spent;
            for (size_t i = 0; i < n; ++i) {
                tx.vin.emplace_back(legacy[i].outpoint, CScript(), uint32_t{CTxIn::MAX_SEQUENCE_NONFINAL});
                spent.push_back(legacy[i].txout);
                in_total += legacy[i].txout.nValue;
            }
            tx.vout.emplace_back(0, dest_script);
            const int64_t base = GetSerializeSize(TX_NO_WITNESS(CTransaction(tx)));
            const int64_t weight = base * WITNESS_SCALE_FACTOR + 2 + int64_t(n) * MIGRATION_INPUT_WITNESS_BYTES;
            const int64_t vsize = (weight + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
            if (weight > MAX_STANDARD_TX_WEIGHT) throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many inputs for one transaction; lower max_inputs");
            const CAmount fee = fee_rate.GetFee(vsize);
            if (in_total - fee <= 0) throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Coins do not cover the fee");
            tx.vout[0].nValue = in_total - fee;

            PrecomputedTransactionData txdata;
            txdata.Init(tx, std::vector<CTxOut>(spent), /*force=*/true);
            for (size_t i = 0; i < n; ++i) {
                ScriptExecutionData ed;
                ed.m_annex_init = true;
                ed.m_annex_present = false;
                uint256 sighash;
                if (!SignatureHashSchnorr(sighash, ed, tx, i, SIGHASH_DEFAULT, SigVersion::TAPROOT, txdata, MissingDataBehavior::FAIL)) {
                    throw JSONRPCError(RPC_WALLET_ERROR, "Signature hash failed");
                }
                int ver{-1};
                std::vector<unsigned char> prog;
                spent[i].scriptPubKey.IsWitnessProgram(ver, prog);
                std::vector<unsigned char> sphincs_sig, bundle;
                if (!pwallet->SignLegacyMigrationSighash(sighash, prog, sphincs_sig, bundle)) {
                    throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Could not sign input %u with SPHINCS+", i));
                }
                tx.vin[i].scriptWitness.stack = {std::vector<unsigned char>{}, std::move(sphincs_sig), std::move(bundle)};
            }
            // Check every input against the v0.3 rules before broadcasting.
            for (size_t i = 0; i < n; ++i) {
                ScriptError err;
                MutableTransactionSignatureChecker checker(&tx, i, spent[i].nValue, txdata, MissingDataBehavior::FAIL);
                if (!VerifyScript(CScript(), spent[i].scriptPubKey, &tx.vin[i].scriptWitness,
                                  STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_STATELESS_PQ, checker, &err)) {
                    throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Input %u failed verification: %s", i, ScriptErrorString(err)));
                }
            }

            const CTransactionRef txr = MakeTransactionRef(std::move(tx));
            UniValue ret(UniValue::VOBJ);
            ret.pushKV("txid", txr->GetHash().GetHex());
            ret.pushKV("destination", EncodeDestination(*dest));
            ret.pushKV("inputs", uint64_t(n));
            ret.pushKV("amount", ValueFromAmount(txr->vout[0].nValue));
            ret.pushKV("fee", ValueFromAmount(fee));
            ret.pushKV("remaining", uint64_t(legacy.size() - n));
            if (broadcast) {
                pwallet->CommitTransaction(txr, {}, {});
            } else {
                ret.pushKV("hex", EncodeHexTx(*txr));
            }
            return ret;
        },
    };
}

} // namespace wallet
