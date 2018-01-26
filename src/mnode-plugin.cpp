// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "util.h"
#include "base58.h"

#include "mnode-plugin.h"
#include "mnode-msgsigner.h"
#include "mnode-db.h"
#include "mnode-connman.h"

#include <boost/lexical_cast.hpp>

constexpr const CConnman::CFullyConnectedOnly CConnman::FullyConnectedOnly;
constexpr const CConnman::CAllNodes CConnman::AllNodes;

/*
MasterNode specific logic and initializations
*/
const int CMasterNodePlugin::MASTERNODE_PROTOCOL_VERSION = 0x1;

CCriticalSection CMasterNodePlugin::cs_mapMasternodeBlocks;

#ifdef ENABLE_WALLET
bool CMasterNodePlugin::EnableMasterNode(std::ostringstream& strErrors, CWallet* pwalletMain)
#else
bool CMasterNodePlugin::EnableMasterNode(std::ostringstream& strErrors)
#endif
{
    // Masternode should have no wallet
    fMasterNode = GetBoolArg("-masternode", false);

    if((fMasterNode || masternodeConfig.getCount() > -1) && fTxIndex == false) {
        strErrors << _("Enabling Masternode support requires turning on transaction indexing.")
                  << _("Please add txindex=1 to your configuration and start with -reindex");
        return false;
    }

    if(fMasterNode) {
        LogPrintf("MASTERNODE:\n");

        std::string strMasterNodePrivKey = GetArg("-masternodeprivkey", "");
        if(!strMasterNodePrivKey.empty()) {
            if(!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, activeMasternode.keyMasternode, activeMasternode.pubKeyMasternode)) {
                strErrors << _("Invalid masternodeprivkey. Please see documenation.");
                return false;
            }

            LogPrintf("  pubKeyMasternode: %s\n", CBitcoinAddress(activeMasternode.pubKeyMasternode.GetID()).ToString());
        } else {
            strErrors << _("You must specify a masternodeprivkey in the configuration. Please see documentation for help.");
            return false;
        }
    }

#ifdef ENABLE_WALLET
    LogPrintf("Using masternode config file %s\n", GetMasternodeConfigFile().string());

    //Prevent Wallet from accedintal spending of the collateral!!!
    if(GetBoolArg("-mnconflock", true) && pwalletMain && (masternodeConfig.getCount() > 0)) {
        LOCK(pwalletMain->cs_wallet);
        LogPrintf("Locking Masternodes:\n");
        uint256 mnTxHash;
        int outputIndex;
        BOOST_FOREACH(CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            mnTxHash.SetHex(mne.getTxHash());
            outputIndex = boost::lexical_cast<unsigned int>(mne.getOutputIndex());
            COutPoint outpoint = COutPoint(mnTxHash, outputIndex);
            // don't lock non-spendable outpoint (i.e. it's already spent or it's not from this wallet at all)
            if(pwalletMain->IsMine(CTxIn(outpoint)) != ISMINE_SPENDABLE) {
                LogPrintf("  %s %s - IS NOT SPENDABLE, was not locked\n", mne.getTxHash(), mne.getOutputIndex());
                continue;
            }
            pwalletMain->LockCoin(outpoint);
            LogPrintf("  %s %s - locked successfully\n", mne.getTxHash(), mne.getOutputIndex());
        }
    }
#endif // ENABLE_WALLET

    strNetworkName = Params().NetworkIDString();
    if (strNetworkName == "main") {
        network = CBaseChainParams::MAIN;
        nMasternodeMinimumConfirmations = 15;
        nMasternodePaymentsStartBlock = 100000;
        nMasternodePaymentsIncreaseBlock = 150000;
        nMasternodePaymentsIncreasePeriod = 576*30;
        nFulfilledRequestExpireTime = 60*60; // 60 minutes
    }
    else if (strNetworkName == "testnet") {
        network = CBaseChainParams::TESTNET;
        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsStartBlock = 4010;
        nMasternodePaymentsIncreaseBlock = 4030;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 5*60; // 5 minutes
    }
    else if (strNetworkName == "regtest") {
        network = CBaseChainParams::REGTEST;
        nMasternodeMinimumConfirmations = 1;
        nMasternodePaymentsStartBlock = 240;
        nMasternodePaymentsIncreaseBlock = 350;
        nMasternodePaymentsIncreasePeriod = 10;
        nFulfilledRequestExpireTime = 5*60; // 5 minutes
    }
}

bool CMasterNodePlugin::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    masternodeManager.ProcessMessage(pfrom, strCommand, vRecv, connectionManager);
    // mnpayments.ProcessMessage(pfrom, strCommand, vRecv, connectionManager);
    masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);

    return true;
}

