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
 * >>> 'AnimeCoin' + blake2s(b'The Economist 2018-03-15 the infant world of cryptocurrencies is already mind-bogglingly crowded. BTC#514336 000000000000000000421fe5078b5e3011e572d596374af44516fe1745ec0dc3 ZEC#291407 000000000151900c0248f3eb0b3fbabdb8fe47ec0a56d09a9e8e44f9c7787568 DJIA close on 19 March 2018: 24,610.91').hexdigest()
 *  => AnimeCoin2a1f1b88e25c065678459c577460879513340d2e110eb8d2c735e066ab604907
 * 
 * >>> 'Pascal' + blake2s(b'Bloomberg November 21, 2018 Bitcoin turns 10 this year, but there’s not much to celebrate. Its price has tumbled to near $4,000. BTC#550987 00000000000000000004b65ae193b9f8b10686eed919a5361237f3f957bdc2b0 ZEC#432818 00000000000000000004b65ae193b9f8b10686eed919a5361237f3f957bdc2b0 DJIA Nov 21, 2018 at 3:13 p.m. EST 24,601.93').hexdigest()
 *  => Pascald883709e8e2d2abfc43703c0250cfdeb54af5cc50bca615344128de57afd883b
 * 
 * 
 * nTime - epoc time
 * 03/20/2018 03:13:18 GMT = 1521515598
 * 11/21/2018 20:00:00 GMT = 1542830400
 * 
 * CBlock(hash=00040fe8, ver=4, hashPrevBlock=00000000000000, hashMerkleRoot=c4eaa5, nTime=1477641360, nBits=1f07ffff, nNonce=4695, vtx=1)
 *   CTransaction(hash=c4eaa5, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff071f0104455a6361736830623963346565663862376363343137656535303031653335303039383462366665613335363833613763616331343161303433633432303634383335643334)
 *     CTxOut(nValue=0.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: c4eaa5
 */

static const std::vector<unsigned char> AnimeGenesisPubKey = ParseHex("0421105e5dc396643b48ece657fac0e254cba81b93f1290030869b26097f277281e226f708aa4e98156aae3ee23be7c2793e972081c356cc5afdd6a458fde59f9b");
static const char* AnimeGenesisTimestamp = "Pascald883709e8e2d2abfc43703c0250cfdeb54af5cc50bca615344128de57afd883b";
static const uint32_t EpocTime = 1542830400;

static CBlock CreateMainnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x1f07ffff;

#ifdef MINE_GENESIS_MAIN
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    // uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000130");
    // std::vector<unsigned char> nSolution = ParseHex("001f8eabd201d45f6531b20c0e9de4cd38a34f5b01074ee7440f49b6c9383f4863badcbb1ae803fd196c0895820fb9a36ff9f6f5f12f0a96f29710faddecd3138f30b9f51ea3cdb34775a8eb3854a701b1bb5cb30143f7a1caaecca9c39632c4eba4011d4c401f7e8025b2f30257cfe492a199c2a3ef6ba1ce4b5e7624c202b2f7bc3d380981d6b9b04ee2a9915ea06a9d9182640f975ed323406fcc0e8819a473bf5278c57e8ce9004c7457f48030173d6d024988e9fd1d8187108ba6350d2b6ba065fa65510ad3dadeb133356dc14bb4c50277e6d77ed271a770117030d10e045da16cafec0a33870ff28470961fff2ff4f28a4890d6a37f371030086c546f67aba0f7698f3138a9e4c265f84f3e34cc3b4dfb892a92060e91cb33db30c385868164f7719119eb0b40694e253f116c67e1a2f71622b90378ad0f25a846d978169d47067fc26514c520edaf4a92bd2c011d825892a671878e08106b19b6ea468313d7bfa1303303d49a53abe7231a36efd1c202ce52a11fa61f240b3359dfcaff8af676855afbf3912b3276dfa665373e9dc731154e4d6e03e4abf142751a1f98b679480385d8b2acc28daaa1594083f38ef1f442f5f885972b41d56a159546872977042dd4c03025cb85180fd907bbe657728ec33968eb05c07b6fe89586784e4e46101b668c154e9f33379d0228adfa6db5cba52f7dce040ffa800922a8fbfb0c3673b2eedb4e9bba1555b14a85452cb61f6fbdc25ab518f7545d668e3697d3b707547f42f3c4c276980b126d7ec346ee548bfefdd6330cdb43a3159bed9e1a49f239d1f152e5427884bb0a191912c6c6bbc779ba95a112f1d1ca672e5cca252b4f2510d2f7d227f5daf829b2c86a426bbb93c8470bdad99c9dec4413a9581477e6ea0ee92a5bdfa57d2db3be31b455b38d7ec315ab49c39ef581174e5913007fa7f54acc36751c6080b8401f8e1c7ca13f687225c7645c2f17a296de01a70a1138ec49e14518f88017d9035618144e8b05da147fb2fb11aa21415816003d2f12df6e10780b4b2f47960fcd853a6402fda15805a415f8eed378b96aa0033c634d17450e8e785d532c8e118cc3912a89e69be6bd786ad8822caa1384a7061e2b58768be101629f91059a927d4d25aefdfad168d53422b81ae0b37dcc6b6420f29e5ae0b0bd44340445a12419c93a034a5d330ee8c4923da2a93514350a3c1c2ba487a31761dcb7d3dc41e78f126298e2741792135b7146c03b8028a227832b00a52c21bd77233a5e25d81fdd10ed411e544a70a778c1553712d7de151901b97746cd359190b329b4e42832a63c9b88bb189d433b7e6940b3950ed7d3f5f8becfbd0e7e0a39161a6a59940fdaaf8cd1e3da695226eef18917a9c223b6e4caa61492236f8732b1369fd37d81999cb8ca00cc854fbf8aeb806f51c754b4e5945676c81fa9741eb775f743b191a3c3ee5343b44919b5aa9a3474803097c382422a3737856d149771c72b7a0a8056298242879fec5226581d958a678b6d450457c06b3fe2490595cc432597ed8add779587b6385c098e01f121f316a8eb6a6a0936e117d9b254b7b1d568e9e72e3ee625abaa0cb1964eeabc20576c897d6c8f6f597f671e318ecee89234beede1bf78783c6acc02ff943de73e05e1c6c058e2023743a57595ee71edcdfece34f67d0c71a0ba7be387377676f44ac5f6d49a6dd0173e78238764f28c98ee8ba8d075b7cce5338641b35ae47f242786dc43691539dfc67528c76590d2d7b957a07f0b8172bb89a983d1c5a79281b81d295557f790f60c2c423e5b92cfafb1133d47662447175e94065e5864388ec361c259b3ffdfbec81f5d48e982997cb7ed1a7c6cffa3f9ab2cd594ccfa4b0ae02b9393327e5cc1");
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000002543");
    std::vector<unsigned char> nSolution = ParseHex("003a96d2bf05d1a8897c208b2c42d92555619f1cbb0df5b1716e727979a8e6612f6e76bfb9ca324fb0060fcfb68c569ad20b0e49c3d87725eb899e0eba46d517bd2e010c985ff2cd2c524c1aef71de504458a0ba035382f0aad08a4cbb18b0c2da5d995c48ee2c4bcf862327779a316c5bb0b9f9319652dc76dcfa1f972a132d3abb446d7b4bbe85161bddb79b59b0e5bdc85232b013c12117556b438795879acc0c797a13b9b0a80120d29a07aa706bedcaa4d87433ef55c4387ab54a03e198637a09e9ada600f4cf1f52a8f268bc7c9daf0306e0ae7989e86966cd830c0f4b264a845ddd6be52547ac0fa2686361ef8e462f2743d31990eaf2a58411287157209dc99dff8321e442227da4ceae8ad2802c8495480d778cefc7f1286d234f55ce2f521d653a14f3568b70157f779507a7dae3f559b30b105b98b44bfdb40a1b608763fe6a94d9d5ddc5dd6221564f35049879e8fae59b6535d8b2f351494a70c13dbfbe49076a31f150863f28d16da5e40fda85aac56fb95219222ed95905577d790bb0c571a3d60b6255717f98dc242aa4886c1a96d124caf242d1e0c2294c17efcd6b07dc69ce7b21ece382cc51ea91334d27557d3ff5d010440bac395db6ad866593ddc2de1d35ad46bc2df11ae47f5de906f41f4f39a26e374d774cabbcbec5721bf96a459ce233f54a79664acfe0dc29a7d0f3acab04e226f2399099e13160e052f0daca6cc7060716db20061a1f9e581d8cc12862e78d9f5e8db16df5e7310ab064d370898ae3d6b1c228bab50de1089f8e6ced49c35d2cc6b0ad29e114c978b779003a8f3a9cc2e81a5d9572c4e0b13305a93628be71e037e9bd3f7c5e2c9dff796e0d096110c2070e67e01b0b0d62b98407275212dc60cfaa209491b2bec0bc63f8d1fdfaab8475e3b433efa772717bc1199441576523698e1fc51600bb25eed187116e7f51c1dec25b604a43357b977a0e4616cc235962a9e7ec72a892efda3ea73ad9151f05cf6dddf4a017514116279121dc445b69c13d416d07a789273c53bd9384e824fe8b73b1a1d23b76bea20283f7870c1140b78b4ee4b207e686bbcce27f3b331f69a34b290ba72fdbd5126c5fc1e29158c89e73b805300d3ea68e40c94383b1ee832105e21f9352485145f26c9bfbd8b5aedd1305302e47dcbd96336e783f018ba675e86d90a3b72f8155fd1c44d66bd31abd660ab4e70a820508f27492e1b849f1465e8cfebb2a020407fb5c6c5a083dd995d28c7b255658f5d55b768a0a8a41707582d37df315c0d7d7e0a539da6e3b756b16869fefab8bf0edc06c6200e135d60cceca583dad1c79678ae88b107adc2674606ff1d0953f212d57e92fa8d7de7c77bc87ce2c87b63d4b2fa614379379b673732c1a9aabf469f07b28a13457e58aff2cf914040525767012c688518098c5a296b001464397d62f6b1c146a3a2689b59ab595e55290516a06391cba225e212c7e6ddf6b69bd95f4a41aa773b6d5df421efe232804b9d6b5169f752a93b361f4cb6b4149a23855ec0fe7afd101707397c6aa25581231d2dabeeb98f65124c42c9f5b90bda14ffc1667f048ee262790b2adb31284f9eea53c17b5fe89818c04c342f8de01149afb3961120695e457db50b9f410177ebbb182573d2d1306e559d412964c57ae8fc2a180b1b17e41881abf723f7f96e4739becd16805f704f6e46ef663d5f436383161ac56fc247b0b98465ae78be19c3f84793cb5494824be528e9449519dab366912ec755dadae56e5ba0c5f4f1c08d4fb5cabce247a664cf0f967d80d60d4151ad972750797bb3f4313858d7c8dc4ed5cb9dfac226fa712779f1f0f8031940acdfec7b52d734cbe713f193ca3345b71ad1f356872197c4ba737f8bcc4b9");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_MAIN
    MineGenesis::__mineGenBlock("MainNet", true, 200, 9, &block);
#endif

    return block;
}
// static const uint256 MainnetHashMerkleRoot = uint256S("69390f621a08c045270a5b43253cf269105b395c67e442468f91b879bb403c1f");
// static const uint256 MainnetHashGenesisBlock = uint256S("000733cc513d293a4f6f2d49c8b0c27c8700b3b546e100b194742deb9d52db81");
static const uint256 MainnetHashMerkleRoot = uint256S("1e0712ee9cb9ae7e2affc2f7856c7912d82fa9edfbab2797f069261fa3387c8d");
static const uint256 MainnetHashGenesisBlock = uint256S("00009067632688e02b7c1522037faaa7510be18a0c1c3c09776a16349808b458");

