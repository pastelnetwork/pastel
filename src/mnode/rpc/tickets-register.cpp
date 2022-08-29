// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <rpc/rpc_parser.h>
#include <rpc/rpc_consts.h>
#include <rpc/server.h>
#include <mnode/rpc/mnode-rpc-utils.h>
#include <mnode/rpc/tickets-activate.h>
#include <mnode/mnode-controller.h>
#include <mnode/tickets/tickets-all.h>

using namespace std;

UniValue tickets_register_mnid(const UniValue& params)
{
    if (params.size() < 4)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register mnid "pastelid" "passphrase" ["address"]
Register identity of the current Masternode into the blockchain. If successful, method returns "txid"

Arguments:
1. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
2. "passphrase"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
3. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration.

Masternode PastelID Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"address": "",
		"outpoint": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register masternode ID:
)" + HelpExampleCli("tickets register mnid", R"("jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M, "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "mnid", "jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M", "passphrase")")
);

    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only active MN can register its PastelID");

    string pastelID = params[2].get_str();
    SecureString strKeyPass(params[3].get_str());
    opt_string_t sFundingAddress;
    if (params.size() >= 5)
        sFundingAddress = params[4].get_str();

    auto mnidRegData = make_optional<CMNID_RegData>(true);
    const auto regTicket = CPastelIDRegTicket::Create(move(pastelID), move(strKeyPass), sFundingAddress.value_or(""), mnidRegData);
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(regTicket, sFundingAddress));
}

UniValue tickets_register_id(const UniValue& params)
{
    if (params.size() != 5)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register id "pastelid" "passphrase" "address"
Register PastelID identity. If successful, method returns "txid".

Arguments:
1. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
2. "passphrase"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
3. "address"       (string, required) The Pastel blockchain t-address to use for funding the transaction.

Masternode PastelID Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"address": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register PastelID:
)" + HelpExampleCli("tickets register id", R"("jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M, "passphrase", tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg)") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets register id", R"("register", "id", "jXaShWhNtatHVPWRNPsvjoVHUYes2kA7T9EJVL9i9EKPdBNo5aTYp19niWemJb2EwgYYR68jymULPtmHdETf8M", "passphrase", "tPmjPqWdUXD68JBTWYBTtqeCDwdFwwRjikg")"));

    string pastelID = params[2].get_str();
    SecureString strKeyPass(params[3].get_str());
    string sFundingAddress = params[4].get_str();

    const auto pastelIDRegTicket = CPastelIDRegTicket::Create(move(pastelID), move(strKeyPass), sFundingAddress);
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(pastelIDRegTicket, sFundingAddress));
}

