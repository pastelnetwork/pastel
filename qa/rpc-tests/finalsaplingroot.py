#!/usr/bin/env python3
# Copyright (c) 2018 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .


from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes_bi,
    get_coinbase_address,
    initialize_chain_clean,
    start_nodes,
    wait_and_assert_operationid_status,
)

from decimal import Decimal

SAPLING_TREE_EMPTY_ROOT = "3e49b5f954aa9d3545bc6c37744661eea48d7c34e3000d82b7f0010c30f4c2fb"
NULL_FIELD = "0000000000000000000000000000000000000000000000000000000000000000"

# Verify block header field 'hashFinalSaplingRoot' (returned in rpc as 'finalsaplingroot')
# is updated when Sapling transactions with outputs (commitments) are mined into a block.
class FinalSaplingRootTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.setup_clean_chain = True

    def setup_chain(self):
        print(f"Initializing test directory {self.options.tmpdir}")
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, extra_args=[[
            '-txindex'                # Avoid JSONRPC error: No information available about transaction
            ]] * self.num_nodes)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        connect_nodes_bi(self.nodes,0,3)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        self.nodes[0].generate(200)
        self.sync_all()

        amount1 = Decimal("1.23")
        amount2 = Decimal('12.34')

        # Verfify genesis block contains null field for what is now called the final sapling root field.
        blk = self.nodes[0].getblock("0")
        assert_equal(blk["finalsaplingroot"], NULL_FIELD)

        # Verify all generated blocks contain the empty root of the Sapling tree.
        blockcount = self.nodes[0].getblockcount()
        for height in range(1, blockcount + 1):
            blk = self.nodes[0].getblock(str(height))
            assert_equal(blk["finalsaplingroot"], SAPLING_TREE_EMPTY_ROOT)

        # Node 0 shields some funds
        taddr0 = self.nodes[0].getnewaddress()
        saplingAddr0 = self.nodes[0].z_getnewaddress('sapling')
        recipients = []
        recipients.append({"address": saplingAddr0, "amount": 2*self._reward})
        myopid = self.nodes[0].z_sendmany(taddr0, recipients, 1, 0)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], myopid)

        self.sync_all()
        self.generate_and_sync_inc(1)

        # Verify the final Sapling root has changed
        blk = self.nodes[0].getblock("201")
        root = blk["finalsaplingroot"]
        assert(root is not SAPLING_TREE_EMPTY_ROOT)        
        assert(root is not NULL_FIELD)  

        # Verify there is a Sapling output description (its commitment was added to tree)
        result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(result["vShieldedOutput"]), 1)

        # Mine an empty block and verify the final Sapling root does not change
        self.sync_all()
        self.generate_and_sync_inc(1)
        assert_equal(root, self.nodes[0].getblock("202")["finalsaplingroot"])

        # Mine a block with a transparent tx and verify the final Sapling root does not change
        taddr1 = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(taddr1, amount1)

        self.sync_all()
        self.generate_and_sync_inc(1)

        assert_equal(len(self.nodes[0].getblock("203")["tx"]), 2)
        assert_equal(self.nodes[1].z_getbalance(taddr1), amount1)
        assert_equal(root, self.nodes[0].getblock("203")["finalsaplingroot"])

        self.generate_and_sync_inc(1)

        # Mine a block with a Sapling shielded recipient and verify the final Sapling root changes
        saplingAddr1 = self.nodes[1].z_getnewaddress("sapling")
        recipients = []
        recipients.append({"address": saplingAddr1, "amount": amount2})
        myopid = self.nodes[0].z_sendmany(saplingAddr0, recipients, 1, 0)
        mytxid = wait_and_assert_operationid_status(self.nodes[0], myopid)

        self.sync_all()
        self.generate_and_sync_inc(1)

        assert_equal(len(self.nodes[0].getblock("205")["tx"]), 2)
        assert_equal(self.nodes[1].z_getbalance(saplingAddr1), amount2)
        assert(root is not self.nodes[0].getblock("205")["finalsaplingroot"])

        # Verify there is a Sapling output description (its commitment was added to tree)
        result = self.nodes[0].getrawtransaction(mytxid, 1)
        assert_equal(len(result["vShieldedOutput"]), 2)  # there is Sapling shielded change

        # Mine a block with a Sapling shielded sender and transparent recipient and verify the final Sapling root doesn't change
        taddr2 = self.nodes[0].getnewaddress()
        recipients = []
        recipients.append({"address": taddr2, "amount": amount2})
        myopid = self.nodes[1].z_sendmany(saplingAddr1, recipients, 1, 0)
        mytxid = wait_and_assert_operationid_status(self.nodes[1], myopid)

        self.sync_all()
        self.generate_and_sync_inc(1)

        assert_equal(len(self.nodes[0].getblock("206")["tx"]), 2)
        assert_equal(self.nodes[0].z_getbalance(taddr2), amount2)

        blk = self.nodes[0].getblock("206")
        root = blk["finalsaplingroot"]
        assert_equal(root, self.nodes[0].getblock("205")["finalsaplingroot"])


if __name__ == '__main__':
    FinalSaplingRootTest().main()
