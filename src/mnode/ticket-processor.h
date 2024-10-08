#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <memory>
#include <tuple>
#include <optional>
#include <atomic>

#include <extlibs/json.hpp>

#include <utils/str_types.h>
#include <utils/map_types.h>
#include <utils/numeric_range.h>
#include <utils/datacompressor.h>
#include <dbwrapper.h>
#include <chain.h>
#include <main.h>
#include <primitives/transaction.h>
#include <txmempool_entry.h>
#include <consensus/validation.h>
#include <pastelid/pastel_key.h>
#include <mnode/mnode-consts.h>
#include <mnode/tickets/ticket-types.h>
#include <mnode/tickets/ticket.h>

constexpr uint8_t TICKET_COMPRESS_ENABLE_MASK  = (1<<7); // using bit 7 to mark a ticket is compressed
constexpr uint8_t TICKET_COMPRESS_DISABLE_MASK = 0x7F;
constexpr auto TICKET_KEYTWO_PREFIX = "@2@";  // Ticket DB secondary key prefix (unique)
constexpr auto TICKET_MVKEY_PREFIX = "@M@";   // Ticket DB auxiliary key prefix (non-unique)

// tuple <item id, item registration txid, transfer ticket txid>
using reg_transfer_txid_t = std::tuple<TicketID, std::string, std::string>;

typedef enum class _EraseTicketResultEnum
{
    NotFound = 1,
    Success = 0,
    CreateTicketError = -1,
    EraseFromDBError = -2,
} EraseTicketResult;

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

    // Pastel ID of the creator - mandatory
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

using process_ticket_data_func_t = std::function<bool(std::string &&sKey, const PastelTicketPtr&)>;

// Check if json value passes fuzzy search filter
bool isValuePassFuzzyFilter(const nlohmann::json& jProp, const std::string& sPropFilterValue) noexcept;