/**
* Register NFT ticket.
* 
* \param params - RPC params
* \return rpc result in json format
*/
UniValue tickets_register_nft(const UniValue& params)
{
    if (params.size() < 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register nft "{nft-ticket}" "{signatures}" "pastelid" "passphrase" "label" "fee" ["address"]
Register new NFT ticket. If successful, method returns "txid".

Arguments:
1. "{nft-ticket}"	(string, required) Base64 encoded NFT ticket created by the creator.
    {
        "nft_ticket_version": 2,
        "author":               "<PastelID of the author (creator)>",
        "blocknum":             <block number when the ticket was created>,
        "block_hash":           "<hash of the top block when the ticket was created>",
        "copies":               <number of copies of NFT this ticket is creating, optional in v2>,
        "royalty":              <royalty fee, how much creator should get on all future resales, optional in v2>,
        "green":                <boolean, is there Green NFT payment or not, optional in v2>,
        "nft_collection_txid":  "<transaction id of the NFT collection that NFT belongs to, v2 only, optional, can be empty>",
        "app_ticket":           "<application-specific-data>"
    }
2. "{signatures}"	(string, required) Signatures (base64) and PastelIDs of the principal and verifying masternodes (MN2 and MN3) as JSON:
    {
        "principal": { "principal PastelID": "principal Signature" },
              "mn2": { "mn2 PastelID": "mn2 Signature" },
              "mn3": { "mn3 PastelID": "mn3 Signature" }
    }
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passphrase" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "label"      (string, required) The label which can be used to search for the ticket.
6. "fee"        (int, required) The agreed upon storage fee.
7. "address"    (string, optional) The Pastel blockchain t-address to use for funding the registration.

NFT Registration ticket:
{
    "txid":   <"ticket transaction id">
    "height": <ticket block>,
    "ticket": {
        "type":            "nft-reg",
        "nft_ticket":      {...},
        "version":         <version>
        "signatures": {
            "principal": { "PastelID": <"signature"> },
                  "mn1": { "PastelID": <"signature"> },
                  "mn2": { "PastelID": <"signature"> },
                  "mn3": { "PastelID": <"signature"> }
        },
        "key":             "<search primary key>",
        "label":           "<search label>",
        "creator_height":  <creator height>,
        "total_copies":    <total copies>,
        "royalty":         <royalty fee>,
        "royalty_address": <"address for royalty payment">,
        "green":           boolean,
        "storage_fee":     <agreed upon storage fee>,
    }
}

Register NFT Ticket:
)" + HelpExampleCli("tickets register nft", R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "label", 100)") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "nft", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "label", 100)")
);

    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only an active MN can register an NFT ticket");

    if (fImporting || fReindex)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

    string nft_ticket = params[2].get_str();
    string signatures = params[3].get_str();
    string sPastelID = params[4].get_str();

    SecureString strKeyPass(params[5].get_str());

    string label = params[6].get_str();

    const CAmount nStorageFee = get_long_number(params[7]);

    opt_string_t sFundingAddress;
    if (params.size() >= 9)
        sFundingAddress = params[8].get_str();

    const auto NFTRegTicket = CNFTRegTicket::Create(
        move(nft_ticket), signatures, move(sPastelID), move(strKeyPass),
        move(label), nStorageFee);
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(NFTRegTicket, sFundingAddress));
}

/**
 * Register NFT collection ticket.
 * 
 * \param params - RPC params
 * \return rpc result in json format
 */
UniValue tickets_register_nft_collection(const UniValue& params)
{
    if (params.size() < 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register nft-collection "{nft-collection-ticket}" "{signatures}" "pastelid" "passphrase" "label" "fee" ["address"]
Register new NFT collection ticket. If successful, method returns "txid".

Arguments:
1. "{nft-collection-ticket}"  (string, required) Base64 encoded NFT ticket created by the creator.
    {
        "nft_collection_ticket_version": 1,
        "nft_collection_name": "<NFT collection name>",
        "creator":             "<Pastel ID of the NFT collection creator>",
        "permitted_users": [
           "<Pastel ID of the user 1>",
           "<Pastel ID of the user 2>", 
           ...
        ],
        "blocknum":       <block number when the ticket was created by the creator>,
        "block_hash":     "<base64'ed hash of the NFT collection>",
        "closing_height": <a closing block height after which no new NFTs would be allowed>,
        "nft_max_count":  <max number of NFTs allowed in this collection>,
        "nft_copy_count": <number of copies for NFTs in a collection>,
        "royalty":        <how much creator should get on all future resales>,
        "green":          boolean,
        "app_ticket":     "<application-specific-data>"
    }
2. "signatures"	(string, required) Signatures (base64) and PastelIDs of the principal and verifying masternodes (MN2 and MN3) as JSON:
    {
        "principal": { "principal PastelID": "principal Signature" },
              "mn2": { "mn2 PastelID": "mn2 Signature" },
              "mn3": { "mn3 PastelID": "mn3 Signature" }
    }
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passphrase" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "label"      (string, required) The label which can be used to search for the ticket.
6. "fee"        (int, required) The agreed upon storage fee.
7. "address"    (string, optional) The Pastel blockchain t-address to use for funding the registration.

NFT Collection Registration Ticket:
{
    "txid":   <"ticket transaction id">
    "height": <ticket block>,
    "ticket": {
        "type":            "nft-collection-reg",
        "nft_collection_ticket": {...},
        "version":         <version>
        "signatures": {
            "principal": { "PastelID": <"signature"> },
                  "mn1": { "PastelID": <"signature"> },
                  "mn2": { "PastelID": <"signature"> },
                  "mn3": { "PastelID": <"signature"> }
        },
        "key":             "<search primary key>",
        "label":           "<search label>",
        "creator_height":  <creator height>,
        "closing_height":  <closing height>,
        "nft_max_count":   <nft max count>,
        "royalty":         <royalty fee>,
        "royalty_address": <"address for royalty payment">,
        "green":           boolean,
        "storage_fee":     <agreed upon storage fee>,
    }
}

Register NFT collection ticket:
)" + HelpExampleCli("tickets register nft-collection", R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "label", 100)") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "nft-collection", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "label", 100)")
);

    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only an active MN can register an NFT collection ticket");

    if (fImporting || fReindex)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

    string nft_collection_ticket = params[2].get_str();
    string signatures = params[3].get_str();
    string sPastelID = params[4].get_str();

    SecureString strKeyPass(params[5].get_str());

    string label = params[6].get_str();

    const CAmount nStorageFee = get_long_number(params[7]);

    opt_string_t sFundingAddress;
    if (params.size() >= 9)
        sFundingAddress = params[8].get_str();

    const auto NFTCollectionRegTicket = CNFTCollectionRegTicket::Create(
        move(nft_collection_ticket), signatures, move(sPastelID), move(strKeyPass),
        move(label), nStorageFee);
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(NFTCollectionRegTicket, sFundingAddress));
}

