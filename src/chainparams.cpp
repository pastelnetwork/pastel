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
 * >>> 'Pascal' + blake2s(b'Forbes November 16, 2019 The bitcoin and cryptocurrency industry was spooked earlier this year by reports search giant Google had achieved so-called quantum supremacy... BTC#604581 0000000000000000000b0eac50faef2c12c176daa61c9d502c40793aa6413dab ZEC#641130 00000000009d84b7ac5e0758c59ff9c8f577ba6b56aa72f76b3ea04d0ebde4a7 DJIA Nov 19, 2019 at 5:10 p.m. EST 27,934.02').hexdigest()
 * => Pascal88f33e3ee972755a3f3ac108e50636d38dcc09b0132d1a3f7dc2314344af37a3
 *
 * nTime - epoc time
 * 11/11/2019 00:00:00 GMT = 1573430400
 * 
 */

static const std::vector<unsigned char> PastelGenesisPubKey = ParseHex("04b985ccafe6d17ac5d84cb8c06a69cefad733ee96b4b93bcf5ef0897778c227ee7e74e7680cc219236e4c6a609dbcdeb5bf65cea9c2576c2a0fbef590657c8e7a");
static const char* PastelGenesisTimestamp = "Pascal88f33e3ee972755a3f3ac108e50636d38dcc09b0132d1a3f7dc2314344af37a3";
static const uint32_t EpocTime = 1573430400;

static CBlock CreateMainnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x1f07ffff;

#ifdef MINE_GENESIS_MAIN
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000982");
    std::vector<unsigned char> nSolution = ParseHex("012512d702ada6db7a31655f85e483516c9c8fbf411bf130f2cc9b87fdc41572f88f4a22434fcf7cfa101c6746ada3a74e9f85ea82450237331215ff7419a422eb925d3e542773fea1d602f95aed11b231538ca60c766952b58b98748b2831bc1e3a90e6d06a5d82873f57bcf2189ceb2b8dfcc4b2fce00c954ab9706178219079e1c85692c5b507a309c01f9f97048fba790f262eac89133916d3cc1df37963ad4345eacd51b13901c954a5dfdf552b1859b06cc50d7fec6071855a1b0d7750b1e948eb84e5ecb21fa5d0335f5682dc71033499cde7c9cd3cf1fb7273c9bab73c5e7d789a08868a049fab8d397dfbcd782944d5f4984e67cafafc2e18b6d624bb51d70f36f7f70bfdcb61c6b1cfdb854c42a64c3620f4ccd1d7bdd487e8f9161df82d9ab0c91b266b4ec98cd9bcd7e375427daeeeee428bbdb36d2a2ef4bf1d281dc3428f468cbfd1474ac45df874520286c6f0b2014a5e63c3a7835ce3f51aa2bf9fe6d3047604ccad60cb1f8f4ea3fb04f7764a71d51f0af922991f78565c7b6ae8f8f2aa73deebfea8bd7747cb2f846cba73cdd03c9146e6e73a7e9d2af96838abc30b647f266a7a0565e98ca401d7c2546190eef0594115a092ebf905c9c45a0104bdbffe41765b09975bff1830e6642657c8375c8de4920e2c2849a0a63fe5dc194acfea2022d519f692464191d823fdc929309dae05f35519fe971a8b10e8f717a575aa5dd1d2f2038447ac2ae3c164e7d12e77c4bf9ee5a772bc6239c7f30e5ea5c60d05a431b6712111177e37c47ab0bf055067e8abb5252c9ca3744c57c883ea11b733e19b201e0b06baaf4077b6dbf91bc7ef2bdc6d47af481e517f2646d9562c1ab7eddbf9280b64c7d3030ca3fc96b22711e454c41a86f125093b0d6cf6afdb22fbf95ee95466dbbf45597d3101025560a735e6fe2a793e2f9e020d7f5cf34742c6c768a3a39c6e437950c5ccdeff0eba8e592fd13bfad5af62d5611b8e4d1dfff3b45f05fa71e8ad1ed1537ee384a5fb7877f12ca993af0d362a92a4042834d5c9fbb860dc4a589efd80faa76a02be6bc6b59a0c51cfdfd3fa325a6e7640e376742114642930eb26c35539abe1e147aaba41256e1f974c16963ff2a0d157d5383b43ef97b6a7e986f0bf56ad1bfeeed9464b175bf6271254cf2aa64cb0f5059dee053116259724907fcb96fa18ce64676ab98b9c62a61230e1aad05abfe5366c629accadfdb586232d711a231aca6dd9909d6f4fe2b54a9ff334597aafd2f6d133c62aa0bf53fb416dc56749b9e180aaecd778c31312fabdc6b4517d45ee74f6924b3b3cd9a81fd599ad2c1f648723d26827a5d52560074ef2918d23f0c5fb19b65f9c0e17e2ff40c2d28e6ff201e8e039cd9a34469fbd58891e094db12c950de0fe51b26ac61872f405cafeb6069b5e39f267541981675f592ad0bb9de72d3f956cd6cfdee3fdc2176cf83e10d20272ba13a308caa09885922a5fe5b750e1c8ac92e50340dfac640e33ef8580ba2125deef53c4263c2ce283b0dccbc407a1bf377cd658e782277459cd56a496a5795cb2270b3a0fdc0b1f0b8bcc7f995bb77c71d308a51e229a144049c87456b449a36ed3ffcea9fe4a93d3d5ed1818b378f8c490bf1303d7f454db5fa4bd373cee9aa907e70e6dc377f367e9dd42f65455ebe152dcb223710cdc7d711e1b2883f60c3271c962eda0fa3a2f9a190aeba68720a8c5afeadf8476e0f5bbae7ba1383a4f22495aaf40c8d2f6ef9465d344fd40267ac3be01ee2158e408cb794acde5f0c22ae0a6ed02529b32daaa260cbfeebee013236328343d7a3206111a5632985c2184d223bc6b5f19e2f8991aa2cf923b5ede3cf9f633f59a81cca50dc3d7fd9383a77aad2ab871ffb457");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_MAIN
    MineGenesis::__mineGenBlock("MainNet", true, 200, 9, &block);