typedef struct _ticket_parse_data_t
{
    CTransaction tx;
    std::unique_ptr<CMutableTransaction> mtx;
    uint256 hashBlock;
    uint32_t nTicketHeight = std::numeric_limits<uint32_t>::max();
    TicketID ticket_id = TicketID::InvalidID;
    CCompressedDataStream data_stream;
    uint32_t nMultiSigOutputsCount = 0;
    CAmount nMultiSigTxTotalFee = 0;

    _ticket_parse_data_t() :
        data_stream(SER_NETWORK, DATASTREAM_VERSION)
    {}
} ticket_parse_data_t;

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
    CPastelTicketProcessor() noexcept = default;

    // Create ticket unique_ptr by type
    static PastelTicketPtr CreateTicket(const TicketID ticketId);

    void InitTicketDB();
    void UpdatedBlockTip(const CBlockIndex* cBlockIndex, bool fInitialDownload);
    bool ParseTicketAndUpdateDB(CMutableTransaction& tx, const unsigned int nBlockHeight);

    static std::string RealKeyTwo(const std::string& key) noexcept { return TICKET_KEYTWO_PREFIX + key; }
    static std::string RealMVKey(const std::string& key) noexcept { return TICKET_MVKEY_PREFIX + key; }

    bool UpdateDB(CPastelTicket& ticket, std::string& txid, const unsigned int nBlockHeight);
    void UpdateDB_MVK(const CPastelTicket& ticket, const std::string& mvKey);

    // check whether ticket exists (use keyOne as a key)
    bool CheckTicketExist(const CPastelTicket& ticket, const CBlockIndex* pindexPrev = nullptr) const;
    bool FindTicket(CPastelTicket& ticket, const CBlockIndex* pindexPrev = nullptr) const;
    bool EraseTicketFromDB(std::string &error, const CPastelTicket& ticket) const;
    EraseTicketResult EraseIfTicketTransaction(const uint256& txid, std::string &error);
    size_t EraseTicketsFromDbByList(const block_index_cvector_t& vBlockIndex);
    void RepairTicketDB(const bool bUpdateUI);

    // Check whether ticket exists (use keyTwo as a key).
    bool CheckTicketExistBySecondaryKey(const CPastelTicket& ticket, const CBlockIndex *pindexPrev = nullptr) const;
    bool FindTicketBySecondaryKey(CPastelTicket& ticket, const CBlockIndex *pindexPrev = nullptr) const;

    /**
    * Process tickets of the specified type using functor F.
    *   functor F should return false to stop enumeration.
    *
    * \param mvKey - mvKey to use for tickets enumeration
    * \param f - functor to call for each ticket found by mvKey.
    *       Functor f should return false to stop enumeration.
    */
    template <class _TicketType, typename _TicketFunctor>
    void ProcessTicketsByMVKey(const std::string& mvKey, const CBlockIndex *pindexPrev, _TicketFunctor f) const
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
        const uint32_t nCurrentChainHeight = gl_nChainHeight;
        for (const auto& key : vMainKeys)
        {
            // read ticket & call the functor
            _TicketType ticket;
            if (itDB->second->Read(key, ticket) && !ticket.IsBlockNewerThan(nCurrentChainHeight))
            {
                // check if this ticket is in the same chain as pindexPrev
                if (pindexPrev)
                {
					const uint32_t nTicketBlockHeight = ticket.GetBlock();
					if (nTicketBlockHeight == std::numeric_limits<uint32_t>::max())
						continue;
					const auto pindexTicket = chainActive[nTicketBlockHeight];
					if (pindexPrev->GetAncestor(nTicketBlockHeight) != pindexTicket)
						continue;
                }
                if (!f(ticket))
                    break; // stop processing tickets if functor returned false
            }
        }
    }

    // find all tickets by mvKey
    template <class _TicketType>
    std::vector<_TicketType> FindTicketsByMVKey(const std::string& mvKey, const CBlockIndex *pindexPrev = nullptr);

     // Process all tickets from the database of the given type.
    bool ProcessAllTickets(TicketID id, process_ticket_data_func_t ticketFunctor) const;

    template <class _TicketType>
    std::tuple<size_t, size_t, size_t, size_t> calculateTicketSizes(
        const uint32_t nMinHeight,
        const size_t nTicketsToCheckSize,
        const size_t nTicketsToUseForEstimation) const;

    v_strings GetAllKeys(const TicketID id) const;

    std::string getValueBySecondaryKey(const CPastelTicket& ticket) const;

    template <class _TicketType>
    std::string ListTickets(const uint32_t nMinHeight) const;

    // list NFT registration tickets using filter
    std::string ListFilterPastelIDTickets(const uint32_t nMinHeight, const short filter = 0, // 1 - mn;        2 - personal;     3 - mine
                                          const pastelid_store_t* pmapIDs = nullptr) const;
    std::string ListFilterNFTTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - active;    2 - inactive;     3 - transferred
    std::string ListFilterCollectionTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - active;    2 - inactive;
    std::string ListFilterActTickets(const uint32_t nMinHeight, const short filter = 0) const;   // 1 - available; 2 - transferred
    std::string ListFilterOfferTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const;  // 0 - all, 1 - available; 2 - unavailable;  3 - expired; 4 - transferred
    std::string ListFilterAcceptTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const;   // 0 - all, 1 - transferred; 2 - expired
    std::string ListFilterTransferTickets(const uint32_t nMinHeight, const short filter = 0, const std::string& pastelID = "") const; // 0 - all, 1 - available; 2 - transferred
    std::string ListFilterActionTickets(const uint32_t nMinHeight, const short filter = 0) const; // 1 - active;    2 - inactive
    std::string ListFilterContractTickets(const uint32_t nMinHeight, const std::string& subtype) const;

    // search for NFT registration tickets, calls functor for each matching ticket
    void SearchForNFTs(const search_thumbids_t &p, std::function<size_t(const CPastelTicket *, const nlohmann::json &)> &fnMatchFound) const;

    static bool FindAndValidateTicketTransaction(const CPastelTicket& ticket,
        const std::string& new_txid, const uint32_t nNewHeight, const bool bPreReg,
        std::string &sMessage, const bool bAllowDuplicates = false);
    static void RemoveTicketFromMempool(const std::string& txid);