UniValue tickets_register_offer(const UniValue& params)
{
    if (params.size() < 6)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register offer "txid" "price" "PastelID" "passphrase" [valid-after] [valid-before] [copy-number] ["address"] ["intendedFor"]
Register offer ticket. If successful, method returns "txid".

Arguments:
1. "txid"          (string, required) txid of the ticket to offer, this is either:
                       1) NFT Activation ticket, if current owner is original creator
                       2) Transfer ticket, if current owner is the owner of the transferred NFT
2. price           (int, required) Offer price in PSL.
3. "PastelID"      (string, required) The Pastel ID of the current owner. This MUST be the same Pastel ID that was used to sign the ticket referred by the 'txid'.
4. "passphrase"    (string, required) The passphrase to the private key associated with creator's Pastel ID and stored inside node.
5. valid-after     (int, optional) The block height after which this offer ticket will become active (use 0 for upon registration).
6. valid-before    (int, optional) The block height after which this offer ticket is no more valid (use 0 for never).
7. copy-number     (int, optional) If presented - will replace the original not yet accepted Offer ticket with this copy number.
                                   If the original has been already offered - operation will fail.
8. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration (leave empty for default funding).
9. "intendedFor"   (string, optional) The PastelID of the intended recipient of the offer (empty by default).
Offer Ticket:
{
	"ticket": {
		"type": "offer",
		"pastelID": "",
		"txid": "",
		"copy_number": "",
		"asked_price": "",
		"valid_after": "",
		"valid_before": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Offer Ticket:
)" + HelpExampleCli("tickets register offer", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "offer", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
);

    string txid = params[2].get_str();
    const int priceInPSL = get_number(params[3]);

    string pastelID = params[4].get_str();
    SecureString strKeyPass(params[5].get_str());

    int64_t nValidAfter = 0;
    if (params.size() >= 7)
    {
        nValidAfter = get_long_number(params[6]);
        rpc_check_unsigned_param<uint32_t>("<valid-after>", nValidAfter);
    }
    int64_t nValidBefore = 0;
    if (params.size() >= 8)
    {
        nValidBefore = get_long_number(params[7]);
        rpc_check_unsigned_param<uint32_t>("<valid-before>", nValidBefore);
    }
    if (nValidAfter > 0 && nValidBefore > 0 && nValidBefore <= nValidAfter)
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "<valid-before> parameter cannot be less than or equal <valid-after>");
    int64_t nCopyNumber = 0;
    if (params.size() >= 9)
    {
        nCopyNumber = get_long_number(params[8]);
        rpc_check_unsigned_param<uint16_t>("<copy number>", nCopyNumber);
    }
    string sFundingAddress;
    if (params.size() >= 10)
        sFundingAddress = params[9].get_str();
    
    string sIntendedForPastelID;
    if (params.size() >= 11)
        sIntendedForPastelID = params[10].get_str();

    const auto offerTicket = COfferTicket::Create(move(txid), priceInPSL, 
        static_cast<uint32_t>(nValidAfter), static_cast<uint32_t>(nValidBefore), 
        static_cast<uint16_t>(nCopyNumber), 
        move(sIntendedForPastelID), move(pastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(offerTicket, sFundingAddress));
}

