// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key_io.h"
#include "main.h"
#include "crypto/equihash.h"

#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>
#include <vector>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

using namespace std;

#define OVERWINTER_STARTING_BLOCK 10
#define SAPLING_STARTING_BLOCK 20

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

// #define MINE_GENESIS
#ifdef MINE_GENESIS
#define MINE_GENESIS_MAIN
#define MINE_GENESIS_TEST
#define MINE_GENESIS_REGT

namespace MineGenesis {
#include "pow/tromp/equi_miner.h"
static void __mineGenBlock(std::string network, bool tromp, unsigned int n, unsigned int k, CBlock *pblock)
{
    printf("Will be mining Genesis block for %s using %s solver\n", network.c_str(), tromp? "tromp": "default");

    pblock->nNonce.SetNull();
    pblock->nSolution.clear();

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    int counter = 0;
    bool bContinue = true;
    while (bContinue) {
        printf("\titteration %d\r", ++counter);
        fflush(stdout);

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
            [&network, &pblock, &hashTarget]
            (std::vector<unsigned char> soln) {
            
            // Write the solution to the hash and compute the result.
            pblock->nSolution = soln;

            if (UintToArith256(pblock->GetHash()) > hashTarget) {
                return false;
            }
            printf("Genesis block for %s found  \n  merkle root hash: %s\n  header hash: %s\n  nonce: %s\n  solution: %s\n",
                                                        network.c_str(),
                                                        pblock->hashMerkleRoot.GetHex().c_str(),
                                                        pblock->GetHash().GetHex().c_str(), 
                                                        pblock->nNonce.GetHex().c_str(),
                                                        HexStr(pblock->nSolution).c_str());

            return true;
        };

        if (tromp) {
            equi eq(1);
            eq.setstate(&curr_state);

            // Intialization done, start algo driver.
            eq.digit0(0);
            eq.xfull = eq.bfull = eq.hfull = 0;
            eq.showbsizes(0);
            for (u32 r = 1; r < WK; r++) {
                (r&1) ? eq.digitodd(r, 0) : eq.digiteven(r, 0);
                eq.xfull = eq.bfull = eq.hfull = 0;
                eq.showbsizes(r);
            }
            eq.digitK(0);

            // Convert solution indices to byte array (decompress) and pass it to validBlock method.
            for (size_t s = 0; s < eq.nsols; s++) {
                LogPrint("pow", "Checking solution %d\n", s+1);
                std::vector<eh_index> index_vector(PROOFSIZE);
                for (size_t i = 0; i < PROOFSIZE; i++) {
                    index_vector[i] = eq.sols[s][i];
                }
                std::vector<unsigned char> sol_char = GetMinimalFromIndices(index_vector, DIGITBITS);

                if (validBlock(sol_char)) {
                    // If we find a POW solution, do not try other solutions
                    // because they become invalid as we created a new block in blockchain.
                    bContinue = false;
                    break;
                }
            }
        } else if (EhOptimisedSolveUncancellable(n, k, curr_state, validBlock)) {
            break;
        }

        pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);
    }
}
}
#endif

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database (and is in any case of zero value).
 *
 * >>> from pyblake2 import blake2s
 * 
 * >>> 'Pascal' + blake2s(b'Forbes February 26, 2019 People Are Saying Bitcoin Has Bottomed--New Price Data Suggests They Might Be Right. BTC#564824 0000000000000000000ff7f392bc97294718b8a3f63c8a0fe88a46ef80ddf44d ZEC#488580 0000000000e7383a64ce15f2cea9d3ce9641c0e42bcf049bf854be7dc00170b2 DJIA Feb 26, 2019 at 5:00 p.m. EST 26,057.98').hexdigest()
 *  => Pascal75190917cbad4d664271864af9e5b95cdbbb8f3bc66bb58973da87a34fbd3a91
 * 
 * 
 * nTime - epoc time
 * 02/10/2019 00:00:00 GMT = 1549756800
 * 
 */

static const std::vector<unsigned char> PastelGenesisPubKey = ParseHex("04b985ccafe6d17ac5d84cb8c06a69cefad733ee96b4b93bcf5ef0897778c227ee7e74e7680cc219236e4c6a609dbcdeb5bf65cea9c2576c2a0fbef590657c8e7a");
static const char* PastelGenesisTimestamp = "Pascal75190917cbad4d664271864af9e5b95cdbbb8f3bc66bb58973da87a34fbd3a91";
static const uint32_t EpocTime = 1549756800;