static CBlock CreateTestnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x2007ffff;

#ifdef MINE_GENESIS_TEST
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    // uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000000c");
    // std::vector<unsigned char> nSolution = ParseHex("0037c32e5f02856791383894426e0f5651aa3aaad4448195fd5d1e5e272b5a650b5a6ab10255bb1ae581009d4bbea14a49a29a4c8096403698a9b5be91f06743d2778b29554dd8c1465aa0b667e80f2fbf79aa1401b2fe96311e9851ca65f79d63ced3e211e15f35e903a94340e7a1da03a2c6e7aa12efe3922f1a12d6d704005535f84c1886a3fe433feab5414cfa6157664e1056128ba207e740621241bdbaf38be0a11029eb69027f46c9de14db15f561510515184ad1cf6a9202f710c48d31aa04f732cdd4f1670921ea9d36416f187625b353bcf86be147de9eb50070acb76d405ddfaeff2a1507f1c2f02eedd64cb2b66a29c82e7bd19427070928b967b119db1504d55253296ed30e5d1878a345350e61e4819cbe43d10755a66c31e89ef77f3e90ff11974399fc5dca5b3668b313877fc7bb01fb7a5fea6022bb9daf2413f9fa6dd9f6f6ee2ac2a95e5dafa7080f94766d49fcc481abb6f788ecbc3b01831f7e2e2b7c6454809430abd4db3698dc7acdd6393e5760bc1a3c51e0259d4d2f726f140701b4f03170eebd7df92e6052ef6da07ac9239833ddbe9f08b93b544d1dd10dde1a27a40d8a6b207815896b5877de7717bbc04622827ab40153d9d9989d4255e9d1370132565193c12c99cfa4034db899721bf3eae96e94457347d9d60650133b7973ebe8f9d231b76a90f5a2f702713891600cca739940a1c509f659b222b5c9a3a1e191ba2f5d24c2427f298bd428a19c05aed4f15785c69a1330750cfdd366edc3e641fd6558e3f9ddd4ee5dd0fa0e191e6dbd4ec59968b9865ad2b00ffab9aacf2dde56a31c28e268626f3ea78f9694c6bf65860e1801d3459b3f6b7dd183dd38bfd2cd97457d42e5f5f6f2b3a995323cac302de4ea2f929325654b354c43d9f6df854054d94f5898337c73bc0805b3e8e675f21876f39dc6014e8cd17e5dbc61a03048adf16407af66729ba09b03506b14a544dcfbddff235efae4dcd514651c156d201ca2c34da001bdba24e422cbc4d1099bbe9c7e935d391d8de01ef601594fae2d5a7939b7aa9c1f8faa07a4bc0f090383a35d849b9f53e3e5d72cc07e7c4918db4d541fe4249b7ec6f622d7d8d26656095840ff2a922f245d225193a10216731cc22882f637791ac82b5c0bbb6a2faca5db9a84c324628b4219d4d0e3650257ae8edbc48b71b21719c800f4ba42de415b9e181d8161f1cfca9c7774d7a48260b64c3b131a794ed11defc5f8860edcddf3afa42034453e5a1d3cfddc612a893d517dd5ea36cfce67b495c6519a6facd4caa3028e7361d273e289ddf7f297bfa66e8a9b96dfe5200aed27f2a68b7e1ed997615cecee01a868e7ec4c740bb078754505cb016bb240e99f24a5fc4ded77f18f1c0f5530e37304e3d62e01dc8c621e90944127c5bf0168cc8088d139d1154b755b826fa06acc0b9d121c08bea4cfc304e3418ce3e377e8faa821c7297d9d4217f23d3c4230387b887be3bbdefbf145981736da312a652e0232939c81b556161e53dbd9becffc96b5dc01a4f040f0922b68967162c12322ef74bf2a7443974842060d7113018ebb15254bcdd167a6c8129fff17154ea91b800a11e8b8baea502a6be81fb2abddb6562ee42322175eaaf188943784f0e09c8e500f5335070724a12ab252f91d43a76256366371a55fab9194be4052f6a574943c96b84324f8f72a6a3e016a7c7214142c635c870cb3dca2b844f1336dc29577df918e001721e6ce548b3cebe2a9b69d675e5a1eb26739d49909bb84d2f14c3d49f3e260ca841ce248c19f9d20172833af93e0938de51bc273973ec638b934a31e80e7268027dd424e73aecd02d8adb6d83c7e6cb29d3a885bd165b274d9cf3edde9f7e86f7314e68bf8381b7b");
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000004");
    std::vector<unsigned char> nSolution = ParseHex("030615b3751af22d12419465fe7207331b419ccea73b453ed936a40165ca9015b4cae0fd2b37985bd169296d4a100858db5dcef895cce23f37a591287f7c1233ffd512125bb4557f756375339df2a4f2d5ab16fb0d6dbc764bd4c9c8cf74119631b76665463d1c0e2935107a2f4ed777f5bd6e959554cc3d49736a7600a551138b665e2013dfff0d56f75f448ba1ca55fc72be6212b72c0668dfa171259b3a8ce5707335b63a95a60351fb7b1c429daba98f05041e4250b5a63acf9b391934caa91b58162313f6021c149a3dfea1e0dc9a8d08f877b76d3934efef19b10063624fb118afefa6730b4b38d3e020538d69e6b12370ce000dd3109b07a70b55dd8aea39b5d1dc2896227453613efcd7bc2b4c1ef3f36a8a50199b856454e432c196ca27991a37ea1055e580f8a07f719404f285246f7b0d22f278af2d3157151d2f4c7b0e9ff3c46540b6b10b1e2b1b8c25041ee30778d3d2e55b8d343505329565dd0793e91c20ebae5f4a1ad9317b52d56aaebc5f76d818dda1071586b95f1f2ddfaf9219a1c56bb0e845eae31672695f6fe517a8a81363611cb6f4224849a29ba935e9650e4ac945918b2099ad3d46b3f4ff6873a31bbe40101dcb8f59e8cfc9ddc888d9aecf6552ef889dbd5c9d2134d74512b06667afdfd3776cec992d0de5dfb8292bea5d1c811bb5e370a094530a7ce6a23fb03a5b6605a1ed7c9492addb673df91317f441fef54199ce742200d26c01182087c714767213658bc2aa8998321a05c87f8d85a53c178b84d14aa4a1472dc7655816f94c581ead0c9d0465794df66205c6878a9f123e1cb6176c3f2f688cd88d92e2b6d787ef978e4600d2a1e91802c8ddaa864597c189752b1edfa2aa57063e77011df1156f6fdcd77755b882a5add0befec2d0dd5cde4ca51d9f7666fefd9b1dc8c4e3f0fce6650179117d0747b25d1c06023f9f8ce63bc6f186bd9bda5e5a550b20433dd0e52961590cd6d1b0b70b4a009a7364bc0ba1738dbe066a3af401020efde31bd115a43d5985356c27dc2c1210f51cd1e7466fdf7946a4533636621d6cbfcb694b542d52f3c2f3bb5c5baa0bbffcd23335cde2756f2c206b87fcb5a1c56c442978d23f6c16236f83bcb3e886f9ac60e2b7b878d84226b05de8f637c6a370ab5e00f5a2a884854d4eb459827694946908a754fc2b5bdc25a03142346d4e5835418ed6f62a35f9b3fc7a25e8d9be72438261f32979a890ad6f321f558c486129b99bb3bc74d03a5757daf02b18362424d36bc34819492977c0d35532b77bf8dc7d4b8b610ac6795a46c9fb0ba9f1080cbee62ceeb07019c4e52fcae4c2515c62b7ae45c4b5b9c6ac96e4493da2c40c282bb46e0d22fd528a0770175f2a4a256c5383942470962bd993d7ed2b5a23bdd37bb7ca581cbad37008a4d3d8bc8b58a9912ad29dadaf278181349b29eb12bcc4573eebff75fde62206b1bedab98e045a7f44374071f479d31879f8303599e6f2fc4b4ac41f4bd75662fb3acfa3f497de47d96fb35c58e26f6f583a9e144220b353d49955fbaf924bf6c284e0ced71013e218bbce721ecca759a6bcc2dec1aefda0d76759598218237a92cbdfa3a964a4c86cae565bfee2a9dce7634d99aa8ee9e2178f860df5125b763312365d75b7ae0e3f3201a3502df1540bb15f90d4b26e65c53f38c7107fe92dadcd7b528421b1c00fcea20a325f3a934724a1ed757260ee0d55c4829ae41b644527d9b125e734bd62f06dd30a0371a475c0fed59ada384a7cf4c90e846ace85a9cd1fb4e922ec1eaa491949e96c03cf789edc516ae29dd191aea9413deaa52e625e5d58c6134206576e631d67d862f4c95f2830ba22159e67ac17aaf353b0ad8b998c3953430c641f5d256c16e318");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_TEST
    MineGenesis::__mineGenBlock("TestNet", true, 200, 9, &block);