UniValue tickets_register_accept(const UniValue& params)
{
    if (params.size() < 6)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register accept "offer_txid" "price" "PastelID" "passphrase" ["address"]
Register Accept ticket. If successful, method returns "txid".

Arguments:
1. "offer_txid"    (string, required) txid of the offer ticket to accept.
2. price           (int, required) accepted price, shall be equal or more then asked price in the offer ticket.
3. "PastelID"      (string, required) The Pastel ID of the new owner.
4. "passphrase"    (string, required) The passphrase to the private key associated with creator's PastelID and stored inside node.
5. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration.

Accept Ticket:
{
	"ticket": {
		"type": "accept",
		"pastelID": "",
		"offer_txid": "",
		"price": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Accept Ticket:
)" + HelpExampleCli("tickets register accept", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 100000 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "accept", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "100000" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
);

    string offerTxID = params[2].get_str();
    int price = get_number(params[3]);

    string sPastelID = params[4].get_str();
    SecureString strKeyPass(params[5].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 7)
        sFundingAddress = params[6].get_str();

    const auto acceptTicket = CAcceptTicket::Create(move(offerTxID), price, move(sPastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(acceptTicket, sFundingAddress));
}

UniValue tickets_register_transfer(const UniValue& params)
{
    if (params.size() < 6)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register transfer "offer_txid" "accept_txid" "PastelID" "passphrase" ["address"]
Register Transfer ticket. And pay price requested in Offer ticket and confirmed in Accept ticket to the address associated with 
the current owner's Pastel ID. If successful, method returns "txid".

Arguments:
1. "offer_txid"    (string, required) txid of the Offer ticket.
2. "accept_txid"   (string, required) txid of the Accept ticket.
3. "PastelID"      (string, required) The Pastel ID of the new owner. This MUST be the same Pastel ID that was used to sign the Accept ticket.
4. "passphrase"    (string, required) The passphrase to the private key associated with creator's Pastel ID and stored inside node. See "pastelid newkey".
5. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration.

Transfer Ticket:
{
	"ticket": {
		"type": "transfer",
		"pastelID": "",
		"offer_txid": "",
		"accept_txid": "",
        "txid": "",
        "price": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Transfer Ticket:
)" + HelpExampleCli("tickets register transfer", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440 jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "transfer", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
);

    string offerTxID = params[2].get_str();
    string acceptTxID = params[3].get_str();

    string sPastelID = params[4].get_str();
    SecureString strKeyPass(params[5].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 7)
        sFundingAddress = params[6].get_str();

    const auto transferTicket = CTransferTicket::Create(move(offerTxID), 
        move(acceptTxID), move(sPastelID), move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(transferTicket, sFundingAddress));
}

UniValue tickets_register_royalty(const UniValue& params)
{
    if (params.size() < 6)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register royalty "nft-txid" "new-pastelid" "old-pastelid" "passphrase" ["address"]
Register new change payee of the NFT royalty ticket. If successful, method returns "txid".

Arguments:
1. "nft-txid"    (string, required) The txid of the NFT register ticket
2. "new-pastelid" (string, required) The pastelID of the new royalty recipient
3. "old-pastelid" (string, required) The pastelID of the current royalty recipient
4. "passphrase"   (string, required) The passphrase to the private key associated with 'old-pastelid' and stored inside node. See "pastelid newkey".
5. "address"      (string, optional) The Pastel blockchain t-address to use for funding the registration.

NFT Royalty ticket:
{
    "txid":   <"ticket transaction id">
    "height": <ticket block>,
    "ticket": {
        "type":         "nft-royalty",
        "version":      <version>
        "pastelID":     <"the pastelID of the current royalty recipient">,
        "new_pastelID": <"the pastelID of the new royalty recipient">,
        "nft_txid":     <"the txid of the NFT register ticket">,
        "signature":    <"">,
    }
}

Royalty Ticket:
)" + HelpExampleCli("tickets register royalty", R"("907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", "hjGBJHujvvlnBKg8h1kFgjnjfTF76HV7w9fD85VdmBbndm3sfmFdKjfFskht59v53b0h65cGVJVdSHVYT47vjj", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "royalty", "907e5e4c6fc4d14660a22afe2bdf6d27a3c8762abf0a89355bb19b7d9e7dc440", "hjGBJHujvvlnBKg8h1kFgjnjfTF76HV7w9fD85VdmBbndm3sfmFdKjfFskht59v53b0h65cGVJVdSHVYT47vjj", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
);

    // should only active MN register royalty ticket?
    //if (!masterNodeCtrl.IsActiveMasterNode())
    //  throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only active MN can register royalty ticket");

    string NFTTxnId = params[2].get_str();
    string newPastelID = params[3].get_str();
    string pastelID = params[4].get_str();

    SecureString strKeyPass(params[5].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 7)
        sFundingAddress = params[6].get_str();

    const auto NFTRoyaltyTicket = CNFTRoyaltyTicket::Create(NFTTxnId, newPastelID, pastelID, move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(NFTRoyaltyTicket, sFundingAddress));
}

