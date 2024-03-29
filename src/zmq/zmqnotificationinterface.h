// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once
#include <string>
#include <map>

#include <validationinterface.h>
#include <consensus/validation.h>

class CBlockIndex;
class CZMQAbstractNotifier;

class CZMQNotificationInterface : public CValidationInterface
{
public:
    virtual ~CZMQNotificationInterface();

    static CZMQNotificationInterface* CreateWithArguments(const std::map<std::string, std::string> &args);

protected:
    bool Initialize();
    void Shutdown();

    // CValidationInterface
    void SyncTransaction(const CTransaction &tx, const CBlock *pblock) override;
    void UpdatedBlockTip(const CBlockIndex *pindex, bool fInitialDownload) override;
    void BlockChecked(const CBlock& block, const CValidationState& state) override;

private:
    CZMQNotificationInterface();

    void *pcontext;
    std::list<CZMQAbstractNotifier*> notifiers;
};
