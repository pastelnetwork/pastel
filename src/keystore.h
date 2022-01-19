#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "zcash/Address.hpp"
#include "zcash/NoteEncryption.hpp"

#include <boost/signals2/signal.hpp>

/** A virtual base class for key stores */
class CKeyStore
{
protected:
    mutable CCriticalSection cs_KeyStore;

public:
    virtual ~CKeyStore() {}

    //! Set the HD seed for this keystore
    virtual bool SetHDSeed(const HDSeed& seed) =0;
    virtual bool HaveHDSeed() const =0;
    //! Get the HD seed for this keystore
    virtual bool GetHDSeed(HDSeed& seedOut) const =0;

    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual std::set<CKeyID> GetKeys() const noexcept = 0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const = 0;

    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    //! Support for Watch-only addresses
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveWatchOnly() const =0;

    //! Add a Sapling spending key to the store.
    virtual bool AddSaplingSpendingKey(const libzcash::SaplingExtendedSpendingKey &sk) =0;
    
    //! Check whether a Sapling spending key corresponding to a given Sapling viewing key is present in the store.
    virtual bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &fvk) const =0;
    virtual bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &fvk, libzcash::SaplingExtendedSpendingKey& skOut) const =0;

    //! Support for Sapling full viewing keys
    virtual bool AddSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const =0;
    virtual bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk, 
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const =0;

    //! Sapling incoming viewing keys 
    virtual bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr) =0;
    virtual bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const =0;
    virtual bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr, 
        libzcash::SaplingIncomingViewingKey& ivkOut) const =0;
    virtual void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const =0;
    virtual bool GetSaplingExtendedSpendingKey(
        const libzcash::SaplingPaymentAddress& addr,
        libzcash::SaplingExtendedSpendingKey& extskOut) const = 0;
};

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CKeyID, CPubKey> WatchKeyMap;
typedef std::map<CScriptID, CScript > ScriptMap;
typedef std::set<CScript> WatchOnlySet;

// Full viewing key has equivalent functionality to a transparent address
// When encrypting wallet, encrypt SaplingSpendingKeyMap, while leaving SaplingFullViewingKeyMap unencrypted
typedef std::map<libzcash::SaplingExtendedFullViewingKey, libzcash::SaplingExtendedSpendingKey> SaplingSpendingKeyMap;
typedef std::map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey> SaplingFullViewingKeyMap;
// Only maps from default addresses to ivk, may need to be reworked when adding diversified addresses. 
typedef std::map<libzcash::SaplingPaymentAddress, libzcash::SaplingIncomingViewingKey> SaplingIncomingViewingKeyMap;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
    HDSeed hdSeed;
    KeyMap mapKeys;
    WatchKeyMap mapWatchKeys;
    ScriptMap mapScripts;
    WatchOnlySet setWatchOnly;

    SaplingSpendingKeyMap mapSaplingSpendingKeys;
    SaplingFullViewingKeyMap mapSaplingFullViewingKeys;
    SaplingIncomingViewingKeyMap mapSaplingIncomingViewingKeys;

public:
    bool SetHDSeed(const HDSeed& seed) override;
    bool HaveHDSeed() const override;
    bool GetHDSeed(HDSeed& seedOut) const override;

    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    bool HaveKey(const CKeyID &address) const override
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
    std::set<CKeyID> GetKeys() const noexcept override
    {
        std::set<CKeyID> set_address;
        LOCK(cs_KeyStore);
        for (const auto& mi : mapKeys)
            set_address.insert(mi.first);
        return set_address;
    }
    bool GetKey(const CKeyID &address, CKey &keyOut) const override
    {
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut = mi->second;
                return true;
            }
        }
        return false;
    }
    bool AddCScript(const CScript& redeemScript) override;
    bool HaveCScript(const CScriptID &hash) const override;
    bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const override;

    bool AddWatchOnly(const CScript &dest) override;
    bool RemoveWatchOnly(const CScript& dest) override;
    bool HaveWatchOnly(const CScript& dest) const override;
    bool HaveWatchOnly() const override;

    //! Sapling 
    bool AddSaplingSpendingKey(const libzcash::SaplingExtendedSpendingKey &sk) override;
    bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const override
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapSaplingSpendingKeys.count(extfvk) > 0);
        }
        return result;
    }
    bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey &skOut) const override
    {
        {
            LOCK(cs_KeyStore);

            SaplingSpendingKeyMap::const_iterator mi = mapSaplingSpendingKeys.find(extfvk);
            if (mi != mapSaplingSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }

    bool AddSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) override;
    bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const override;
    bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const override;

    bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr) override;
    bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const override;
    bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr, 
        libzcash::SaplingIncomingViewingKey& ivkOut) const override;

    bool GetSaplingExtendedSpendingKey(
        const libzcash::SaplingPaymentAddress &addr, 
        libzcash::SaplingExtendedSpendingKey &extskOut) const override;
    
    void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const override
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            auto mi = mapSaplingIncomingViewingKeys.begin();
            while (mi != mapSaplingIncomingViewingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;
typedef std::map<CKeyID, std::pair<CPubKey, v_uint8>> CryptedKeyMap;

//! Sapling 
typedef std::map<libzcash::SaplingExtendedFullViewingKey, v_uint8> CryptedSaplingSpendingKeyMap;
