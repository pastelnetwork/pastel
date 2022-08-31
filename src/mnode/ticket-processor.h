#pragma once
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <memory>
#include <tuple>
#include <optional>
#include <json/json.hpp>

#include <dbwrapper.h>
#include <chain.h>
#include <str_types.h>
#include <map_types.h>
#include <numeric_range.h>
#include <primitives/transaction.h>
#include <txmempool_entry.h>
#include <pastelid/pastel_key.h>
#include <mnode/mnode-consts.h>
#include <mnode/tickets/ticket-types.h>
#include <mnode/tickets/ticket.h>
#include <datacompressor.h>

constexpr int DATASTREAM_VERSION = 1;
constexpr uint8_t TICKET_COMPRESS_ENABLE_MASK  = (1<<7); // using bit 7 to mark a ticket is compressed
constexpr uint8_t TICKET_COMPRESS_DISABLE_MASK = 0x7F;

// tuple <NFT registration txid, NFT trade txid>
using reg_trade_txid_t = std::tuple<std::string, std::string>;

// Get height of the active blockchain + 1
uint32_t GetActiveChainHeight();

// structure used by 'tickets tools searchthumbids' rpc
typedef struct _search_thumbids_t
{
    // map of fuzzy search criteria -> actual NFT ticket property name
    inline static mu_strings fuzzyMappings = 
    {
        { "creator", "creator_name" },
        { "nft", "nft_title" },
        { "series", "nft_series_name" },
        { "keyword", "nft_keyword_set" },
        { "descr", "creator_written_statement" }
    };

    // PastelID of the creator - mandatory
    std::string sCreatorPastelId;
    // block range for nft activation ticket search
    std::optional<numeric_range<uint32_t>> blockRange = std::nullopt;
    // range for number of created copies
    std::optional<numeric_range<uint32_t>> copyCount = std::nullopt;
    // range for rareness score
    std::optional<numeric_range<uint32_t>> rarenessScore = std::nullopt;
    // range for nsfw score
    std::optional<numeric_range<uint32_t>> nsfwScore = std::nullopt;
    // max number of nft reg tickets to return
    std::optional<size_t> nMaxResultCount = std::nullopt;
    // fuzzy search map
    mu_strings fuzzySearchMap;
} search_thumbids_t;

// Check if json value passes fuzzy search filter
bool isValuePassFuzzyFilter(const nlohmann::json& jProp, const std::string& sPropFilterValue) noexcept;

// Ticket  Processor ////////////////////////////////////////////////////////////////////////////////////////////////////
class CPastelTicketProcessor
{
    using db_map_t = std::unordered_map<TicketID, std::unique_ptr<CDBWrapper>>;
    db_map_t dbs; // ticket db storage

    template <class _TicketType, typename F>
    void listTickets(F f, const uint32_t nMinHeight) const;

    // filter tickets of the specific type using functor f
    template <class _TicketType, typename F>
    std::string filterTickets(F f, const uint32_t nMinHeight, const bool bCheckConfirmation = true) const;

public:
    CPastelTicketProcessor() = default;

    // Create ticket unique_ptr by type
    static std::unique_ptr<CPastelTicket> CreateTicket(const TicketID ticketId);

    void InitTicketDB();
    void UpdatedBlockTip(const CBlockIndex* cBlockIndex, bool fInitialDownload);
    bool ParseTicketAndUpdateDB(CMutableTransaction& tx, const unsigned int nBlockHeight);

    static std::string RealKeyTwo(const std::string& key) noexcept { return "@2@" + key; }
    static std::string RealMVKey(const std::string& key) noexcept { return "@M@" + key; }

    bool UpdateDB(CPastelTicket& ticket, std::string& txid, const unsigned int nBlockHeight);
    void UpdateDB_MVK(const CPastelTicket& ticket, const std::string& mvKey);

    // check whether ticket exists (use keyOne as a key)
    bool CheckTicketExist(const CPastelTicket& ticket);
    bool FindTicket(CPastelTicket& ticket) const;

    // Check whether ticket exists (use keyTwo as a key).
    bool CheckTicketExistBySecondaryKey(const CPastelTicket& ticket);
    bool FindTicketBySecondaryKey(CPastelTicket& ticket);

    /**
    * Process tickets of the specified type using functor F.
    *   functor F should return false to stop enumeration.
    *
    * \param mvKey - mvKey to use for tickets enumeration
    * \param f - functor to call for each ticket found by mvKey.
    *       Functor f should return false to stop enumeration.
    */
    template <class _TicketType, typename _TicketFunctor>
    void ProcessTicketsByMVKey(const std::string& mvKey, _TicketFunctor f) const
    {
        v_strings vMainKeys;
        // get real MV key: "@M@" + key
        const auto realMVKey = RealMVKey(mvKey);
        // get DB for the given ticket type
        const auto itDB = dbs.find(_TicketType::GetID());
        if (itDB == dbs.cend())
            return;
        // read primary keys for the given MV key
        itDB->second->Read(realMVKey, vMainKeys);
        for (const auto& key : vMainKeys)
        {
            // read ticket & call the functor
            _TicketType ticket;
            if (itDB->second->Read(key, ticket))
            {
                if (!f(ticket))
                    break; // stop processing tickets if functor returned false
            }
        }
    }

