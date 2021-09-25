#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_true, \
    assert_greater_than, initialize_chain_clean, \
    initialize_datadir, start_nodes, start_node, connect_nodes_bi, \
    pasteld_processes, wait_and_assert_operationid_status, p2p_port, \
    stop_node
from test_framework.authproxy import JSONRPCException
from decimal import Decimal, getcontext
getcontext().prec = 16

class PastelTestFramework (BitcoinTestFramework):
    passphrase = "passphrase"
    new_passphrase = "new passphrase"

    # create new PastelID and associated LegRoast keys on node node_no
    # returns PastelID
    def create_pastelid(self, node_no = 0, LegRoastKeyVar = None):
        keys = self.nodes[node_no].pastelid("newkey", self.passphrase)
        NewPastelID = keys["pastelid"]
        assert_true(NewPastelID, f"No PastelID was created on node #{node_no}")
        if LegRoastKeyVar:
            LegRoastKeyVar = keys["legroast"]
            assert_true(LegRoastKeyVar, f"No LegRoast public key was generated on node #{node_no}")
        return NewPastelID