static CBlock CreateMainnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x1f07ffff;

#ifdef MINE_GENESIS_MAIN
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("00000000000000000000000000000000000000000000000000000000000000cb");
    std::vector<unsigned char> nSolution = ParseHex("00db635c1dd6bc116fa5d17a3f5a73bd4687cc80f40146c29250078ec570ba1479b03fdf2ef401397c01058c99393d112d3fc220a2dc43fe865cf55e2946190deaa9c58cee1337d25c9484183c95dfa353bde7cc06ffd68b199eb0f51e1c25dad9659a52f259f9215b0c79623465cbbc537c0b10dbb3c7eee4f5d8bbc8462bdf5e352e2b525de761d3077476938d44cdf0780e33d71d00d2ded6d57adf1661517bbea2a7fdfe276a023dbddb84c163e6d957e52ae23014adb7ecb5d9192adb8523a5d7312ccc9ca803d1eebcfe60379be2a00c2f63dc5e4b794ab281724786d96f3d6d44f5f08c3853f1e02917f11d23cd65a7f9367209ef2cb8b3830d1de956497cb0d7e6c9c50eb2d2b5f602363992d218bca8fcfb462fc3199168f112d94947002899714121108df2274e95aacd780456f6aa7beaa78c1b4a4f22a5d7ac501cfc83edb4f3e309e0c631eb7e3ab75003653613b6d32cbfde16417e912a25e290385aaa3d0fcbf0e474ea7f81d040751f27c265299183b9ae3210fe1a60084f02bbc0b5a3e0f3a9adcabb265a755627a6fd9fd3e2f153f133481ecb4a7ad7d5fa5f54e7066e33c62759522bb62ec09fec7327d49fda1cc5410e79ce51bbc81f0cc1c1e4168f3eac85e223711ec10e9824d28789f6b07105b830117f687f194038cae310798fd9f6a1b2513c2801bda15c4a42305817c801045149a860445b8046a0a37030e97c1ab291df9b7013c05720e8d72eaec68323ccbc6b776da3fad6d0e7188b3d31620c5a69b3b671cdea426e747e51fa80aa241ae7d445d03cff547714bd93f7c3a61abb1d38dd093d61a2b53c16c7fb5b629481503478dbc48c42f32e0adea8a059d67d86d45420f7479c223bbbbab7ea32509fa664109e1f4aa304bc1fb8b3baf2571b666f4c25ba9d45397bddd45da8cc57cf88ba35a83a9a080142142758d6b094f65db253a857ae05d7513d3a4811863fc48e0b4e0a8fdba4d28655dd01c8a2d0c2c7047293ec499040bcb02f7165b9aa271c7f7ace94910c7cce011cc327b5c6ce21f0fe254f5d45d2ccaa3601ea5d1b3008bdef3669a48ff15724d6bf74b768031dd177dbf31af43782a27233b2ee92f797dafd4ae10aa4c88cf69cbabff8227237f676fea671569ed38a100ebb2237df8189b120d9fd1ce64b528a79f868900803ab9e0224fb975b0e034908c24e9170e2b09dfb36acabe3add6e290dbd0341539be2589c245d4d5a71288ceeded86ec8ef95c91d4f4bca054ab9ebb42484d47d79a845c0042f3da764fe13f005da66cfedadd094b8d3037899f210d9ff7fd65e0ac1af71fff7d0e1c9d2a033c18488b956b2264c7ec003dcfb8fd7179131450c56f552302f7d6fd66de73e3e35e4cbfb5e12465f175e3edd479a8f1a5507151bc31681fd676bb0372af2f66ee3fb791a95135b694c8ed160ccdd95c059ca8dffed8f21da0cb2649dc657eaf8b933dbd8c16de71a86987a2d2d224b2a815b5730dba3ff264841ac876ee6c12f09bfaecd3f0ae4a60e2f4adfb768008dfff2f47ab2403a049f0c25b1a9c588f0172c258673de5a06c5d8c291d69a7c9aee5fbee4f2c9798f416f72c941ee7f8dd53329ab31b674bb6ad799f095926df0ae12b9f02c558d3930b137a6911bba1fa4f1f0390c6361b57d1610504556272be44d64cf739167d0e59f42dbc4a4817ba0d0a4425db51cf138cf8bdcb1547ba7d7109a7c96c578385207999172bc03d67b9184f34a88f4a437e5e60c4e33e4b439594df788ea80c4df761a1e2174f9198b2a94f61729ed28e38a6a432805537cfe1102d521de56918e19351e67178399417bc31fdc78b4af0a209f2f095fe699d3587b0a820330c4cebeacefdf75bc6f7b26d7c8c5afd3859336b");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_MAIN
    MineGenesis::__mineGenBlock("MainNet", true, 200, 9, &block);
