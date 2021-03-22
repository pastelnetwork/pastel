#include <gtest/gtest.h>

#include <string>

#include "main.h"
#include "key.h"
#include "key_io.h"
#include "chain.h"
#include "chainparams.h"
#include "mnode-governance.h"

TEST(mnode_governance, CalculateLastPaymentBlock) {
    SelectParams(CBaseChainParams::Network::TESTNET);

    CMasternodeGovernance gov;

    //6250(*100000) * 5% = 312.5(*100000) => 31250(*100000) will take 100 blocks
    int blocks = gov.CalculateLastPaymentBlock(3125000000, 1+1001);
    EXPECT_EQ(100+1001, blocks);

    //Overflow
    //6250(*100000) * 5% = 312.5(*100000) => 600(*100000) will take 2 blocks (with 25 overflow)
    blocks = gov.CalculateLastPaymentBlock(60000000, 1+1001);
    EXPECT_EQ(2+1001, blocks);

    // gov.AddTicket("tPVQMdSyVnSYgrww5TTXSJeF75aPQ3bAfdm", 1000000, "", true);
}

TEST(mnode_governance, TicketProcessing) {
    SelectParams(CBaseChainParams::Network::TESTNET);

    std::string address("tPVQMdSyVnSYgrww5TTXSJeF75aPQ3bAfdm");
    
    CTxDestination destination = DecodeDestination(address);
    assert(IsValidDestination(destination));
    CScript scriptPubKey = GetScriptForDestination(destination);

    CMasternodeGovernance gov;

    //test of AddTicket logic
    std::string note1("ticket1");
    CGovernanceTicket ticket1(scriptPubKey, 3125000000, note1, 0+1001); //31250 - need 100 blocks to pay
    uint256 ticketId1 = ticket1.GetHash();
    ticket1.ticketId = ticketId1;
    gov.mapTickets[ticketId1] = ticket1;

    std::string note2("ticket2");
    CGovernanceTicket ticket2(scriptPubKey, 60000000, note2, 1+1001); //600 - needs 2 blocks to pay
    uint256 ticketId2 = ticket2.GetHash();
    ticket2.ticketId = ticketId2;
    gov.mapTickets[ticketId2] = ticket2;


    //test of CheckAndRemove logic
    int lastScheduledPaymentBlock = gov.GetLastScheduledPaymentBlock();
    EXPECT_EQ(0, lastScheduledPaymentBlock);

    for (int i=0+1001; i<2+1001; i++) {
        auto it = gov.mapTickets.begin();
        while(it != gov.mapTickets.end()) {
            CGovernanceTicket& ticket = (*it).second;
            
            if (ticket.nStopVoteBlockHeight == i) {
                if (ticket.nLastPaymentBlockHeight == 0) {
                    ticket.nFirstPaymentBlockHeight = lastScheduledPaymentBlock == 0? 0+1+1001: lastScheduledPaymentBlock+1;
                    ticket.nLastPaymentBlockHeight = gov.CalculateLastPaymentBlock(ticket.nAmountToPay, ticket.nFirstPaymentBlockHeight);
                    lastScheduledPaymentBlock = ticket.nLastPaymentBlockHeight;
                    gov.mapPayments[lastScheduledPaymentBlock] = ticket.ticketId;
                }
            }
            ++it;
        }
    }

    EXPECT_EQ(1+1001, gov.mapTickets[ticketId1].nFirstPaymentBlockHeight);
    EXPECT_EQ(100+1001, gov.mapTickets[ticketId1].nLastPaymentBlockHeight);
    EXPECT_EQ(101+1001, gov.mapTickets[ticketId2].nFirstPaymentBlockHeight);
    EXPECT_EQ(102+1001, gov.mapTickets[ticketId2].nLastPaymentBlockHeight);
    EXPECT_EQ(102+1001, gov.GetLastScheduledPaymentBlock());

    bool res;
    CGovernanceTicket ticket;
    
    res = gov.GetCurrentPaymentTicket(1+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket1", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(50+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket1", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(100+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket1", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(101+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket2", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(102+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket2", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(103+1001, ticket);
    EXPECT_EQ(false, res);
    
    res = gov.GetCurrentPaymentTicket(1000000, ticket);
    EXPECT_EQ(false, res);


    //test of Process new ticket message logic
    std::string note3("ticket3");
    CGovernanceTicket ticket3(scriptPubKey, 1250000000, note3, 2); //12500 - needs 40 blocks to pay
    uint256 ticketId3 = ticket3.GetHash();
    ticket3.ticketId = ticketId3;
    ticket3.nFirstPaymentBlockHeight = gov.GetLastScheduledPaymentBlock()+1;
    ticket3.nLastPaymentBlockHeight = gov.CalculateLastPaymentBlock(ticket3.nAmountToPay, ticket3.nFirstPaymentBlockHeight);

    if (!gov.mapTickets.count(ticketId3)) {
        //TODO verify ticket
        gov.mapTickets[ticketId3] = ticket3;
    }

    if (ticket3.nLastPaymentBlockHeight != 0) {
        gov.mapPayments[ticket3.nLastPaymentBlockHeight] = ticket3.ticketId;
    }
    EXPECT_EQ(142+1001, gov.GetLastScheduledPaymentBlock());

    res = gov.GetCurrentPaymentTicket(103+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket3", ticket.strDescription);

    res = gov.GetCurrentPaymentTicket(120+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket3", ticket.strDescription);

    res = gov.GetCurrentPaymentTicket(142+1001, ticket);
    EXPECT_EQ(true, res);
    EXPECT_EQ("ticket3", ticket.strDescription);
    
    res = gov.GetCurrentPaymentTicket(143+1001, ticket);
    EXPECT_EQ(false, res);
}