#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test RPC calls related to blockchain state. Tests correspond to code in
# rpc/blockchain.cpp.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    start_nodes,
    connect_nodes_bi,
)

class BlockchainTest(BitcoinTestFramework):
    """
    Test blockchain-related RPC calls:

        - gettxoutsetinfo

    """

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = False
        self.num_nodes = 2

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        assert_equal(res[u'total_amount'], 150*self._reward+49*self._reward/2)
        assert_equal(res[u'transactions'], 200)
        assert_equal(res[u'height'], 200)
        assert_equal(res[u'txouts'], 200)
        assert_equal(res[u'bytes_serialized'], 11724),
        assert_equal(len(res[u'bestblock']), 64)
        assert_equal(len(res[u'hash_serialized']), 64)


if __name__ == '__main__':
    BlockchainTest().main()
