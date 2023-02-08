#!/usr/bin/env python3
# Copyright (c) 2018-2023 The Pastel developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or httpa://www.opensource.org/licenses/mit-license.php.
import time
from decimal import getcontext

from test_framework.util import (
    assert_equal,
    assert_greater_than,
    initialize_chain_clean
)
from mn_common import MasterNodeCommon

getcontext().prec = 16

class MasterNodePaymentTest (MasterNodeCommon):

    def __init__(self):
        super().__init__()

        self.number_of_master_nodes = 12
        self.number_of_simple_nodes = 2
        self.number_of_cold_nodes = self.number_of_master_nodes
        self.mining_node_num = self.number_of_master_nodes
        self.hot_node_num = self.number_of_master_nodes + 1


    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network()

    def run_test (self):
        start_block = self.nodes[0].getinfo()["blocks"]
        next_block = start_block + 11

        winners_counts = dict()

        for _ in range (120):
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
        for mn in self.mn_nodes:
            number_payed_nodes += 1
            address = mn.collateral_address
            mn_payments = winners_counts.get(address, 0)
            print(f"{address} - {mn_payments}")
            # test that MN was selected as winner at least 80% times (8 out of 10 in this case)
            assert_greater_than(mn_payments, 7)
            number_mn_payments += mn_payments

        assert_equal(number_mn_payments, 120)
        assert_equal(number_payed_nodes, self.number_of_master_nodes)
        assert_equal(number_payed_nodes, len(winners_counts))

if __name__ == '__main__':
    MasterNodePaymentTest ().main ()
