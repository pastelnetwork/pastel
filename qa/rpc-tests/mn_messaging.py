#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import time
from decimal import getcontext

from test_framework.util import assert_equal, initialize_chain_clean
from mn_common import MasterNodeCommon

getcontext().prec = 16

class MasterNodeMessagingTest(MasterNodeCommon):
    number_of_master_nodes = 4
    number_of_simple_nodes = 3
    total_number_of_nodes = number_of_master_nodes+number_of_simple_nodes

    non_active_mn = number_of_master_nodes-1

    non_mn1 = number_of_master_nodes        # mining node - will have coins #13
    non_mn2 = number_of_master_nodes+1      # hot node - will have collateral for all active MN #14
    non_mn3 = number_of_master_nodes+2      # will not have coins by default #15

    mining_node_num = number_of_master_nodes    # same as non_mn1
    hot_node_num = number_of_master_nodes+1     # same as non_mn2

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)

    def setup_network(self, split=False):
        self.nodes = []
        self.is_network_split = False
        self.setup_masternodes_network(self.number_of_master_nodes, self.number_of_simple_nodes,
            self.mining_node_num, self.hot_node_num, self.number_of_master_nodes)
        

    def run_test(self):
        mns = self.nodes[0].masternodelist("pubkey")
        for out in mns:
            print(mns[out])
            self.nodes[0].masternode("message", "send", mns[out], out)

        self.nodes[self.mining_node_num].generate(1)
        self.sync_all(10, 10)

        time.sleep(20)

        #self.nodes[0].masternode("message", "list")
        msg1 = list(self.nodes[1].masternode("message", "list")[0].values())[0]["Message"]
        msg2 = list(self.nodes[2].masternode("message", "list")[0].values())[0]["Message"]
        msg3 = list(self.nodes[3].masternode("message", "list")[0].values())[0]["Message"]

        print(msg1)
        print(msg2)
        print(msg3)

        outs = list(mns)

        assert_equal(msg1 in outs, True)
        assert_equal(msg2 in outs, True)
        assert_equal(msg3 in outs, True)


if __name__ == '__main__':
    MasterNodeMessagingTest().main()