#endif
    return block;
}
// static const uint256 TestnetHashGenesisBlock = uint256S("05cb336861b74bc7f081c0f244dc9e73250dcda39beef2ffea4c382dc79c0080");
static const uint256 TestnetHashGenesisBlock = uint256S("07329f7726fde9f99f0c6440f67af0bcf81e9745a6b2a5c5b0ae1484ca949ab8");

static CBlock CreateRegtestGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x200f0f0f;

#ifdef MINE_GENESIS_REGT
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    // uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000001d");
    // std::vector<unsigned char> nSolution = ParseHex("05b1aa5d42bf1315c230bb4c74c4ddada4d316491bfb128beccd462d17eef9e4cb3653ea");
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000006");
    std::vector<unsigned char> nSolution = ParseHex("03af814c3088a233b60b4e8c27f1a940f0d30656ebdd40c0934dba313a0d9ed5e555ab20");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_REGT
    MineGenesis::__mineGenBlock("RegTest", false, 48, 5, &block);
#endif

    return block;
}
// static const uint256 RegtestHashGenesisBlock = uint256S("07e5442be390f1e52fa99963279ed335a6794f34704def1dd3de753828b09ffc");
static const uint256 RegtestHashGenesisBlock = uint256S("0e2849244d2234bd48507e85b42ade9f7aa944c9f80d6d10ad268a5a8913d0cd");

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
        // consensus.fCoinbaseMustBeProtected = false;
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
        consensus.nMaxGovernanceAmount = 1000000*COIN;
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
#ifndef MINE_GENESIS
        assert(consensus.hashGenesisBlock == MainnetHashGenesisBlock);
        assert(genesis.hashMerkleRoot == MainnetHashMerkleRoot);