UniValue tickets_register_down(const UniValue& params)
{
    if (params.size() < 5) //-V560
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register down "txid" "pastelid" "passphrase" ["address"]
Register take down request ticket. If successful, method returns "txid"

Arguments:
1. "txid"
2. "pastelid"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
3. "passphrase"    (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
4. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration.

Take Down Ticket:
{
	"ticket": {
		"type": "pastelid",
		"pastelID": "",
		"timeStamp": "",
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register PastelID:
)" + HelpExampleCli("tickets register down", R"(jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "down", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "passphrase")")
);

    UniValue result(UniValue::VOBJ);
    return result;
}

UniValue tickets_register_username(const UniValue& params)
{
    if (params.size() < 5)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register username "username" "PastelId" "passphrase" ["address"]
Register Username Change Request ticket. If successful, method returns "txid"

Arguments:
1. "username"      (string, required) The username that will be mapped with above PastelID
2. "PastelId"      (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
3. "passphrase"    (string, required) The passphrase to access the private key associated with PastelID and stored inside node. See "pastelid newkey".
4. "address"       (string, optional) The Pastel blockchain t-address to use for funding the registration.

Username Change Request Ticket:
{
    "ticket": {
		"type": "username",
		"pastelID": "",    // PastelID of the username
		"username": "",    // new valid username
		"fee": "",         // fee to change username
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register Username:
)" + HelpExampleCli("tickets register username", R"(jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "bsmith84" "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "username", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "bsmith84", "passphrase")")
);

    string username = params[2].get_str();
    string pastelID = params[3].get_str();
    SecureString strKeyPass(params[4].get_str());

    opt_string_t sFundingAddress;
    if (params.size() >= 6)
        sFundingAddress = params[5].get_str();

    const auto changeUsernameTicket = CChangeUsernameTicket::Create(pastelID, username, move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(changeUsernameTicket, sFundingAddress));
}

UniValue tickets_register_ethereumaddress(const UniValue& params)
{
    if (params.size() < 5)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register ethereumaddress "ethereumaddress" "PastelId" "passphrase" ["address"]
Register Ethereum Address Change Request ticket. If successful, method returns "txid"

Arguments:
1. "ethereumAddress"  (string, required) The ethereum address that will be mapped with PastelID
2. "PastelId"         (string, required) The PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
3. "passphrase"       (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
4. "address"          (string, optional) The Pastel blockchain t-address to use for funding the registration.

Ethereum Address Change Request Ticket:
{
    "ticket": {
		"type": "ethereumAddress",
		"pastelID": "",         //PastelID of the ethereum address
		"ethereumAddress": "",  //new valid ethereum address
		"fee": "",              // fee to change ethereum address
		"signature": ""
	},
	"height": "",
	"txid": ""
  }

Register Ethereum Address:
)" + HelpExampleCli("tickets register ethereumaddress", R"(jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "0x863c30dd122a21f815e46ec510777fd3e3398c26" "passphrase")") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "ethereumaddress", "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF", "0x863c30dd122a21f815e46ec510777fd3e3398c26", "passphrase")")
);

    string ethereumAddress = params[2].get_str();
    string pastelID = params[3].get_str();
    SecureString strKeyPass(params[4].get_str());
    opt_string_t sFundingAddress;
    if (params.size() >= 6)
        sFundingAddress = params[5].get_str();

    const auto EthereumAddressTicket = CChangeEthereumAddressTicket::Create(pastelID, ethereumAddress, move(strKeyPass));
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(EthereumAddressTicket, sFundingAddress));
}

