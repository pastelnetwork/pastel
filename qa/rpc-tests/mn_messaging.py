#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.util import assert_equal, assert_greater_than, assert_true, initialize_chain_clean
from mn_common import MasterNodeCommon
import time
from test_framework.authproxy import JSONRPCException

from decimal import getcontext
getcontext().prec = 16

# 4 Master Nodes
private_keys_list = ["91sY9h4AQ62bAhNk1aJ7uJeSnQzSFtz7QmW5imrKmiACm7QJLXe",  # 0
                     "923JtwGJqK6mwmzVkLiG6mbLkhk1ofKE1addiM8CYpCHFdHDNGo",  # 1
                     "91wLgtFJxdSRLJGTtbzns5YQYFtyYLwHhqgj19qnrLCa1j5Hp5Z",  # 2
                     "92XctTrjQbRwEAAMNEwKqbiSAJsBNuiR2B8vhkzDX4ZWQXrckZv"  # 3
                     ]


class MasterNodeMessagingTest(MasterNodeCommon):
    number_of_master_nodes = len(private_keys_list)
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
        self.setup_masternodes_network(private_keys_list, self.number_of_simple_nodes)

    def run_test(self):
        self.mining_enough(self.mining_node_num, self.number_of_master_nodes)
        cold_nodes = {k: v for k, v in enumerate(private_keys_list)}
        _, _, _ = self.start_mn(self.mining_node_num, self.hot_node_num, cold_nodes, self.total_number_of_nodes)

        self.reconnect_nodes(0, self.number_of_master_nodes)
        self.sync_all()

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