#endif

    return block;
}
static const uint256 MainnetHashMerkleRoot = uint256S("f52ff4252868600f68869e216d7e953ed1fae8daee204af9bc4013682d91a6e2");
static const uint256 MainnetHashGenesisBlock = uint256S("0007f073905011559e93a58994d778791e742998dd8161110aee08ebb4f7d86a");

static CBlock CreateTestnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x2007ffff;

#ifdef MINE_GENESIS_TEST
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000009");
    std::vector<unsigned char> nSolution = ParseHex("0065541d15ec78cbef2cc23206ca8ae2a223b70c44189b40c5569ae79f80fc11e42f94426d0868db39ca36e426cc3d119ecf8a67b5c22cd4785770c25e0f3440132dec08708f6ddd0dea25615e3066c6e1bbe749050f3f8ddb492741be11187698ec844a96f03a01f21beffbe8e8166d0108d2c79c137abf7ad193373ecf075e4aea7fdfb5813ef3d840b6c9f4b7aa9f9dee68090968ab588929b7794022dc91da15024d3c9d799f0140032bcfcb17bd8b7af1aa3ec1b9e0ee508e73492f6ac38c7adc3b75084a0341e05da8a8f052b7c88423dd2a9dda6622359277f63ac237ef49d74990226c308c650e27232b8bd481d588d2be73d5f89befc73c12f4190c6e66bd19958ef26aa69f02ed87c812ed6d3860377e1ede71ada00cd67020bde06ead4f1b4f541c2935745b965baf5f80257267b4b54a4c395780d638db73f719fb6c23ec49142a32f5b2bd4405dbc45d01aa9b7ee34cb789de8fa1a3cf136fc506895865790f2b6618d82a599de57e17787ede60cdedb773b27f04b9f774c2414556290b70906b8d73493b796ea7861f4fa9e65dd77e0ae8550323eaf339ce74915e75e30744f5e9b5d4d60f187ad2520bd7d4765682169ae92a9ddcb9174ec85b55aa282eabca6376d59e9f8d272583878e698fbfebc7a6342b5be7073e53ecf8b87d395acf78318f4fc7130627f8076be95f59719c64e401ae692282f123d3d4de9289a7f8f9f5651b1ab0a7105336e5da60bc9bda143610e75f5c31f606fa2730122a331c29d98889b5e1828b83e2b0bcb43f6604d97b1ba5c2f73042cdfdd118668cc719f6e78ffa2b380230d5337b4c9206df8a617baf68cc5134f758248f0a4b5bbfb1fb0173f9c202ef0c4fb6aae194daa43304c4bd33544b535316a0a52aedb5c0694b686b33a939cae24b68173a0b817037b1e9f7fb2ebe819b68d00166e55c3f9130c7cf49f39ffb7977c393ccdcc89f132bda584b0d0424b12b83706a273821b8065bd3c326c4c7f70689d87ff7b4b42c88fc6ea276ea1980a234e7b3aa43d6ef357ace34d62242aba2468cb81a1d03377f88c181571a5db555321235f2f5cf70d85c651e33ffd01509ea7aa67ba6f9634d2bb5d5ca7658470a3cfc608517cba151a685c753d5547deea451833b0d4cf95d7258b8eb2fd0d760b33e4ab5fb93fca3f303840fec5d1154a5b9029741dfe28f6bae869f3dfe0e46f0bcdc0879474d9f34cc1ab54f059002b184af0e0296b1879659e4e974622f7dba7d22819dbf64803e42ebd0ae6147d15640b64672609bc6a1d6355c011490ee81de601b3b5cf6522711a4002ab2fab979c02f10afe76fd15f631ab4e3928f33f474ea52ebc9de42d492d0e2af6ed781f2a7f9806077f6b58e7e0dff53b765c744b424c1f6e607efb0772053227e5a02df01aad663dbc9af2b4c5293346dd934a131d00f1e1208619f8da41dd0875d6824efa473766b4f6c3e4180176d6a41000f77b525d503a0a4a6187d1b8daecba0331793f809a52e8b5b8ed3de982659159b8735cba3020716cf33298055560552b30556f8ed18d629ed9d38de9d0de85470aeb31c77832d7256c77c0cff00dd1fd5a23b9460224594e664225537936d23ded6a3214f080bac6c5a562dafaa09b54160f88aa7c977b42501e8e628c957560b5fc1662f1d7f9e82abadb6ed020505f3f2794734b79b9cb377ae49de3a0e425699500de38541c6492925543d836e355d838ef6f1fff0653da90589c595f17b2f844680ccf1b9b5fa7d7ab47204c99ae50990e71bc3bd70516eef694836ecd112b90d205a4e7be2102df0ef163385c5f6d35f983b493b0765a5468c4e7cc943a5f25ddf7980dd4e50cce86c0df89d88648e969bda36213031ae6dfdc16c92b3c7");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_TEST
    MineGenesis::__mineGenBlock("TestNet", true, 200, 9, &block);
