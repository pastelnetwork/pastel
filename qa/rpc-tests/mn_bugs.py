#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    initialize_datadir, start_nodes, start_node, connect_nodes_bi, \
    pasteld_processes, wait_and_assert_operationid_status, p2p_port, \
    stop_node
from mn_common import MasterNodeCommon
import argparse

import os
import sys
import time

from decimal import Decimal, getcontext
getcontext().prec = 16


TEST_CASE_EXEC_NR = 40300409; # Default: 1st subtask of 38980425

# 12 Master Nodes
private_keys_list = ["91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe", #0 
                     "923JtwGJqK6mwmzVkLiG6mbLkhk1ofKE1addiM8CYpCHFdHDNGo", #1
                     "91wLgtFJxdSRLJGTtbzns5YQYFtyYLwHhqgj19qnrLCa1j5Hp5Z", #2
                     "92XctTrjQbRwEAAMNEwKqbiSAJsBNuiR2B8vhkzDX4ZWQXrckZv", #3
                     "923JCnYet1pNehN6Dy4Ddta1cXnmpSiZSLbtB9sMRM1r85TWym6", #4
                     "93BdbmxmGp6EtxFEX17FNqs2rQfLD5FMPWoN1W58KEQR24p8A6j", #5
                     "92av9uhRBgwv5ugeNDrioyDJ6TADrM6SP7xoEqGMnRPn25nzviq", #6
                     "91oHXFR2NVpUtBiJk37i8oBMChaQRbGjhnzWjN9KQ8LeAW7JBdN", #7
                     "92MwGh67mKTcPPTCMpBA6tPkEE5AK3ydd87VPn8rNxtzCmYf9Yb", #8
                     "92VSXXnFgArfsiQwuzxSAjSRuDkuirE1Vf7KvSX7JE51dExXtrc", #9
                     "91hruvJfyRFjo7JMKnAPqCXAMiJqecSfzn9vKWBck2bKJ9CCRuo", #10
                     "92sYv5JQHzn3UDU6sYe5kWdoSWEc6B98nyY5JN7FnTTreP8UNrq"  #11
                    ]