#endif

    return block;
}
static const uint256 MainnetHashMerkleRoot = uint256S("f7c3558575fd9e5bad6667915192b7fc8fbe164b41571e644b21519a51c60152");
static const uint256 MainnetHashGenesisBlock = uint256S("00063058b434e66f86f34c7be78a16a2f2863952934ae95ec51c6f9c89ec63ca");

static CBlock CreateTestnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x2007ffff;

#ifdef MINE_GENESIS_TEST
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000010");
    std::vector<unsigned char> nSolution = ParseHex("00dbaabb958acfad55c716108a499bb381c5fdbd7d4b09cf2bfb9be7bdc1d5a5929a355b4179d2ed155205389c9f9ac6ac75608992ca755c39b19fbaee42f1367f7c9a63b3bb51b10c43eb5dad8711a85df41181074b73d46f5877a77524a28507df13998ef193ea4e0b56dd007b8fe303a136c7642ac5f501da4c9d72371639390d7e5315ffefaf18ddf5d326277f653cffe82a83b4678aaad7d1baf2331c9d260d497a9d95b9a30eb4cd4b238d13219c4be3dc90b0859638403810542b9f474b92650915ec96e4232e76254afedebe4ea129117c7c3f1bbf1f9c249303eb2c7db1c19f8fefed6ee59d18e3213dbb0c8ba8c628ee31567e6f76a4541038f7ac211c6d43171201b3120e1db9a91bdbcfbf2a003732e9a8b3f96abce4af884683223eba92bf8336deb3409228f2a9521f0393136fe05a13c49aebb646678572c1a8742b6d07979c4df02d7726f9fe0ff002eefa7e6301b19b93bd54e612673acd77967e2dbf11e10a043fce99bfe28aa26d1219aa0107f76bf2e00ae513bce94c60deab8548d683f945d28d865cfc4a698536a69ba19b035f23a73c6cfaab72098f1f93840d3858b534c8bd0a96958590e4e8cdf2a926bb03252733d49fdcd04e9f33b4c4cd24af5c9e2a74fb9c800f1c0596fb0b76a3f2495413da4f2211af675d579665757fd84c5ddad1f5f12a2167f2dc6ab3cb7b61620379c96743051af3aff251a0675c7697b04a9d86973fa54d18ded118ed289174a5547862f9ba5893768907b1a2a654cb03666899b1a2ff91e519bf927f351b15e90fe6a6d0dc75ef18b2a7ddcae0aa2699d2dfda10349f3b6d9f69212d61731625a974d8e773bae8cf46b16a6fe35cc47b7a3555ae48704372ed6dd998dd14e2c7a70bf369cba5a209ae48e4b2e2cbc7dedf3e2bcc497f3c1f18bd51f544f2a47a88a5f0d84fd64f014e4ff5291b1d034ad843ddd96effc975661241dc18335e43804d9dc8ecba5573c078e9edfac85a5c0b0711620b280e50652302613a16a82c21358b18b43e1778e9fd14d778bb9184518bc2ad2c4e7b89b68d621891fa74035aabe35d50220b4f740ce9fc3bda0a0b5620a49ee26b0de9aa26d76440c889473c487aa81b27cd531b958c3532dc1ce30683e5a6c11f2f1437c431439d7852a899e19e5e29c2a8d9ab26d2ddbc2d9802b9465655cc46fccf82e2d0603476f9835f7c0d944b78b4117175ec33bf8d0686dd7769edbce4af3eec089b58798717f2af203fd0a90afd8e982c58fe28e81f3c4b66a813c8e312e3f7a20074fe9ec31716e9690643866b9d3469c9cb4344b37a4579f9ec107a8d430dbb174a43040ec47b60b157ef419a921b1d1376820865b8a63a967cbdfc8f4134e2b2b8154ffe5633c0097f0332374aeae5eaf204c62279f20557b51e899101f88f0db1ee58edd1c2e661d96842d1feb9fd3f4419026346a088cdb8575ee1d0a6a0c5d15bebde675e1a5dac69072b3d33a30677183abe7a43c1213f4ea31e99d9482620eaa98f7465d0bdf759923321d5a33519290404d656bb892856c236e3395861bb63f6ccae20f5f6ced5cf22849d1796b29ecd994201dd56a43e1bd486b451d70562fc1012e08bbfb9595d82158d2c45fcf6d06f9489d5bed1b5a31fec062aef2fb7fdb607770a7c849004695787d2588e37b44e2d02dec82d45e82ba016e33a9b2a49364bcb385f11e29439ba4614f281c20429d477a651315b6bb5671501c51199371a157d32430fe3ab6619c2f970a9efaec1611d813d0d62b30fc2c9bbe9992ae0f3c2b505c64d071af17c1e4fdd7301126ca09b56034b0b4288b25a567ed0fc1f08f2bd304e95e0c54e8c37656f5187a983df1c7f543f0af4c562cdf92be4481c8dc49bf625afb5199d");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_TEST
    MineGenesis::__mineGenBlock("TestNet", true, 200, 9, &block);