bool CMasterNodePlugin::AlreadyHave(const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_MASTERNODE_PAYMENT_VOTE:
        return masternodePayments.mapMasternodePaymentVotes.count(inv.hash);

    case MSG_MASTERNODE_PAYMENT_BLOCK:
        {
            BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
            return mi != mapBlockIndex.end() && masternodePayments.mapMasternodeBlocks.find(mi->second->nHeight) != masternodePayments.mapMasternodeBlocks.end();
        }

    case MSG_MASTERNODE_ANNOUNCE:
        return masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash) && !masternodeManager.IsMnbRecoveryRequested(inv.hash);

    case MSG_MASTERNODE_PING:
        return masternodeManager.mapSeenMasternodePing.count(inv.hash);

    case MSG_MASTERNODE_VERIFY:
        return masternodeManager.mapSeenMasternodeVerification.count(inv.hash);
    };
}

bool CMasterNodePlugin::ProcessGetData(CNode* pfrom, const CInv& inv)
{
    bool pushed = false;

    if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_VOTE) {
        if(masternodePayments.HasVerifiedPaymentVote(inv.hash)) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
//TEMP!!!!!            ss << masternodePayments.mapMasternodePaymentVotes[inv.hash];
            pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_PAYMENT_BLOCK) {
        BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
        LOCK(cs_mapMasternodeBlocks);
        if (mi != mapBlockIndex.end() && masternodePayments.mapMasternodeBlocks.count(mi->second->nHeight)) {
            BOOST_FOREACH(CMasternodePayee& payee, masternodePayments.mapMasternodeBlocks[mi->second->nHeight].vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256& hash, vecVoteHashes) {
                    if(masternodePayments.HasVerifiedPaymentVote(hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
//TEMP!!!!!                        ss << masternodePayments.mapMasternodePaymentVotes[hash];
                        pfrom->PushMessage(NetMsgType::MASTERNODEPAYMENTVOTE, ss);
                    }
                }
            }
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) {
        if(masternodeManager.mapSeenMasternodeBroadcast.count(inv.hash)){
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeManager.mapSeenMasternodeBroadcast[inv.hash].second;
            pfrom->PushMessage(NetMsgType::MNANNOUNCE, ss);
            pushed = true;
        }
    }

    if (!pushed && inv.type == MSG_MASTERNODE_PING) {
        if(masternodeManager.mapSeenMasternodePing.count(inv.hash)) {
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss.reserve(1000);
            ss << masternodeManager.mapSeenMasternodePing[inv.hash];
            pfrom->PushMessage(NetMsgType::MNPING, ss);
            pushed = true;
        }
    }
    return pushed;
}
void CMasterNodePlugin::StoreData()
{
    // STORE DATA CACHES INTO SERIALIZED DAT FILES
    CFlatDB<CMasternodeMan> flatdb1("mncache.dat", "magicMasternodeCache");
    flatdb1.Dump(masternodeManager);
    // CFlatDB<CMasternodePayments> flatdb2("mnpayments.dat", "magicMasternodePaymentsCache");
    // flatdb2.Dump(mnpayments);
    CFlatDB<CNetFulfilledRequestManager> flatdb4("netfulfilled.dat", "magicFulfilledCache");
    flatdb4.Dump(netFulfilledManager);
}

boost::filesystem::path CMasterNodePlugin::GetMasternodeConfigFile()
{
    boost::filesystem::path pathConfigFile(GetArg("-mnconf", "masternode.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir() / pathConfigFile;
    return pathConfigFile;
}

/*
Wrappers for BlockChain specific logic
*/

CAmount CMasterNodePlugin::GetMasternodePayment(int nHeight, CAmount blockValue)
{
    CAmount ret = blockValue/5; // start at 20%

    int nMNPIBlock = nMasternodePaymentsIncreaseBlock;
    int nMNPIPeriod = nMasternodePaymentsIncreasePeriod;

                                                                      // mainnet:
    if(nHeight > nMNPIBlock)                  ret += blockValue / 20; // 158000 - 25.0% - 2014-10-24
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 1)) ret += blockValue / 20; // 175280 - 30.0% - 2014-11-25
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 2)) ret += blockValue / 20; // 192560 - 35.0% - 2014-12-26
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 3)) ret += blockValue / 40; // 209840 - 37.5% - 2015-01-26
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 4)) ret += blockValue / 40; // 227120 - 40.0% - 2015-02-27
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 5)) ret += blockValue / 40; // 244400 - 42.5% - 2015-03-30
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 6)) ret += blockValue / 40; // 261680 - 45.0% - 2015-05-01
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 7)) ret += blockValue / 40; // 278960 - 47.5% - 2015-06-01
    if(nHeight > nMNPIBlock+(nMNPIPeriod* 9)) ret += blockValue / 40; // 313520 - 50.0% - 2015-08-03

    return ret;
}