class MasterNodeGovernanceTest (MasterNodeCommon):
    number_of_master_nodes = len(private_keys_list)
    number_of_simple_nodes = 2
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes
    mining_node_num = number_of_master_nodes
    hot_node_num = number_of_master_nodes+1
    Test_func_dictionary = {
        40104615: "test_40104615",
        40104682: "test_40104682"
    } 

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    def run_test (self):
        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)
        cold_nodes = {k: v for k, v in enumerate(private_keys_list)}
        _, _, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        self.reconnect_nodes(0, self.number_of_master_nodes)
        self.sync_all()

        print("Run freedcamp ID specific test")
        
        if(TEST_CASE_EXEC_NR == 40300409):
            self.test_40300409()

    def test_40300409(self):
        print("Wait a min!")
        time.sleep(60)
        mns = self.nodes[0].masternodelist("extra")
        for out in mns:
            print(mns[out])

        print("Wait a min!")
        time.sleep(60)
        mns = self.nodes[self.hot_node_num].masternodelist("extra")
        for out in mns:
            print(mns[out])

    def test_40104615 (self):
        print("Register first ticket")
        #####################################
        #              NODE 0               #
        ######################################

        # NODE #0: 1. First ticket registration
        address1 = self.nodes[0].getnewaddress()
        res1 = self.nodes[0].governance("ticket", "add", address1, "1000", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket1_id = res1['ticketId']
        print(ticket1_id)
        # ticket1_id now: 1 yes

        #This is a failed TICKET re-REGISTRATION with YES 
        print("NODE #0: This is a failed TICKET re-REGISTRATION with YES")
        res1 = self.nodes[0].governance("ticket", "add", address1, "1000", "test", "yes")
        assert_equal(res1['result'], 'failed')

        #This is a failed TICKET re-REGISTRATION with NO 
        print("NODE #0: This is a failed TICKET re-REGISTRATION with NO")
        res1 = self.nodes[0].governance("ticket", "add", address1, "1000", "test", "no")
        assert_equal(res1['result'], 'failed')

        #This is a failed TICKET VOTE with YES - already voted yes
        print("NODE #0: This is a failed TICKET VOTE with YES - already voted yes")
        res1 = self.nodes[0].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')
        
        #This is a failed TICKET VOTE with NO - one-time change shall be possible only
        print("NODE #0: This is a failed TICKET VOTE with NO - first change to: no -> no change accepted")
        res1 = self.nodes[0].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'failed')
        
        #MINIG
        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)
        #This is a failed TICKET VOTE with NO - one-time change shall be possible
        print("NODE #0: This is a failed TICKET VOTE with NO - already voted no in another block")
        res1 = self.nodes[0].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'failed')

        #This is a failed TICKET VOTE with YES - already voted yes in another block
        print("NODE #0: This is a failed TICKET VOTE with YES - already voted with yes in another block")
        res1 = self.nodes[0].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')

        #If we reach this point it means we did not allow it to vote
        print("Yes! It was not able to vote !")

        time.sleep(3)
        print ("Let's have some votes and ticket registration from another nodes")
        #####################################
        #              NODE 1               #
        #####################################    
        # First vote 'NO' with NODE 1    
        print("NODE #1: This is a passed TICKET VOTE with NO - from Node #1")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'successful')

        # Vote again ->not accepted anymore
        print("NODE #1: This is a failed TICKET VOTE with YES - first change not yet allowed, from Node #1")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')
        
        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)

        #Now new blocks are mined and it should not accept the voting again.
        print("NODE #1: This is a failed TICKET re-VOTE with YES - already there, from Node #1")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')

        #Now new blocks are mined and it should not accept the voting again.
        print("NODE #1: This is a failed TICKET re-VOTE with No - already voted, from Node #1")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'failed')

        
        #Node #1 shall not re-register already existing ticket
        print("NODE #1: This is a failed TICKET re-Registration from Node #1")
        res1 = self.nodes[1].governance("ticket", "add", address1, "1000", "test", "no")
        assert_equal(res1['result'], 'failed')

        time.sleep(3)
        #####################################
        #              NODE 2               #
        #####################################  
         # First vote 'NO' with NODE 2 - passed
        print("NODE #2: This is a passed TICKET VOTE with NO - from Node #2")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'successful')

        # First vote 'YES' with NODE 2 -> failed not allowed anymore 
        print("NODE #2: This is a failed TICKET VOTE with YES - from Node #2")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')
        # ticket1_id now: 1 yes

        # Second change it is not allowed
        print("NODE #2: This is a failed TICKET VOTE with NO - from Node #2")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'failed')

        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)

        # Second change it is not allowed
        print("NODE #2: This is a failed TICKET VOTE with NO - from Node #2 - already voted in another block")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'failed')
        time.sleep(3)
        
        #Only active masternode can vote or add ticket!
        res1 = self.nodes[self.mining_node_num].governance("ticket", "add", address1, str(self.collateral), "test", "no")
        assert_equal(res1['result'], 'failed')

        res1 = self.nodes[self.mining_node_num].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')

        address2 = self.nodes[self.mining_node_num].getnewaddress()
        res1 = self.nodes[self.mining_node_num].governance("ticket", "add", address2, str(self.collateral), "test", "yes")
        assert_equal(res1['errorMessage'], "Only Active Master Node can vote")

        time.sleep(3)

        #Just to have a winning ticket let gather some yes votes
        print("Make a ticket a winning ticket!")
        print("NODE #3: This is a passed TICKET VOTE with YES - from Node #3")
        res1 = self.nodes[3].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        print("NODE #4: This is a passed TICKET VOTE with YES - from Node #4")
        res1 = self.nodes[4].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)

        print("Register second ticket")
        #2. Second ticket
        res1 = self.nodes[2].governance("ticket", "add", address2, "2000", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket2_id = res1['ticketId']

        print ("Minig after 2nd ticket...")
        time.sleep(3)
        self.slow_mine(2, 10, 2, 0.5)

        #Add some votes for the second ticket
        print("NODE #4: This is a passed TICKET VOTE with YES - from Node #4")
        res1 = self.nodes[0].governance("ticket", "vote", ticket2_id, "yes")
        assert_equal(res1['result'], 'successful')
        print("NODE #5: This is a passed TICKET VOTE with No - from Node #5")
        res1 = self.nodes[5].governance("ticket", "vote", ticket2_id, "no")
        assert_equal(res1['result'], 'successful')

        self.nodes[self.mining_node_num].generate(5)

        print("Waiting 60 seconds")
        time.sleep(60)

        #Mining to have a winner ticket :)
        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)
        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)

        #Currently disable to check the status of a possible bug!
        #Mining to have everyone in 'synch'. (qucik and slow)
        self.nodes[self.mining_node_num].generate(150)
        print ("Minig...")
        self.slow_mine(2, 10, 2, 0.5)

        #Simply print all nodes ticket list
        for i in range(0, self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
            print ("\n\nNode : {}. tickets are: \n\n".format(i))
            res1 = self.nodes[i].governance("list", "tickets")

        print("\n\nTesting  tickets votes\n")
        #3. Preliminary test, should be 2 tickets: 1st ticket - 3 votes, 2 yes; 2nd ticket - 1 vote, 1 yes
        for i in range(0, self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
            print ("Node : {}. tickets are: \n".format(i))
            res1 = self.nodes[i].governance("list", "tickets")
            print(res1)
            for j in range(0, 2):
                if res1[j]['id'] == ticket1_id:
                    print(res1[j]['ticket'])
                    assert_equal("Total votes: 6, Yes votes: 4" in res1[j]['ticket'], True) #nnot necessarily correct
                elif res1[j]['id'] == ticket2_id:
                    print(res1[j]['ticket'])
                    assert_equal("Total votes: 1, Yes votes: 1" in res1[j]['ticket'], True)
                else:
                    assert_equal(res1[0]['id'], res1[1]['id'])

    def test_40104682 (self):
        print ("Wait 3min")
        time.sleep(180)
        print("Register first ticket")
        #####################################
        #              NODE 0               #
        ######################################

        # NODE #0: 1. First ticket registration
        address1 = self.nodes[0].getnewaddress()
        res1 = self.nodes[0].governance("ticket", "add", address1, "300", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket1_id = res1['ticketId']
        print(ticket1_id)

        time.sleep(3)
        print ("Let's have some votes and ticket registration from another nodes")
        #####################################
        #              NODE 1               #
        #####################################    
        # First vote 'NO' with NODE 1    
        print("NODE #1: This is a passed TICKET VOTE with NO - from Node #1")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'successful')

        print ("Minig...")
        self.slow_mine(2, 5, 2, 2)
        print ("Wait 10 seconds")
        time.sleep(10)

        
        time.sleep(3)
        #####################################
        #              NODE 2               #
        #####################################  
         # First vote 'NO' with NODE 2 - passed
        print("NODE #2: This is a passed TICKET VOTE with NO - from Node #2")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'successful')

        print ("Minig...")
        self.slow_mine(2, 5, 2, 2)

        time.sleep(3)

        #Just to have a winning ticket let's gather some yes votes
        print("Make ticket1 a winning ticket!")
        print("NODE #3: This is a passed TICKET VOTE with YES - from Node #3")
        res1 = self.nodes[3].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        print("NODE #4: This is a passed TICKET VOTE with YES - from Node #4")
        res1 = self.nodes[4].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        print("NODE #5: This is a passed TICKET VOTE with NO - from Node #5 and vote is accepted but not counted by itself?")
        res1 = self.nodes[5].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        print ("Minig...")
        self.slow_mine(2, 5, 2, 2)
        print("Waiting 20 seconds")
        time.sleep(20)

        print("Register second ticket")
        #2. Second ticket
        address2 = self.nodes[self.mining_node_num].getnewaddress()
        res1 = self.nodes[2].governance("ticket", "add", address2, "110", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket2_id = res1['ticketId']

        print ("Minig after 2nd ticket...")
        #time.sleep(3)
        print ("Minig...")
        self.slow_mine(2, 5, 2, 2)
        print("Waiting 20 seconds")
        time.sleep(20)

        #Add some votes for the second ticket
        print("NODE #4: This is a passed TICKET VOTE with YES - from Node #4")
        res1 = self.nodes[0].governance("ticket", "vote", ticket2_id, "yes")
        assert_equal(res1['result'], 'successful')
        print("NODE #5: This is a passed TICKET VOTE with No - from Node #5")
        res1 = self.nodes[5].governance("ticket", "vote", ticket2_id, "no")
        assert_equal(res1['result'], 'successful')

        print ("Minig...")
        self.slow_mine(2, 5, 2, 2)
        print("Waiting 20 minutes") # 25 minute works
        time.sleep(60)

        #Simply print all nodes ticket list
        for i in range(0, self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
            print ("\n\nNode : {}. tickets are: \n\n".format(i))
            res1 = self.nodes[i].governance("list", "tickets")

        #Mining blocks to have at least one winning ticket:

        #First without waiting too much and printing status
        # Theoretically baseed on testing 516 tickets shall be mined to have a winning ticket
        print ("Minig...")
        self.slow_mine(5, 100, 7, 7)
        print ("Almost last mining finshed wait 60 seconds!")
        #Checkout how to reproduce not in-synch nodes
        #time.sleep(60)

        print ("Mine last blocks before stopVoteHeight!")
        self.nodes[self.mining_node_num].generate(15)
        time.sleep(20)

        print ("Mine 'over' stopVoteHeight and restart and read ticket list")
        self.nodes[self.mining_node_num].generate(3)

        time.sleep(20)

        time.sleep(10)
        print ("Generate some more blocks just to be sure")
        self.nodes[self.mining_node_num].generate(3)

        time.sleep(10)
        actual_block_height=self.nodes[0].getblockcount()
        print ("Generate even more, to be right in front of ticket1 close. Node 0 blockheight: {}".format(actual_block_height))
        self.nodes[self.mining_node_num].generate(3)
        
        print("Last wait time for 25 min")
        time.sleep(1500)
        print ("Hopefully this time we have a borken network")
        
        for i in range(8, 12):
            self.generate_split_ticket(i)

        print("3-4 new ticket created...Wait 2 min and let's see")
        print("T+10 is the nFirstPaymentBloc now. Let's see!")
        
        time.sleep(180)
        print("generate T-2 and wait 1 min")
        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
        self.nodes[self.mining_node_num].generate(1)
        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
        time.sleep(60)
        print("generate T-1 and wait")
        self.nodes[self.mining_node_num].generate(1)
        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
        time.sleep(60)
        print("generate T and wait")
        self.nodes[self.mining_node_num].generate(1)
        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
        time.sleep(60)
        print("generate T+1 and wait")
        self.nodes[self.mining_node_num].generate(1)
        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
        time.sleep(60)
        print("generate T+2 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+2 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+4 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+5 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+6 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+7 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+8 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+9 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)
        print("generate T+10 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+11 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+12 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+13 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+14 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+15 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        print("generate T+16 and wait")
        self.nodes[self.mining_node_num].generate(1)
        time.sleep(60)

        # This could be a reference point to examine log results
        print("Wait for 5 min")
        time.sleep(300)

        print("\n\n\n Check if tickets are raised 1 or 2 chains: \n")

        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
            print ("\n\nNode : {}. tickets are: \n\n".format(i))
            res1 = self.nodes[i].governance("list", "tickets")

        print ("\nSecond long iteration of block generation.\n")
        print ("Minig...")
        print ("\nMine 100 more !")
        for x in range(100):
            self.nodes[self.mining_node_num].generate(1)
            time.sleep(7)
        
        # This will be a second iteration point
        print("\nWait for 10 min")
        time.sleep(600)

        print("\n\n\n Last iteration if tickets are raised 1 or 2 chains: \n")

        for i in range(self.total_number_of_nodes):
            blocckount=self.nodes[i].getblockcount()
            print("Node:{} block_count: {}".format(i,blocckount))
            print ("\n\nNode : {}. tickets are: \n\n".format(i))
            res1 = self.nodes[i].governance("list", "tickets")
        

    def generate_split_ticket(self, node_nr):
        address = self.nodes[node_nr].getnewaddress()
        tcket_amount = node_nr*10 +1
        res1 = self.nodes[0].governance("ticket", "add", address, tcket_amount, "Splitted_by_node_{}".format(node_nr), "yes")
    
        #Implement "mining" actually
    def slow_mine(self, number_of_bursts, num_in_each_burst, wait_between_bursts, wait_inside_burst):
        for x in range(number_of_bursts):
            for y in range(num_in_each_burst):
                self.nodes[self.mining_node_num].generate(1)
                time.sleep(wait_inside_burst)
            time.sleep(wait_between_bursts)

if __name__ == '__main__':
    #parser = argparse.ArgumentParser()
    #parser.add_argument('freedcamp', help='Freedcamp ID to test against')
    #args = parser.parse_args()
    #TEST_CASE_EXEC_NR = args.freedcamp
    MasterNodeGovernanceTest ().main ()
