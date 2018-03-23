// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "crypto/equihash.h"

#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "base58.h"

using namespace std;

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, 
                                 const std::vector<unsigned char> &genesisPubKey, 
                                 uint32_t nTime, 
                                 uint256 nNonce, 
                                 const std::vector<unsigned char> &nSolution, 
                                 uint32_t nBits, 
                                 int32_t nVersion = 4, 
                                 const CAmount& genesisReward = 0)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << (int)nBits << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = CScript() << genesisPubKey << OP_CHECKSIG;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nSolution = nSolution;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = genesis.BuildMerkleTree();
    return genesis;
}

static void __mineGenBlock(unsigned int n, unsigned int k, CBlock *pblock)
{
    pblock->nNonce.SetNull();
    pblock->nSolution.clear();

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    while (true) {
        // Hash state
        crypto_generichash_blake2b_state state;
        EhInitialiseState(n, k, state);

        // I = the block header minus nonce and solution.
        CEquihashInput I{*pblock};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << I;

        // H(I||...
        crypto_generichash_blake2b_update(&state, (unsigned char*)&ss[0], ss.size());

        // H(I||V||...
        crypto_generichash_blake2b_state curr_state;
        curr_state = state;
        crypto_generichash_blake2b_update(&curr_state,
                                            pblock->nNonce.begin(),
                                            pblock->nNonce.size());

        // (x_1, x_2, ...) = A(I, V, n, k)
        std::function<bool(std::vector<unsigned char>)> validBlock =
            [&pblock, &hashTarget]
            (std::vector<unsigned char> soln) {
            
            // Write the solution to the hash and compute the result.
            pblock->nSolution = soln;

            if (UintToArith256(pblock->GetHash()) > hashTarget) {
                return false;
            }
            printf("Genesis block found  \n  merkle root hash: %s\n  header hash: %s\n  nonce: %s\n  solution: %s\n", 
                                                        pblock->hashMerkleRoot.GetHex().c_str(),
                                                        pblock->GetHash().GetHex().c_str(), 
                                                        pblock->nNonce.GetHex().c_str(),
                                                        HexStr(pblock->nSolution).c_str());

            return true;
        };

        if (EhOptimisedSolveUncancellable(n, k, curr_state, validBlock))
            break;

        pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);
    }
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database (and is in any case of zero value).
 *
 * >>> from pyblake2 import blake2s
 * >>> 'AnimeCoin' + blake2s(b'The Economist 2018-03-15 the infant world of cryptocurrencies is already mind-bogglingly crowded. BTC#514336 000000000000000000421fe5078b5e3011e572d596374af44516fe1745ec0dc3 ZEC#291407 000000000151900c0248f3eb0b3fbabdb8fe47ec0a56d09a9e8e44f9c7787568 DJIA close on 19 March 2018: 24,610.91').hexdigest()
 * 
 * CBlock(hash=00040fe8, ver=4, hashPrevBlock=00000000000000, hashMerkleRoot=c4eaa5, nTime=1477641360, nBits=1f07ffff, nNonce=4695, vtx=1)
 *   CTransaction(hash=c4eaa5, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff071f0104455a6361736830623963346565663862376363343137656535303031653335303039383462366665613335363833613763616331343161303433633432303634383335643334)
 *     CTxOut(nValue=0.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: c4eaa5
 */
static const char* AnimeGenesisTimestamp = "AnimeCoin2a1f1b88e25c065678459c577460879513340d2e110eb8d2c735e066ab604907";
static const std::vector<unsigned char> AnimeGenesisPubKey = ParseHex("0421105e5dc396643b48ece657fac0e254cba81b93f1290030869b26097f277281e226f708aa4e98156aae3ee23be7c2793e972081c356cc5afdd6a458fde59f9b");

static CBlock CreateMainnetGenesisBlock()
{
    uint32_t nTime = 1521515598;
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000130");
    std::vector<unsigned char> nSolution = ParseHex("001f8eabd201d45f6531b20c0e9de4cd38a34f5b01074ee7440f49b6c9383f4863badcbb1ae803fd196c0895820fb9a36ff9f6f5f12f0a96f29710faddecd3138f30b9f51ea3cdb34775a8eb3854a701b1bb5cb30143f7a1caaecca9c39632c4eba4011d4c401f7e8025b2f30257cfe492a199c2a3ef6ba1ce4b5e7624c202b2f7bc3d380981d6b9b04ee2a9915ea06a9d9182640f975ed323406fcc0e8819a473bf5278c57e8ce9004c7457f48030173d6d024988e9fd1d8187108ba6350d2b6ba065fa65510ad3dadeb133356dc14bb4c50277e6d77ed271a770117030d10e045da16cafec0a33870ff28470961fff2ff4f28a4890d6a37f371030086c546f67aba0f7698f3138a9e4c265f84f3e34cc3b4dfb892a92060e91cb33db30c385868164f7719119eb0b40694e253f116c67e1a2f71622b90378ad0f25a846d978169d47067fc26514c520edaf4a92bd2c011d825892a671878e08106b19b6ea468313d7bfa1303303d49a53abe7231a36efd1c202ce52a11fa61f240b3359dfcaff8af676855afbf3912b3276dfa665373e9dc731154e4d6e03e4abf142751a1f98b679480385d8b2acc28daaa1594083f38ef1f442f5f885972b41d56a159546872977042dd4c03025cb85180fd907bbe657728ec33968eb05c07b6fe89586784e4e46101b668c154e9f33379d0228adfa6db5cba52f7dce040ffa800922a8fbfb0c3673b2eedb4e9bba1555b14a85452cb61f6fbdc25ab518f7545d668e3697d3b707547f42f3c4c276980b126d7ec346ee548bfefdd6330cdb43a3159bed9e1a49f239d1f152e5427884bb0a191912c6c6bbc779ba95a112f1d1ca672e5cca252b4f2510d2f7d227f5daf829b2c86a426bbb93c8470bdad99c9dec4413a9581477e6ea0ee92a5bdfa57d2db3be31b455b38d7ec315ab49c39ef581174e5913007fa7f54acc36751c6080b8401f8e1c7ca13f687225c7645c2f17a296de01a70a1138ec49e14518f88017d9035618144e8b05da147fb2fb11aa21415816003d2f12df6e10780b4b2f47960fcd853a6402fda15805a415f8eed378b96aa0033c634d17450e8e785d532c8e118cc3912a89e69be6bd786ad8822caa1384a7061e2b58768be101629f91059a927d4d25aefdfad168d53422b81ae0b37dcc6b6420f29e5ae0b0bd44340445a12419c93a034a5d330ee8c4923da2a93514350a3c1c2ba487a31761dcb7d3dc41e78f126298e2741792135b7146c03b8028a227832b00a52c21bd77233a5e25d81fdd10ed411e544a70a778c1553712d7de151901b97746cd359190b329b4e42832a63c9b88bb189d433b7e6940b3950ed7d3f5f8becfbd0e7e0a39161a6a59940fdaaf8cd1e3da695226eef18917a9c223b6e4caa61492236f8732b1369fd37d81999cb8ca00cc854fbf8aeb806f51c754b4e5945676c81fa9741eb775f743b191a3c3ee5343b44919b5aa9a3474803097c382422a3737856d149771c72b7a0a8056298242879fec5226581d958a678b6d450457c06b3fe2490595cc432597ed8add779587b6385c098e01f121f316a8eb6a6a0936e117d9b254b7b1d568e9e72e3ee625abaa0cb1964eeabc20576c897d6c8f6f597f671e318ecee89234beede1bf78783c6acc02ff943de73e05e1c6c058e2023743a57595ee71edcdfece34f67d0c71a0ba7be387377676f44ac5f6d49a6dd0173e78238764f28c98ee8ba8d075b7cce5338641b35ae47f242786dc43691539dfc67528c76590d2d7b957a07f0b8172bb89a983d1c5a79281b81d295557f790f60c2c423e5b92cfafb1133d47662447175e94065e5864388ec361c259b3ffdfbec81f5d48e982997cb7ed1a7c6cffa3f9ab2cd594ccfa4b0ae02b9393327e5cc1");
    uint32_t nBits = 0x1f07ffff;

    return CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, nTime, nNonce, nSolution, nBits);
}
static const uint256 MainnetHashMerkleRoot = uint256S("0x69390f621a08c045270a5b43253cf269105b395c67e442468f91b879bb403c1f");
static const uint256 MainnetHashGenesisBlock = uint256S("0x000733cc513d293a4f6f2d49c8b0c27c8700b3b546e100b194742deb9d52db81");

