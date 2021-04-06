#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel developers
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

class MasterNodePaymentTest (MasterNodeCommon):
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
        _, _, mn_collateral_addresses = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        start_block = self.nodes[0].getinfo()["blocks"]
        next_block = start_block + 11

        winners_counts = dict()

        for ind in range (120):
            self.nodes[self.mining_node_num].generate(1)
            self.sync_all()
            block_winners_str = self.nodes[0].masternode("winners")[str(next_block)]
            block_winners_pair = block_winners_str.split(':')
            block_winner = block_winners_pair[0]
            winners_counts[block_winner] = winners_counts.get(block_winner, 0) + 1
            time.sleep(1)
            next_block += 1

            # block_winners_map = {v: k for k, v in (x.split(':') for x in block_winners_str.split(', '))}
            # block_winner = max(block_winners_map, key=block_winners_map.get)
            # winners_counts[block_winner] = winners_counts.get(block_winner, 0) + 1
        number_mn_payments = 0
        number_payed_nodes = 0
        for item in mn_collateral_addresses.items():
            number_payed_nodes += 1
            address = item[1]
            mn_payments = winners_counts.get(address, 0)
            print("{0} - {1}".format(address, mn_payments))
            # test that MN was selected as winner at least 80% times (8 out of 10 in this case)
            assert_greater_than(mn_payments, 7)
            number_mn_payments += mn_payments

        assert_equal(number_mn_payments, 120)
        assert_equal(number_payed_nodes, len(mn_collateral_addresses))
        assert_equal(number_payed_nodes, len(winners_counts))

if __name__ == '__main__':
    MasterNodePaymentTest ().main ()
