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
 * >>> 'Pascal' + blake2s(b'Bloomberg November 21, 2018 Bitcoin turns 10 this year, but there’s not much to celebrate. Its price has tumbled to near $4,000. BTC#550987 00000000000000000004b65ae193b9f8b10686eed919a5361237f3f957bdc2b0 ZEC#432818 00000000000000000004b65ae193b9f8b10686eed919a5361237f3f957bdc2b0 DJIA Nov 21, 2018 at 3:13 p.m. EST 24,601.93').hexdigest()
 *  => Pascald883709e8e2d2abfc43703c0250cfdeb54af5cc50bca615344128de57afd883b
 * 
 * 
 * nTime - epoc time
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
    // uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000002543");
    // std::vector<unsigned char> nSolution = ParseHex("003a96d2bf05d1a8897c208b2c42d92555619f1cbb0df5b1716e727979a8e6612f6e76bfb9ca324fb0060fcfb68c569ad20b0e49c3d87725eb899e0eba46d517bd2e010c985ff2cd2c524c1aef71de504458a0ba035382f0aad08a4cbb18b0c2da5d995c48ee2c4bcf862327779a316c5bb0b9f9319652dc76dcfa1f972a132d3abb446d7b4bbe85161bddb79b59b0e5bdc85232b013c12117556b438795879acc0c797a13b9b0a80120d29a07aa706bedcaa4d87433ef55c4387ab54a03e198637a09e9ada600f4cf1f52a8f268bc7c9daf0306e0ae7989e86966cd830c0f4b264a845ddd6be52547ac0fa2686361ef8e462f2743d31990eaf2a58411287157209dc99dff8321e442227da4ceae8ad2802c8495480d778cefc7f1286d234f55ce2f521d653a14f3568b70157f779507a7dae3f559b30b105b98b44bfdb40a1b608763fe6a94d9d5ddc5dd6221564f35049879e8fae59b6535d8b2f351494a70c13dbfbe49076a31f150863f28d16da5e40fda85aac56fb95219222ed95905577d790bb0c571a3d60b6255717f98dc242aa4886c1a96d124caf242d1e0c2294c17efcd6b07dc69ce7b21ece382cc51ea91334d27557d3ff5d010440bac395db6ad866593ddc2de1d35ad46bc2df11ae47f5de906f41f4f39a26e374d774cabbcbec5721bf96a459ce233f54a79664acfe0dc29a7d0f3acab04e226f2399099e13160e052f0daca6cc7060716db20061a1f9e581d8cc12862e78d9f5e8db16df5e7310ab064d370898ae3d6b1c228bab50de1089f8e6ced49c35d2cc6b0ad29e114c978b779003a8f3a9cc2e81a5d9572c4e0b13305a93628be71e037e9bd3f7c5e2c9dff796e0d096110c2070e67e01b0b0d62b98407275212dc60cfaa209491b2bec0bc63f8d1fdfaab8475e3b433efa772717bc1199441576523698e1fc51600bb25eed187116e7f51c1dec25b604a43357b977a0e4616cc235962a9e7ec72a892efda3ea73ad9151f05cf6dddf4a017514116279121dc445b69c13d416d07a789273c53bd9384e824fe8b73b1a1d23b76bea20283f7870c1140b78b4ee4b207e686bbcce27f3b331f69a34b290ba72fdbd5126c5fc1e29158c89e73b805300d3ea68e40c94383b1ee832105e21f9352485145f26c9bfbd8b5aedd1305302e47dcbd96336e783f018ba675e86d90a3b72f8155fd1c44d66bd31abd660ab4e70a820508f27492e1b849f1465e8cfebb2a020407fb5c6c5a083dd995d28c7b255658f5d55b768a0a8a41707582d37df315c0d7d7e0a539da6e3b756b16869fefab8bf0edc06c6200e135d60cceca583dad1c79678ae88b107adc2674606ff1d0953f212d57e92fa8d7de7c77bc87ce2c87b63d4b2fa614379379b673732c1a9aabf469f07b28a13457e58aff2cf914040525767012c688518098c5a296b001464397d62f6b1c146a3a2689b59ab595e55290516a06391cba225e212c7e6ddf6b69bd95f4a41aa773b6d5df421efe232804b9d6b5169f752a93b361f4cb6b4149a23855ec0fe7afd101707397c6aa25581231d2dabeeb98f65124c42c9f5b90bda14ffc1667f048ee262790b2adb31284f9eea53c17b5fe89818c04c342f8de01149afb3961120695e457db50b9f410177ebbb182573d2d1306e559d412964c57ae8fc2a180b1b17e41881abf723f7f96e4739becd16805f704f6e46ef663d5f436383161ac56fc247b0b98465ae78be19c3f84793cb5494824be528e9449519dab366912ec755dadae56e5ba0c5f4f1c08d4fb5cabce247a664cf0f967d80d60d4151ad972750797bb3f4313858d7c8dc4ed5cb9dfac226fa712779f1f0f8031940acdfec7b52d734cbe713f193ca3345b71ad1f356872197c4ba737f8bcc4b9");
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000094");
    std::vector<unsigned char> nSolution = ParseHex("008ad14c9284fb37265ad013746828307cff253c3d1bc3efd2ab988d5ba73854eb33d3e6fadd2d7863f61444bd6142475808d77791c3306d27ceae3058c8511c9fbde9c25abaadad73327af229e0ec9fe0393c900386497c4489b2ca8fab217fe564d5387c65cf614e0e59279443cc91ff6e29653cbce6c109933d3a880407f1e298b20ebd68b0522a4f5cd48b1781a87f6e11469ce3ea055dca67a796a50d672fbd8acd6f1fbb460ae21ac270d04c479226c3ee5d465cbf32ad1f5073210f1dd5a8559bb736203337d3372b7b686d1bb2d30eb25def7e651843a68de1301ed849ed0a395dec0022d86a0497894642ef712687e4f473d364c1fec0ed10d6826faeaa4a5de3ba635922f67e7d0c03db779a15ce29a1a7d0d6b3a66851e5c4619dcd495b6e7558183caf927e288c5dbbfe662fb27cc06195b61a67753543eba4cb9ef4b3db66d97e82e5ec8b87d37dac86024febf563d8388adf7ea7fce3528646ea3abd500519bd4c8e8c290dc975bd01cc3d531de96a7df3d01827555a5eb7eb5749ec00727b1fd85954d5f3adca6e75794ff61b5f40e9af4a9a3316ee151eb31fbe271f1a9e94afc19a3d4fdb8b73dfd5cb4dc5287397a77b1ab7b16e73a168b1300603b86ddead367bddb76bd959034c50ffdc7e7b601129e2f8dc0e532d9dfc1d7e916e87ffa4ec559b6563ca022460c13b207ed9765c0c3fd6d96a0d9e9addfa36079db77f0dc5cdb28f8313cb743919c85d599ff201a515e8859d0cff8d852e0c4c729e2a8bfa9dd5ad72a283625ac589fed72a6f0cb5835ae152f87ab2c821bcc76c933945af5df2a3123e859994b50303ebc1c7a279ffbc0a0dc3f1af99338d05fec396dfb9d9b034ece3fbc30dfcce9e3b162c5bcadd3922100b2a9ac5092bff4ea950125fb7bd362fdaa5f7eb53fdd573c9b604ee3b767a51bc69e100f97ea0fbdc20112572e1af235fdea5121c501fb15e263f3194e50fd97269b5e87640513abb5b3d7282112ae354e593435c9bbae431a7244376448a7dd99a26940e35305289b99625d4d99a4e247fb395ff54070107c9b41ea21301909ab3022866c45ebe9f9abf9a04921d13f28efb34cebb5086eb8fa3b0792ac8717404121ac8a550144eec8b3069f5996bb5184e4bb0670e6bd7062753a4e57cf70129cdcac934ab59e7389102bb3063a3941232adf681e4a4f695a8d9ff930bb20e4e49fbef5615e5f4bb9a1b737af1e315207d4fa80491b38c1a26c5f37fe5e23c344d44e9542f9678972041e2a547c97c1cd55cea50d97ba0c32dc3df08e202f0d9328ad219ebcecd63ddfec7f55314ae1d4d3037aae38e255ded43754d2603d57a1fc998921d561708365719a3d1342cf5432ae7fffe8646ed6c3d163a1aee3471ead2681d608ed5525ec175a9dda312ad2f013ebbc0fd10f63b729440e5ff7406a48cfd105a1511fff4a836933907e61df1bc84fbd609ba0c1f1d270c8b7710260d303d791472d0c46509fd526c1bfc7f22e2599f4fcbeee320f387d325ee00764be79d414e14584910348567eb9f61798c83f31676ee617d3ce43119dcabcfd043339673e3c39875db12d2845b77fb149ad597cb99a90de221d1a3c8e7f9c57400b07c232f97820bd662d457696498c74a5c27831f1b7f7ce4034f33f8ec059f02bd4e70c1836d9e6acdc37fe74936108431da154da33ddb53923fe5874926b429a16704ac6cf2694b3828e66b33bc537e886505f6b64c7d0f747e5dd7617099166ea23562ffb4cdb2ce5bf3ee071d8b1867b36a5b9c9203007f1cc75e4cde1706941ff4bb095c4827d78897a5375c2a5fd603dbfaa0b6118039631614f4ffd5c41a7a20f81fa2ca4816b8631c17a7b3310fcb37b425e2b7cdf885e5a9463a2dce");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_MAIN
    MineGenesis::__mineGenBlock("MainNet", true, 200, 9, &block);