#endif
    return block;
}
static const uint256 TestnetHashGenesisBlock = uint256S("069b9bd746578352497a4c13dfc69de4ff8abdd89ea1d26871b8adef130184f8");

static CBlock CreateRegtestGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x200f0f0f;

#ifdef MINE_GENESIS_REGT
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000001a");
    std::vector<unsigned char> nSolution = ParseHex("0d5a0f9fa2868693ed2b3de73d1472e97f291dd6fcdfd3b1f9414024ac90b09566feab59");
#endif

    CBlock block = CreateGenesisBlock(PastelGenesisTimestamp, PastelGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_REGT
    MineGenesis::__mineGenBlock("RegTest", false, 48, 5, &block);
#endif

    return block;
}
static const uint256 RegtestHashGenesisBlock = uint256S("0070f6026ebff52c988e1139fdc73d41e1b18bd14870741b0bac40a553d39816");

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
        // consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000006f31c0e1f30221");
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");
        
        /**
         * The message start string
         */
        pchMessageStart[0] = 0x6b;
        pchMessageStart[1] = 0xda;
        pchMessageStart[2] = 0xb8;
        pchMessageStart[3] = 0xfc;
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
        // consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000001d0c4d9cd");
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        /**
         * The message start string
         */
        pchMessageStart[0] = 0x38;
        pchMessageStart[1] = 0xb6;
        pchMessageStart[2] = 0xfe;
        pchMessageStart[3] = 0x64;
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