UniValue tickets_register_action(const UniValue& params)
{
    if (params.size() < 8)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register action "action-ticket" "{signatures}" "pastelid" "passphrase" "label" "fee" ["address"]
Register new Action ticket. If successful, method returns "txid".
Supported action types:
  - sense: dupe detection
  - cascade: storage

Arguments:
1. "action-ticket"	(string, required) Base64 encoded Action ticket created by the MN 1.
    {
        "action_ticket_version": 1,
        "action_type": "<action-type>",
        "caller":      "<caller-PastelID>",
        "blocknum":    <block-number-when-the-ticket-was-created-by-the-mn1>,
        "block_hash":  "<base64'ed-hash-of-the-action>",
        "app_ticket":  "<application-specific-data>",
    }
2. "signatures"	(string, required) Signatures (base64) and PastelIDs of the principal and verifying masternodes (MN2 and MN3) as JSON:
    {
        "principal": { "principal PastelID": "principal Signature" },
              "mn2": { "mn2 PastelID": "mn2 Signature" },
              "mn3": { "mn3 PastelID": "mn3 Signature" }
    }
3. "pastelid"   (string, required) The current, registering masternode (MN1) PastelID. NOTE: PastelID must be generated and stored inside node. See "pastelid newkey".
4. "passphrase" (string, required) The passphrase to the private key associated with PastelID and stored inside node. See "pastelid newkey".
5. "label"      (string, required) The label which can be used to search for the ticket.
6. "fee"        (int, required) The agreed upon storage fee.
7. "address"    (string, optional) The Pastel blockchain t-address to use for funding the registration.

Action Reg Ticket:
{
    "txid":   <"ticket transaction id">
    "height": <ticket block>,
    "ticket": {
        "type":            "action-reg",
        "action_ticket":   {...},
        "action_type":   "<action-type>",
        "version":         <version>
        "signatures": {
            "principal": { "PastelID": <"signature"> },
                  "mn1": { "PastelID": <"signature"> },
                  "mn2": { "PastelID": <"signature"> },
                  "mn3": { "PastelID": <"signature"> }
        },
        "key":         "<search primary key>",
        "label":       "<search label>",
        "called_at":   <block height at which action was called>,
        "storage_fee": <agreed upon storage fee>,
    }
}

Register Action Ticket:
)" + HelpExampleCli("tickets register action", R"(""ticket-blob" "{signatures}" jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF "passphrase", "label", 100)") +
R"(
As json rpc:
)" + HelpExampleRpc("tickets", R"("register", "action", "ticket" "{signatures}" "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF" "passphrase", "label", 100)"));

    if (!masterNodeCtrl.IsActiveMasterNode())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not an active masternode. Only an active MN can register an Action ticket");

    if (fImporting || fReindex)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Initial blocks download. Re-try later");

    string sActionTicket = params[2].get_str();
    string signatures = params[3].get_str();
    string sPastelID = params[4].get_str();

    SecureString strKeyPass(params[5].get_str());

    string label = params[6].get_str();

    const CAmount nStorageFee = get_long_number(params[7]);

    opt_string_t sFundingAddress;
    if (params.size() >= 9)
        sFundingAddress = params[8].get_str();

    const auto ActionRegTicket = CActionRegTicket::Create(
        move(sActionTicket), signatures, move(sPastelID), move(strKeyPass),
        move(label), nStorageFee);
    return GenerateSendTicketResult(CPastelTicketProcessor::SendTicket(ActionRegTicket, sFundingAddress));
}

