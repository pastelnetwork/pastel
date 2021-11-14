#!/usr/bin/env python3
# Copyright (c) 2021 The Pastel developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from decimal import Decimal, getcontext
getcontext().prec = 16
from functools import reduce
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, start_nodes, initialize_chain_clean, connect_nodes_bi

import logging
import sys

log = logging.getLogger("PastelRPC")

class KeyImportExportTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.setup_clean_chain = True

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir )
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        connect_nodes_bi(self.nodes,0,3)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        [alice, bob, charlie, miner] = self.nodes

        def alice_to_bob(amount):
            alice.sendtoaddress(addr, Decimal(amount))
            self.sync_all()
            miner.generate(1)
            self.sync_all()

        def verify_utxos(node, amounts):
            utxos = node.listunspent(1, 10**9, [addr])
            utxos.sort(key=lambda x: x["confirmations"])
            utxos.reverse()

            try:
                assert_equal(amounts, [utxo["amount"] for utxo in utxos])
            except AssertionError:
                log.error(
                    'Expected amounts: %r; utxos: %r',
                    amounts, utxos)
                raise

        # Seed Alice with some funds
        alice.generate(10)
        self.sync_all()
        miner.generate(100)
        self.sync_all()

        # Now get a pristine address for receiving transfers:
        addr = bob.getnewaddress()
        verify_utxos(bob, [])
        verify_utxos(charlie, [])

        # the amounts of each txn embodied which generates a single UTXO:
        amounts = list(map(Decimal, ['2.3', '3.7', '0.1', '0.5', '1.0', '0.19']))

        # Internal test consistency assertion:
        assert_greater_than(
            alice.getbalance(),
            reduce(Decimal.__add__, amounts))

        log.info("Sending pre-export txns...")
        for amount in amounts[0:2]:
            alice_to_bob(amount)

        log.info("Exporting privkey from bob...")
        privkey = bob.dumpprivkey(addr)

        log.info("Sending post-export txns...")
        for amount in amounts[2:4]:
            alice_to_bob(amount)

        verify_utxos(bob, amounts[:4])
        verify_utxos(charlie, [])

        log.info("Importing privkey into charlie...")
        ipkaddr = charlie.importprivkey(privkey, '', True)
        assert_equal(addr, ipkaddr)

        # importprivkey should have rescanned, so this should pass:
        verify_utxos(charlie, amounts[:4])

        # Verify idempotent behavior:
        ipkaddr2 = charlie.importprivkey(privkey, '', True)
        assert_equal(addr, ipkaddr2)

        # amounts should be unchanged
        verify_utxos(charlie, amounts[:4])

        log.info("Sending post-import txns...")
        for amount in amounts[4:]:
            alice_to_bob(amount)

        verify_utxos(bob, amounts)
        verify_utxos(charlie, amounts)


if __name__ == '__main__':
    KeyImportExportTest().main()
