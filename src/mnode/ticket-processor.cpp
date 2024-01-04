// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <cinttypes>
#include <json/json.hpp>

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif
#include <utils/str_utils.h>
#include <utils/utilstrencodings.h>
#include <chain.h>
#include <main.h>
#include <deprecation.h>
#include <script/sign.h>
#include <core_io.h>
#include <key_io.h>
#include <init.h>
#include <accept_to_mempool.h>
#include <mnode/tickets/ticket-types.h>
#include <mnode/tickets/tickets-all.h>
#include <mnode/mnode-controller.h>
#include <mnode/ticket-processor.h>
#include <mnode/ticket-txmempool.h>
#include <mnode/p2fms-txbuilder.h>

using json = nlohmann::json;
using namespace std;

static tx_mempool_tracker_t TicketTxMemPoolTracker;

void CPastelTicketProcessor::InitTicketDB()
{
    fs::path ticketsDir = GetDataDir() / "tickets";
    if (!fs::exists(ticketsDir))
        fs::create_directories(ticketsDir);

    uint64_t nTotalCache = (GetArg("-dbcache", 450) << 20);
    constexpr uint64_t nMinDbCache = 4;
    constexpr uint64_t nMaxDbCache = 16384; //16KB
    nTotalCache = max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbCache
    const uint64_t nTicketDBCache = nTotalCache / 8 / uint8_t(TicketID::COUNT);

    // create DB for each ticket type
    for (uint8_t id = to_integral_type<TicketID>(TicketID::PastelID); id != to_integral_type<TicketID>(TicketID::COUNT); ++id)
        dbs.emplace(static_cast<TicketID>(id), make_unique<CDBWrapper>(ticketsDir / TICKET_INFO[id].szDBSubFolder, nTicketDBCache, false, fReindex));
}

/**
 * Create ticket unique_ptr by type.
 * 
 * \param ticketId - ticket type
 */
unique_ptr<CPastelTicket> CPastelTicketProcessor::CreateTicket(const TicketID ticketId)
{
    unique_ptr<CPastelTicket> ticket;
    switch (ticketId)
    {
    case TicketID::PastelID:
        ticket = make_unique<CPastelIDRegTicket>();
        break;

    case TicketID::NFT:
        ticket = make_unique<CNFTRegTicket>();
        break;

    case TicketID::CollectionReg:
        ticket = make_unique<CollectionRegTicket>();
        break;

    case TicketID::CollectionAct:
        ticket = make_unique<CollectionActivateTicket>();
        break;

    case TicketID::Activate:
        ticket = make_unique<CNFTActivateTicket>();
        break;

    case TicketID::Offer:
        ticket = make_unique<COfferTicket>();
        break;

    case TicketID::Accept:
        ticket = make_unique<CAcceptTicket>();
        break;

    case TicketID::Transfer:
        ticket = make_unique<CTransferTicket>();
        break;

    case TicketID::Royalty:
        ticket = make_unique<CNFTRoyaltyTicket>();
        break;

    case TicketID::Down:
        ticket = make_unique<CTakeDownTicket>();
        break;

    case TicketID::Username:
        ticket = make_unique<CChangeUsernameTicket>();
        break;

    case TicketID::EthereumAddress:
        ticket = make_unique<CChangeEthereumAddressTicket>();
        break;

    case TicketID::ActionReg:
        ticket = make_unique<CActionRegTicket>();
        break;

    case TicketID::ActionActivate:
        ticket = make_unique<CActionActivateTicket>();
        break;

    default: // to suppress compiler warning for not handling TicketID::COUNT
        break;
    }
    return ticket;
}

void CPastelTicketProcessor::UpdatedBlockTip(const CBlockIndex* cBlockIndex, bool fInitialDownload)
{
    if (!cBlockIndex)
        return;

    CBlock block;
    if (!ReadBlockFromDisk(block, cBlockIndex, Params().GetConsensus()))
    {
        LogFnPrintf("ERROR: Can't read block from disk");
        return;
    }

    for (const auto& tx : block.vtx)
    {
        CMutableTransaction mtx(tx);
        ParseTicketAndUpdateDB(mtx, cBlockIndex->nHeight);
    }
}

void CPastelTicketProcessor::UpdateDB_MVK(const CPastelTicket& ticket, const string& mvKey)
{
    v_strings mainKeys;
    const auto realMVKey = RealMVKey(mvKey);
    auto itDB = dbs.find(ticket.ID());
    if (itDB == dbs.end())
        return;
    itDB->second->Read(realMVKey, mainKeys);
    if (find(mainKeys.cbegin(), mainKeys.cend(), ticket.KeyOne()) == mainKeys.cend())
    {
        mainKeys.emplace_back(ticket.KeyOne());
        itDB->second->Write(realMVKey, mainKeys, true);
    }
}

