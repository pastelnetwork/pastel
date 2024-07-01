#!/usr/bin/env python3
# Copyright (c) 2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
#
# Test burntxindex generation and fetching for scanburntransactions
#
# RPCs tested here:
#
#   scanburntransactions

from decimal import getcontext, Decimal
import random
from typing import (
    Dict,
    Optional,
)

from pastel_test_framework import PastelTestFramework
from test_framework.test_framework import (
    node_id_0,
    node_id_1,
)
from test_framework.util import (
    wait_and_assert_operationid_status_result,
    assert_equal,
    assert_true,
    start_nodes,
    stop_nodes,
    connect_nodes,
    wait_pastelds,
)

class BurnTxIndexTest(PastelTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.tracking_addresses = []


    def setup_network(self, split = False):
        args = [
            '-debug=txdb'
        ]
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, [args] * self.num_nodes)

        for n in range(self.num_nodes):
            connect_nodes(self.nodes[0], n + 1)

        self.sync_all()


    def compare_transactions(self, txno: int, current_height: int, txid: str, expected, actual):
        keys_to_compare = ['blockhash', 'blockindex', 'timestamp', 'from_address', 'amount', 'amountPat', 'confirmations']
        for key in keys_to_compare:
            if key == 'amount':
                assert_equal(Decimal(str(expected[key])), Decimal(str(actual[key])),
                             f"tx #{txno}: amount mismatch for txid {txid}")
            elif key == 'confirmations':
                assert_equal(current_height - expected['blockindex'] + 1, actual[key],
                             f"tx #{txno}: confirmations mismatch for txid {txid}")
            elif key == 'amountPat':
                assert_equal(Decimal(str(expected['amount'] * self._coin)), Decimal(str(actual[key])),
                             f"tx #{txno}: amountPat mismatch for txid {txid}")
            else:
                assert_equal(expected[key], actual[key], 
                             f"tx #{txno}: {key.capitalize()} mismatch for txid {txid}")
        return True


    def compare_mempool_transactions(self, txno: int, current_height: int, txid: str, expected, actual):
        keys_to_compare = ['blockhash', 'blockindex', 'timestamp', 'from_address', 'amount', 'amountPat', 'confirmations']
        for key in keys_to_compare:
            if key == 'amount':
                assert_equal(Decimal(str(expected[key])), Decimal(str(actual[key])),
                             f"tx #{txno}: amount mismatch for txid {txid}")
            elif key == 'confirmations':
                assert_equal(0, actual[key],
                             f"tx #{txno}: confirmations mismatch for txid {txid}")
            elif key == 'blockhash':
                assert_equal("NA", actual[key],
                             f"tx #{txno}: blockhash mismatch for txid {txid}")
            elif key == 'blockindex':
                assert_equal(0, actual[key],
                             f"tx #{txno}: blockindex mismatch for txid {txid}")
            elif key == 'timestamp':
                assert_equal(0, actual[key],
                             f"tx #{txno}: timestamp mismatch for txid {txid}")
            elif key == 'amountPat':
                assert_equal(Decimal(str(expected['amount'] * self._coin)), Decimal(str(actual[key])),
                             f"tx #{txno}: amountPat mismatch for txid {txid}")
            else:
                assert_equal(expected[key], actual[key], 
                             f"tx #{txno}: {key.capitalize()} mismatch for txid {txid}")
        return True


    def verify_transactions(self, current_height: int, expected: Dict, txs: Dict, expected_in_mempool: Optional[Dict] = None):
        expected_tx_count = len(expected)
        if expected_in_mempool:
            expected_tx_count += len(expected_in_mempool)
        assert_equal(expected_tx_count, len(txs), "Number of transactions doesn't match")

        for i, act_tx in enumerate(txs):
            txid = act_tx['txid']
            is_mempool_tx = act_tx['confirmations'] == 0
            if is_mempool_tx:
                assert_true(expected_in_mempool and txid in expected_in_mempool,
                            f"Transaction #{i+1} with txid {txid} not found in expected mempool data")
                if expected_in_mempool:
                    exp_tx = expected_in_mempool[txid]
                    self.compare_mempool_transactions(i, current_height, txid, exp_tx, act_tx)
            else:
                assert_true(txid in expected,
                            f"Transaction #{i+1} with txid {txid} not found in expected data")
                exp_tx = expected[txid]
                self.compare_transactions(i, current_height, txid, exp_tx, act_tx)
            

    def generate_txs_to_burn_address(self, amount_inc):
        current_height = self.nodes[0].getblockcount()
        expected_total_amount = Decimal(0)
        expected = {}
        for _ in range(5):
            opids = []
            txids = []
            for j, tracking_address in enumerate(self.tracking_addresses):
                opids.append(self.nodes[0].z_sendmanywithchangetosender(tracking_address,
                    [{"address": PastelTestFramework.BURN_ADDRESS, "amount": Decimal(j + amount_inc)}], 1, 0))
                expected_total_amount += j + amount_inc
            for opid in opids:
                opresult = wait_and_assert_operationid_status_result(self.nodes[0], opid, "success")
                assert_true(opresult and ("result" in opresult))
                if opresult:
                    result = opresult["result"]
                    if result:
                        txids.append(result["txid"])
            block_hash = self.generate_and_sync_inc(1, node_id_0)[0]
            current_height += 1
            block_info = self.nodes[0].getblock(current_height)
            block_time = block_info["time"]
            for j, tracking_address in enumerate(self.tracking_addresses):
                expected.update({txids[j]: {
                    "blockhash": block_hash,
                    "blockindex": current_height,
                    "amount": -1 * Decimal(j + amount_inc),
                    "from_address": tracking_address,
                    "timestamp": block_time,
                }})
        return expected_total_amount, expected


    def test_scanburntransactions(self, expected: Dict, expected_in_mempool: Optional[Dict] = None):
        current_height = self.nodes[0].getblockcount()
        # execute scanburntransactions for all tracking addresses
        # first scanburntransactions request should trigger burntxindex generation
        # some transactions are returned from the mempool
        txs = self.nodes[0].scanburntransactions("*")
        print(f'scanburntransactions returned {len(txs)} transactions')
        self.verify_transactions(current_height, expected, txs, expected_in_mempool)

        # scanburntransactions for the specific address
        for _ in range(5):
            # just take random tracking address
            tracking_address = random.choice(self.tracking_addresses)
            txs_addr = self.nodes[0].scanburntransactions(tracking_address)
            # filter out transactions for the specific address from expected dict
            expected_addr ={txid: tx for txid, tx in expected.items() if tx['from_address'] == tracking_address}
            specific_address_tx_count = len(expected_addr)
            # filter out transactions for the specific address from expected_in_mempool dict
            expected_in_mempool_addr: Optional[Dict] = None
            if expected_in_mempool:
                expected_in_mempool_addr ={txid: tx for txid, tx in expected_in_mempool.items() if tx['from_address'] == tracking_address}
                specific_address_tx_count += len(expected_in_mempool_addr)
            assert_equal(len(txs_addr), specific_address_tx_count, f"Expected {specific_address_tx_count} transactions")
            self.verify_transactions(current_height, expected_addr, txs_addr, expected_in_mempool_addr)

        # scanburntransactions for the specific address with starting height
        start_height = current_height - 2
        for _ in range(5):
            # just take random tracking address
            tracking_address = random.choice(self.tracking_addresses)
            txs_addr = self.nodes[0].scanburntransactions(tracking_address, start_height)
            # filter out transactions for the specific address from expected dict, filter by height as well
            expected_addr ={txid: tx for txid, tx in expected.items()
                                if (tx['from_address'] == tracking_address) and (tx['blockindex'] >= start_height)}
            specific_address_tx_count = len(expected_addr)
            expected_in_mempool_addr: Optional[Dict] = None
            if expected_in_mempool:
                expected_in_mempool_addr ={txid: tx for txid, tx in expected_in_mempool.items()
                                            if tx['from_address'] == tracking_address}
                specific_address_tx_count += len(expected_in_mempool_addr)
            assert_equal(len(txs_addr), specific_address_tx_count, f"Expected {specific_address_tx_count} transactions")
            self.verify_transactions(current_height, expected_addr, txs_addr, expected_in_mempool_addr)


    def run_test(self):

        self.generate_and_sync_inc(110, node_id_0)
        for node_no in range(self.num_nodes):
            addr = self.nodes[node_no].importprivkey(PastelTestFramework.BURN_ADDRESS_PKEY)
            assert_equal(addr, PastelTestFramework.BURN_ADDRESS)

        coinbase_addr0 = self.nodes[0].getnewaddress()

        self.tracking_addresses = [self.nodes[0].getnewaddress() for _ in range(10)]
        for tracking_address in self.tracking_addresses:
            self.nodes[0].sendtoaddress(tracking_address, 1000)
        self.generate_and_sync_inc(1, node_id_0)

        # generate transactions to burn address
        expected_total_amount1, expected1 = self.generate_txs_to_burn_address(1)
        amount = self.nodes[0].getreceivedbyaddress(PastelTestFramework.BURN_ADDRESS, 1)
        assert_equal(amount, expected_total_amount1)

        self.test_scanburntransactions(expected1)

        # add more transactions from tracking addresses  - this should be added automatically to the burn txindex
        expected_total_amount2, expected2 = self.generate_txs_to_burn_address(10)

        expected_total_amount = expected_total_amount1 + expected_total_amount2
        amount = self.nodes[0].getreceivedbyaddress(PastelTestFramework.BURN_ADDRESS, 1)
        assert_equal(amount, expected_total_amount)

        # combined expected data
        expected = {**expected1, **expected2}
        self.test_scanburntransactions(expected)

        # invalidate last 5 blocks
        current_height = self.nodes[0].getblockcount()
        block_hash = self.nodes[0].getblockhash(current_height - 4)
        self.nodes[0].invalidateblock(block_hash)

        # get (expected2 - expected1) transactions
        expected_in_mempool = {txid: tx for txid, tx in expected2.items() if txid not in expected1}

        # all transactions from the last 5 blocks should be removed from the burn txindex
        # but they will still be returned by scanburntransactions as they are still be in the mempool
        self.test_scanburntransactions(expected1, expected_in_mempool)

        # Restart all nodes to ensure indices are saved to disk and recovered
        stop_nodes(self.nodes)
        wait_pastelds()
        self.setup_network()

        # chain will be restored from other nodes and burn txindex will be regenerated
        self.test_scanburntransactions(expected)

if __name__ == '__main__':
    BurnTxIndexTest().main()
