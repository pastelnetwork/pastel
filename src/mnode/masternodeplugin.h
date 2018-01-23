// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEPLUGIN_H
#define MASTERNODEPLUGIN_H

#include <string>

#include "sync.h"
#include "main.h"
#include "init.h"
#include "util.h"
#include "chainparams.h"
#include "protocol.h"
#include "net.h"
#include "key.h"
#include "timedata.h"
#include "arith_uint256.h"
#include "primitives/transaction.h"
#include "chain.h"
#include "base58.h"
#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "checkpoints.h"
#include "ui_interface.h"
#include "univalue.h"
#include "validation.h"
#include "netbase.h"
#include "serialize.h"

// #include "script/standard.h"
// #include "addrman.h"

// #include "governance.h"
// #include "netfulfilledman.h"
//#include "netfulfilledman.h"
// #include "spork.h"

#ifdef ENABLE_WALLET
// #include "privatesend-client.h"
// #include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include "activemasternode.h"
#include "masternodeconfig.h"
#include "netfulfilledman.h"

class CConnman;

class CMasterNodePlugin;
extern CMasterNodePlugin masterNodePlugin;

class CMasterNodePlugin
{
public:
    CMasternodeConfig masternodeConfig;

    // Keep track of the active Masternode
    CActiveMasternode activeMasternode;

    // Keep track of what node has/was asked for and when
    CNetFulfilledRequestManager netfulfilledman;

    //
    CMasternodeSync masternodeSync;

    //Connection Manger - wrapper around network operations
    std::unique_ptr<CConnman> connman;

    bool fMasterNode;
    std::string strNetworkName;
    CBaseChainParams::Network network;

public:
    int nMasternodeMinimumConfirmations, nMasternodePaymentsStartBlock, nMasternodePaymentsIncreaseBlock, nMasternodePaymentsIncreasePeriod;
    int nFulfilledRequestExpireTime;

    static CCriticalSection cs_mapMasternodeBlocks;

    CMasterNodePlugin() : 
        fMasterNode(false),
        nMasternodeMinimumConfirmations(15),
        nMasternodePaymentsStartBlock(100000),
        nMasternodePaymentsIncreaseBlock(158000),
        nMasternodePaymentsIncreasePeriod(576*30),
        nFulfilledRequestExpireTime(60*60) // fulfilled requests expire in 1 hour    
    {
    }

    operator bool() const {return fMasterNode;}

    bool EnableMasterNode(std::ostringstream& strErrors);

    boost::filesystem::path GetMasternodeConfigFile();

    bool GetBlockHash(uint256& hashRet, int nBlockHeight);
    CAmount GetMasternodePayment(int nHeight, CAmount blockValue);

    inline bool IsMainNet() {return network == CBaseChainParams::MAIN;}
    inline bool IsTestNet() {return network == CBaseChainParams::TESTNET;}
    inline bool IsRegTest() {return network == CBaseChainParams::REGTEST;}
};

namespace NetMsgType {
// extern const char *TXLOCKREQUEST;
// extern const char *TXLOCKVOTE;
// extern const char *SPORK;
// extern const char *GETSPORKS;
// extern const char *MASTERNODEPAYMENTVOTE;
extern const char *MASTERNODEPAYMENTSYNC;
extern const char *MNANNOUNCE;
extern const char *MNPING;
// extern const char *DSACCEPT;
// extern const char *DSVIN;
// extern const char *DSFINALTX;
// extern const char *DSSIGNFINALTX;
// extern const char *DSCOMPLETE;
// extern const char *DSSTATUSUPDATE;
// extern const char *DSTX;
// extern const char *DSQUEUE;
extern const char *DSEG;
extern const char *SYNCSTATUSCOUNT;
// extern const char *MNGOVERNANCESYNC;
// extern const char *MNGOVERNANCEOBJECT;
// extern const char *MNGOVERNANCEOBJECTVOTE;
extern const char *MNVERIFY;
};

class CConnman 
{
public:
    void RelayInv(CInv& inv) {}
    void ReleaseNodeVector(const std::vector<CNode*>& vecNodes) {}

    void AddNewAddress(const CAddress& addr, const CAddress& addrFrom, int64_t nTimePenalty = 0) {}
    void AddNewAddresses(const std::vector<CAddress>& vAddr, const CAddress& addrFrom, int64_t nTimePenalty = 0) {}

    template <typename... Args>
    void PushMessageWithVersionAndFlag(CNode* pnode, int nVersion, int flag, const std::string& sCommand, Args&&... args)
    {
        auto msg(BeginMessage(pnode, nVersion, flag, sCommand));
        ::SerializeMany(msg, msg.nType, msg.nVersion, std::forward<Args>(args)...);
        EndMessage(msg);
        PushMessage(pnode, msg, sCommand);
    }

    template <typename... Args>
    void PushMessageWithFlag(CNode* pnode, int flag, const std::string& sCommand, Args&&... args)
    {
        PushMessageWithVersionAndFlag(pnode, 0, flag, sCommand, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void PushMessageWithVersion(CNode* pnode, int nVersion, const std::string& sCommand, Args&&... args)
    {
        PushMessageWithVersionAndFlag(pnode, nVersion, 0, sCommand, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void PushMessage(CNode* pnode, const std::string& sCommand, Args&&... args)
    {
        PushMessageWithVersionAndFlag(pnode, 0, 0, sCommand, std::forward<Args>(args)...);
    }

    template<typename Condition, typename Callable>
    bool ForEachNodeContinueIf(const Condition& cond, Callable&& func)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes)
            if (cond(node))
                if(!func(node))
                    return false;
        return true;
    };

    template<typename Callable>
    bool ForEachNodeContinueIf(Callable&& func)
    {
        return ForEachNodeContinueIf(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    bool ForEachNodeContinueIf(const Condition& cond, Callable&& func) const
    {
        LOCK(cs_vNodes);
        for (const auto& node : vNodes)
            if (cond(node))
                if(!func(node))
                    return false;
        return true;
    };

    template<typename Callable>
    bool ForEachNodeContinueIf(Callable&& func) const
    {
        return ForEachNodeContinueIf(FullyConnectedOnly, func);
    }


    template<typename Condition, typename Callable>
    void ForEachNode(const Condition& cond, Callable&& func)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (cond(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable&& func)
    {
        ForEachNode(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    void ForEachNode(const Condition& cond, Callable&& func) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (cond(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable&& func) const
    {
        ForEachNode(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable, typename CallableAfter>
    void ForEachNodeThen(const Condition& cond, Callable&& pre, CallableAfter&& post)
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (cond(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post)
    {
        ForEachNodeThen(FullyConnectedOnly, pre, post);
    }

    template<typename Condition, typename Callable, typename CallableAfter>
    void ForEachNodeThen(const Condition& cond, Callable&& pre, CallableAfter&& post) const
    {
        LOCK(cs_vNodes);
        for (auto&& node : vNodes) {
            if (cond(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable&& pre, CallableAfter&& post) const
    {
        ForEachNodeThen(FullyConnectedOnly, pre, post);
    }

    CNode* ConnectNode(CAddress addrConnect, const char *pszDest = NULL, bool fConnectToMasternode = false);

    std::vector<CNode*> CopyNodeVector() {}

    struct CAllNodes {
        bool operator() (const CNode*) const {return true;}
    };

    constexpr static const CAllNodes AllNodes{};
};

#endif