bool CPastelTicketProcessor::UpdateDB(CPastelTicket &ticket, string& txid, const unsigned int nBlockHeight)
{
    if (!txid.empty())
        ticket.SetTxId(move(txid));
    if (nBlockHeight != 0)
        ticket.SetBlock(nBlockHeight);
    auto itDB = dbs.find(ticket.ID());
    if (itDB == dbs.end())
        return false;
    itDB->second->Write(ticket.KeyOne(), ticket, true);
    if (ticket.HasKeyTwo())
    {
        auto realKeyTwo = RealKeyTwo(ticket.KeyTwo());
        itDB->second->Write(realKeyTwo, ticket.KeyOne(), true);
    }

    if (ticket.HasMVKeyOne())
        UpdateDB_MVK(ticket, ticket.MVKeyOne());
    if (ticket.HasMVKeyTwo())
        UpdateDB_MVK(ticket, ticket.MVKeyTwo());
    if (ticket.HasMVKeyThree())
        UpdateDB_MVK(ticket, ticket.MVKeyThree());
        
    //LogFnPrintf("tickets", "Ticket added into DB with key %s (txid - %s)", ticket.KeyOne(), ticket.ticketTnx);
    return true;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into CDataStream object.
 * Parses the first byte from the stream - it defines ticket id.
 * 
 * \param tx - transaction
 * \param data_stream - compressed data stream ()
 * \param ticket_id - ticket id (first byte in the data stream)
 * \param error - returns error message if any
 * \param bLog - log flag, default is true
 * \param bUncompressData - uncompress ticket data (if stored in compressed zstd format), default is true
 * \return - true if ticket was parsed successfully
 */
bool CPastelTicketProcessor::preParseTicket(const CMutableTransaction& tx, CCompressedDataStream& data_stream,
    TicketID& ticket_id, string& error, const bool bLog, const bool bUncompressData)
{
    CSerializeData vOutputData;
    bool bRet = false;
    do
    {
        if (!ParseP2FMSTransaction(tx, vOutputData, error))
            break;
        if (vOutputData.empty())
        {
            error = "No correct data found in transaction - empty data";
            break;
        }
        // first byte of the ticket data:
        // +---------bits---------+------------+
        // | 1  2  3  4  5  6  7  |      8     |
        // +----------------------+------------+
        // | ticket id (0..127)   | compressed |
        // +----------------------+------------+
        uint8_t nTicketID = vOutputData[0];

        const bool bCompressed = (nTicketID & TICKET_COMPRESS_ENABLE_MASK) > 0;
        nTicketID &= TICKET_COMPRESS_DISABLE_MASK;
        // validate ticket id
        if (nTicketID >= to_integral_type<TicketID>(TicketID::COUNT))
        {
            error = strprintf("Unknown ticket type (%hhu) found in P2FMS transaction", nTicketID);
            break;
        }
        ticket_id = static_cast<TicketID>(nTicketID);
        const size_t nOutputDataSize = vOutputData.size();
        bRet = data_stream.SetData(error, bCompressed, 1, move(vOutputData), bUncompressData);
        if (!bRet)
            error = strprintf("Failed to uncompress data for '%s' ticket. %s", GetTicketDescription(ticket_id), error);
        else if (bCompressed)
            LogPrint("compress", "Ticket [%hhu] data uncompressed [%zu]->[%zu]\n", nTicketID, nOutputDataSize, data_stream.size());
    } while (false);
    return bRet;
}

/**
 * Ticket fees validation.
 * 
 * \param nHeight - current height
 * \param tx - ticket transaction
 * \param ticket - ticket object
 * \return validation status with error message if any
 */
ticket_validation_t CPastelTicketProcessor::ValidateTicketFees(const uint32_t nHeight, const CTransaction& tx, unique_ptr<CPastelTicket>&& ticket) noexcept
{
    ticket_validation_t tv;
    do
    {
        // expected ticket fee in patoshis
        const CAmount expectedTicketFee = ticket->TicketPricePSL(nHeight) * COIN;

        CAmount transferPrice = 0;
        optional<CAmount> royaltyFee;
        optional<CAmount> greenFee;

        const TicketID ticket_id = ticket->ID(); 
        if (ticket_id == TicketID::Transfer)
        {
            const auto pTransferTicket = dynamic_cast<const CTransferTicket*>(ticket.get());
            if (!pTransferTicket)
            {
                tv.errorMsg = strprintf("Invalid %s ticket", CTransferTicket::GetTicketDescription());
                break;
            }
            // transfer price in patoshis
            transferPrice = pTransferTicket->getPricePSL() * COIN;

            const auto pTicket = pTransferTicket->FindItemRegTicket();
            if (!pTicket)
            {
                tv.errorMsg = strprintf(
                    "Registration ticket not found for the %s ticket with txid=%s", 
                    CTransferTicket::GetTicketDescription(), ticket->GetTxId());
                break;
            }

            switch (pTicket->ID())
            {
                case TicketID::NFT:
                {
                    const auto pNFTRegTicket = dynamic_cast<const CNFTRegTicket*>(pTicket.get());
                    if (!pNFTRegTicket)
                    {
                        tv.errorMsg = strprintf(
                            "Invalid %s ticket referred by the %s ticket with txid=%s",
                            CNFTRegTicket::GetTicketDescription(), CTransferTicket::GetTicketDescription(), ticket->GetTxId());
                        break;
                    }
                    if (pNFTRegTicket->getRoyalty() > 0)
                        royaltyFee = static_cast<CAmount>(transferPrice * pNFTRegTicket->getRoyalty());
                    if (pNFTRegTicket->hasGreenFee())
                        greenFee = transferPrice * CNFTRegTicket::GreenPercent(nHeight) / 100;
                } break;

                case TicketID::ActionReg:
                {
                    const auto pActionRegTicket = dynamic_cast<const CActionRegTicket *>(pTicket.get());
                    if (!pActionRegTicket)
                    {
                        tv.errorMsg = strprintf(
                            "Invalid %s ticket referred by the %s ticket with txid=%s",
                            CActionRegTicket::GetTicketDescription(), CTransferTicket::GetTicketDescription(), ticket->GetTxId());
                        break;
                    }
                } break;

                default:
                    break;
            }

            transferPrice -= (royaltyFee.value_or(0) + greenFee.value_or(0));
        }

        // Validate various fees
        const auto num = tx.vout.size();
        CAmount ticketFee = 0;

        // set of tickets where the last output is a change
        static unordered_set<TicketID> TICKETS_LAST_OUTPUT_AS_CHANGE = 
            {
                TicketID::PastelID,
                TicketID::NFT,
                TicketID::Offer,
                TicketID::Accept,
                TicketID::Royalty,
                TicketID::Username,
                TicketID::EthereumAddress,
                TicketID::ActionReg,
                TicketID::CollectionReg
            };
        ticket_validation_t tv1;
        tv1.state = TICKET_VALIDATION_STATE::VALID;
        for (size_t i = 0; i < num; i++)
        {
            // in these tickets last output is a change
            if (TICKETS_LAST_OUTPUT_AS_CHANGE.count(ticket_id) && i == num - 1)
                break;
            const auto& txOut = tx.vout[i];
            // in these tickets last 4 outputs are: change and payments to 3 MNs
            if (is_enum_any_of(ticket_id, TicketID::Activate, TicketID::ActionActivate, TicketID::CollectionAct))
            {
                if (i == num - 4)
                    continue;
                const auto mnFeeTicket = dynamic_cast<const CPastelTicketMNFee*>(ticket.get());
                if (!mnFeeTicket)
                {
                    tv1.errorMsg = "Internal error - invalid activation ticket";
                    tv1.state = TICKET_VALIDATION_STATE::INVALID;
                    break;
                }

                if (i == num - 3)
                {
                    // principal MN fee in patoshis
                    const CAmount mnFee = mnFeeTicket->getPrincipalMNFee();
                    if (mnFee != txOut.nValue)
                    {
                        tv1.errorMsg = strprintf(
                            "Wrong principal MN fee: expected - %" PRId64 ", real - %" PRId64, 
                            mnFee, txOut.nValue);
                        tv1.state = TICKET_VALIDATION_STATE::INVALID;
                        break;
                    }
                    continue;
                }
                if (i >= num - 2)
                {
                    // other MN fee in patoshis
                    const CAmount mnFee = mnFeeTicket->getOtherMNFee();
                    if (mnFee != txOut.nValue)
                    {
                        tv1.errorMsg = strprintf(
                            "Wrong MN%zu fee: expected - %" PRId64 ", real - %" PRId64, 
                            i - num - 4, mnFee, txOut.nValue);
                        tv1.state = TICKET_VALIDATION_STATE::INVALID;
                        break;
                    }
                    continue;
                }
            }
            // in these tickets last 2 outputs are: change and payment to the current owner
            if (ticket_id == TicketID::Transfer)
            {
                size_t nFeesDefined = 0;
                if (royaltyFee.has_value())
                    ++nFeesDefined;
                if (greenFee.has_value())
                    ++nFeesDefined;
                if (i == (nFeesDefined == 2 ? num - 4 : (nFeesDefined == 1 ? num - 3 : num - 2)))
                    continue;
                if (i == (nFeesDefined == 2 ? num - 3 : (nFeesDefined == 1 ? num - 2 : num - 1)))
                {
                    if (transferPrice != txOut.nValue)
                    {
                        tv1.errorMsg = strprintf(
                            "Wrong payment to the seller: expected - %" PRId64 ", real - %" PRId64,
                            transferPrice, txOut.nValue);
                        tv1.state = TICKET_VALIDATION_STATE::INVALID;
                        break;
                    }
                    continue;
                }
                if (royaltyFee.has_value() && i == (greenFee.has_value() ? num - 2 : num - 1))
                {
                    if (royaltyFee != txOut.nValue)
                    {
                        tv1.errorMsg = strprintf(
                            "Wrong payment to the royalty owner: expected - %" PRId64 ", real - %" PRId64, 
                            royaltyFee.value(), txOut.nValue);
                        tv1.state = TICKET_VALIDATION_STATE::INVALID;
                        break;
                    }
                    continue;
                }
                if (greenFee.has_value() && i == num - 1)
                {
                    if (greenFee != txOut.nValue)
                    {
                        tv1.errorMsg = strprintf("Wrong payment to the green NFT: expected - %" PRId64 ", real - %" PRId64, 
                            greenFee.value(), txOut.nValue);
                        tv1.state = TICKET_VALIDATION_STATE::INVALID;
                        break;
                    }
                    continue;
                }
            }

            ticketFee += txOut.nValue;
        } // for tx.vouts
        if (tv1.IsNotValid())
        {
            tv = move(tv1);
            break;
        }

        if (expectedTicketFee != ticketFee)
        {
            tv.errorMsg = strprintf(
                "Wrong ticket fee: expected - %" PRId64 ", real - %" PRId64, 
                expectedTicketFee, ticketFee);
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

 /**
 * Called for contextual validation of ticket transactions in the blocks (new or not).
 *                   
 *  CPU PastelMiner -> TestBlockValidity --------------------------------+
 *      |                                                                 |
 *      +-- ProcessBlockFound --+                                         |
 *                              |                                         |
 *    ProcessMessages (block) --+--> ProcessNewBlock --+--> AcceptBlock --+-> ContextualCheckBlock --+--> ContextualCheckTransaction -> ValidateIfTicketTransaction
 *                              |                                                                    |
 *     LoadExternalBlockFile ---+   SendTicket -> StoreP2FMSTransaction --+-> AcceptToMemoryPool ----+
 *                              |                                         |
 *               submitblock ---+                  ProcessMessages (tx) --+
 *                              |
 *                   generate --+
 *
 * ContextualCheckBlock calls for all transactions in a block ContextualCheckTransaction.
 * AcceptToMemoryPool calls both CheckTransaction and ContextualCheckTransaction to validation transaction.
 * TestBlockValidity called from miner only.
 * "generate" RPC API is used only in regtest network.
 * 
 * \param state - validation state
 * \param nHeight - current block height
 * \param tx - transaction to validate
 * \return ticket validation status (TICKET_VALIDATION_STATE enum)
 *    NOT_TICKET     - if transaction being validated is not a ticket transaction
 *    VALID          - ticket is valid
 *    MISSING_INPUTS - ticket is missing some dependent data (for example, nft-act ticket can't find nft-reg ticket)
 *    INVALID        - ticket validation failed, error message is returned in errorMsg
 */
ticket_validation_t CPastelTicketProcessor::ValidateIfTicketTransaction(CValidationState &state, const uint32_t nHeight, const CTransaction& tx)
{
    ticket_validation_t tv;
    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;

    do
    {
        if (!preParseTicket(tx, data_stream, ticket_id, tv.errorMsg, false))
        {
            // this is not a ticket
            tv.state = TICKET_VALIDATION_STATE::NOT_TICKET;
            break;
        }

        // this is a ticket and it needs to be validated
        bool bOk = false;
        unique_ptr<CPastelTicket> ticket;
        try
        {
            string ticketBlockTxIdStr = tx.GetHash().GetHex();
            string sTxOrigin;
            if (state.getTxOrigin() != TxOrigin::UNKNOWN)
                sTxOrigin = strprintf(", tx origin: %s", GetTxOriginName(state.getTxOrigin()));
            LogFnPrintf("Processing ticket ['%s', txid=%s, nHeight=%u, size=%zu%s]", 
               GetTicketDescription(ticket_id), ticketBlockTxIdStr, nHeight, data_stream.size(), sTxOrigin);

            // create ticket by id
            ticket = CreateTicket(ticket_id);
            if (!ticket)
            {
                tv.errorMsg = strprintf("unknown ticket type %hhu", to_integral_type<TicketID>(ticket_id));
                break;
            }
            // deserialize ticket data
            data_stream >> *ticket;
            ticket->SetTxId(move(ticketBlockTxIdStr));
            ticket->SetBlock(nHeight);
            // ticket validation
            tv = ticket->IsValid(state.getTxOrigin(), 0);
            if (tv.IsNotValid())
                break;
            ticket->SetSerializedSize(data_stream.GetSavedDecompressedSize());
            if (data_stream.IsCompressed())
                ticket->SetCompressedSize(data_stream.GetSavedCompressedSize());
        }
        catch (const exception& ex)
        {
            tv.errorMsg = strprintf("Failed to parse and unpack ticket - %s", ex.what());
            break;
        }
        catch (...)
        {
            tv.errorMsg = "Failed to parse and unpack ticket - Unknown exception";
            break;
        }
        tv = ValidateTicketFees(nHeight, tx, move(ticket));
        if (tv.IsNotValid())
        {
            LogFnPrintf("Invalid ticket ['%s', txid=%s, nHeight=%u]. ERROR: %s",
                      GetTicketDescription(ticket_id), tx.GetHash().GetHex(), nHeight, tv.errorMsg);
            break;
        }

        tv.setValid();
    } while (false);
    return tv;
}

bool CPastelTicketProcessor::ParseTicketAndUpdateDB(CMutableTransaction& tx, const unsigned int nBlockHeight)
{
    string error_ret;
    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    TicketID ticket_id;

    if (!preParseTicket(tx, data_stream, ticket_id, error_ret))
        return false;

    try
    {
        string txid = tx.GetHash().GetHex();

        LogFnPrintf("Processing ticket ['%s', txid=%s, nBlockHeight=%u]",
            GetTicketDescription(ticket_id), txid, nBlockHeight);

        auto ticket = CreateTicket(ticket_id);
        if (!ticket)
            error_ret = strprintf("unknown ticket type %hhu", to_integral_type<TicketID>(ticket_id));
        else
        {
            data_stream >> *ticket;
            ticket->SetSerializedSize(data_stream.GetSavedDecompressedSize());
            if (data_stream.IsCompressed())
                ticket->SetCompressedSize(data_stream.GetSavedCompressedSize());           
            return UpdateDB(*ticket, txid, nBlockHeight);
        }
    }
    catch (const exception& ex)
    {
        error_ret = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error_ret = "Failed to parse and unpack ticket - Unknown exception";
    }

    LogFnPrintf("Invalid ticket ['%s', txid=%s, nBlockHeight=%u]. ERROR: %s", 
        GetTicketDescription(ticket_id), tx.GetHash().GetHex(), nBlockHeight, error_ret);

    return false;
}

string CPastelTicketProcessor::GetTicketJSON(const uint256& txid, const bool bDecodeProperties)
{
    auto ticket = GetTicket(txid);
    if (ticket)
        return ticket->ToJSON(bDecodeProperties);
    return "";
}

bool CPastelTicketProcessor::SerializeTicketToStream(const uint256& txid, string &error, ticket_parse_data_t &data, const bool bUncompressData)
{
    error.erase();
    // get ticket transaction by txid, also may return ticket height
    if (!GetTransaction(txid, data.tx, Params().GetConsensus(), data.hashBlock, true, &data.nTicketHeight))
    {
        error = "No information available about transaction";
        return false;
    }

    data.mtx = make_unique<CMutableTransaction>(data.tx);

    // parse ticket transaction into data_stream
    if (!preParseTicket(*data.mtx, data.data_stream, data.ticket_id, error, true, bUncompressData))
    {
        error = strprintf("Failed to parse P2FMS transaction from data provided. %s", error);
        return false;
    }
    return true;
}

/**
 * Get Pastel ticket by transaction id (txid).
 * May throw runtime_error exception in case:
 *  - transaction not found by txid
 *  - failed to parse ticket transaction
 *  - not supported ticket (new nodes introduced new ticket types)
 * 
 * \param txid - ticket transaction id
 * \return created unique_ptr for Pastel ticket by txid
 */
unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const uint256 &txid)
{
    string error;
    ticket_parse_data_t data;
    if (!SerializeTicketToStream(txid, error, data, true))
    {
        LogFnPrintf("Failed to get ticket by txid=%s. ERROR: %s. Will throw exception", txid.GetHex(), error);
        throw runtime_error(error);
    }

    unique_ptr<CPastelTicket> ticket;
    string sTxId = data.tx.GetHash().GetHex();
    try
    {
        if (!data.hashBlock.IsNull()) // this will filter out tickets from mempool (not in block yet) NOTE: transactions in mempool DOES have non-zero block height!
        {
            if (data.nTicketHeight == numeric_limits<uint32_t>::max())
            {
                // if ticket block height is still not defined - lookup it up in mapBlockIndex by hash
                const auto mi = mapBlockIndex.find(data.hashBlock);
                if (mi != mapBlockIndex.cend() && mi->second)
                {
                    const auto pindex = mi->second;
                    if (chainActive.Contains(pindex))
                        data.nTicketHeight = pindex->nHeight;
                }
            }
        } else
            data.nTicketHeight = numeric_limits<uint32_t>::max();

        // create Pastel ticket by id
        ticket = CreateTicket(data.ticket_id);
        if (ticket)
        {
            // deserialize data to ticket object
            data.data_stream >> *ticket;
            ticket->SetTxId(move(sTxId));
            ticket->SetBlock(data.nTicketHeight);
            ticket->SetSerializedSize(data.data_stream.GetSavedDecompressedSize());
            if (data.data_stream.IsCompressed())
                ticket->SetCompressedSize(data.data_stream.GetSavedCompressedSize());
        }
        else
            error = strprintf("unknown ticket_id %hhu", to_integral_type<TicketID>(data.ticket_id));
    }
    catch (const exception& ex)
    {
        error = strprintf("Failed to parse and unpack ticket - %s", ex.what());
    }
    catch (...)
    {
        error = "Failed to parse and unpack ticket - Unknown exception";
    }

    if (!ticket)
        LogFnPrintf("Invalid ticket ['%s', txid=%s]. ERROR: %s", 
            GetTicketDescription(data.ticket_id), data.tx.GetHash().GetHex(), error);

    return ticket;
}

unique_ptr<CPastelTicket> CPastelTicketProcessor::GetTicket(const string& _txid, const TicketID ticketID)
{
    uint256 txid;
    txid.SetHex(_txid);
    auto ticket = GetTicket(txid);
    if (!ticket || ticketID != ticket->ID())
        throw runtime_error(strprintf(
            "The ticket with this txid [%s] is not in the blockchain", _txid));
    return ticket;
}

bool CPastelTicketProcessor::EraseIfTicketTransaction(const uint256& txid, string& error)
{
    ticket_parse_data_t data;
    if (!SerializeTicketToStream(txid, error, data, true))
    {
        error = strprintf("Failed to get ticket by txid=%s. ERROR: %s.", txid.GetHex(), error);
        return false;
    }

    // create Pastel ticket by id
    unique_ptr<CPastelTicket> ticket = CreateTicket(data.ticket_id);
    if (!ticket)
    {
        error = strprintf("Failed to create ticket by id=%hhu.", to_integral_type<TicketID>(data.ticket_id));
        return false;
    }

    // deserialize data to ticket object
    data.data_stream >> *ticket;
    if (!EraseTicketFromDB(*ticket))
    {
        error = strprintf("Failed to erase ticket from DB by txid=%s.", txid.GetHex());
		return false;
    }
    return true;
}

/**
 * Check whether ticket exists in TicketDB (use keyOne as a key).
 *
 * \param ticket - ticket to search (ID and keyOne are used for search)
 * \return true if ticket with the given parameters was found in ticket DB
 */
bool CPastelTicketProcessor::CheckTicketExist(const CPastelTicket& ticket) const
{
    const auto key = ticket.KeyOne();
    const auto itDB = dbs.find(ticket.ID());
    if (itDB == dbs.cend())
        return false;
    return itDB->second->Exists(key);
}

/**
* Check whether ticket exists in TicketDB (use keyTwo as a key).
*
* \param ticket - ticket to search (ID and keyTwo are used for search)
* \return true if ticket with the given parameters was found in ticket DB
*/
bool CPastelTicketProcessor::CheckTicketExistBySecondaryKey(const CPastelTicket& ticket) const
{
    if (ticket.HasKeyTwo())
    {
        string mainKey;
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        const auto itDB = dbs.find(ticket.ID());
        if (itDB != dbs.cend() && itDB->second->Read(sRealKeyTwo, mainKey))
            return itDB->second->Exists(mainKey);
    }
    return false;
}

/**
 * Find ticket in TicketDB by primary key.
 * Is ticket height returned by DB is higher than active chain height - ticket is considered invalid.
 * 
 * \param ticket - ticket object to return
 * \return true if ticket was found and successfully read from DB
 */
bool CPastelTicketProcessor::FindTicket(CPastelTicket& ticket) const
{
    const auto sKey = ticket.KeyOne();
    const auto itDB = dbs.find(ticket.ID());
    if (itDB == dbs.cend())
        return false;
    bool bRet = itDB->second->Read(sKey, ticket);
    if (bRet)
    {
        if (ticket.IsBlockNewerThan(gl_nChainHeight))
        {
            bRet = false;
            ticket.Clear();
        }
    }
    return bRet;
}

/**
 * Delete ticket from TicketDB by primary key.
 *
 * \param ticket - ticket object to return
 * \return true if ticket was found and successfully deleted from DB
 */
bool CPastelTicketProcessor::EraseTicketFromDB(const CPastelTicket& ticket) const
{
    const auto sKey = ticket.KeyOne();
    const auto itDB = dbs.find(ticket.ID());
    if (itDB == dbs.cend())
        return false;
    return itDB->second->Erase(sKey);
}

/**
 * Delete tickets from TicketDB by block index list.
 * 
 * \param vBlockIndex - list of block indices
 * \return number of deleted tickets
 */
size_t CPastelTicketProcessor::EraseTicketsFromDbByList(const block_index_cvector_t& vBlockIndex)
{
    size_t nErasedCount = 0;
    string error;
    const auto &consensusParams = Params().GetConsensus();
    for (const auto& pindex : vBlockIndex)
    {
        if (!pindex || !(pindex->nTx) || !(pindex->nStatus & BLOCK_HAVE_DATA))
            continue;
        
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, consensusParams))
			continue;

        for (const auto& tx : block.vtx)
        {
            if (EraseIfTicketTransaction(tx.GetHash(), error))
                ++nErasedCount;
        }
	}
    return nErasedCount;
}

void CPastelTicketProcessor::RepairTicketDB(const bool bUpdateUI)
{
    AssertLockHeld(cs_main);

    string sMsg = translate("Repairing ticket database...");
    LogFnPrintf(sMsg);
    if (bUpdateUI)
        uiInterface.InitMessage(sMsg);
    auto pindex = chainActive.Tip();
    int nCount = 0;
    int nTotal = pindex->nHeight;
    int nLastPercentage = 0;
    while (pindex && pindex != chainActive.Genesis())
    {
        if (pindex->nStatus & BLOCK_HAVE_DATA)
        {
            UpdatedBlockTip(pindex, false);

            int nCurrentPercentage = (nCount * 100) / nTotal;
            if (bUpdateUI && (nCurrentPercentage != nLastPercentage))
            {
                uiInterface.InitMessage(strprintf("Repairing ticket database %d%% ...", nCurrentPercentage));
                nLastPercentage = nCurrentPercentage;
            }
        }
        pindex = pindex->pprev;
        ++nCount;
    }
    LogFnPrintf("Ticket database has been repaired");
    if (bUpdateUI)
		uiInterface.ShowProgress(translate("Repaired Ticket Database..."), 100);
}

/**
 * Find ticket in TicketDB by secondary key.
 * If ticket height returned by DB is higher than active chain height - ticket is considered invalid.
 * 
 * \param ticket - ticket object to return
 * \return true if ticket has secondary key and the ticket was found
 */
bool CPastelTicketProcessor::FindTicketBySecondaryKey(CPastelTicket& ticket) const
{
    // check if this ticket type supports secondary key
    if (ticket.HasKeyTwo())
    {
        string sMainKey;
        // get real secondary key: @2@ + key
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        // find in DB record: <real_secondary_key> -> <primary_key>
        const auto itDB = dbs.find(ticket.ID());
        if (itDB != dbs.cend() && itDB->second->Read(sRealKeyTwo, sMainKey))
        {
            if (itDB->second->Read(sMainKey, ticket))
            {
                if (!ticket.IsBlockNewerThan(gl_nChainHeight))
    				return true;
				ticket.Clear();
            }
        }
    }
    return false;
}

/**
 * Find all tickets in TicketDB by mvKey.
 * If ticket height returned by DB is higher than active chain height - ticket is considered invalid.
 * 
 * \param mvKey - key to search for. Tickets support up to 3 mvkeys.
 * \return vector of found tickets with the given mvKey
 */
template <class _TicketType>
vector<_TicketType> CPastelTicketProcessor::FindTicketsByMVKey(const string& mvKey)
{
    vector<_TicketType> tickets;
    v_strings vMainKeys;
    // get real MV key stored in DB: "@M@" + key
    const auto realMVKey = RealMVKey(mvKey);
    // get DB for the given ticket type
    const auto itDB = dbs.find(_TicketType::GetID());
    // find primary keys of the tickets with mvKey
    if (itDB != dbs.cend() && itDB->second->Read(realMVKey, vMainKeys))
    {
        const uint32_t nCurrentChainHeight = gl_nChainHeight;
        // read all tickets
        for (const auto& key : vMainKeys)
        {
            _TicketType ticket;
            if (itDB->second->Read(key, ticket) && !ticket.IsBlockNewerThan(nCurrentChainHeight))
                tickets.emplace_back(ticket);
        }
    }
    return tickets;
}

/**
 * Find ticket in TicketDB by secondary key.
 * Returns primary key if ticket was found or empty value.
 * 
 * \param ticket - ticket to get secondary key from
 * \return - primary key of the ticket or empty value
 */
string CPastelTicketProcessor::getValueBySecondaryKey(const CPastelTicket& ticket) const
{
    string sMainKey;
    // check if this ticket type supports secondary key
    if (ticket.HasKeyTwo())
    {
        // get real secondary key: @2@ + key
        const auto sRealKeyTwo = RealKeyTwo(ticket.KeyTwo());
        // find in DB record: <real_secondary_key> -> <primary_key>
        const auto itDB = dbs.find(ticket.ID());
        if (itDB != dbs.cend())
            itDB->second->Read(sRealKeyTwo, sMainKey);
    }
    return sMainKey;
}

template PastelIDRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CPastelIDRegTicket>(const string&);
template NFTRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTRegTicket>(const string&);
template CollectionRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CollectionRegTicket>(const string&);
template CollectionActivateTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CollectionActivateTicket>(const string&);
template NFTActivateTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTActivateTicket>(const string&);
template OfferTickets_t CPastelTicketProcessor::FindTicketsByMVKey<COfferTicket>(const string&);
template AcceptTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CAcceptTicket>(const string&);
template TransferTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CTransferTicket>(const string&);
template NFTRoyaltyTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CNFTRoyaltyTicket>(const string&);
template ChangeUsernameTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CChangeUsernameTicket>(const string&);
template ChangeEthereumAddressTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CChangeEthereumAddressTicket>(const string&);
template ActionRegTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CActionRegTicket>(const string&);
template ActionActivateTickets_t CPastelTicketProcessor::FindTicketsByMVKey<CActionActivateTicket>(const string&);

v_strings CPastelTicketProcessor::GetAllKeys(const TicketID id) const
{
    v_strings vResults;
    const auto itDB = dbs.find(id);
    if (itDB != dbs.cend())
    {
        unique_ptr<CDBIterator> pcursor(itDB->second->NewIterator());
        pcursor->SeekToFirst();
        string sKey;
        while (pcursor->Valid())
        {
            sKey.clear();
            if (pcursor->GetKey(sKey))
                vResults.emplace_back(move(sKey));
            pcursor->Next();
        }
    }
    return vResults;
}

template <TicketID>
struct TicketTypeMapper;

template <> struct TicketTypeMapper<TicketID::PastelID>
{
	using TicketType = CPastelIDRegTicket;
};
template <> struct TicketTypeMapper<TicketID::NFT>
{
	using TicketType = CNFTRegTicket;
};
template <> struct TicketTypeMapper<TicketID::Activate>
{
	using TicketType = CNFTActivateTicket;
};
template <> struct TicketTypeMapper<TicketID::Offer>
{
	using TicketType = COfferTicket;
};
template <> struct TicketTypeMapper<TicketID::Accept>
{
	using TicketType = CAcceptTicket;
};
template <> struct TicketTypeMapper<TicketID::Transfer>
{
	using TicketType = CTransferTicket;
};
template <> struct TicketTypeMapper<TicketID::Down>
{
	using TicketType = CTakeDownTicket;
};
template <> struct TicketTypeMapper<TicketID::Royalty>
{
	using TicketType = CNFTRoyaltyTicket;
};
template <> struct TicketTypeMapper<TicketID::Username>
{
	using TicketType = CChangeUsernameTicket;
};
template <> struct TicketTypeMapper<TicketID::EthereumAddress>
{
	using TicketType = CChangeEthereumAddressTicket;
};
template <> struct TicketTypeMapper<TicketID::ActionReg>
{
	using TicketType = CActionRegTicket;
};
template <> struct TicketTypeMapper<TicketID::ActionActivate>
{
	using TicketType = CActionActivateTicket;
};
template <> struct TicketTypeMapper<TicketID::CollectionReg>
{
	using TicketType = CollectionRegTicket;
};
template <> struct TicketTypeMapper<TicketID::CollectionAct>
{
	using TicketType = CollectionActivateTicket;
};

template <TicketID ID>
bool ReadTicketFromDB(unique_ptr<CDBIterator>& pcursor, string& sKey, unique_ptr<CPastelTicket> &ticket)
{
    sKey.clear();
    typename TicketTypeMapper<ID>::TicketType dbTicket;
    if (pcursor->GetKey(sKey))
    {
        if (str_starts_with(sKey, TICKET_KEYTWO_PREFIX) || str_starts_with(sKey, TICKET_MVKEY_PREFIX))
        {
            sKey.clear();
            return false;
        }
        if (pcursor->GetValue(dbTicket))
        {
            ticket = make_unique<typename TicketTypeMapper<ID>::TicketType>(move(dbTicket));
            return true;
        }
	}
    return false;
}

/**
* Process all tickets from the database of the given type.
* 
* \param id - ticket type
* \param ticketFunctor - functor to apply for each ticket, should accept key and ticket parameters
* \return true if database for the given ticket type was found, false otherwise
*/
bool CPastelTicketProcessor::ProcessAllTickets(TicketID id, process_ticket_data_func_t ticketFunctor) const
{
    const auto itDB = dbs.find(id);
    if (itDB == dbs.cend())
        return false;

	unique_ptr<CDBIterator> pcursor(itDB->second->NewIterator());
	pcursor->SeekToFirst();

	string sKey;
	while (pcursor->Valid())
	{
		sKey.clear();
        unique_ptr<CPastelTicket> ticket;

        switch (id)
        {
            case TicketID::PastelID:
			{
				if (ReadTicketFromDB<TicketID::PastelID>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

            case TicketID::NFT:
            {
                if (ReadTicketFromDB<TicketID::NFT>(pcursor, sKey, ticket))
                    ticketFunctor(move(sKey), ticket);
            } break;

            case TicketID::Activate:
			{
				if (ReadTicketFromDB<TicketID::Activate>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

            case TicketID::Offer:
            {
                if (ReadTicketFromDB<TicketID::Offer>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

            case TicketID::Accept:
			{
				if (ReadTicketFromDB<TicketID::Accept>(pcursor, sKey, ticket))
                    ticketFunctor(move(sKey), ticket);
            } break;

            case TicketID::Transfer:
            {
                if (ReadTicketFromDB<TicketID::Transfer>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

            case TicketID::Down:
			{
				if (ReadTicketFromDB<TicketID::Down>(pcursor, sKey, ticket))
                    ticketFunctor(move(sKey), ticket);
            } break;

            case TicketID::Royalty:
			{
				if (ReadTicketFromDB<TicketID::Royalty>(pcursor, sKey, ticket))
                    ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::Username:
			{
				if (ReadTicketFromDB<TicketID::Username>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::EthereumAddress:
			{
				if (ReadTicketFromDB<TicketID::EthereumAddress>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::ActionReg:
			{
				if (ReadTicketFromDB<TicketID::ActionReg>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::ActionActivate:
			{
				if (ReadTicketFromDB<TicketID::ActionActivate>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::CollectionReg:
			{
				if (ReadTicketFromDB<TicketID::CollectionReg>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;

			case TicketID::CollectionAct:
			{
				if (ReadTicketFromDB<TicketID::CollectionAct>(pcursor, sKey, ticket))
					ticketFunctor(move(sKey), ticket);
			} break;
		}
		pcursor->Next();
	}
    return true;
}

/**
 * Apply functor F for all tickets with type _TicketType.
 * 
 * \param f - functor to apply
 *      if functor returns false - enumerations will be stopped
 */
template <class _TicketType, typename F>
void CPastelTicketProcessor::listTickets(F f, const uint32_t nMinHeight) const
{
    auto vKeys = GetAllKeys(_TicketType::GetID());
    for (auto& key : vKeys)
    {
        if (key.front() == '@')
            continue;
        _TicketType ticket;
        ticket.SetKeyOne(move(key));
        if (!FindTicket(ticket))
            continue;
        if (ticket.GetBlock() < nMinHeight)
            continue;
        if (!f(ticket))
            break;
    }
}

template <class _TicketType>
string CPastelTicketProcessor::ListTickets(const uint32_t nMinHeight) const
{
    json jArray = json::array();
    listTickets<_TicketType>([&](const _TicketType& ticket) -> bool
    {
        jArray.push_back(json::parse(ticket.ToJSON()));
        return true;
    }, nMinHeight);
    return jArray.dump();
}
template string CPastelTicketProcessor::ListTickets<CPastelIDRegTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CNFTRegTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CollectionRegTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CollectionActivateTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CNFTActivateTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<COfferTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CAcceptTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CTransferTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CNFTRoyaltyTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CChangeUsernameTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CChangeEthereumAddressTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CActionRegTicket>(const uint32_t nMinHeight) const;
template string CPastelTicketProcessor::ListTickets<CActionActivateTicket>(const uint32_t nMinHeight) const;

template <class _TicketType, typename F>
string CPastelTicketProcessor::filterTickets(F f, const uint32_t nMinHeight, const bool bCheckConfirmation) const
{
    json jArray = json::array();
    const auto nActiveChainHeight = gl_nChainHeight + 1;
    // list tickets with the specific type (_TicketType) and add to json array if functor f applies
    listTickets<_TicketType>([&](const _TicketType& ticket) -> bool
    {
        //check if the ticket is confirmed
        if (bCheckConfirmation && (nActiveChainHeight - ticket.GetBlock() < masterNodeCtrl.MinTicketConfirmations))
            return true;
        // apply functor to the current ticket
        if (f(ticket, nActiveChainHeight))
            return true;
        jArray.push_back(json::parse(ticket.ToJSON()));
        return true;
    }, nMinHeight);
    return jArray.dump();
}

/**
 * Calculates statistics about specific ticket types serialization.
 * 
 * \param nMinHeight - minimal block height to start searching tickets from
 * \param nTicketsToCheckSize - number of tickets to check serialized size
 * \param nTicketsToUseForEstimation - number of tickets to use for estimation
 * \return - tuple with the following values:
 *    1) number of tickets checked, 1 - if no tickets found
 *    2) min ticket serialized size
 *    3) max ticket serialized size
 *    4) average ticket serialized size
 * If no tickets found - default serialized size is returned (for now only for the
 * NFT Registration ticket).
 */
template <class _TicketType>
tuple<size_t, size_t, size_t, size_t> CPastelTicketProcessor::calculateTicketSizes(
    const uint32_t nMinHeight, 
    const size_t nTicketsToCheckSize, 
    const size_t nTicketsToUseForEstimation) const
{
    set<pair<uint32_t, size_t>> ticketInfo;
    size_t nCount = 0;

    listTickets<_TicketType>([&](const _TicketType& ticket) -> bool
    {
        if (nCount >= nTicketsToCheckSize)
            return false;

        const size_t nTicketStreamSize = ticket.GetSerializedSize();
        ticketInfo.emplace(ticket.GetBlock(), nTicketStreamSize);
        nCount++;
        return true;
    }, nMinHeight);

    if (ticketInfo.empty())
        return { 1, DEFAULT_NFT_TICKET_SIZE, DEFAULT_NFT_TICKET_SIZE, DEFAULT_NFT_TICKET_SIZE };

    v_sizet lastTicketSizes;
    auto it = ticketInfo.rbegin();
    for (size_t i = 0; i < nTicketsToUseForEstimation && it != ticketInfo.rend(); ++i, ++it)
        lastTicketSizes.push_back(it->second);

    auto minMaxIt = minmax_element(lastTicketSizes.begin(), lastTicketSizes.end());
    size_t nMinSizeInBytes = *minMaxIt.first;
    size_t nMaxSizeInBytes = *minMaxIt.second;
    size_t nAvgSizeInBytes = accumulate(lastTicketSizes.cbegin(), lastTicketSizes.cend(), static_cast<size_t>(0)) / lastTicketSizes.size();

    return { nCount, nMinSizeInBytes, nMaxSizeInBytes, nAvgSizeInBytes };
}

template tuple<size_t, size_t, size_t, size_t> CPastelTicketProcessor::calculateTicketSizes<CNFTRegTicket>(
    const uint32_t nMinHeight, const size_t nTicketsToCheckSize,
    const size_t nTicketsToUseForEstimation) const;

/**
 * List Pastel NFT registration tickets using filter.
 *
 * \param filter - ticket filter
 *      1 - mn, include only masternode Pastel IDs
 *      2 - personal, include only personal Pastel IDs
 *      3 - mine, include only tickets for locally stored Pastel IDs in pvPastelIDs 
 * \param pmapIDs - map of locally stored Pastel IDs -> LegRoast public key
 * \return json with filtered tickets
 */
string CPastelTicketProcessor::ListFilterPastelIDTickets(const uint32_t nMinHeight, const short filter, const pastelid_store_t* pmapIDs) const
{
    return filterTickets<CPastelIDRegTicket>(
        [&](const CPastelIDRegTicket& t, const unsigned int chainHeight) -> bool
        {
            const bool bIsPersonal = t.isPersonal();
            if ((filter == 1 && // don't skip mn
                    !bIsPersonal) || 
                (filter == 2 && // don't skip personal
                    bIsPersonal) || 
                (filter == 3 && // don't skip locally stored tickets
                    pmapIDs && pmapIDs->find(t.getPastelID()) != pmapIDs->cend()))
                return false;
            return true;
        }, nMinHeight);
}

// 1 - active;    2 - inactive;     3 - transferred
string CPastelTicketProcessor::ListFilterNFTTickets(const uint32_t nMinHeight, const short filter) const
{
    return filterTickets<CNFTRegTicket>(
        [&](const CNFTRegTicket& t, const unsigned int nChainHeight) -> bool
        {
            if (filter == 3)
            {
                CNFTActivateTicket actTicket;
                // find NFT Activate ticket for this Reg ticket
                if (CNFTActivateTicket::FindTicketInDb(t.GetTxId(), actTicket))
                {
                    // find Transfer tickets listing that NFT Activate ticket txid as NFT ticket
                    const auto vTransferTickets = CTransferTicket::FindAllTicketByMVKey(actTicket.GetTxId());
                    if (!vTransferTickets.empty() && (vTransferTickets.size() >= t.getTotalCopies()))
                        return false; //don't skip transferred|sold
                }
            }

            // check if there is Activation ticket for this Registration ticket
            if (CNFTActivateTicket::CheckTicketExistByNFTTicketID(t.GetTxId()))
            {
                if (filter == 1)
                    return false; //don't skip active
            } else if (filter == 2)
                return false; //don't skip inactive
            return true;
        }, nMinHeight);
}

// 1 - active;    2 - inactive;
string CPastelTicketProcessor::ListFilterCollectionTickets(const uint32_t nMinHeight, const short filter) const
{
    return filterTickets<CollectionRegTicket>(
        [&](const CollectionRegTicket& t, const unsigned int nChainHeight) -> bool
        {
            // check if there is collection-act ticket for this collection-reg ticket
            if (CollectionActivateTicket::CheckTicketExistByCollectionTicketID(t.GetTxId()))
            {
                if (filter == 1)
                    return false; //don't skip active
            } else
            if (filter == 2)
                return false; //don't skip inactive
            return true;
        }, nMinHeight);
}

// 1 - active; 2 - inactive; 3 - transferred
string CPastelTicketProcessor::ListFilterActionTickets(const uint32_t nMinHeight, const short filter) const
{
    return filterTickets<CActionRegTicket>(
        [&](const CActionRegTicket& t, const unsigned int nChainHeight) -> bool
        {
            if (filter == 3)
            {
                CActionActivateTicket actionActTicket;
                // find Action Activation ticket for this Action Registration ticket
                if (CActionActivateTicket::FindTicketInDb(t.GetTxId(), actionActTicket))
                {
                    // find Transfer tickets listing that Action Activate ticket txid
                    const auto vTransferTickets = CTransferTicket::FindAllTicketByMVKey(actionActTicket.GetTxId());
                    if (!vTransferTickets.empty())
                        return false; //don't skip transferred
                }
            }
            // check if there is an Action Activation ticket for this Action Registration ticket
            if (CActionActivateTicket::CheckTicketExistByActionRegTicketID(t.GetTxId()))
            {
                if (filter == 1)
                    return false; //don't skip active
            } else if (filter == 2)
                return false; //don't skip inactive
            return true;
        }, nMinHeight);
}

// 1 - available;      2 - transferred|sold
string CPastelTicketProcessor::ListFilterActTickets(const uint32_t nMinHeight, const short filter) const
{
    return filterTickets<CNFTActivateTicket>(
        [&](const CNFTActivateTicket& t, const unsigned int chainHeight) -> bool
        {
            // find Transfer tickets listing this NFT Activation ticket txid as NFT ticket
            const auto vTransferTickets = CTransferTicket::FindAllTicketByMVKey(t.GetTxId());
            const auto ticket = GetTicket(t.getRegTxId(), TicketID::NFT);
            const auto NFTRegTicket = dynamic_cast<CNFTRegTicket*>(ticket.get());
            if (!NFTRegTicket || vTransferTickets.empty())
                return true;
            if (vTransferTickets.size() < NFTRegTicket->getTotalCopies())
            {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip transferred|sold
            return true;
        }, nMinHeight);
}

// 0 - all, 1 - available; 2 - unavailable; 3 - expired; 4 - transferred|sold
string CPastelTicketProcessor::ListFilterOfferTickets(const uint32_t nMinHeight, const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty()) {
            return ListTickets<COfferTicket>(nMinHeight); // get all
    }
    return filterTickets<COfferTicket>(
        [&](const COfferTicket& t, const unsigned int chainHeight) -> bool
        {
            if (!pastelID.empty() && t.getPastelID() != pastelID)
                return true; // ignore tickets that do not belong to this pastelID
            CAcceptTicket existingAcceptTicket;
            // find accept ticket for this offer ticket, if any
            if (CAcceptTicket::FindTicketInDb(t.GetTxId(), existingAcceptTicket))
            {
                // check if transfer ticket exists for this offer ticket
                if (CTransferTicket::CheckTransferTicketExistByAcceptTicket(existingAcceptTicket.GetTxId()))
                {
                    if (filter == 4)
                        return false; // don't skip transferred|sold
                    else
                        return true;
                }
                // if not - check age
                if (existingAcceptTicket.GetBlock() + masterNodeCtrl.MaxAcceptTicketAge <= chainHeight)
                    return true;
            }
            const OFFER_TICKET_STATE state = t.checkValidState(chainHeight);
            if (filter == 1)
            {
                // skip offer ticket that is not yet active or expired
                if (state == OFFER_TICKET_STATE::NOT_ACTIVE || state == OFFER_TICKET_STATE::EXPIRED)
                    return true;
            } else if (filter == 2) {
                // skip offer ticket that is already active or expired
                if (state == OFFER_TICKET_STATE::ACTIVE || state == OFFER_TICKET_STATE::EXPIRED)
                    return true;
            } else if (filter == 3) {
                // skip offer ticket that is still active
                if (state != OFFER_TICKET_STATE::EXPIRED)
                    return true;
            }
            return false;
        }, checkConfirmation);
}

// 0 - all, 1 - expired;    2 - transferred|sold
string CPastelTicketProcessor::ListFilterAcceptTickets(const uint32_t nMinHeight, const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty()) {
            return ListTickets<CAcceptTicket>(nMinHeight); // get all
    }
    return filterTickets<CAcceptTicket>(
        [&](const CAcceptTicket& t, const unsigned int chainHeight) -> bool
        {
            if (!pastelID.empty() && t.getPastelID() != pastelID)
                return true; // ignore tickets that do not belong to this pastelID
            if (filter == 0)
                return false; // get all belong to this pastel ID
            if (CTransferTicket::CheckTransferTicketExistByAcceptTicket(t.GetTxId())) {
                if (filter == 2)
                    return false; // don't skip transferred
            } else if (filter == 1 && t.GetBlock() + masterNodeCtrl.MaxAcceptTicketAge < chainHeight)
                return false; //don't skip non transferred|sold, and expired
            return true;
        }, checkConfirmation);
}

// 0 - all, 1 - available; 2 - transferred|sold
string CPastelTicketProcessor::ListFilterTransferTickets(const uint32_t nMinHeight, const short filter, const string& pastelID) const
{
    const bool checkConfirmation{filter > 0};
    if (filter == 0 && pastelID.empty())
        return ListTickets<CTransferTicket>(nMinHeight); // get all
    return filterTickets<CTransferTicket>(
        [&](const CTransferTicket& t, const unsigned int chainHeight) -> bool
        {
            // find Transfer tickets listing this Transfer ticket txid as NFT ticket
            const auto vTransferTickets = CTransferTicket::FindAllTicketByMVKey(t.GetTxId());
            if (!pastelID.empty() && t.getPastelID() != pastelID)
                return true; // ignore tickets that do not belong to this pastelID
            if (filter == 0)
                return false; // get all tickets that belong to this pastel ID
            if (vTransferTickets.empty())
            {
                if (filter == 1)
                    return false; //don't skip available
            } else if (filter == 2)
                return false; //don't skip transferred|sold
            return true;
        }, checkConfirmation);
}

bool CPastelTicketProcessor::WalkBackTradingChain(
    const string& sTxId,
    PastelTickets_t& chain,
    bool shortPath,
    string& errRet) noexcept
{
    unique_ptr<CPastelTicket> pastelTicket;
    uint256 txid = uint256S(sTxId);

    // Get ticket pointed by txid. This is either Activation or Transfer tickets (Offer, Accept, Transfer)
    try
    {
        pastelTicket = CPastelTicketProcessor::GetTicket(txid);
    }
    catch ([[maybe_unused]] const runtime_error& ex)
    {
        errRet = strprintf("Ticket [txid=%s] is not in the blockchain.", sTxId);
        return false;
    }
    
    bool bOk = false;
    do
    {
        switch (pastelTicket->ID())
        {
            case TicketID::Transfer:
            {
                const auto pTransferTicket = dynamic_cast<const CTransferTicket *>(pastelTicket.get());
                if (!pTransferTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                        CTransferTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(shortPath ? pTransferTicket->getItemTxId() : pTransferTicket->getAcceptTxId(), 
                    chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::Accept:
            {
                const auto pAcceptTicket = dynamic_cast<const CAcceptTicket *>(pastelTicket.get());
                if (!pAcceptTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                        CAcceptTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(pAcceptTicket->getOfferTxId(), chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::Offer:
            {
                const auto pOfferTicket = dynamic_cast<const COfferTicket *>(pastelTicket.get());
                if (!pOfferTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by this ticket [txid=%s] is invalid",
                        COfferTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(pOfferTicket->getItemTxId(), chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::Activate:
            {
                const auto pNftActTicket = dynamic_cast<const CNFTActivateTicket *>(pastelTicket.get());
                if (!pNftActTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CNFTActivateTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(pNftActTicket->getRegTxId(), chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::NFT:
            {
                const auto pNftRegTicket = dynamic_cast<const CNFTRegTicket *>(pastelTicket.get());
                if (!pNftRegTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CNFTRegTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
            } break;

            case TicketID::CollectionAct:
            {
                const auto pCollectionActTicket = dynamic_cast<const CollectionActivateTicket *>(pastelTicket.get());
                if (!pCollectionActTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CollectionActivateTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(pCollectionActTicket->getRegTxId(), chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::CollectionReg:
            {
                const auto pCollectionRegTicket = dynamic_cast<const CollectionRegTicket *>(pastelTicket.get());
                if (!pCollectionRegTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CollectionRegTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
            } break;

            case TicketID::ActionActivate:
            {
                const auto pActionActTicket = dynamic_cast<const CActionActivateTicket*>(pastelTicket.get());
                if (!pActionActTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CActionActivateTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
                if (!WalkBackTradingChain(pActionActTicket->getRegTxId(), chain, shortPath, errRet))
                    break;
            } break;

            case TicketID::ActionReg:
            {
                const auto pActionRegTicket = dynamic_cast<const CActionRegTicket*>(pastelTicket.get());
                if (!pActionRegTicket)
                {
                    errRet = strprintf("The %s ticket [txid=%s] referred by ticket [txid=%s] is invalid",
                        CActionRegTicket::GetTicketDescription(), pastelTicket->GetTxId(), sTxId);
                    break;
                }
            } break;

            default:
                errRet = strprintf("The ticket [txid=%s] referred by ticket [txid=%s] has unknown type - %s]",
                    pastelTicket->GetTxId(), sTxId, pastelTicket->GetTicketName());
                break;
        }
        chain.emplace_back(move(pastelTicket));
        bOk = true;
    }
    while(false);
    
    return bOk;
}

/**
 * Create ticket transaction in the mempool.
 * 
 * \param ticket - Pastel ticket
 * \return - tuple with:
 *              - transaction hash in hex format (txid)
 *              - primary ticket key
 */
tuple<string, string> CPastelTicketProcessor::SendTicket(const CPastelTicket& ticket, const opt_string_t& sFundingAddress)
{
    string error;

    const ticket_validation_t tv = ticket.IsValid(TxOrigin::NEW_TX, 0);
    if (tv.IsNotValid())
        throw runtime_error(strprintf("Ticket (%s) is invalid. %s", ticket.GetTicketName(), tv.errorMsg));

    vector<CTxOut> vExtraOutputs;
    const CAmount nExtraAmountInPat = ticket.GetExtraOutputs(vExtraOutputs);

    CCompressedDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    auto nTicketID = to_integral_type<TicketID>(ticket.ID());
#ifdef ENABLE_TICKET_COMPRESS
    // compressed flag is saved in highest bit of the ticket id
    nTicketID |= TICKET_COMPRESS_ENABLE_MASK;
#endif
    data_stream << nTicketID;
    data_stream << ticket;
    const size_t nUncompressedSize = data_stream.size();
#ifdef ENABLE_TICKET_COMPRESS
    // compress ticket data
    if (!data_stream.CompressData(error, sizeof(TicketID), 
        [&](CSerializeData::iterator start, CSerializeData::iterator end)
        {
            if (start != end)
                *start = nTicketID & TICKET_COMPRESS_DISABLE_MASK;
        }))
        throw runtime_error(strprintf("Failed to compress ticket (%s) data. %s", ticket.GetTicketName(), error));
    if (data_stream.IsCompressed())
        LogPrint("compress", "Ticket (%hhu) data compressed [%zu]->[%zu]\n", to_integral_type<TicketID>(ticket.ID()), nUncompressedSize, data_stream.size() + 1);
    else
        LogPrint("compress", "Ticket (%hhu) data [%zu bytes] was not compressed due to size or bad compression ratio\n", to_integral_type<TicketID>(ticket.ID()), nUncompressedSize);
#endif

    CMutableTransaction tx;
    CP2FMS_TX_Builder txBuilder(data_stream, ticket.TicketPricePSL(gl_nChainHeight + 1), sFundingAddress);
    txBuilder.setExtraOutputs(move(vExtraOutputs), nExtraAmountInPat);
    if (!txBuilder.build(error, tx))
        throw runtime_error(strprintf("Failed to create P2FMS from data provided. %s", error));

    if (!StoreP2FMSTransaction(tx, error))
        throw runtime_error(strprintf("Failed to send P2FMS transaction. %s", error));
    return make_tuple(tx.GetHash().GetHex(), ticket.KeyOne());
}

#ifdef ENABLE_WALLET
bool CPastelTicketProcessor::CreateP2FMSTransaction(const string& input_string, CMutableTransaction& tx_out, 
    const CAmount nPricePSL, const opt_string_t& sFundingAddress, string& error_ret)
{
    // Convert string data into binary buffer
    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << input_string;
    return CreateP2FMSTransaction(data_stream, tx_out, nPricePSL, sFundingAddress, error_ret);
}

bool CPastelTicketProcessor::CreateP2FMSTransaction(const CDataStream& input_stream, CMutableTransaction& tx_out, 
    const CAmount nPricePSL, const opt_string_t& sFundingAddress, string& error_ret)
{
    CP2FMS_TX_Builder txBuilder(input_stream, nPricePSL, sFundingAddress);
    return txBuilder.build(error_ret, tx_out);
}

/**
 * Check if json value passes fuzzy search filter.
 * 
 * \param jProp - json property value
 * \param sPropFilterValue - filter value
 * \return true if value passes the filter
 */
bool isValuePassFuzzyFilter(const json &jProp, const string &sPropFilterValue) noexcept
{
    bool bPassedFilter = false;
    string sPropValue;
    do
    {
        if (jProp.is_string())
        {
            jProp.get_to(sPropValue);
            // just make case insensitive substring search
            if (!str_ifind(sPropValue, sPropFilterValue))
                break;
        } else if (jProp.is_boolean())
        {
            bool bValue = false;
            // filter value should be convertible to bool
            if (!str_tobool(sPropFilterValue, bValue) || 
                (jProp.get<bool>() != bValue))
                break;
        } else if (jProp.is_number())
        {
            // number should be convertible to string
            try
            {
                sPropValue = to_string(jProp);
                if (sPropValue != sPropFilterValue)
                    break;
            } catch ([[maybe_unused]] const bad_alloc& ex)
            {
                break;
            }
        } else
            break;
        bPassedFilter = true;
    } while (false);
    return bPassedFilter;
}

/**
 * Search for NFT tickets using multiple criterias (defined in 'tickets tools searchthumbids').
 * 
 * \param p - structure with search criterias
 * \param fnMatchFound - functor to apply when NFT registration ticket found that matches all search criterias
 */
void CPastelTicketProcessor::SearchForNFTs(const search_thumbids_t& p, function<size_t(const CPastelTicket*, const json&)> &fnMatchFound) const
{
    v_strings vPastelIDs;
    // Creator Pastel ID can have special 'mine' value
    if (str_icmp(p.sCreatorPastelId, "mine"))
    {
        const auto mapIDs = CPastelID::GetStoredPastelIDs(true);
        if (!mapIDs.empty())
        {
            vPastelIDs.reserve(mapIDs.size());
            for (const auto& [sPastelID, sLegRoastPubKey] : mapIDs)
                vPastelIDs.push_back(sPastelID);
        }
    } else
        vPastelIDs.push_back(p.sCreatorPastelId);
    size_t nResultCount = 0;
    string sAppTicket, sData;
    // process NFT activation tickets by PastelID (mvkey #1)
    for (const auto &sPastelID : vPastelIDs)
    {
        ProcessTicketsByMVKey<CNFTActivateTicket>(sPastelID, [&](const CNFTActivateTicket& actTicket) -> bool
        {
            // check if we exceeded max results
            if (p.nMaxResultCount.has_value() && (nResultCount >= p.nMaxResultCount.value()))
                return false; // stop enumeration

            do
            {
                // filter NFT activation tickets by block height range
                if (p.blockRange.has_value() && !p.blockRange.value().contains(actTicket.GetBlock()))
                    break;

                const auto& regTxId = actTicket.getRegTxId();
                // find NFT registration ticket by txid
                auto pNftTicketPtr = CPastelTicketProcessor::GetTicket(regTxId, TicketID::NFT);
                if (!pNftTicketPtr)
                    break;
                auto pNftTicket = dynamic_cast<const CNFTRegTicket*>(pNftTicketPtr.get());
                if (!pNftTicket)
                    break;
                // filter by number of copies
                if (p.copyCount.has_value() && !p.copyCount.value().contains(pNftTicket->getTotalCopies()))
                    break;
                // NFT ticket data are base64 encoded
                bool bInvalid = false;
                sData = DecodeBase64(pNftTicket->ToStr(), &bInvalid);
                if (bInvalid)
                {
                    LogPrintf("ERROR: failed to decode base64 encoded NFT ticket (%s)", regTxId);
                    break;
                }
                json j;
                try
                {
                    // parse NFT ticket json
                    j = json::parse(sData);
                } catch (const json::exception& ex)
                {
                    LogPrintf("ERROR: failed to parse NFT ticket json (%s). %s", regTxId, SAFE_SZ(ex.what()));
                    break;
                }
                json jApp; // app ticket json
                if (j.contains(NFT_TICKET_APP_OBJ))
                {
                    const json& jAppTicketBase85 = j[NFT_TICKET_APP_OBJ];
                    if (jAppTicketBase85.is_string())
                    {
                        bInvalid = false;
                        sData = DecodeAscii85(jAppTicketBase85, &bInvalid);
                        if (bInvalid)
                        {
                            LogPrintf("ERROR: failed to decode ascii85 encoded NFT app ticket (%s)", regTxId);
                            break;
                        }
                        try
                        {
                            // parse app ticket json
                            jApp = json::parse(sData);
                        } catch (const json::exception& ex)
                        {
                            LogPrintf("ERROR: failed to parse NFT app ticket json (%s). %s", regTxId, SAFE_SZ(ex.what()));
                            break;
                        }
                    }
                }
                uint32_t nScore;
                // filter by rareness score
                if (p.rarenessScore.has_value() && jApp.contains("rareness_score"))
                {
                    jApp["rareness_score"].get_to(nScore);
                    if (!p.rarenessScore.value().contains(nScore))
                        break;
                }
                // filter by nsfw score
                if (p.nsfwScore.has_value() && jApp.contains("nsfw_score"))
                {
                    jApp["nsfw_score"].get_to(nScore);
                    if (!p.nsfwScore.value().contains(nScore))
                        break;
                }
                // nft reg app ticket properties (name: value)
                string sPropName, sPropValue;
                // fuzzy search (key is lowercased in the fuzzySearchMap)
                bool bPassedFilter = true;
                for (const auto &[sSearchProp, sPropFilterValue] : p.fuzzySearchMap)
                {
                    // first let's check if we have any fuzzy search mappings (search keyword->nft ticket property name)
                    const auto itMapping = p.fuzzyMappings.find(sSearchProp);
                    if (itMapping != p.fuzzyMappings.cend())
                        sPropName = itMapping->second; // found property name in the map -> use it
                    else
                        sPropName = sSearchProp; // try to use search property and nft ticket property name as is
                    if (!jApp.contains(sPropName))
                        continue; // just skip unknown properties
                    bPassedFilter = isValuePassFuzzyFilter(jApp[sPropName], sPropFilterValue);
                }
                if (!bPassedFilter)
                    break;
                // add NFT reg ticket info to the json array
                nResultCount = fnMatchFound(pNftTicket, jApp);
            } while (false);
            return true;
        });
    } // for
}
#endif // ENABLE_WALLET

/**
 * Add P2FMS transaction to the memory pool.
 * 
 * \param tx_out - P2FMS transaction
 * \param error_ret - returns an error message if any
 * \return true if transaction was successfully added to the transaction memory pool (txmempool)
 */
bool CPastelTicketProcessor::StoreP2FMSTransaction(const CMutableTransaction& tx_out, string& error_ret)
{
    static constexpr auto TX_NOT_ACCEPTED_MSG = "Transaction was not accepted to memory pool";

    bool bRet = false;
    CValidationState state(TxOrigin::NEW_TX);
    do
    {
        error_ret.clear();
        bool fMissingInputs = false;

        LOCK(cs_main);
        if (AcceptToMemoryPool(Params(), mempool, state, tx_out, false, &fMissingInputs, true))
        {
            RelayTransaction(tx_out);
            bRet = true;
            break;
        }
        int nDoS = 0;
        if (state.IsInvalid(nDoS))
        {
            error_ret = strprintf("%s - %i: %s, DoS: %d",
                TX_NOT_ACCEPTED_MSG, state.GetRejectCode(), state.GetRejectReason(), nDoS);
            break;
        }
        if (fMissingInputs)
        {
            error_ret = strprintf("%s. Missing inputs", TX_NOT_ACCEPTED_MSG);
            break;
        }
        error_ret = strprintf("%s. %s", TX_NOT_ACCEPTED_MSG, state.GetRejectReason());
    } while (false);
    return bRet;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into output_string.
 * 
 * \param tx_in - transaction
 * \param output_string - output string
 * \param error - returns an error message if any
 * \return true if P2FMS was found in the transaction and successfully parsed, validated and copied to the output string
 */
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, string& output_string, string& error)
{
    CSerializeData output_data;
    const bool bOk = ParseP2FMSTransaction(tx_in, output_data, error);
    if (bOk)
        output_string = vector_to_string(output_data);
    return bOk;
}

/**
 * Reads P2FMS (Pay-to-Fake-Multisig) transaction into output_data byte vector.
 * 
 * \param tx_in - transaction
 * \param output_data - output byte vector
 * \param error_ret - returns an error if any
 * \return true if P2FMS was found in the transaction and successfully parsed, validated and copied to the output data
 */
bool CPastelTicketProcessor::ParseP2FMSTransaction(const CMutableTransaction& tx_in, CSerializeData& output_data, string& error_ret)
{
    bool bFoundMS = false;
    // reuse vector to process tx outputs
    vector<v_uint8> vSolutions;

    for (const auto& vout : tx_in.vout)
    {
        txnouttype typeRet; // script type
        vSolutions.clear();

        if (!Solver(vout.scriptPubKey, typeRet, vSolutions) || typeRet != TX_MULTISIG)
            continue;

        bFoundMS = true;
        size_t nReserve = 0;
        for (size_t i = 1; vSolutions.size() - 1 > i; i++)
            nReserve += vSolutions[i].size();
        output_data.reserve(output_data.size() + nReserve);
        for (size_t i = 1; vSolutions.size() - 1 > i; i++)
            output_data.insert(output_data.end(), vSolutions[i].cbegin(), vSolutions[i].cend());
    }

    bool bRet = false;
    do
    {
        error_ret.clear();
        if (!bFoundMS)
        {
            error_ret = "No data multisigs found in transaction";
            break;
        }

        if (output_data.empty())
        {
            error_ret = "No data found in transaction";
            break;
        }

        constexpr auto DATA_POS = uint256::SIZE + sizeof(uint64_t);
        //size_t size = 8 bytes; hash size = 32 bytes
        if (output_data.size() < DATA_POS)
        {
            error_ret = "No correct data found in transaction";
            break;
        }

        // interpret first 8-bytes as an output size
        const auto nOutputLength = *reinterpret_cast<const uint64_t*>(output_data.data());
        v_uint8 input_hash_vec(output_data.cbegin() + sizeof(uint64_t), output_data.cbegin() + DATA_POS); //hash length == 32
        // erase [size(8 bytes) + hash(32-bytes)], leave data only
        output_data.erase(output_data.cbegin(), output_data.cbegin() + DATA_POS);

        if (output_data.size() < nOutputLength)
        {
            error_ret = "No correct data found in transaction - length is not matching";
            break;
        }

        if (output_data.size() > nOutputLength)
            output_data.erase(output_data.cbegin() + nOutputLength, output_data.cend());

        const uint256 input_hash_stored(input_hash_vec);
        const uint256 input_hash_real = Hash(output_data.cbegin(), output_data.cend());

        if (input_hash_stored != input_hash_real)
        {
            error_ret = "No correct data found in transaction - hash is not matching";
            break;
        }
        bRet = true;
    } while (false);

    return bRet;
}

/**
 * Validate NFT ownership.
 * 
 * \param item_txid - item registration txid
 * \param pastelID - Pastel ID of the owner to validate
 * \return optional tuple <TicketID, Item Registration txid, Item Transfer txid>
 */
optional<reg_transfer_txid_t> CPastelTicketProcessor::ValidateOwnership(const string &item_txid, const string &sPastelID)
{
    optional<reg_transfer_txid_t> retVal;
    TicketID ticket_id = TicketID::InvalidID;
    bool bOwns = false;
    try
    {
        // Find ticket by txid
        uint256 txid;
        txid.SetHex(item_txid);

        auto ticket = CPastelTicketProcessor::GetTicket(txid);
        string sTransferTxId;
        if (ticket)
        {
            switch (ticket->ID())
            {
                case TicketID::NFT: {
                    const auto NFTTicket = dynamic_cast<const CNFTRegTicket*>(ticket.get());
                    if (!NFTTicket)
                        break;

                    ticket_id = ticket->ID();
                    // Check if creator and pastelID are equal
                    if ((NFTTicket->IsCreatorPastelId(sPastelID)) && CNFTActivateTicket::CheckTicketExistByNFTTicketID(NFTTicket->GetTxId()))
                    {
                        bOwns = true;
                        break;
                    }
                } break;

                case TicketID::CollectionReg: {
                    const auto CollRegTicket = dynamic_cast<const CollectionRegTicket*>(ticket.get());
                    if (!CollRegTicket)
                        break;

                    ticket_id = ticket->ID();
                    // Check if creator and pastelID are equal
                    if ((CollRegTicket->IsCreatorPastelId(sPastelID)) &&
                        CollectionActivateTicket::CheckTicketExistByCollectionTicketID(CollRegTicket->GetTxId()))
                    {
                        bOwns = true;
                        break;
                    }
                } break;

                case TicketID::ActionReg: {
                    const auto ActionRegTicket = dynamic_cast<const CActionRegTicket*>(ticket.get());
                    if (!ActionRegTicket)
                        break;

                    ticket_id = ticket->ID();
                    // Check if action caller and pastelID are equal
                    if ((ActionRegTicket->IsCallerPastelId(sPastelID)) &&
                        CActionActivateTicket::CheckTicketExistByActionRegTicketID(ActionRegTicket->GetTxId()))
                    {
                        bOwns = true;
                        break;
                    }
                 } break;

                default:
                    break;
            }
        }
    }
    catch(const runtime_error& e)
    {
        LogPrintf("Was not able to process ValidateOwnership request due to: %s\n", e.what()); 
    }
    if (ticket_id == TicketID::InvalidID)
        return nullopt;
    if (bOwns)
        return make_tuple(ticket_id, item_txid, "");

    // If we are here it means it is a nested transfer ticket 
    // List transfer tickets by reg txID and rearrange them by blockheight
    const auto vTransferTickets = CTransferTicket::FindAllTicketByMVKey(item_txid);
    
    // Go through each if not empty and rearrange them by block-height
    if (!vTransferTickets.empty())
    {
        //sort(vTransferTickets.begin(), vTransferTickets.end(), [](CTransferTicket & one, CTransferTicket & two){ return one.GetBlock() < two.GetBlock(); });
        const auto ownersPastelIds_with_TxIds = CTransferTicket::GetPastelIdAndTxIdWithTopHeightPerCopy(vTransferTickets);
        const auto it = ownersPastelIds_with_TxIds.find(sPastelID);
        if (it != ownersPastelIds_with_TxIds.cend())
            retVal = make_tuple(ticket_id, item_txid, it->second);
    }

    return retVal;
}

#ifdef FAKE_TICKET
string CPastelTicketProcessor::CreateFakeTransaction(CPastelTicket& ticket, const CAmount ticketPricePSL,
    const vector<pair<string, CAmount>>& extraPayments, const string& strVerb, bool bSend)
{
    string error;

    if (ticket.ID() == TicketID::PastelID)
    {
        if (strVerb == "1") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->clearPSLIDsignature();
        } else if (strVerb == "2") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->clearMNsignature();
        } else if (strVerb == "3") {
            auto t = (CPastelIDRegTicket*)&ticket;
            t->clearOutPoint();
        }
    } else if (ticket.ID() == TicketID::NFT) {
        if (strVerb == "1") {
            auto t = (CNFTRegTicket*)&ticket;
            t->clear_signature(CTicketSigning::SIGN_MN2);
            t->clear_signature(CTicketSigning::SIGN_MN3);
        }
    } else if (ticket.ID() == TicketID::Activate) {
        if (strVerb == "1") {
            auto t = (CNFTActivateTicket*)&ticket;
            t->clearSignature();
        }
        if (strVerb == "2") {
            auto t = (CNFTActivateTicket*)&ticket;
            t->setCreatorHeight(1);
        }
    } else if (ticket.ID() == TicketID::ActionReg) {
        if (strVerb == "1") {
            auto t = (CActionRegTicket*)&ticket;
            t->clear_signature(CTicketSigning::SIGN_MN2);
            t->clear_signature(CTicketSigning::SIGN_MN3);
        }
    } else if (ticket.ID() == TicketID::ActionActivate) {
        if (strVerb == "1") {
            auto t = (CActionActivateTicket*)&ticket;
            t->clearSignature();
        }
        if (strVerb == "2") {
            auto t = (CActionActivateTicket*)&ticket;
            t->setCalledAtHeight(1);
        }
    } else if (ticket.ID() == TicketID::Offer) {
        if (strVerb == "1") {
            auto t = dynamic_cast<COfferTicket*>(&ticket);
            t->clearSignature();
        }
    } else if (ticket.ID() == TicketID::Accept) {
        ;
    } else if (ticket.ID() == TicketID::Transfer) {
        ;
    }

    KeyIO keyIO(Params());
    vector<CTxOut> extraOutputs;
    CAmount extraAmount = 0;
    if (!extraPayments.empty())
    {
        for (auto& p : extraPayments)
        {
            auto dest = keyIO.DecodeDestination(p.first);
            if (!IsValidDestination(dest))
                return string{};
            extraOutputs.emplace_back(p.second, GetScriptForDestination(dest));
            extraAmount += p.second;
        }
    }

    CDataStream data_stream(SER_NETWORK, DATASTREAM_VERSION);
    data_stream << to_integral_type<TicketID>(ticket.ID());
    data_stream << ticket;

    CMutableTransaction tx;
    CP2FMS_TX_Builder txBuilder(data_stream, ticketPricePSL);
    txBuilder.setExtraOutputs(move(extraOutputs), extraAmount);
    if (!txBuilder.build(error, tx))
        throw runtime_error(strprintf("Failed to create P2FMS from data provided. %s", error));

    if (bSend)
    {
        if (!StoreP2FMSTransaction(tx, error))
            throw runtime_error(strprintf("Failed to send P2FMS transaction. %s", error));
        return tx.GetHash().GetHex();
    }
    return EncodeHexTx(tx);
}
#endif // FAKE_TICKET

tx_mempool_tracker_t CPastelTicketProcessor::GetTxMemPoolTracker()
{
    if (!TicketTxMemPoolTracker)
        TicketTxMemPoolTracker = make_shared<CTicketTxMemPoolTracker>();
    return TicketTxMemPoolTracker;
}

bool CPastelTicketProcessor::FindAndValidateTicketTransaction(const CPastelTicket& ticket,
                                                              const string& new_txid, uint32_t new_height,
                                                              bool bPreReg, string &message)
{
    bool bFound= true;
    const uint256 txid = uint256S(ticket.GetTxId());
    try {
        const auto pTicket = CPastelTicketProcessor::GetTicket(txid);
        if (pTicket)
        {
            if (pTicket->GetBlock() == numeric_limits<uint32_t>::max())
            {
                CTransaction tx;
                if (mempool.lookup(txid, tx))
                    message += " found in mempool.";
                else
                {
                    bFound = false;
                    bool ok = masterNodeCtrl.masternodeTickets.EraseTicketFromDB(ticket);
                    message += strprintf(" found in stale block. %s removed from TicketDB",
                                        ok ? "Successfully" : "Failed to be");
                }
            } else {
                message += " already exists in blockchain.";
                CTransaction new_tx;
                if (mempool.lookup(uint256S(new_txid), new_tx)) {
                    message += strprintf(" Removing new[%s] from mempool.", new_txid);
                    mempool.remove(new_tx, false);
                }
            }
        } else {
            bFound = false;
            bool ok = masterNodeCtrl.masternodeTickets.EraseTicketFromDB(ticket);
            message += strprintf(" found in Ticket DB, but not in blockchain. %s removed from TicketDB",
                                 ok ? "Successfully" : "Failed to be");
        }
    } catch (...) {
        bFound = false;
        bool ok = masterNodeCtrl.masternodeTickets.EraseTicketFromDB(ticket);
        message += strprintf(" found in Ticket DB, but not in blockchain (bad transaction?). %s removed from TicketDB",
                             ok ? "Successfully" : "Failed to be");
    }
    message += strprintf(" [%sfound ticket block=%u, txid=%s]",
                        bPreReg ? "" : strprintf("this ticket block=%u txid=%s; ", new_height, new_txid),
                        ticket.GetBlock(), ticket.GetTxId());
    if (!bFound) {
        LogFnPrintf("WARNING: %s", message);
    }
    return bFound;
}

void CPastelTicketProcessor::RemoveTicketFromMempool(const string& txid)
{
    try {
        CTransaction tx;
        if (mempool.lookup(uint256S(txid), tx)) {
            mempool.remove(tx, false);
        }
    } catch (const std::exception& e) {
        LogFnPrintf("ERROR: txid [%s]. %s", txid, e.what());
    }
}

uint32_t CPastelTicketProcessor::GetTicketBlockHeightInActiveChain(const uint256& txid)
{
    ticket_parse_data_t data;
    // get ticket transaction by txid, also may return ticket height
    if (!GetTransaction(txid, data.tx, Params().GetConsensus(), data.hashBlock, true, &data.nTicketHeight))
    {
        LogFnPrintf("WARNING: No information available about transaction - %s", txid.GetHex());
        return false;
    }
    try {
        if (!data.hashBlock.IsNull()) // this will filter out tickets from mempool (not in block yet) NOTE: transactions in mempool DOES have non-zero block height!
        {
            if (data.nTicketHeight == numeric_limits<uint32_t>::max())
            {
                LOCK(cs_main);

                // if ticket block height is still not defined - lookup it up in mapBlockIndex by hash
                const auto mi = mapBlockIndex.find(data.hashBlock);
                if (mi != mapBlockIndex.cend() && mi->second)
                {
                    const auto pindex = mi->second;
                    if (chainActive.Contains(pindex))
                        return pindex->nHeight;
                }
            }
            return data.nTicketHeight;
        }
    }
    catch (const exception &ex) {
        LogFnPrintf("ERROR: Failed to check if ticket with txid=%s is in active chain. %s", txid.GetHex(), ex.what());
    }
    catch (...) {
        LogFnPrintf("ERROR: Failed to check if ticket with txid=%s is in active chain. . Unknown error", txid.GetHex());
    }
    return numeric_limits<uint32_t>::max();
}