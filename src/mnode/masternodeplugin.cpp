// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternodeplugin.h"
#include "messagesigner.h"
#include "flat-database.h"

/*
MasterNode specific logic and initializations
*/
const int CMasterNodePlugin::MASTERNODE_PROTOCOL_VERSION = 0x1;

CCriticalSection CMasterNodePlugin::cs_mapMasternodeBlocks;

bool CMasterNodePlugin::EnableMasterNode(std::ostringstream& strErrors)
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

namespace NetMsgType {
// const char *TXLOCKREQUEST="ix";
// const char *TXLOCKVOTE="txlvote";
// const char *SPORK="spork";
// const char *GETSPORKS="getsporks";
// const char *MASTERNODEPAYMENTVOTE="mnw";
// const char *MASTERNODEPAYMENTBLOCK="mnwb";
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
