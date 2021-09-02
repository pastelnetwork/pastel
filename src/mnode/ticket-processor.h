#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <unordered_map>
#include <memory>
#include <tuple>

#include "dbwrapper.h"
#include "chain.h"
#include "primitives/transaction.h"
#include "pastelid/pastel_key.h"
#include "mnode/mnode-consts.h"
#include "mnode/ticket.h"

constexpr int DATASTREAM_VERSION = 1;

// tuple <NFT registration txid, NFT trade txid>
using reg_trade_txid_t = std::tuple<std::string, std::string>;

#define FAKE_TICKET
// Ticket  Processor ////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelTicketProcessor
{
    using db_map_t = std::unordered_map<TicketID, std::unique_ptr<CDBWrapper>>;
    db_map_t dbs;

    template <class _TicketType, typename F>
    void listTickets(F f) const;

    template <class _TicketType, typename F>
    std::string filterTickets(F f, const bool checkConfirmation = true) const;

public:
    CPastelTicketProcessor() = default;

    // Create ticket unique_ptr by type
    static std::unique_ptr<CPastelTicket> CreateTicket(const TicketID ticketId);

    void InitTicketDB();
    void UpdatedBlockTip(const CBlockIndex* cBlockIndex, bool fInitialDownload);
    bool ParseTicketAndUpdateDB(CMutableTransaction& tx, const unsigned int nBlockHeight);

    static std::string RealKeyTwo(const std::string& key) { return "@2@" + key; }
    static std::string RealMVKey(const std::string& key) { return "@M@" + key; }

    bool UpdateDB(CPastelTicket& ticket, std::string& txid, const unsigned int nBlockHeight);
    void UpdateDB_MVK(const CPastelTicket& ticket, const std::string& mvKey);

    bool CheckTicketExist(const CPastelTicket& ticket);
    bool FindTicket(CPastelTicket& ticket) const;

    bool CheckTicketExistBySecondaryKey(const CPastelTicket& ticket);
    bool FindTicketBySecondaryKey(CPastelTicket& ticket);

    template <class _TicketType>
    std::vector<_TicketType> FindTicketsByMVKey(const std::string& mvKey);

    v_strings GetAllKeys(const TicketID id) const;

    std::string getValueBySecondaryKey(const CPastelTicket& ticket) const;

    template <class _TicketType>
    std::string ListTickets() const;

    std::string ListFilterPastelIDTickets(const short filter = 0, // 1 - mn;        2 - personal;     3 - mine
                                          const pastelid_store_t* pmapIDs = nullptr) const;
    std::string ListFilterNFTTickets(const short filter = 0) const;   // 1 - active;    2 - inactive;     3 - sold
    std::string ListFilterActTickets(const short filter = 0) const;   // 1 - available; 2 - sold
    std::string ListFilterSellTickets(const short filter = 0, const std::string& pastelID = "") const;  // 0 - all, 1 - available; 2 - unavailable;  3 - expired; 4 - sold
    std::string ListFilterBuyTickets(const short filter = 0, const std::string& pastelID = "") const;   // 0 - all, 1 - traded;    2 - expired
    std::string ListFilterTradeTickets(const short filter = 0, const std::string& pastelID = "") const; // 0 - all, 1 - available; 2 - sold

#ifdef ENABLE_WALLET
    static bool CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
    static bool CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
    static bool CreateP2FMSTransactionWithExtra(const CDataStream& input_data, const std::vector<CTxOut>& extraOutputs, CAmount extraAmount, CMutableTransaction& tx_out, CAmount price, std::string& error_ret);
#endif // ENABLE_WALLET
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::vector<unsigned char>& output_data, std::string& error_ret);
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_string, std::string& error_ret);
    static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);

    static std::string SendTicket(const CPastelTicket& ticket);

    static std::unique_ptr<CPastelTicket> GetTicket(const uint256 &txid);
    static std::unique_ptr<CPastelTicket> GetTicket(const std::string& _txid, const TicketID ticketID);
    static std::string GetTicketJSON(const uint256 &txid);

    static bool ValidateIfTicketTransaction(const int nHeight, const CTransaction& tx);
    
    static bool WalkBackTradingChain(
            const std::string& sTxId,                               // txid of the starting ticket
            std::vector< std::unique_ptr<CPastelTicket> >& chain,   // vector with the tickets in chain
            bool shortPath,                                         // follow short or long path
                                                                    //      Trade, Act, Reg in short walk
                                                                    //      Trade, Buy, Sell, Act or Reg in long walk
            std::string& errRet) noexcept;
    
    std::optional<reg_trade_txid_t> ValidateOwnership(const std::string& _txid, const std::string& _pastelID);

#ifdef FAKE_TICKET
    static std::string CreateFakeTransaction(CPastelTicket& ticket, CAmount ticketPrice, const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif
};