void tickets_register_help()
{
    throw JSONRPCError(RPC_INVALID_PARAMETER,
R"(tickets register "type" ...
Set of commands to register different types of Pastel tickets.
If successful, returns "txid" of the registered ticket.

Available types:
  mnid       - Register Masternode PastelID. If successful, returns "txid".
               Ticket contains:
                   Masternode Collateral Address
                   Masternode Collateral outpoint (transaction id and index)
                   PastelID
                   Timestamp
                   Signature (above fields signed by PastelID)
  id         - Register personal PastelID. If successful, returns "txid".
               Ticket contains:
                   Provided Address
                   PastelID
                   Timestamp
                   Signature (above fields signed by PastelID)
  nft        - Register new NFT ticket.
  act        - Send activation for the new registered NFT ticket.
               Same as "tickets activate nft...".
  nft-collection - Register new NFT collection ticket.
  nft-collection-act - Activate NFT collection. Same as "activate nft-collection".
  offer      - Register Offer ticket.
  accept     - Register Accept ticket.
  transfer   - Register Transfer ticket. 
  down       - Register take down ticket.
  username   - Register Username Change Request ticket.
  royalty    - Register NFT royalty ticket.
  action     - Register new Action ticket.
  action-act - Send activation for the new registered Action ticket.
               Same as "tickets activate action...".
)");
}

UniValue tickets_register(const UniValue& params)
{
    RPC_CMD_PARSER2(REGISTER, params, mnid, id, nft, act, 
        sell, offer, buy, accept, trade, transfer,
        down, royalty, username, ethereumaddress, action, action__act, nft__collection, nft__collection__act);

    if (!REGISTER.IsCmdSupported())
        tickets_register_help();
    
    UniValue result(UniValue::VOBJ);

    switch (REGISTER.cmd())
    {
        case RPC_CMD_REGISTER::mnid: 
            result = tickets_register_mnid(params);
            break;

        case RPC_CMD_REGISTER::id:
            result = tickets_register_id(params);
            break;

        case RPC_CMD_REGISTER::nft:
            result = tickets_register_nft(params);
            break;

        case RPC_CMD_REGISTER::act:
            result = tickets_activate_nft(params, true);
            break;

        case RPC_CMD_REGISTER::sell:
        case RPC_CMD_REGISTER::offer:
            result = tickets_register_offer(params);
            break;

        case RPC_CMD_REGISTER::buy:
        case RPC_CMD_REGISTER::accept:
            result = tickets_register_accept(params);
            break;

        case RPC_CMD_REGISTER::trade:
        case RPC_CMD_REGISTER::transfer:
            result = tickets_register_transfer(params);
            break;

        case RPC_CMD_REGISTER::royalty:
            result = tickets_register_royalty(params);
            break;

        case RPC_CMD_REGISTER::down:
            result = tickets_register_down(params);
            break;

        case RPC_CMD_REGISTER::username:
            result = tickets_register_username(params);
            break;

        case RPC_CMD_REGISTER::ethereumaddress:
            result = tickets_register_ethereumaddress(params);
            break;

        case RPC_CMD_REGISTER::action:
            result = tickets_register_action(params);
            break;

        case RPC_CMD_REGISTER::action__act:
            result = tickets_activate_action(params, true);
            break;

        case RPC_CMD_REGISTER::nft__collection:
            result = tickets_register_nft_collection(params);
            break;

        case RPC_CMD_REGISTER::nft__collection__act:
            result = tickets_activate_nft_collection(params, true);
            break;

        default:
            break;
    } // switch (REGISTER.cmd())
    return result;
}