#endif

    return block;
}
// static const uint256 MainnetHashMerkleRoot = uint256S("1e0712ee9cb9ae7e2affc2f7856c7912d82fa9edfbab2797f069261fa3387c8d");
// static const uint256 MainnetHashGenesisBlock = uint256S("00009067632688e02b7c1522037faaa7510be18a0c1c3c09776a16349808b458");
static const uint256 MainnetHashMerkleRoot = uint256S("1e0712ee9cb9ae7e2affc2f7856c7912d82fa9edfbab2797f069261fa3387c8d");
static const uint256 MainnetHashGenesisBlock = uint256S("00049264094c6972fe6209203944e36581410a7e03b25294d44f15f88496c293");

static CBlock CreateTestnetGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x2007ffff;

#ifdef MINE_GENESIS_TEST
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    // uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000004");
    // std::vector<unsigned char> nSolution = ParseHex("030615b3751af22d12419465fe7207331b419ccea73b453ed936a40165ca9015b4cae0fd2b37985bd169296d4a100858db5dcef895cce23f37a591287f7c1233ffd512125bb4557f756375339df2a4f2d5ab16fb0d6dbc764bd4c9c8cf74119631b76665463d1c0e2935107a2f4ed777f5bd6e959554cc3d49736a7600a551138b665e2013dfff0d56f75f448ba1ca55fc72be6212b72c0668dfa171259b3a8ce5707335b63a95a60351fb7b1c429daba98f05041e4250b5a63acf9b391934caa91b58162313f6021c149a3dfea1e0dc9a8d08f877b76d3934efef19b10063624fb118afefa6730b4b38d3e020538d69e6b12370ce000dd3109b07a70b55dd8aea39b5d1dc2896227453613efcd7bc2b4c1ef3f36a8a50199b856454e432c196ca27991a37ea1055e580f8a07f719404f285246f7b0d22f278af2d3157151d2f4c7b0e9ff3c46540b6b10b1e2b1b8c25041ee30778d3d2e55b8d343505329565dd0793e91c20ebae5f4a1ad9317b52d56aaebc5f76d818dda1071586b95f1f2ddfaf9219a1c56bb0e845eae31672695f6fe517a8a81363611cb6f4224849a29ba935e9650e4ac945918b2099ad3d46b3f4ff6873a31bbe40101dcb8f59e8cfc9ddc888d9aecf6552ef889dbd5c9d2134d74512b06667afdfd3776cec992d0de5dfb8292bea5d1c811bb5e370a094530a7ce6a23fb03a5b6605a1ed7c9492addb673df91317f441fef54199ce742200d26c01182087c714767213658bc2aa8998321a05c87f8d85a53c178b84d14aa4a1472dc7655816f94c581ead0c9d0465794df66205c6878a9f123e1cb6176c3f2f688cd88d92e2b6d787ef978e4600d2a1e91802c8ddaa864597c189752b1edfa2aa57063e77011df1156f6fdcd77755b882a5add0befec2d0dd5cde4ca51d9f7666fefd9b1dc8c4e3f0fce6650179117d0747b25d1c06023f9f8ce63bc6f186bd9bda5e5a550b20433dd0e52961590cd6d1b0b70b4a009a7364bc0ba1738dbe066a3af401020efde31bd115a43d5985356c27dc2c1210f51cd1e7466fdf7946a4533636621d6cbfcb694b542d52f3c2f3bb5c5baa0bbffcd23335cde2756f2c206b87fcb5a1c56c442978d23f6c16236f83bcb3e886f9ac60e2b7b878d84226b05de8f637c6a370ab5e00f5a2a884854d4eb459827694946908a754fc2b5bdc25a03142346d4e5835418ed6f62a35f9b3fc7a25e8d9be72438261f32979a890ad6f321f558c486129b99bb3bc74d03a5757daf02b18362424d36bc34819492977c0d35532b77bf8dc7d4b8b610ac6795a46c9fb0ba9f1080cbee62ceeb07019c4e52fcae4c2515c62b7ae45c4b5b9c6ac96e4493da2c40c282bb46e0d22fd528a0770175f2a4a256c5383942470962bd993d7ed2b5a23bdd37bb7ca581cbad37008a4d3d8bc8b58a9912ad29dadaf278181349b29eb12bcc4573eebff75fde62206b1bedab98e045a7f44374071f479d31879f8303599e6f2fc4b4ac41f4bd75662fb3acfa3f497de47d96fb35c58e26f6f583a9e144220b353d49955fbaf924bf6c284e0ced71013e218bbce721ecca759a6bcc2dec1aefda0d76759598218237a92cbdfa3a964a4c86cae565bfee2a9dce7634d99aa8ee9e2178f860df5125b763312365d75b7ae0e3f3201a3502df1540bb15f90d4b26e65c53f38c7107fe92dadcd7b528421b1c00fcea20a325f3a934724a1ed757260ee0d55c4829ae41b644527d9b125e734bd62f06dd30a0371a475c0fed59ada384a7cf4c90e846ace85a9cd1fb4e922ec1eaa491949e96c03cf789edc516ae29dd191aea9413deaa52e625e5d58c6134206576e631d67d862f4c95f2830ba22159e67ac17aaf353b0ad8b998c3953430c641f5d256c16e318");
    uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    std::vector<unsigned char> nSolution = ParseHex("0042aac856a43619453f323a4da4dc6e45b31ec57239837b5adae0f9798c5543bdb4c095594d8a195f3c03141c111ac47d9934e1c1adf86df53cc686bd586a201fc5781ade94bf5b8c03899db82ccac2e77ab41b01673b67eaee98297f14005dfd1ccf01d8555130cf411e463ece114b839661a71d28f7e61686f07596b007b07fbea6d4ef377eb44ab84a7a5c86f9895a207961df3c7880ad0669b67b8846c95eb8469f6ffb96f103ab0fe6c3eb9dd56667218d631c6e58bd8aaf67fd1ea62dc9d85f14edf34902dcac1e7db9ddf356538b06584f337690740be50e41694c952f9c88137c1a79165578c7155d2d696615e5068d55d12adf2c5cfa5f170626f13e86b8f3f92148bc446eb61e4f017beec52609d450830b9e6887ee02e77bcb6ab93a6db8ac8b596cd552dd1b8e42dd16c6a963d72812398756aab7687e33697aed1a6fd60e58f0fccaa6f2e06e58bae90052349ee2c63539ffb0533b5958d3e9846a57681d0d426b292414bb69033b22bd6b788189ed8cbb9bb4123d611a068c9b5f1520655023661dc6d4655f00551c8455473a8fea71dabbf3e3bb7937ab7ef23ece6e0172b5464c4eb28703bcb2afa4228db361327cd1e90391fb2dc08b92311092957bf7391381beb294f4b50352151762d3460fc5e3a330e6c3b3e738fc9b9ed33cb7bc683299faaf3ebdd524936f15417fb97a733100c02973c515a6d0d8c352f406e70a21aa49d58d3c6ea02d32d7aacaf3bc9238effccdfa1e85fad85a05027cb964f2467dd184cfd3d39fc5d611d5ab95deb36ffa5c7b4265cbedfbc12957cb519312ae649e137b1ddc633a9747a15ca5d3a3fc32b1dc19002d7a430023c4f2f0f614627701ae936e53647e12eeccfac493223a7a9cf3e153eb61005654e3cfbe8e8dd87a77ee3340f34406b07cf7f1dd64557e3b308e525d5565fa006bbb85b39452b3afa225b009e5bf81a6fd5093b227c1be1a130d5d13806844988c474d252fd01771c90a700742d4cecde48a3610d792cb56e984d61ccbd840628cb265330bd9b82d249265b91fa9a24cf2d3ec0cd9d0831d9bd72b15a9b181ba6931f106aa56749b380f364b3952a447ba5654959ff162f6baf57c74e51144e1693312e5cca95132623699e90b8c687c81a62d1947f7a6277873af00d2f11358d95cfd26bc05900dbbeb2832843dabf05b9214089928ce1f5c1fcf631a92ba9811dc17e18cbae250bd70f8ad00334c01b41cafac8af378be59d917a204c4b9aff8968994b21e3747c335592158f161b5356b795d0b89e31b74d64512250132d884fe9153e873a92fc7bc9632b7990d1d771f1f95957ba175eb43a86023edbd3b15341d8c6314fa2cbb22df0429779a78190f7b0742742555256c57de37611be70939a6af6a62cdd4de22dfa637a9d300902b23804699f2bf52e2c15b5260054d5dffc15233ac7ba952edb4796f351418413e39295a6031d6b80f1ab2109b56795f757b965bf6695c36c9bc5ecee03b93ae1e4d1a9c5ad76ac481454a6205f20a54d4fb0381a048c80bc11fe7feb74efce3d029e55d3fd75c0ae3106f47588b415d277364c06ca90a74779ca75304d1c97dd250abd6fa4ab5f210ccebfbac13bfcff932b9a1f3eb2271392509b81254f84957787cfd0ac206abd7ad66d73512cf39a3ba3b289c4f32c6fb36ea49bfcd7276f022d1d6c20500ea474092ee9259f6be0e2eed3f09cb36765a8c230d7c38faa18001b3e09916d02b75e0d4f35f804f952bbab70ec1731db6540e09d345478870bc89a3b2579a5855e1965cb2166acc1037c8fdb8deef7341ef62d2d81e26ba981bd897df2ac53cd85b4be8e94aa2878e86cc3c8fb58b7ea193405d0ef6a09a44499050a6dbbd7bd9cdceb85e727f");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_TEST
    MineGenesis::__mineGenBlock("TestNet", true, 200, 9, &block);