    // find all tickets by mvKey
    template <class _TicketType>
    std::vector<_TicketType> FindTicketsByMVKey(const std::string& mvKey);

    v_strings GetAllKeys(const TicketID id) const;

    std::string getValueBySecondaryKey(const CPastelTicket& ticket) const;

    template <class _TicketType>
    std::string ListTickets(const uint32_t nMinHeight) const;

    // list NFT registration tickets using filter
    std::string ListFilterPastelIDTickets(const uint32_t nMinHeight, const short filter = 0, // 1 - mn;        2 - personal;     3 - mine
                                          const pastelid_store_t* pmapIDs = nullptr) const;
    std::string ListFilterNFTTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - active;    2 - inactive;     3 - sold
    std::string ListFilterNFTCollectionTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - active;    2 - inactive;
    std::string ListFilterActTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - available; 2 - sold
    std::string ListFilterSellTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const;  // 0 - all, 1 - available; 2 - unavailable;  3 - expired; 4 - sold
    std::string ListFilterBuyTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const;   // 0 - all, 1 - traded;    2 - expired
    std::string ListFilterTradeTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const; // 0 - all, 1 - available; 2 - sold
    std::string ListFilterActionTickets(const uint32_t nMinHeight, const short filter = 0) const; // 1 - active;    2 - inactive

    // search for NFT registration tickets, calls functor for each matching ticket
    void SearchForNFTs(const search_thumbids_t &p, std::function<size_t(const CPastelTicket *, const nlohmann::json &)> &fnMatchFound) const;

    static size_t CreateP2FMSScripts(const CDataStream& input_stream, std::vector<CScript>& vOutScripts);
#ifdef ENABLE_WALLET
    static bool CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, 
        const CAmount pricePSL, const opt_string_t& sFundingAddress, std::string& error_ret);
    static bool CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, 
        const CAmount pricePSL, const opt_string_t& sFundingAddress, std::string& error_ret);
    static bool CreateP2FMSTransactionWithExtra(const CDataStream& input_data, 
        const std::vector<CTxOut>& extraOutputs, const CAmount extraAmount, CMutableTransaction& tx_out, 
        const CAmount pricePSL, const opt_string_t& sFundingAddress, std::string& error_ret);
#endif // ENABLE_WALLET
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, CSerializeData& output_data, std::string& error_ret);
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_string, std::string& error_ret);
    // Add P2FMS transaction to the memory pool
    static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);

    static std::tuple<std::string, std::string> SendTicket(const CPastelTicket& ticket, const opt_string_t& sFundingAddress = std::nullopt);

    static std::unique_ptr<CPastelTicket> GetTicket(const uint256 &txid);
    static std::unique_ptr<CPastelTicket> GetTicket(const std::string& _txid, const TicketID ticketID);
    static std::string GetTicketJSON(const uint256 &txid);

    static ticket_validation_t ValidateIfTicketTransaction(const uint32_t nHeight, const CTransaction& tx);
    
    static bool WalkBackTradingChain(
            const std::string& sTxId,   // txid of the starting ticket
            PastelTickets_t& chain,     // vector with the tickets in chain
            bool shortPath,             // follow short or long path
                                        //      Trade, Act, Reg in short walk
                                        //      Trade, Buy, Sell, Act or Reg in long walk
            std::string& errRet) noexcept;
    
    std::optional<reg_trade_txid_t> ValidateOwnership(const std::string& _txid, const std::string& _pastelID);
#ifdef FAKE_TICKET
    static std::string CreateFakeTransaction(CPastelTicket& ticket, const CAmount ticketPricePSL,
        const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif // FAKE_TICKET

    // Reads P2FMS (Pay-to-Fake-Multisig) transaction into CCompressedDataStream object.
    static bool preParseTicket(const CMutableTransaction& tx, CCompressedDataStream& data_stream, TicketID& ticket_id, std::string& error, const bool bLog = true);

    // Get mempool tracker for ticket transactions
    static std::shared_ptr<ITxMemPoolTracker> GetTxMemPoolTracker();

private:
    static ticket_validation_t ValidateTicketFees(const uint32_t nHeight, const CTransaction& tx, std::unique_ptr<CPastelTicket>&& ticket) noexcept;
};