static CBlock CreateTestnetGenesisBlock()
{
    uint32_t nTime = 1521515598;
    uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000000c");
    std::vector<unsigned char> nSolution = ParseHex("0037c32e5f02856791383894426e0f5651aa3aaad4448195fd5d1e5e272b5a650b5a6ab10255bb1ae581009d4bbea14a49a29a4c8096403698a9b5be91f06743d2778b29554dd8c1465aa0b667e80f2fbf79aa1401b2fe96311e9851ca65f79d63ced3e211e15f35e903a94340e7a1da03a2c6e7aa12efe3922f1a12d6d704005535f84c1886a3fe433feab5414cfa6157664e1056128ba207e740621241bdbaf38be0a11029eb69027f46c9de14db15f561510515184ad1cf6a9202f710c48d31aa04f732cdd4f1670921ea9d36416f187625b353bcf86be147de9eb50070acb76d405ddfaeff2a1507f1c2f02eedd64cb2b66a29c82e7bd19427070928b967b119db1504d55253296ed30e5d1878a345350e61e4819cbe43d10755a66c31e89ef77f3e90ff11974399fc5dca5b3668b313877fc7bb01fb7a5fea6022bb9daf2413f9fa6dd9f6f6ee2ac2a95e5dafa7080f94766d49fcc481abb6f788ecbc3b01831f7e2e2b7c6454809430abd4db3698dc7acdd6393e5760bc1a3c51e0259d4d2f726f140701b4f03170eebd7df92e6052ef6da07ac9239833ddbe9f08b93b544d1dd10dde1a27a40d8a6b207815896b5877de7717bbc04622827ab40153d9d9989d4255e9d1370132565193c12c99cfa4034db899721bf3eae96e94457347d9d60650133b7973ebe8f9d231b76a90f5a2f702713891600cca739940a1c509f659b222b5c9a3a1e191ba2f5d24c2427f298bd428a19c05aed4f15785c69a1330750cfdd366edc3e641fd6558e3f9ddd4ee5dd0fa0e191e6dbd4ec59968b9865ad2b00ffab9aacf2dde56a31c28e268626f3ea78f9694c6bf65860e1801d3459b3f6b7dd183dd38bfd2cd97457d42e5f5f6f2b3a995323cac302de4ea2f929325654b354c43d9f6df854054d94f5898337c73bc0805b3e8e675f21876f39dc6014e8cd17e5dbc61a03048adf16407af66729ba09b03506b14a544dcfbddff235efae4dcd514651c156d201ca2c34da001bdba24e422cbc4d1099bbe9c7e935d391d8de01ef601594fae2d5a7939b7aa9c1f8faa07a4bc0f090383a35d849b9f53e3e5d72cc07e7c4918db4d541fe4249b7ec6f622d7d8d26656095840ff2a922f245d225193a10216731cc22882f637791ac82b5c0bbb6a2faca5db9a84c324628b4219d4d0e3650257ae8edbc48b71b21719c800f4ba42de415b9e181d8161f1cfca9c7774d7a48260b64c3b131a794ed11defc5f8860edcddf3afa42034453e5a1d3cfddc612a893d517dd5ea36cfce67b495c6519a6facd4caa3028e7361d273e289ddf7f297bfa66e8a9b96dfe5200aed27f2a68b7e1ed997615cecee01a868e7ec4c740bb078754505cb016bb240e99f24a5fc4ded77f18f1c0f5530e37304e3d62e01dc8c621e90944127c5bf0168cc8088d139d1154b755b826fa06acc0b9d121c08bea4cfc304e3418ce3e377e8faa821c7297d9d4217f23d3c4230387b887be3bbdefbf145981736da312a652e0232939c81b556161e53dbd9becffc96b5dc01a4f040f0922b68967162c12322ef74bf2a7443974842060d7113018ebb15254bcdd167a6c8129fff17154ea91b800a11e8b8baea502a6be81fb2abddb6562ee42322175eaaf188943784f0e09c8e500f5335070724a12ab252f91d43a76256366371a55fab9194be4052f6a574943c96b84324f8f72a6a3e016a7c7214142c635c870cb3dca2b844f1336dc29577df918e001721e6ce548b3cebe2a9b69d675e5a1eb26739d49909bb84d2f14c3d49f3e260ca841ce248c19f9d20172833af93e0938de51bc273973ec638b934a31e80e7268027dd424e73aecd02d8adb6d83c7e6cb29d3a885bd165b274d9cf3edde9f7e86f7314e68bf8381b7b");
    uint32_t nBits = 0x2007ffff;

    return CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, nTime, nNonce, nSolution, nBits);
}
static const uint256 TestnetHashGenesisBlock = uint256S("05cb336861b74bc7f081c0f244dc9e73250dcda39beef2ffea4c382dc79c0080");