/* static */ bool CMasterNodePlugin::GetBlockHash(uint256& hashRet, int nBlockHeight)
{
    LOCK(cs_main);
    if(chainActive.Tip() == NULL) return false;
    if(nBlockHeight < -1 || nBlockHeight > chainActive.Height()) return false;
    if(nBlockHeight == -1) nBlockHeight = chainActive.Height();
    hashRet = chainActive[nBlockHeight]->GetBlockHash();
    return true;
}

/* static */ bool CMasterNodePlugin::GetUTXOCoin(const COutPoint& outpoint, CCoins& coins)
{
    LOCK(cs_main);
    if (!pcoinsTip->GetCoins(outpoint.hash, coins))
        return false;
    if (coins.vout[outpoint.n].IsNull())
        return false;
    return true;
}

/* static */ int CMasterNodePlugin::GetUTXOHeight(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    CCoins coins;
    return GetUTXOCoin(outpoint, coins) ? coins.nHeight : -1;
}

/* static */ int CMasterNodePlugin::GetUTXOConfirmations(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    LOCK(cs_main);
    int nPrevoutHeight = GetUTXOHeight(outpoint);
    return (nPrevoutHeight > -1 && chainActive.Tip()) ? chainActive.Height() - nPrevoutHeight + 1 : -1;
}

#ifdef ENABLE_WALLET
/* static */ bool CMasterNodePlugin::GetMasternodeOutpointAndKeys(CWallet* pwalletMain, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex || pwalletMain == NULL) return false;

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
/*ANIM-->
    pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_1000);
<--ANIM*/
    pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, false);
    if(vPossibleCoins.empty()) {
        LogPrintf("CMasterNodePlugin::GetMasternodeOutpointAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if(strTxHash.empty()) // No output specified, select the first one
        return GetOutpointAndKeysFromOutput(pwalletMain, vPossibleCoins[0], outpointRet, pubKeyRet, keyRet);

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    BOOST_FOREACH(COutput& out, vPossibleCoins)
        if(out.tx->GetHash() == txHash && out.i == nOutputIndex) // found it!
            return GetOutpointAndKeysFromOutput(pwalletMain, out, outpointRet, pubKeyRet, keyRet);

    LogPrintf("CMasterNodePlugin::GetMasternodeOutpointAndKeys -- Could not locate specified masternode vin\n");
    return false;
}

/* static */ bool CMasterNodePlugin::GetOutpointAndKeysFromOutput(CWallet* pwalletMain, const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex || pwalletMain == NULL) return false;

    CScript pubScript;

    outpointRet = COutPoint(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CMasterNodePlugin::GetOutpointAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, keyRet)) {
        LogPrintf ("CMasterNodePlugin::GetOutpointAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}
#endif

namespace NetMsgType {
// const char *TXLOCKREQUEST="ix";
// const char *TXLOCKVOTE="txlvote";
// const char *SPORK="spork";
// const char *GETSPORKS="getsporks";
const char *MASTERNODEPAYMENTVOTE="mnw";
const char *MASTERNODEPAYMENTBLOCK="mnwb";
const char *MASTERNODEPAYMENTSYNC="mnget";
const char *MNANNOUNCE="mnb";
const char *MNPING="mnp";
// const char *DSACCEPT="dsa";
// const char *DSVIN="dsi";
// const char *DSFINALTX="dsf";
// const char *DSSIGNFINALTX="dss";
// const char *DSCOMPLETE="dsc";
// const char *DSSTATUSUPDATE="dssu";
// const char *DSTX="dstx";
// const char *DSQUEUE="dsq";
const char *DSEG="dseg";
const char *SYNCSTATUSCOUNT="ssc";
// const char *MNGOVERNANCESYNC="govsync";
// const char *MNGOVERNANCEOBJECT="govobj";
// const char *MNGOVERNANCEOBJECTVOTE="govobjvote";
const char *MNVERIFY="mnv";
};

InsecureRand::InsecureRand(bool _fDeterministic)
    : nRz(11),
      nRw(11),
      fDeterministic(_fDeterministic)
{
    // The seed values have some unlikely fixed points which we avoid.
    if(fDeterministic) return;
    uint32_t nTmp;
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x9068ffffU);
    nRz = nTmp;
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x464fffffU);
    nRw = nTmp;
}