#ifdef ENABLE_WALLET
    static bool CreateP2FMSTransaction(const std::string& input_string, CMutableTransaction& tx_out, 
        const CAmount nPricePSL, const opt_string_t& sFundingAddress, std::string& error_ret);
    static bool CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, 
        const CAmount nPricePSL, const opt_string_t& sFundingAddress, std::string& error_ret);
#endif // ENABLE_WALLET
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, CSerializeData& output_data, std::string& error_ret,
        uint32_t &nMultiSigOutputsCount, CAmount &nMultiSigTxTotalFee);
    static bool ParseP2FMSTransaction(const CMutableTransaction& tx_in, std::string& output_string, std::string& error_ret,
        uint32_t &nMultiSigOutputsCount, CAmount &nMultiSigTxTotalFee);
    // Add P2FMS transaction to the memory pool
    static bool StoreP2FMSTransaction(const CMutableTransaction& tx_out, std::string& error_ret);

    static std::tuple<std::string, std::string> SendTicket(const CPastelTicket& ticket, const opt_string_t& sFundingAddress = std::nullopt);

    static PastelTicketPtr GetTicket(const uint256 &txid, uint256* pBlockHash = nullptr, 
        const CBlockIndex *pindexPrev = nullptr);
    static PastelTicketPtr GetTicket(const std::string& _txid, const TicketID ticketID,
        const CBlockIndex *pindexPrev = nullptr);
    static std::string GetTicketJSON(const uint256 &txid, const bool bDecodeProperties = false);

    static ticket_validation_t ValidateIfTicketTransaction(CValidationState &state, const uint32_t nHeight, 
        const CTransaction& tx, const CBlockIndex *pindexPrev);
    
    static bool WalkBackTradingChain(
            const std::string& sTxId,   // txid of the starting ticket
            PastelTickets_t& chain,     // vector with the tickets in chain
            bool shortPath,             // follow short or long path
                                        //      Transfer, Act, Reg in short walk
                                        //      Transfer, Accept, Offer, Act or Reg in long walk
            std::string& errRet,
            const CBlockIndex *pindexPrev = nullptr) noexcept;
    
    std::optional<reg_transfer_txid_t> ValidateOwnership(const std::string& _txid, const std::string& _pastelID);
#ifdef FAKE_TICKET
    static std::string CreateFakeTransaction(CPastelTicket& ticket, const CAmount ticketPricePSL,
        const std::vector<std::pair<std::string, CAmount>>& extraPayments, const std::string& strVerb, bool bSend);
#endif // FAKE_TICKET

    static bool SerializeTicketToStream(const uint256& txid, std::string& error,
        ticket_parse_data_t &data, const bool bUncompressData = true);

    // Reads P2FMS (Pay-to-Fake-Multisig) transaction into CCompressedDataStream object.
    static bool preParseTicket(const CMutableTransaction& tx, CCompressedDataStream& data_stream,
        TicketID& ticket_id, std::string& error, uint32_t &nMultiSigOutputsCount, CAmount &nMultiSigTxTotalFee,
        const bool bLog = true, const bool bUncompressData = true);

    // Get mempool tracker for ticket transactions
    static tx_mempool_tracker_t GetTxMemPoolTracker();

    static uint32_t GetTicketBlockHeightInActiveChain(const uint256& txid);

protected:
    bool m_bTicketDBInitialized = false;

    static ticket_validation_t ValidateTicketFees(const uint32_t nHeight, const CTransaction& tx, PastelTicketPtr&& ticket) noexcept;
};