#endif
    return block;
}
static const uint256 TestnetHashGenesisBlock = uint256S("007aefcd7d4741f7988d6a69a06bd1358c57c0e278098901e32c67f6b30ed1bc");

static CBlock CreateRegtestGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x200f0f0f;

#ifdef MINE_GENESIS_REGT
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000002c");
    std::vector<unsigned char> nSolution = ParseHex("0436417004470d55c41f3d22fb024c6d518e34dd4e3d098f8e75d850b8a3fd556339bdc8");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_REGT
    MineGenesis::__mineGenBlock("RegTest", false, 48, 5, &block);
#endif

    return block;
}
static const uint256 RegtestHashGenesisBlock = uint256S("08ab5f4878719b76002bcefd34d4088c22ae98b110d6e5779a61b1904887b063");

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
        strCurrencyUnits = "PSL";
        bip44CoinType = 133; // As registered in https://github.com/patoshilabs/slips/blob/master/slip-0044.md
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
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = boost::none;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight = Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170005;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = OVERWINTER_STARTING_BLOCK;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = SAPLING_STARTING_BLOCK;
        consensus.nMaxGovernanceAmount = 1000000*COIN;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000006f31c0e1f30221");
        
        /**
         * The message start string
         */
        pchMessageStart[0] = 0x6b;
        pchMessageStart[1] = 0xda;
        pchMessageStart[2] = 0xb8;
        pchMessageStart[3] = 0xfc;
        // vAlertPubKey = ParseHex("04584eef713ec776ee69cb8001d65fd02c513e896247f1715e77ea718b5b208819b5cbdcdafd8bbe7816f53e51d6c87f133051165888ba82c21215edbb6daf4aba");
        vAlertPubKey = ParseHex("0441f3821b035bc418b8fbe8e912005112826a5c51fdcf5fbac6d7dd2ab545183049e51c3f2ed2a70b1e48a59b4c3367c15d30fbff461afc6b83932fefedfe5d41");
        nDefaultPort = 9933;
        nPruneAfterHeight = 100000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateMainnetGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
#ifndef MINE_GENESIS
        assert(consensus.hashGenesisBlock == MainnetHashGenesisBlock);
        assert(genesis.hashMerkleRoot == MainnetHashMerkleRoot);