#endif

        vFixedSeeds.clear();
        vSeeds.clear();
        // vSeeds.push_back(CDNSSeedData("z.cash", "dnsseed.z.cash"));

        // guarantees the first 2 characters, when base58 encoded, are "Ae"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x05,0x6E};
        // guarantees the first 2 characters, when base58 encoded, are "ae"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x12,0xF1};
        // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0x80};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
        // guarantees the first 2 characters, when base58 encoded, are "aZ"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x0D,0x2A};
        // guarantees the first 4 characters, when base58 encoded, are "aX"
        base58Prefixes[ZCVIEWING_KEY]      = {0x0D,0x27};
        // guarantees the first 2 characters, when base58 encoded, are "aS"
        base58Prefixes[ZCSPENDING_KEY]     = {0xE1,0xFF};

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
        // consensus.fCoinbaseMustBeProtected = false;
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
        consensus.nMaxGovernanceAmount = 1000000*COIN;
        /**
         * The message start string
         */
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
#ifndef MINE_GENESIS
        assert(consensus.hashGenesisBlock == TestnetHashGenesisBlock);
#endif

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("anime.cash", "testnet.anime.cash"));

        // guarantees the first 2 characters, when base58 encoded, are "eA"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x14,0xEC};
        // guarantees the first 2 characters, when base58 encoded, are "ee"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x15,0x32};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "eZ"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x0E,0xBC};
        // guarantees the first 4 characters, when base58 encoded, are "eX"
        base58Prefixes[ZCVIEWING_KEY]      = {0x0E,0xB9};
        // guarantees the first 2 characters, when base58 encoded, are "eS"
        base58Prefixes[ZCSPENDING_KEY]     = {0xFD,0x09};

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
        // consensus.fCoinbaseMustBeProtected = false;
        consensus.nSubsidyHalvingInterval = 500;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f");
        consensus.nPowAveragingWindow = 17;
        assert(maxUint/UintToArith256(consensus.powLimit) >= consensus.nPowAveragingWindow);
        consensus.nPowMaxAdjustDown = 0; // Turn off adjustment down
        consensus.nPowMaxAdjustUp = 0; // Turn off adjustment up
        consensus.nPowTargetSpacing = 2.5 * 60;
        consensus.nMaxGovernanceAmount = 1000000*COIN;
        /**
         * The message start string
         */
        pchMessageStart[0] = 0x31;
        pchMessageStart[1] = 0x20;
        pchMessageStart[2] = 0x76;
        pchMessageStart[3] = 0x22;
        vAlertPubKey = ParseHex("04a40ca15344822fd6bc9719929050614ab045775fcca1e46880ba7e4c55dc999b6f618091a7e5f5c24bd80854f14ebbc369c35cd2e4944c6cf27a3dc5f54f52dc");
        nDefaultPort = 18344;
        nMaxTipAge = 24 * 60 * 60;
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

        // guarantees the first 2 characters, when base58 encoded, are "eA"
        base58Prefixes[PUBKEY_ADDRESS]     = {0x14,0xEC};
        // guarantees the first 2 characters, when base58 encoded, are "ee"
        base58Prefixes[SCRIPT_ADDRESS]     = {0x15,0x32};
        // the first character, when base58 encoded, is "9" or "c" (as in Bitcoin)
        base58Prefixes[SECRET_KEY]         = {0xEF};
        // do not rely on these BIP32 prefixes; they are not specified and may change
        base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x35,0x87,0xCF};
        base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x35,0x83,0x94};
        // guarantees the first 2 characters, when base58 encoded, are "eZ"
        base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x0E,0xBC};
        // guarantees the first 4 characters, when base58 encoded, are "eX"
        base58Prefixes[ZCVIEWING_KEY]      = {0x0E,0xB9};
        // guarantees the first 2 characters, when base58 encoded, are "eS"
        base58Prefixes[ZCSPENDING_KEY]     = {0xFD,0x09};

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
    // if (network == CBaseChainParams::REGTEST && mapArgs.count("-regtestprotectcoinbase")) {
    //     regTestParams.SetRegTestCoinbaseMustBeProtected();
    // }
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