static CBlock CreateRegtestGenesisBlock()
{
    uint32_t nTime = 1521515598;
    uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000001d");
    std::vector<unsigned char> nSolution = ParseHex("05b1aa5d42bf1315c230bb4c74c4ddada4d316491bfb128beccd462d17eef9e4cb3653ea");
    uint32_t nBits = 0x200f0f0f;

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, nTime, nNonce, nSolution, nBits);

    // __mineGenBlock(48, 5, &block);

    return block;
}
static const uint256 RegtestHashGenesisBlock = uint256S("07e5442be390f1e52fa99963279ed335a6794f34704def1dd3de753828b09ffc");

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

const arith_uint256 maxUint = UintToArith256(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        network = CBaseChainParams::MAIN;
        strCurrencyUnits = "ANI";
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 4000;
        consensus.powLimit = uint256S("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        /**
         * The message start string
         */
        pchMessageStart[0] = 0x40;
        pchMessageStart[1] = 0x83;
        pchMessageStart[2] = 0x22;
        pchMessageStart[3] = 0x38;
        vAlertPubKey = ParseHex("04a40ca15344822fd6bc9719929050614ab045775fcca1e46880ba7e4c55dc999b6f618091a7e5f5c24bd80854f14ebbc369c35cd2e4944c6cf27a3dc5f54f52dc");
        nDefaultPort = 9933;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 100000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateMainnetGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == MainnetHashGenesisBlock);
        assert(genesis.hashMerkleRoot == MainnetHashMerkleRoot);

        vFixedSeeds.clear();
        vSeeds.clear();
        // vSeeds.push_back(CDNSSeedData("z.cash", "dnsseed.z.cash"));

        // guarantees the first 2 characters, when base58 encoded, are "t1"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xB8};
        // guarantees the first 2 characters, when base58 encoded, are "t3"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBD};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "zc"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0x9A};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVK"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAB,0xD3};
        // guarantees the first 2 characters, when base58 encoded, are "SK"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAB,0x36};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (Checkpoints::CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,      // * UNIX timestamp of last checkpoint block
            0,                  // * total number of transactions between genesis and last checkpoint
                                //   (the tx=... number in the SetBestChain debug.log lines)
            500                 // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        network = CBaseChainParams::TESTNET;
        strCurrencyUnits = "INA";
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidySlowStartInterval = 20000;
        consensus.nSubsidyHalvingInterval = 840000;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 400;
        consensus.powLimit = uint256S("07ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 32; // 32% adjustment down
        consensus.nPowMaxAdjustUp = 16; // 16% adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        pchMessageStart[0] = 0x1b;
        pchMessageStart[1] = 0x16;
        pchMessageStart[2] = 0x83;
        pchMessageStart[3] = 0xd2;
        vAlertPubKey = ParseHex("04e2c4bfd845e0de6b3fced1bbae723154f751f1a11c41765ce8ca0d97b9d44989964b6e733520bd1d265c18710d307ad4a7cc7042225955c3d38e3c9138dc1eac");
        nDefaultPort = 19933;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateTestnetGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == TestnetHashGenesisBlock);

        vFixedSeeds.clear();
        vSeeds.clear();
        // vSeeds.push_back(CDNSSeedData("z.cash", "dnsseed.testnet.z.cash"));

        // guarantees the first 2 characters, when base58 encoded, are "tm"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        // guarantees the first 2 characters, when base58 encoded, are "t2"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "zt"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        // guarantees the first 4 characters, when base58 encoded, are "ZiVt"
        base58Prefixes[ZCVIEWING_KEY]      = {0xA8,0xAC,0x0C};
        // guarantees the first 2 characters, when base58 encoded, are "ST"
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        checkpointData = (Checkpoints::CCheckpointData) {
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,      // * UNIX timestamp of last checkpoint block
            0,                  // * total number of transactions between genesis and last checkpoint
                                //   (the tx=... number in the SetBestChain debug.log lines)
            250                 // * estimated number of transactions per day after checkpoint
                                //   total number of tx / (checkpoint block height / (24 * 24))
        };
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        network = CBaseChainParams::REGTEST;
        strCurrencyUnits = "REG";
        consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidySlowStartInterval = 0;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 0; // Turn off adjustment down
        consensus.nPowMaxAdjustUp = 0; // Turn off adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        pchMessageStart[0] = 0x31;
        pchMessageStart[1] = 0x20;
        pchMessageStart[2] = 0x76;
        pchMessageStart[3] = 0x22;
        nDefaultPort = 18344;
        nMaxTipAge = 24 * 60 * 60;
        nPruneAfterHeight = 1000;
        const size_t N = 48, K = 5;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;
        
        genesis = CreateRegtestGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == RegtestHashGenesisBlock);

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        // These prefixes are the same as the testnet prefixes
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1D,0x25};
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBA};
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0xB6};
        base58Prefixes[ZCSPENDING_KEY]     = {0xAC,0x08};

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (Checkpoints::CCheckpointData){
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,
            0,
            0
        };
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams &Params(CBaseChainParams::Network network) {
    switch (network) {
        case CBaseChainParams::MAIN:
            return mainParams;
        case CBaseChainParams::TESTNET:
            return testNetParams;
        case CBaseChainParams::REGTEST:
            return regTestParams;
        default:
            assert(false && "Unimplemented network");
            return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network) {
    SelectBaseParams(network);
    pCurrentParams = &Params(network);

    // Some python qa rpc tests need to enforce the coinbase consensus rule
    if (network == CBaseChainParams::REGTEST && mapArgs.count("-regtestprotectcoinbase")) {
        regTestParams.SetRegTestCoinbaseMustBeProtected();
    }
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
