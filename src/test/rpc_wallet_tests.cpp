// Copyright (c) 2013-2014 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "rpc/client.h"

#include "fs.h"
#include "key_io.h"
#include "main.h"
#include "wallet/wallet.h"

#include "zcash/Address.hpp"

#include "asyncrpcqueue.h"
#include "asyncrpcoperation.h"
#include "wallet/asyncrpcoperation_mergetoaddress.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"

#include "init.h"
#include "utiltest.h"
#include "tinyformat.h"

#include "test/rpc_tests.h"
#include "test/test_bitcoin.h"

#include <array>
#include <chrono>
#include <optional>
#include <thread>
#include <variant>

#include <fstream>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/format.hpp>

#include <univalue.h>

using namespace std;

extern UniValue createArgs(int nRequired, const char* address1 = nullptr, const char* address2 = nullptr);
extern UniValue CallRPC(string args);

extern CWallet* pwalletMain;

bool find_error(const UniValue& objError, const string& expected) {
    return find_value(objError, "message").get_str().find(expected) != string::npos;
}

static UniValue ValueFromString(const string &str)
{
    UniValue value;
    BOOST_CHECK(value.setNumStr(str));
    return value;
}

BOOST_FIXTURE_TEST_SUITE(rpc_wallet_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_addmultisig)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    rpcfn_type addmultisig = tableRPC["addmultisigaddress"]->actor;

    // old, 65-byte-long:
    const char address1Hex[] = "0434e3e09f49ea168c5bbf53f877ff4206923858aab7c7e1df25bc263978107c95e35065a27ef6f1b27222db0ec97e0e895eaca603d3ee0d4c060ce3d8a00286c8";
    // new, compressed:
    const char address2Hex[] = "0388c2037017c62240b6b72ac1a2a5f94da790596ebd06177c8572752922165cb4";

    KeyIO keyIO(Params());
    UniValue v;
    CTxDestination address;
    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex, address2Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(2, address1Hex, address2Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_THROW(addmultisig(createArgs(0), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(2, address1Hex), false), runtime_error);

    BOOST_CHECK_THROW(addmultisig(createArgs(1, ""), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1, "NotAValidPubkey"), false), runtime_error);

    string short1(address1Hex, address1Hex + sizeof(address1Hex) - 2); // last byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short1.c_str()), false), runtime_error);

    string short2(address1Hex + 1, address1Hex + sizeof(address1Hex)); // first byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short2.c_str()), false), runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_wallet)
{
    // Test RPC calls for various wallet statistics
    UniValue r;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CPubKey demoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination demoAddress(CTxDestination(demoPubkey.GetID()));
    UniValue retValue;
    string strAccount = "";
    string strPurpose = "receive";
    BOOST_CHECK_NO_THROW({ /*Initialize Wallet with an account */
        CWalletDB walletdb(pwalletMain->strWalletFile);
        CAccount account;
        account.vchPubKey = demoPubkey;
        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, strPurpose);
        walletdb.WriteAccount(strAccount, account);
    });

    CPubKey setaccountDemoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination setaccountDemoAddress(CTxDestination(setaccountDemoPubkey.GetID()));

    /*********************************
     * 			setaccount
     *********************************/
    KeyIO keyIO(Params());
    BOOST_CHECK_NO_THROW(CallRPC("setaccount " + keyIO.EncodeDestination(setaccountDemoAddress) + " \"\""));
    /* Accounts are disabled */
    BOOST_CHECK_THROW(CallRPC("setaccount " + keyIO.EncodeDestination(setaccountDemoAddress) + " nullaccount"), runtime_error);
    /* PtkqegiGBYiKjGorBWW78i6dgXCHaYY7mdE is not owned by the test wallet. */
    BOOST_CHECK_THROW(CallRPC("setaccount PtkqegiGBYiKjGorBWW78i6dgXCHaYY7mdE nullaccount"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("setaccount"), runtime_error);
    /* PtkqegiGBYiKjGorBWW78i6dgXCHaYY7md (34 chars) is an illegal address (should be 35 chars) */
    BOOST_CHECK_THROW(CallRPC("setaccount PtkqegiGBYiKjGorBWW78i6dgXCHaYY7md nullaccount"), runtime_error);


    /*********************************
     *                  getbalance
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getbalance"));
    BOOST_CHECK_THROW(CallRPC("getbalance " + keyIO.EncodeDestination(demoAddress)), runtime_error);

    /*********************************
     * 			listunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listunspent"));
    BOOST_CHECK_THROW(CallRPC("listunspent string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 not_array"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 [] extra"), runtime_error);
    BOOST_CHECK_NO_THROW(r = CallRPC("listunspent 0 1 []"));
    BOOST_CHECK(r.get_array().empty());

    /*********************************
     * 		listreceivedbyaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 true extra"), runtime_error);

    /*********************************
     * 		listreceivedbyaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 true extra"), runtime_error);

    /*********************************
     *          listsinceblock
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listsinceblock"));

    /*********************************
     *          listtransactions
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions"));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress)));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " 20"));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " 20 0"));
    BOOST_CHECK_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " not_int"), runtime_error);

    /*********************************
     *          listlockunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listlockunspent"));

    /*********************************
     *          listaccounts
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listaccounts"));

    /*********************************
     *          listaddressgroupings
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listaddressgroupings"));

    /*********************************
     * 		getrawchangeaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getrawchangeaddress"));

    /*********************************
     * 		getnewaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress \"\""));
    /* Accounts are deprecated */
    BOOST_CHECK_THROW(CallRPC("getnewaddress getnewaddress_demoaccount"), runtime_error);

    /*********************************
     * 		getaccountaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress \"\""));
    /* Accounts are deprecated */
    BOOST_CHECK_THROW(CallRPC("getaccountaddress accountThatDoesntExists"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaccountaddress " + strAccount));
    BOOST_CHECK(keyIO.DecodeDestination(retValue.get_str()) == demoAddress);

    /*********************************
     * 			getaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getaccount " + keyIO.EncodeDestination(demoAddress)));

    /*********************************
     * 	signmessage + verifymessage
     *********************************/
    BOOST_CHECK_NO_THROW(retValue = CallRPC("signmessage " + keyIO.EncodeDestination(demoAddress) + " mymessage"));
    BOOST_CHECK_THROW(CallRPC("signmessage"), runtime_error);
    /* Should throw error because this address is not loaded in the wallet */
    BOOST_CHECK_THROW(CallRPC("signmessage PtkqegiGBYiKjGorBWW78i6dgXCHaYY7mdE mymessage"), runtime_error);

    /* missing arguments */
    BOOST_CHECK_THROW(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress)), runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str()), runtime_error);
    /* Illegal address */
    BOOST_CHECK_THROW(CallRPC("verifymessage PtkqegiGBYiKjGorBWW78i6dgXCHaYY7md " + retValue.get_str() + " mymessage"), runtime_error);
    /* wrong address */
    BOOST_CHECK(CallRPC("verifymessage PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n " + retValue.get_str() + " mymessage").get_bool() == false);
    /* Correct address and signature but wrong message */
    BOOST_CHECK(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str() + " wrongmessage").get_bool() == false);
    /* Correct address, message and signature*/
    BOOST_CHECK(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str() + " mymessage").get_bool() == true);

    /*********************************
     * 		getaddressesbyaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaddressesbyaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaddressesbyaccount " + strAccount));
    UniValue arr = retValue.get_array();
    BOOST_CHECK_EQUAL(4, arr.size());
    bool notFound = true;
    for (auto a : arr.getValues()) {
        notFound &= keyIO.DecodeDestination(a.get_str()) != demoAddress;
    }
    BOOST_CHECK(!notFound);

    /*********************************
     * 	     fundrawtransaction
     *********************************/
    BOOST_CHECK_THROW(CallRPC("fundrawtransaction 28z"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("fundrawtransaction 01000000000180969800000000001976a91450ce0a4b0ee0ddeb633da85199728b940ac3fe9488ac00000000"), runtime_error);

    /*
     * getblocksubsidy
     */
    BOOST_CHECK_THROW(CallRPC("getblocksubsidy too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblocksubsidy -1"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 50000"));
    UniValue obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), REWARD);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 1000000"));
    obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), REWARD/2.0);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 2000000"));
    obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), REWARD/4.0);

    /*
     * getblock
     */
    BOOST_CHECK_THROW(CallRPC("getblock too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblock -1"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblock 2147483647"), runtime_error); // allowed, but > height of active chain tip
    BOOST_CHECK_THROW(CallRPC("getblock 2147483648"), runtime_error); // not allowed, > int32 used for nHeight
    BOOST_CHECK_THROW(CallRPC("getblock 100badchars"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 0"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 1"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 2"));
    BOOST_CHECK_THROW(CallRPC("getblock 0 -1"), runtime_error); // bad verbosity
    BOOST_CHECK_THROW(CallRPC("getblock 0 3"), runtime_error); // bad verbosity
}

BOOST_AUTO_TEST_CASE(rpc_wallet_getbalance)
{
    SelectParams(CBaseChainParams::Network::TESTNET);

    LOCK2(cs_main, pwalletMain->cs_wallet);


    BOOST_CHECK_THROW(CallRPC("z_getbalance too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getbalance invalidaddress"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_getbalance tPViri8Zo9JTsE4gh9pU9EbtPGnm1L66y1g"));
    BOOST_CHECK_THROW(CallRPC("z_getbalance tPViri8Zo9JTsE4gh9pU9EbtPGnm1L66y1g -1"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_getbalance tPViri8Zo9JTsE4gh9pU9EbtPGnm1L66y1g 0"));
    BOOST_CHECK_THROW(CallRPC("z_getbalance ptestsapling1zlgc9r90eqapx0vxc00hv3gunpgtm4wj3w9u29ehs4n5dgtdmg406dsemzl5rc7602ravnt3zr6 1"), runtime_error);


    BOOST_CHECK_THROW(CallRPC("z_gettotalbalance too manyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_gettotalbalance -1"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_gettotalbalance 0"));


    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress too many args"), runtime_error);
    // negative minconf not allowed
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tPWB75duYHtmDGgnM1A9hvDQijnNY8AULXk -1"), runtime_error);
    // invalid zaddr, taddr not allowed
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tPWB75duYHtmDGgnM1A9hvDQijnNY8AULXk 0"), runtime_error);
    // don't have the spending key
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tnRZ8bPq2pff3xBWhTJhNkVUkm2uhzksDeW5PvEa7aFKGT9Qi3YgTALZfjaY4jU3HLVKBtHdSXxoPoLA3naMPcHBcY88FcF 1"), runtime_error);
}

/**
 * This test covers RPC command z_validateaddress
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_validateaddress)
{
    SelectParams(CBaseChainParams::Network::MAIN);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // Check number of args
    BOOST_CHECK_THROW(CallRPC("z_validateaddress"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_validateaddress toomany args"), runtime_error);

    // This address is not valid, it belongs to another network
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress tZRprtxz3ZzEYaSYSTZmptBSSaHwavexM1ivj78Vv2QppzpUcqQAhwPAYF99Ld6onX1i9b6YhJSLmsz1dcYzCnA5RpUgUQG"));
    UniValue resultObj = retValue.get_obj();
    bool b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // This Sapling address is not valid, it belongs to another network
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress ptestsapling1vqv3eu7n68k2n4fkngtqcc4qc0gca0rzx9pygyydzv9um4qty58hf9qx3pumfs2klzacxaykwnq"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // This Sapling address is valid, but the spending key is not in this wallet
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress ps1d5pj2rmj07ndntpfasjppv6cd0ru00rv06a6pudqp948knn9zmt39caxgj6gyjawljgtgpetpr0"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "type").get_str(), "sapling");
    b = find_value(resultObj, "ismine").get_bool();
    BOOST_CHECK_EQUAL(b, false);
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifier").get_str(), "6d03250f727fa6d9ac29ec");
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifiedtransmissionkey").get_str(), "b490fcae4b82b444a6e312d716654e7b6a09a0f1a0bb7e6cbcc7c76b58b31024");
}

/*
 * This test covers RPC commands z_listaddresses, z_importkey, z_exportkey
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_importexport)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    UniValue retValue;
    size_t n1 = 1000; // number of times to import/export

    // error if no args
    BOOST_CHECK_THROW(CallRPC("z_importkey"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_exportkey"), runtime_error);

    // error if too many args
    BOOST_CHECK_THROW(CallRPC("z_importkey way too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_exportkey toomany args"), runtime_error);

    // wallet should currently be empty
    set<libzcash::SaplingPaymentAddress> saplingAddrs;
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK(saplingAddrs.empty());

    auto m = GetTestMasterSaplingSpendingKey();

    // verify import and export key
    for (size_t i = 0; i < n1; i++)
    {
        // create a random Sapling key locally
        auto testSaplingSpendingKey = m.Derive(static_cast<uint32_t>(i));
        auto testSaplingPaymentAddress = testSaplingSpendingKey.DefaultAddress();
        string testSaplingAddr = keyIO.EncodePaymentAddress(testSaplingPaymentAddress);
        string testSaplingKey = keyIO.EncodeSpendingKey(testSaplingSpendingKey);
        BOOST_CHECK_NO_THROW(CallRPC(string("z_importkey ") + testSaplingKey));
        BOOST_CHECK_NO_THROW(retValue = CallRPC(string("z_exportkey ") + testSaplingAddr));
        BOOST_CHECK_EQUAL(retValue.get_str(), testSaplingKey);
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n1);

    // Put addresses into a set
    unordered_set<string> myaddrs;
    for (UniValue element : arr.getValues()) {
        myaddrs.insert(element.get_str());
    }

    // Verify number of addresses stored in wallet is n1
    size_t numAddrs = myaddrs.size();
    BOOST_CHECK(numAddrs == n1);
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK(saplingAddrs.size() == numAddrs);

    // Ask wallet to list addresses
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == numAddrs);

    // Create a set from them
    unordered_set<string> listaddrs;
    for (UniValue element : arr.getValues()) {
        listaddrs.insert(element.get_str());
    }

    // Verify the two sets of addresses are the same
    BOOST_CHECK(listaddrs.size() == numAddrs);
    BOOST_CHECK(myaddrs == listaddrs);

     // Check if too many args
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress toomanyargs"), runtime_error);
}

// Check if address is of given type and spendable from our wallet.
template <typename ADDR_TYPE>
void CheckHaveAddr(const libzcash::PaymentAddress& addr) {

    BOOST_CHECK(IsValidPaymentAddress(addr));
    auto addr_of_type = get_if<ADDR_TYPE>(&addr);
    BOOST_ASSERT(addr_of_type != nullptr);

    HaveSpendingKeyForPaymentAddress test(pwalletMain);
    BOOST_CHECK(test(*addr_of_type));
}

void CheckRPCThrows(string rpcString, string expectedErrorMessage) {
    try {
        CallRPC(rpcString);
        // Note: CallRPC catches (const UniValue& objError) and rethrows a runtime_error
        BOOST_FAIL("Should have caused an error");
    } catch (const runtime_error& e) {
        BOOST_CHECK_EQUAL(expectedErrorMessage, e.what());
    } catch(const exception& e) {
        BOOST_FAIL(string("Unexpected exception: ") + typeid(e).name() + ", message=\"" + e.what() + "\"");
    }
}

BOOST_AUTO_TEST_CASE(rpc_wallet_z_getnewaddress)
{
    using namespace libzcash;
    UniValue addr;

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    KeyIO keyIO(Params());
    // No parameter defaults to sapling address
    addr = CallRPC("z_getnewaddress");
    CheckHaveAddr<SaplingPaymentAddress>(keyIO.DecodePaymentAddress(addr.get_str()));

    // Passing 'sapling' should also work
    addr = CallRPC("z_getnewaddress sapling");
    CheckHaveAddr<SaplingPaymentAddress>(keyIO.DecodePaymentAddress(addr.get_str()));

    // Should throw on invalid argument
    CheckRPCThrows("z_getnewaddress garbage", "Invalid address type");

    // Too many arguments will throw with the help
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress many args"), runtime_error);
}

/**
 * Test Async RPC operations.
 * Tip: Create mock operations by subclassing AsyncRPCOperation.
 */

class MockSleepOperation : public AsyncRPCOperation {
public:
    chrono::milliseconds naptime;
    MockSleepOperation(int t=1000) {
        this->naptime = chrono::milliseconds(t);
    }
    virtual ~MockSleepOperation() {
    }
    virtual void main() {
        set_state(OperationStatus::EXECUTING);
        start_execution_clock();
        this_thread::sleep_for(chrono::milliseconds(naptime));
        stop_execution_clock();
        UniValue v(UniValue::VSTR, "done");
        set_result(move(v));
        set_state(OperationStatus::SUCCESS);
    }
};


/*
 * Test Aysnc RPC queue and operations.
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations)
{
    shared_ptr<AsyncRPCQueue> q = make_shared<AsyncRPCQueue>();
    BOOST_CHECK(q->getNumberOfWorkers() == 0);
    vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size()==0);

    shared_ptr<AsyncRPCOperation> op1 = make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    BOOST_CHECK(q->getOperationCount() == 1);

    OperationStatus status = op1->getState();
    BOOST_CHECK(status == OperationStatus::READY);

    AsyncRPCOperationId id1 = op1->getId();
    int64_t creationTime1 = op1->getCreationTime();

    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 1);

    // an AsyncRPCOperation doesn't do anything so will finish immediately
    this_thread::sleep_for(chrono::seconds(1));
    BOOST_CHECK(q->getOperationCount() == 0);

    // operation should be a success
    BOOST_CHECK_EQUAL(op1->isCancelled(), false);
    BOOST_CHECK_EQUAL(op1->isExecuting(), false);
    BOOST_CHECK_EQUAL(op1->isReady(), false);
    BOOST_CHECK_EQUAL(op1->isFailed(), false);
    BOOST_CHECK_EQUAL(op1->isSuccess(), true);
    BOOST_CHECK_EQUAL(op1->getError().isNull(), true);
    BOOST_CHECK_EQUAL(op1->getResult().isNull(), false);
    BOOST_CHECK_EQUAL(op1->getStateAsString(), "success");
    BOOST_CHECK_NE(op1->getStateAsString(), "executing");

    // Create a second operation which just sleeps
    auto op2 = make_shared<MockSleepOperation>(2500);
    AsyncRPCOperationId id2 = op2->getId();
    int64_t creationTime2 = op2->getCreationTime();

    // it's different from the previous operation
    BOOST_CHECK_NE(op1.get(), op2.get());
    BOOST_CHECK_NE(id1, id2);
    BOOST_CHECK_NE(creationTime1, creationTime2);

    // Only the first operation has been added to the queue
    vector<AsyncRPCOperationId> v = q->getAllOperationIds();
    set<AsyncRPCOperationId> opids(v.begin(), v.end());
    BOOST_CHECK(opids.size() == 1);
    BOOST_CHECK(opids.count(id1)==1);
    BOOST_CHECK(opids.count(id2)==0);
    shared_ptr<AsyncRPCOperation> p1 = q->getOperationForId(id1);
    BOOST_CHECK_EQUAL(p1.get(), op1.get());
    shared_ptr<AsyncRPCOperation> p2 = q->getOperationForId(id2);
    BOOST_CHECK(!p2); // null ptr as not added to queue yet

    // Add operation 2 and 3 to the queue
    q->addOperation(op2);
    auto op3 = make_shared<MockSleepOperation>(1000);
    q->addOperation(op3);
    this_thread::sleep_for(chrono::milliseconds(500));
    BOOST_CHECK_EQUAL(op2->isExecuting(), true);
    op2->cancel();  // too late, already executing
    op3->cancel();
    this_thread::sleep_for(chrono::milliseconds(3000));
    BOOST_CHECK_EQUAL(op2->isSuccess(), true);
    BOOST_CHECK_EQUAL(op2->isCancelled(), false);
    BOOST_CHECK_EQUAL(op3->isCancelled(), true);


    v = q->getAllOperationIds();
    copy( v.begin(), v.end(), inserter( opids, opids.end() ) );
    BOOST_CHECK(opids.size() == 3);
    BOOST_CHECK(opids.count(id1)==1);
    BOOST_CHECK(opids.count(id2)==1);
    BOOST_CHECK(opids.count(op3->getId())==1);
    q->finishAndWait();
}


// The CountOperation will increment this global
atomic<int64_t> gCounter(0);

class CountOperation : public AsyncRPCOperation {
public:
    CountOperation() {}
    virtual ~CountOperation() {}
    virtual void main() {
        set_state(OperationStatus::EXECUTING);
        gCounter++;
        this_thread::sleep_for(chrono::milliseconds(1000));
        set_state(OperationStatus::SUCCESS);
    }
};

// This tests the queue waiting for multiple workers to finish
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations_parallel_wait)
{
    gCounter = 0;

    auto q = make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 4);

    int64_t numOperations = 10;     // 10 * 1000ms / 4 = 2.5 secs to finish
    for (int i=0; i<numOperations; i++)
    {
        auto op = make_shared<CountOperation>();
        q->addOperation(op);
    }

    vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size()==numOperations);
    q->finishAndWait();
    BOOST_CHECK_EQUAL(q->isFinishing(), true);
    BOOST_CHECK_EQUAL(numOperations, gCounter.load());
}

// This tests the queue shutting down immediately
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations_parallel_cancel)
{
    gCounter = 0;

    auto q = make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 2);

    int numOperations = 10000;  // 10000 seconds to complete
    for (int i=0; i<numOperations; i++)
    {
        auto op = make_shared<CountOperation>();
        q->addOperation(op);
    }
    auto ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size()==numOperations);
    q->closeAndWait();

    int numSuccess = 0;
    int numCancelled = 0;
    for (auto & id : ids) {
        auto ptr = q->popOperationForId(id);
        if (ptr->isCancelled()) {
            numCancelled++;
        } else if (ptr->isSuccess()) {
            numSuccess++;
        }
    }

    BOOST_CHECK_EQUAL(numOperations, numSuccess+numCancelled);
    BOOST_CHECK_EQUAL(gCounter.load(), numSuccess);
    BOOST_CHECK(q->getOperationCount() == 0);
    ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size()==0);
}

// This tests z_getoperationstatus, z_getoperationresult, z_listoperationids
BOOST_AUTO_TEST_CASE(rpc_z_getoperations)
{
    auto q = getAsyncRPCQueue();
    auto sharedInstance = AsyncRPCQueue::sharedInstance();
    BOOST_CHECK(q == sharedInstance);

    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus []"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus [\"opid-1234\"]"));
    BOOST_CHECK_THROW(CallRPC("z_getoperationstatus [] toomanyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getoperationstatus not_an_array"), runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult []"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult [\"opid-1234\"]"));
    BOOST_CHECK_THROW(CallRPC("z_getoperationresult [] toomanyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getoperationresult not_an_array"), runtime_error);

    auto op1 = make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    auto op2 = make_shared<AsyncRPCOperation>();
    q->addOperation(op2);

    BOOST_CHECK(q->getOperationCount() == 2);
    BOOST_CHECK(q->getNumberOfWorkers() == 0);
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 1);
    this_thread::sleep_for(chrono::milliseconds(1000));
    BOOST_CHECK(q->getOperationCount() == 0);

    // Check if too many args
    BOOST_CHECK_THROW(CallRPC("z_listoperationids toomany args"), runtime_error);

    UniValue retValue;
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listoperationids"));
    BOOST_CHECK(retValue.get_array().size() == 2);

    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    UniValue array = retValue.get_array();
    BOOST_CHECK(array.size() == 2);

    // idempotent
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    BOOST_CHECK(array.size() == 2);

    for (UniValue v : array.getValues()) {
        UniValue obj = v.get_obj();
        UniValue id = find_value(obj, "id");

        UniValue result;
        // removes result from internal storage
        BOOST_CHECK_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        UniValue resultArray = result.get_array();
        BOOST_CHECK(resultArray.size() == 1);

        UniValue resultObj = resultArray[0].get_obj();
        UniValue resultId = find_value(resultObj, "id");
        BOOST_CHECK_EQUAL(id.get_str(), resultId.get_str());

        // verify the operation has been removed
        BOOST_CHECK_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        resultArray = result.get_array();
        BOOST_CHECK(resultArray.size() == 0);
    }

    // operations removed
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    BOOST_CHECK(array.size() == 0);

    q->close();
}

BOOST_AUTO_TEST_CASE(rpc_z_sendmany_parameters)
{
    SelectParams(CBaseChainParams::Network::TESTNET);
    KeyIO keyIO(Params());
    LOCK2(cs_main, pwalletMain->cs_wallet);

    BOOST_CHECK_THROW(CallRPC("z_sendmany"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_sendmany toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_sendmany just too many args here"), runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ []"), runtime_error);
    // empty amounts
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ []"), runtime_error);

    // don't have the spending key for this address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"
            "UkJ1oSfbhTJhm72WiZizvkZz5aH1 []"), runtime_error);

    // duplicate address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "[{\"address\":\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\", \"amount\":50.0},"
            " {\"address\":\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\", \"amount\":12.0} ]"
            ), runtime_error);

    // invalid fee amount, cannot be negative
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "[{\"address\":\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\", \"amount\":50.0}] "
            "1 -0.0001"
            ), runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "[{\"address\":\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\", \"amount\":50.0}] "
            "1 21000001"
            ), runtime_error);

    // fee amount is bigger than sum of outputs
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "[{\"address\":\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\", \"amount\":50.0}] "
            "1 50.00000001"
            ), runtime_error);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    vector<SendManyRecipient> vTRecipients; // vector of transparent-address recipients
    vector<SendManyRecipient> vZRecipients; // vector of z-address recipients
    // Test constructor of AsyncRPCOperation_sendmany
    try {
        auto operation = make_shared<AsyncRPCOperation_sendmany>(nullopt, mtx, "", vTRecipients, vZRecipients, -1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Minconf cannot be negative"));
    }

    try {
        auto operation = make_shared<AsyncRPCOperation_sendmany>(nullopt, mtx, "", vTRecipients, vZRecipients, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "From address parameter missing"));
    }

    try {
        auto operation = make_shared<AsyncRPCOperation_sendmany>(nullopt, mtx, "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ", vTRecipients, vZRecipients, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "No recipients"));
    }

    vTRecipients.emplace_back("dummy", 1, "");
    try {
        auto operation = make_shared<AsyncRPCOperation_sendmany>(nullopt, mtx, "INVALID", vTRecipients, vZRecipients, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Invalid from address"));
    }

    // Testnet payment addresses begin with 'tZ'.  This test detects an incorrect prefix.
    try {
        auto operation = make_shared<AsyncRPCOperation_sendmany>(nullopt, mtx, 
            "tTWgZLnrRJ13fF6YDJmnL32QZqJJD8UfMBcjGhECgF8GTT54SrAkHyvUW5AgbqTF2v4WLRq7Nchrymbr3eyWY2RNoGJjmNL", vTRecipients, vZRecipients, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Invalid from address"));
    }
}

BOOST_AUTO_TEST_CASE(rpc_z_sendmany_taddr_to_sapling)
{
    SelectParams(CBaseChainParams::Network::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    UniValue retValue;

    KeyIO keyIO(Params());
    // add keys manually
    auto taddr = pwalletMain->GenerateNewKey().GetID();
    string taddr1 = keyIO.EncodeDestination(taddr);
    auto pa = pwalletMain->GenerateNewSaplingZKey();
    string zaddr1 = keyIO.EncodePaymentAddress(pa);

    auto consensusParams = Params().GetConsensus();
    retValue = CallRPC("getblockcount");
    int nextBlockHeight = retValue.get_int() + 1;

    // Add a fake transaction to the wallet
    auto mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight);
    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(taddr) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(5 * COIN, scriptPubKey));
    CWalletTx wtx(pwalletMain, mtx);
    pwalletMain->AddToWallet(wtx, true, nullptr);

    // Fake-mine the transaction
    BOOST_CHECK_EQUAL(0, chainActive.Height());
    CBlock block;
    block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex {block};
    fakeIndex.nHeight = 1;
    mapBlockIndex.insert(make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    BOOST_CHECK(chainActive.Contains(&fakeIndex));
    BOOST_CHECK_EQUAL(1, chainActive.Height());
    wtx.SetMerkleBranch(block);
    pwalletMain->AddToWallet(wtx, true, nullptr);

    // Context that z_sendmany requires
    auto builder = TransactionBuilder(consensusParams, nextBlockHeight, pwalletMain);
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight);

    vector<SendManyRecipient> vtRecipients;
    vector<SendManyRecipient> vzRecipients = { SendManyRecipient(zaddr1, 1 * COIN, "ABCD") };
    auto operation = make_shared<AsyncRPCOperation_sendmany>(builder, mtx, taddr1, vtRecipients, vzRecipients, 0);

    // Enable test mode so tx is not sent
    operation->testmode = true;

    // Generate the Sapling shielding transaction
    operation->main();
    BOOST_CHECK(operation->isSuccess());

    // Get the transaction
    auto result = operation->getResult();
    BOOST_ASSERT(result.isObject());
    auto hexTx = result["hex"].getValStr();
    CDataStream ss(ParseHex(hexTx), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    ss >> tx;
    BOOST_ASSERT(!tx.vShieldedOutput.empty());

    // We shouldn't be able to decrypt with the empty ovk
    BOOST_CHECK(!AttemptSaplingOutDecryption(
        tx.vShieldedOutput[0].outCiphertext,
        uint256(),
        tx.vShieldedOutput[0].cv,
        tx.vShieldedOutput[0].cm,
        tx.vShieldedOutput[0].ephemeralKey));

    // We should be able to decrypt the outCiphertext with the ovk
    // generated for transparent addresses
    HDSeed seed;
    BOOST_ASSERT(pwalletMain->GetHDSeed(seed));
    BOOST_CHECK(AttemptSaplingOutDecryption(
        tx.vShieldedOutput[0].outCiphertext,
        ovkForShieldingFromTaddr(seed),
        tx.vShieldedOutput[0].cv,
        tx.vShieldedOutput[0].cm,
        tx.vShieldedOutput[0].ephemeralKey));

    // Tear down
    chainActive.SetTip(nullptr);
    mapBlockIndex.erase(blockHash);
    mapArgs.erase("-developersapling");
    mapArgs.erase("-experimentalfeatures");

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

BOOST_AUTO_TEST_CASE(rpc_wallet_encrypted_wallet_sapzkeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n = 100;

    if(!pwalletMain->HaveHDSeed())
    {
        pwalletMain->GenerateNewSeed();
    }

    // wallet should currently be empty
    set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size()==0);

    // create keys
    for (int i = 0; i < n; i++) {
        CallRPC("z_getnewaddress sapling");
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Verify that the wallet encryption RPC is disabled
    BOOST_CHECK_THROW(CallRPC("encryptwallet passphrase"), runtime_error);

    // Encrypt the wallet (we can't call RPC encryptwallet as that shuts down node)
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";

    fs::current_path(GetArg("-datadir","/tmp/thisshouldnothappen"));
    BOOST_CHECK(pwalletMain->EncryptWallet(strWalletPass));

    // Verify we can still list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Try to add a new key, but we can't as the wallet is locked
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress sapling"), runtime_error);

    // We can't call RPC walletpassphrase as that invokes RPCRunLater which breaks tests.
    // So we manually unlock.
    BOOST_CHECK(pwalletMain->Unlock(strWalletPass));

    // Now add a key
    BOOST_CHECK_NO_THROW(CallRPC("z_getnewaddress sapling"));

    // Verify the key has been added
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n+1);

    // We can't simulate over RPC the wallet closing and being reloaded
    // but there are tests for this in gtest.
}


BOOST_AUTO_TEST_CASE(rpc_z_listunspent_parameters)
{
    SelectParams(CBaseChainParams::Network::TESTNET);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // too many args
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 2 3 4 5"), runtime_error);

    // minconf must be >= 0
    BOOST_CHECK_THROW(CallRPC("z_listunspent -1"), runtime_error);

    // maxconf must be > minconf
    BOOST_CHECK_THROW(CallRPC("z_listunspent 2 1"), runtime_error);

    // maxconf must not be out of range
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 9999999999"), runtime_error);

    constexpr auto TEST_ZADDR = "ptestsapling17jcq5vqv44jpm08qtmcnhey40hlpun3jk0ucamkxsu22ju8yr0pmwhngudqrgulhwyzpw0qmqcc";
    // must be an array of addresses
    BOOST_CHECK_THROW(CallRPC(strprintf("z_listunspent 1 999 false %s", TEST_ZADDR)), runtime_error);

    // address must be string
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 false [123456]"), runtime_error);

    // no spending key
    BOOST_CHECK_THROW(CallRPC(strprintf("z_listunspent 1 999 false [\"%s\"]", TEST_ZADDR)), runtime_error);

    // allow watch only
    BOOST_CHECK_NO_THROW(CallRPC(strprintf("z_listunspent 1 999 true [\"%s\"]", TEST_ZADDR)));

    // wrong network, regtest instead of testnet
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 true [\"pzregtestsapling15r8tvulwztl460m5feqmap5fr0xj7qajlzt9g9vhs58c8d2yd6cvuplc9s7qkk5rd2v37fcdyey\"]"), runtime_error);
}


BOOST_AUTO_TEST_CASE(rpc_z_shieldcoinbase_parameters)
{
    SelectParams(CBaseChainParams::Network::TESTNET);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase too many args shown here"), runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
            "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
    "** tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // bad to address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
    "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ INVALIDtnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"), runtime_error);

    // invalid fee amount, cannot be negative
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "-0.0001"
            ), runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
            "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
            "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
            "21000001"
            ), runtime_error);

    // invalid limit, must be at least 0
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
    "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ "
    "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
    "100 -1"
    ), runtime_error);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test constructor of AsyncRPCOperation_sendmany
    string testnetzaddr = "tZRprtxz3ZzEYaSYSTZmptBSSaHwavexM1ivj78Vv2QppzpUcqQAhwPAYF99Ld6onX1i9b6YhJSLmsz1dcYzCnA5RpUgUQG";
    string mainnetzaddr = "PzWcy67ygestjagHaFZxjWxmawMeShmQWNPE8FNJp23pQS2twecwps5223ajUtN7iihxR4MmLDFQ19heHkBx5AKaDooS6aQ";

    vector<ShieldCoinbaseUTXO> vInputs;
    try {
        auto operation = make_shared<AsyncRPCOperation_shieldcoinbase>(TransactionBuilder(), mtx, vInputs, testnetzaddr, -1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Fee is out of range"));
    }

    try {
        auto operation = make_shared<AsyncRPCOperation_shieldcoinbase>(TransactionBuilder(), mtx, vInputs, testnetzaddr, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Empty inputs"));
    }

    // Testnet payment addresses begin with 'tZ'.  This test detects an incorrect prefix.
    try {
        vInputs.emplace_back(uint256(), 0, 0);
        auto operation = make_shared<AsyncRPCOperation_shieldcoinbase>(TransactionBuilder(), mtx, vInputs, mainnetzaddr, 1);
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Invalid to address"));
    }

}

BOOST_AUTO_TEST_CASE(rpc_z_mergetoaddress_parameters)
{
    SelectParams(CBaseChainParams::Network::TESTNET);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CheckRPCThrows("z_mergetoaddress 1 2",
        "Error: z_mergetoaddress is disabled. Run './pascal-cli help z_mergetoaddress' for instructions on how to enable this feature.");

    // Set global state required for z_mergetoaddress
    fExperimentalMode = true;
    mapArgs["-zmergetoaddress"] = "1";

    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress just too many args present for this method"), runtime_error);

    string taddr1 = "tPmCf9DhN5jv5CgrxDMHRz6wsEjWwM6qJnZ";
    string taddr2 = "tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp";
    string aSaplingAddr = "ptestsapling1vqv3eu7n68k2n4fkngtqcc4qc0gca0rzx9pygyydzv9um4qty58hf9qx3pumfs2klzacxaykwnq";

    CheckRPCThrows("z_mergetoaddress [] " + taddr1,
        "Invalid parameter, fromaddresses array is empty.");

    // bad from address
    CheckRPCThrows("z_mergetoaddress [\"INVALID" + taddr1 + "\"] " + taddr2,
        "Unknown address format: INVALID" + taddr1);

    // bad from address
    CheckRPCThrows("z_mergetoaddress ** " + taddr2,
        "Error parsing JSON:**");

    // bad from address
    CheckRPCThrows("z_mergetoaddress [\"**\"] " + taddr2,
        "Unknown address format: **");

    // bad from address
    CheckRPCThrows("z_mergetoaddress " + taddr1 + " " + taddr2,
        "Error parsing JSON:" + taddr1);

    // bad from address
    CheckRPCThrows("z_mergetoaddress [" + taddr1 + "] " + taddr2,
        "Error parsing JSON:[" + taddr1 + "]");

    // bad to address
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] INVALID" + taddr2,
        "Invalid parameter, unknown address format: INVALID" + taddr2);

    // duplicate address
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\",\"" + taddr1 + "\"] " + taddr2,
        "Invalid parameter, duplicated address: " + taddr1);

    // invalid fee amount, cannot be negative
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " -0.0001",
        "Amount out of range");

    // invalid fee amount, bigger than MAX_MONEY
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] "  + taddr2 + " 210000000001",
        "Amount out of range");

    // invalid transparent limit, must be at least 0
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " 0.0001 -1",
        "Limit on maximum number of UTXOs cannot be negative");

    // invalid shielded limit, must be at least 0
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " 0.0001 100 -1",
        "Limit on maximum number of notes cannot be negative");

    CheckRPCThrows("z_mergetoaddress [\"ANY_TADDR\",\"" + taddr1 + "\"] " + taddr2,
        "Cannot specify specific taddrs when using \"ANY_TADDR\"");

    CheckRPCThrows("z_mergetoaddress [\"ANY_SAPLING\",\"" + aSaplingAddr + "\"] " + taddr2,
        "Cannot specify specific zaddrs when using \"ANY_SAPLING\"");

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);

    // Test constructor of AsyncRPCOperation_mergetoaddress
    MergeToAddressRecipient testnetzaddr(
        "tZRprtxz3ZzEYaSYSTZmptBSSaHwavexM1ivj78Vv2QppzpUcqQAhwPAYF99Ld6onX1i9b6YhJSLmsz1dcYzCnA5RpUgUQG",
        "testnet memo");
    MergeToAddressRecipient mainnetzaddr(
        "PzcUi7fe8dgjCH2bgVxhrHDkYFGnMp4i35GtvFhHbdR3Pb7w9fxD6xj17LyMPwyQi9yayQKkqtP2Ypicj9wMLX8QNA5zNFv",
        "mainnet memo");

    vector<MergeToAddressInputUTXO> utxoInputs;
    vector<MergeToAddressInputSaplingNote> saplingNoteInputs;
    try {
        auto operation = make_shared<AsyncRPCOperation_mergetoaddress>(nullopt, mtx, utxoInputs, saplingNoteInputs, testnetzaddr, -1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Fee is out of range"));
    }

    try {
        auto operation = make_shared<AsyncRPCOperation_mergetoaddress>(nullopt, mtx, utxoInputs, saplingNoteInputs, testnetzaddr, 1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "No inputs"));
    }

    utxoInputs.emplace_back(COutPoint(uint256(), 0), 0, CScript());
    try {
        MergeToAddressRecipient badaddr("", "memo");
        auto operation = make_shared<AsyncRPCOperation_mergetoaddress>(nullopt, mtx, utxoInputs, saplingNoteInputs, badaddr, 1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Recipient parameter missing"));
    }

    // Testnet payment addresses begin with 'tZ'.  This test detects an incorrect prefix.
    try {
        auto operation = make_shared<AsyncRPCOperation_mergetoaddress>(nullopt, mtx, utxoInputs, saplingNoteInputs, mainnetzaddr, 1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Invalid recipient address"));
    }

    // Un-set global state
    fExperimentalMode = false;
    mapArgs.erase("-zmergetoaddress");
}

BOOST_AUTO_TEST_SUITE_END()