#endif
    return block;
}
// static const uint256 TestnetHashGenesisBlock = uint256S("07329f7726fde9f99f0c6440f67af0bcf81e9745a6b2a5c5b0ae1484ca949ab8");
static const uint256 TestnetHashGenesisBlock = uint256S("00a8c297c2f31e33b1496f1652fc03504210381ec83c2a2ad0b9565ffaf998f0");

static CBlock CreateRegtestGenesisBlock()
{
    uint32_t nTime = EpocTime;
    uint32_t nBits = 0x200f0f0f;

#ifdef MINE_GENESIS_REGT
    uint256 nNonce = uint256S("0");
    std::vector<unsigned char> nSolution = ParseHex("0");
#else
    // uint256 nNonce = uint256S("0000000000000000000000000000000000000000000000000000000000000006");
    // std::vector<unsigned char> nSolution = ParseHex("03af814c3088a233b60b4e8c27f1a940f0d30656ebdd40c0934dba313a0d9ed5e555ab20");
    uint256 nNonce = uint256S("000000000000000000000000000000000000000000000000000000000000001c");
    std::vector<unsigned char> nSolution = ParseHex("018330db704084ad7e04f80ab283367a35eb155ae4d421cc42b1834d5974dcd99e7e8b49");
#endif

    CBlock block = CreateGenesisBlock(AnimeGenesisTimestamp, AnimeGenesisPubKey, EpocTime, nNonce, nSolution, nBits);

#ifdef MINE_GENESIS_REGT
    MineGenesis::__mineGenBlock("RegTest", false, 48, 5, &block);
#endif

    return block;
}
// static const uint256 RegtestHashGenesisBlock = uint256S("0e2849244d2234bd48507e85b42ade9f7aa944c9f80d6d10ad268a5a8913d0cd");
static const uint256 RegtestHashGenesisBlock = uint256S("094ff5f0dbb07a2208527822c189800b8d33fe90f668748923ad77759a24f060");

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
        bip44CoinType = 133; // As registered in https://github.com/satoshilabs/slips/blob/master/slip-0044.md
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
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 347500;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 419200;
        consensus.nMaxGovernanceAmount = 1000000*COIN;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000006f31c0e1f30221");
        
        /**
         * The message start string
         */
        pchMessageStart[0] = 0x40;
        pchMessageStart[1] = 0x83;
        pchMessageStart[2] = 0x22;
        pchMessageStart[3] = 0x38;
        //vAlertPubKey = ParseHex("04a40ca15344822fd6bc9719929050614ab045775fcca1e46880ba7e4c55dc999b6f618091a7e5f5c24bd80854f14ebbc369c35cd2e4944c6cf27a3dc5f54f52dc");
        vAlertPubKey = ParseHex("04b4c6cd309cd91173a3033b57eaa0798df23614da65ca4c20d8651420d9a8eff375d7305dd393bb4be982a4e92518340b3d6c9eb2fb124af260f52e745c5c62b5");
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

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zs";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviews";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivks";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-main";

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
        strCurrencyUnits = "INA";
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
        consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight = 207500;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nProtocolVersion = 170007;
        consensus.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight = 280000;
        consensus.nMaxGovernanceAmount = 1000000*COIN;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000000001d0c4d9cd");

        /**
         * The message start string
         */
        pchMessageStart[0] = 0x1b;
        pchMessageStart[1] = 0x16;
        pchMessageStart[2] = 0x83;
        pchMessageStart[3] = 0xd2;
        // vAlertPubKey = ParseHex("04e2c4bfd845e0de6b3fced1bbae723154f751f1a11c41765ce8ca0d97b9d44989964b6e733520bd1d265c18710d307ad4a7cc7042225955c3d38e3c9138dc1eac");
        vAlertPubKey = ParseHex("044bd4eb66eff4a989fce3c35dd179e0d86c4ea865300b52dcee6e005e9f6d2ebf15e6fb55f66a65a8be67c98f74830f0ab943c2d647e8fd1f2a6ce89d3d18e649");
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

        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "ztestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivktestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-test";

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
        pchMessageStart[0] = 0x31;
        pchMessageStart[1] = 0x20;
        pchMessageStart[2] = 0x76;
        pchMessageStart[3] = 0x22;
        // vAlertPubKey = ParseHex("04a40ca15344822fd6bc9719929050614ab045775fcca1e46880ba7e4c55dc999b6f618091a7e5f5c24bd80854f14ebbc369c35cd2e4944c6cf27a3dc5f54f52dc");
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

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            (0, consensus.hashGenesisBlock),
            genesis.nTime,
            0,
            0
        };
        bech32HRPs[SAPLING_PAYMENT_ADDRESS]      = "zregtestsapling";
        bech32HRPs[SAPLING_FULL_VIEWING_KEY]     = "zviewregtestsapling";
        bech32HRPs[SAPLING_INCOMING_VIEWING_KEY] = "zivkregtestsapling";
        bech32HRPs[SAPLING_EXTENDED_SPEND_KEY]   = "secret-extended-key-regtest";
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