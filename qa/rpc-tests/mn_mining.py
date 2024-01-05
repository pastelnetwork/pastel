#!/usr/bin/env python3
# Copyright (c) 2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

import time
from decimal import Decimal, getcontext

from test_framework.util import (
    assert_equal,
    assert_true,
    assert_greater_than,
    assert_shows_help,
    assert_raises_rpc,
    initialize_chain_clean,
    start_node,
    stop_node
)
from mn_common import (
    MasterNodeCommon,
)
import test_framework.rpc_consts as rpc

getcontext().prec = 16

class MasterNodeMiningTest (MasterNodeCommon):
    def __init__(self):
        super().__init__()

        self.number_of_master_nodes = 10
        self.number_of_simple_nodes = 3
        self.number_of_cold_nodes = self.number_of_master_nodes
        self.cold_node_num = 0       # master node
        self.mining_node_num = self.number_of_master_nodes     # mining node
        self.hot_node_num = self.number_of_master_nodes + 1    # keeps all collateral for MNs
        self.is_network_split = False


    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.total_number_of_nodes)


    def setup_network(self, split=False):
        self.nodes = []
        self.setup_masternodes_network("masternode")


    def test_sn_block_signing(self):
        print("===== Test SuperNode block v5 signing =====")
        
        current_height = self.nodes[0].getblockcount()
        print(f"current_height: {current_height}")
        processed_mn_nodes = 0
        expected_confirmations = {}
        for mn in self.mn_nodes:
            if mn.index >= self.number_of_cold_nodes:
                continue
            list_of_block_hashes = self.generate_and_sync_inc(1, mn.index, mn.mnid)
            assert_equal(len(list_of_block_hashes), 1, f"Block was not generated on SuperNode #{mn.index}")
            processed_mn_nodes += 1
            for block_hash in list_of_block_hashes:
                block_header = self.nodes[mn.index].getblockheader(block_hash)
                assert_equal(5, block_header['version'], "Block does not have correct version")
                assert_equal(mn.mnid, block_header['pastelid'], "Block does not have mnid")
                assert_equal(1, block_header['confirmations'], "Block does not have correct confirmations")                
                assert_true(block_header['prevMerkleRootSignature'], "Previous block merkle root signature is not defined")
                # increase expected number of confirmations for already generated blocks
                for blk_hash in expected_confirmations:
                    expected_confirmations[blk_hash] += 1
                    blk_header = self.nodes[mn.index].getblockheader(blk_hash)
                    assert_equal(expected_confirmations[blk_hash], blk_header['confirmations'],
                                 f"Block [{blk_hash}] does not have correct confirmations")
                # set expected confirmations for this block
                expected_confirmations[block_hash] = 1

        new_height = self.nodes[0].getblockcount()
        print(f"new_height: {new_height}")
        assert_equal(processed_mn_nodes, new_height - current_height, "Blocks were not generated on all SuperNodes")


    def run_test(self):
        tests = ['sn-block-signing']

        if 'sn-block-signing' in tests:
            self.test_sn_block_signing()


if __name__ == '__main__':
    MasterNodeMiningTest().main()
            