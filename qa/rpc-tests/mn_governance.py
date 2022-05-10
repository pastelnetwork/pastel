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

import os
import sys
import time

from decimal import Decimal, getcontext
getcontext().prec = 16

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

        print("MN0: register first governance ticket (1000) and vote 'yes'")
        #1. MN0: register first governance ticket
        address1 = self.nodes[0].getnewaddress()
        res1 = self.nodes[0].governance("ticket", "add", address1, "1000", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket1_id = res1['ticketId']
        print(f'MN0: governance ticket successfully registered: {ticket1_id}')

        # MN0: Ticket for this address and amount is already registered
        res1 = self.nodes[0].governance("ticket", "add", address1, "1000", "test", "yes")
        assert_equal(res1['result'], 'failed')
        # MN0: signature already exists: MN has already voted for this ticket
        res1 = self.nodes[0].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')

        time.sleep(3)
        
        print("MN1: vote 'yes' for the first ticket (second 'yes' vote)")
        res1 = self.nodes[1].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'successful')

        # MN1: Ticket for this address and amount is already registered
        res1 = self.nodes[1].governance("ticket", "add", address1, "1000", "test", "no")
        assert_equal(res1['result'], 'failed')

        time.sleep(3)

        # MN2: Ticket for this address and amount is already registered
        res1 = self.nodes[2].governance("ticket", "add", address1, "1000", "test", "no")
        assert_equal(res1['result'], 'failed')

        print("MN2: vote 'no' for the first ticket")
        res1 = self.nodes[2].governance("ticket", "vote", ticket1_id, "no")
        assert_equal(res1['result'], 'successful')

        time.sleep(3)

        # non_mn1: Only Active Master Node can add governance ticket
        res1 = self.nodes[self.mining_node_num].governance("ticket", "add", address1, str(self.collateral), "test", "no")
        assert_equal(res1['result'], 'failed')

        # non_mn1: Only Active Master Node can vote
        res1 = self.nodes[self.mining_node_num].governance("ticket", "vote", ticket1_id, "yes")
        assert_equal(res1['result'], 'failed')

        address2 = self.nodes[self.mining_node_num].getnewaddress()
        res1 = self.nodes[self.mining_node_num].governance("ticket", "add", address2, str(self.collateral), "test", "yes")
        assert_equal(res1['errorMessage'], "Only Active Master Node can add governance ticket")

        time.sleep(3)

        print("MN2: register second governance ticket (2000 PSL) and vote 'yes'")
        #2. Second ticket
        res1 = self.nodes[2].governance("ticket", "add", address2, "2000", "test", "yes")
        assert_equal(res1['result'], 'successful')
        ticket2_id = res1['ticketId']
        print(f'MN2: governance ticket successfully registered: {ticket2_id}')

        self.generate_and_sync_inc(5, self.mining_node_num)

        print("Waiting 120 seconds")
        time.sleep(120)

        print("Test governance tickets votes")
        #3. Preliminary test, should be 2 tickets:
        #     1st ticket - 3 votes, 2 yes (MN0, MN1), 1 no (MN2)
        #     2nd ticket - 1 vote, 1 yes (MN2)
        for i in range(0, self.total_number_of_nodes):
            res1 = self.nodes[i].governance("list", "tickets")
            print(f'node{i}: {res1}')
            for j in range(0, 2):
                if res1[j]['id'] == ticket1_id:
                    print(res1[j]['ticket'])
                    assert_equal("Total votes: 3, Yes votes: 2" in res1[j]['ticket'], True)
                elif res1[j]['id'] == ticket2_id:
                    print(res1[j]['ticket'])
                    assert_equal("Total votes: 1, Yes votes: 1" in res1[j]['ticket'], True)
                else:
                    assert_equal(res1[0]['id'], res1[1]['id'])

        print("Mining 577 blocks")
        #4. mine 577 blocks - ticket 1 should become winner
        # 12 active MN/s - need min 10% voted (2) => need 2 yes votes (51% of 3) 
        for ind in range (57):
            self.generate_and_sync_inc(10, self.mining_node_num)
        self.generate_and_sync_inc(7, self.mining_node_num)

        print(self.nodes[0].governance("list", "tickets"))
        print(self.nodes[0].governance("list", "winners"))

        print("Mining 10 more blocks")
        #4. mine 576 blocks - ticket 1 should become winner
        # 12 active MN/s - needs min 10% voted (2) => need 2 yes votes (51% of 3) 
        self.generate_and_sync_inc(10, self.mining_node_num)
        time.sleep(60)

        print(f"Test winner tickets, should be {ticket1_id}")
        for i in range(0, self.total_number_of_nodes):
            res1 = self.nodes[i].governance("list", "winners")
            print(res1)
            assert_equal(len(res1), 1)
            assert_equal(res1[0]['id'], ticket1_id)  

if __name__ == '__main__':
    MasterNodeGovernanceTest ().main ()