#endif

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("pastel.network", "dnsseed.pastel.network"));

        // guarantees the first 2 characters, when base58 encoded, are "Pt"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x0c,0xe3};
        // guarantees the first 2 characters, when base58 encoded, are "pt"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1a,0xF6};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "Pz"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x09,0x05};
        // guarantees the first 4 characters, when base58 encoded, are "Px"
        base58Prefixes[ZCVIEWING_KEY]      = {0x09,0x01};
        // guarantees the first 2 characters, when base58 encoded, are "Ps"
        base58Prefixes[ZCSPENDING_KEY]     = {0x9A,0x90};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ps";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-extended-key-main";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData) {
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
        strCurrencyUnits = "LSP";
        bip44CoinType = 1;
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
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 299187;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight = Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = OVERWINTER_STARTING_BLOCK;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = SAPLING_STARTING_BLOCK;
        consensus.nMaxGovernanceAmount = 1000000*COIN;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000001d0c4d9cd");

        /**
         * The message start string
         */
        pchMessageStart[0] = 0x38;
        pchMessageStart[1] = 0xb6;
        pchMessageStart[2] = 0xfe;
        pchMessageStart[3] = 0x64;
        // vAlertPubKey = ParseHex("04b985ccafe6d17ac5d84cb8c06a69cefad733ee96b4b93bcf5ef0897778c227ee7e74e7680cc219236e4c6a609dbcdeb5bf65cea9c2576c2a0fbef590657c8e7a");
        vAlertPubKey = ParseHex("0429aff40718031ed61f0166f3e33b5dfb256c78cdbfa916bf6cc9869a40ce1d66ca35b92fe874bd18b69457ecef27bc3a0f089b737b03fb889dc1420b6a6e70cb");
        nDefaultPort = 19933;
        nPruneAfterHeight = 1000;
        const size_t N = 200, K = 9;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;

        genesis = CreateTestnetGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
#ifndef MINE_GENESIS
        assert(consensus.hashGenesisBlock == TestnetHashGenesisBlock);
#endif

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("pastel.network", "dnsseed.testnet.pastel.network"));

        // guarantees the first 2 characters, when base58 encoded, are "tP"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xEF};
        // guarantees the first 2 characters, when base58 encoded, are "tt"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1D,0x37};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "tZ"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x14,0x3A};
        // guarantees the first 4 characters, when base58 encoded, are "tX"
        base58Prefixes[ZCVIEWING_KEY]      = {0x14,0x37};
        // guarantees the first 2 characters, when base58 encoded, are "tQ" OR "tS"
        base58Prefixes[ZCSPENDING_KEY]     = {0x05,0xFE};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ptestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-extended-key-test";

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;


        checkpointData = (CCheckpointData) {
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
        bip44CoinType = 1;
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
        consensus.nPowAllowMinDifficultyBlocksAfterHeight = 0;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::BASE_SPROUT].nActivationHeight = Consensus::NetworkUpgrade::ALWAYS_ACTIVE;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nProtocolVersion = 170002;
        consensus.vUpgrades[Consensus::UPGRADE_TESTDUMMY].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nProtocolVersion = 170003;
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170006;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT;
        consensus.nMaxGovernanceAmount = 1000000*COIN;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        /**
         * The message start string
         */
        pchMessageStart[0] = 0xcd;
        pchMessageStart[1] = 0xd8;
        pchMessageStart[2] = 0xfa;
        pchMessageStart[3] = 0x9e;
        vAlertPubKey = ParseHex("04b985ccafe6d17ac5d84cb8c06a69cefad733ee96b4b93bcf5ef0897778c227ee7e74e7680cc219236e4c6a609dbcdeb5bf65cea9c2576c2a0fbef590657c8e7a");
        nDefaultPort = 18344;
        nPruneAfterHeight = 1000;
        const size_t N = 48, K = 5;
        BOOST_STATIC_ASSERT(equihash_parameters_acceptable(N, K));
        nEquihashN = N;
        nEquihashK = K;
        
        genesis = CreateRegtestGenesisBlock();
        consensus.hashGenesisBlock = genesis.GetHash();
#ifndef MINE_GENESIS
        assert(consensus.hashGenesisBlock == RegtestHashGenesisBlock);
#endif

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        // These prefixes are the same as the testnet prefixes
        // guarantees the first 2 characters, when base58 encoded, are "tP"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xEF};
        // guarantees the first 2 characters, when base58 encoded, are "tt"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x1D,0x37};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "tZ"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x14,0x3A};
        // guarantees the first 4 characters, when base58 encoded, are "tX"
        base58Prefixes[ZCVIEWING_KEY]      = {0x14,0x37};
        // guarantees the first 2 characters, when base58 encoded, are "tQ" OR "tS"
        base58Prefixes[ZCSPENDING_KEY]     = {0x05,0xFE};

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "pzregtestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "pviewregtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "pivkregtestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "p-secret-extended-key-regtest";

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,
            0,
            0
        };
    }

    void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
    {
        assert(idx > Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);
        consensus.vUpgrades[idx].nActivationHeight = nActivationHeight;
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
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}

void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight)
{
    regTestParams.UpdateNetworkUpgradeParameters(idx, nActivationHeight);
}
