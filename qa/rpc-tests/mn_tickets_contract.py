#!/usr/bin/env python3
# Copyright (c) 2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
from itertools import combinations
from typing import Optional

from test_framework.util import (
    assert_equal,
    assert_true,
    assert_greater_than,
    assert_raises_rpc,
    assert_shows_help,
    start_nodes,
    connect_nodes_bi,
    str_to_b64str,
)
from ticket_type import TicketType
from pastel_test_framework import (
    RegisterResultInfo,
    PastelTestFramework,
)
import test_framework.rpc_consts as rpc

class TicketContractTest(PastelTestFramework):
    """
    Test Pastel contract tickets
    """

    def setup_network(self, split = False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
                                 extra_args=[['-debug=compress']] * self.num_nodes)
        for pair in combinations(range(self.num_nodes), 2):
            connect_nodes_bi(self.nodes, pair[0], pair[1])
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        print("---- Contract Ticket tests STARTED ----")

        self.v_coinbase_addr = []
        self.v_addr = []
        # step over coinbase address
        for i in range(self.num_nodes):
            self.v_coinbase_addr.append(self.nodes[i].getnewaddress())
            self.v_addr.append(self.nodes[i].getnewaddress())
        self.generate_and_sync_inc(1)

        # send all utxos from node #3 to addr[0] to make empty balance
        self.nodes[3].sendtoaddress(self.v_addr[0], self.nodes[3].getbalance(), "empty node3", "test", True)
        self.generate_and_sync_inc(1)

        self.contract_register_tests()
        self.contract_rpc_tests()

        print("---- Contract Ticket tests FINISHED ----")


    def contract_register(self, node_index: int, contract_id: str, subtype: str, secondary_key: Optional[str], address: Optional[str]) -> RegisterResultInfo:
        """
        Register contract ticket
        """
        ticket_type_name = TicketType.CONTRACT.type_name
        contract_ticket  = str_to_b64str(f'{{ "contract_id": "{contract_id}" }}')

        args = [ticket_type_name, contract_ticket, subtype]
        if secondary_key:
            args.append(secondary_key)

            if address:
                args.append(address)

        result = self.nodes[node_index].tickets("register", *args)
        return RegisterResultInfo(
            txid=result["txid"],
            key=result["key"])


    def contract_register_tests(self):
        """
        Test contract ticket registration
        """
        ticket_type_name = TicketType.CONTRACT.type_name

        # tickets register contract "{contract-ticket}" "subtype" ["secondary_key"] ["address"]
        assert_shows_help(self.nodes[0].tickets, "register", ticket_type_name)

        # subtype is missing
        assert_raises_rpc(rpc.RPC_INVALID_PARAMETER, f"tickets register {ticket_type_name}",
            self.nodes[1].tickets, "register", ticket_type_name, "contract_ticket")

        # subtype is empty
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Contract sub type is not defined",
            self.nodes[1].tickets, "register", ticket_type_name, "contract_ticket", "")

        # contract_ticket is empty
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "Contract ticket data is empty",
            self.nodes[1].tickets, "register", ticket_type_name, "", "subtype")

        # successful registration
        info = self.contract_register(0, "contract-1", "contract-subtype", "contract-secondary-key", self.v_addr[0])
        assert_true(info.txid, "Contract ticket registration failed - txid is empty")
        assert_true(info.key, "Contract ticket registration failed - key is empty")

        # ticket is only in mempool, try to register contract with the same secondary key
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "ticket transaction in mempool with the same secondary key",
            self.nodes[0].tickets, "register", ticket_type_name, "contract-2", "contract-subtype", "contract-secondary-key", self.v_addr[0])

        self.generate_and_sync_inc(1)
        self.inc_ticket_counter(TicketType.CONTRACT)


    def contract_rpc_tests(self):
        """
        Test contract ticket rpc commands
        """
        ticket_type_name = TicketType.CONTRACT.type_name
        # register contract tickets without secondary key
        v1 = []
        v2 = []
        for i in range(3):
            v1.append(self.contract_register(0, f'v1-{i + 1}', 'subtype1', None, None))
            v2.append(self.contract_register(1, f'v2-{i + 1}', 'subtype2', None, None))
        self.generate_and_sync_inc(1)
        self.inc_ticket_counter(TicketType.CONTRACT, 6)

        current_height = self.nodes[0].getblockcount()
        # test "tickets get" rpc command
        for i in range(3):
            result = self.nodes[2].tickets("get", v1[i].txid)
            tkt = result["ticket"]
            assert_equal(tkt["sub_type"], "subtype1")
            assert_equal(tkt["key"], v1[i].key)
            assert_greater_than(tkt["timestamp"], 0)
            assert_equal(result["height"], current_height)
            assert_equal(result["txid"], v1[i].txid)

            # decode contract ticket
            result = self.nodes[3].tickets("get", v2[i].txid, True)
            tkt = result["ticket"]
            assert_equal(tkt["sub_type"], "subtype2")
            assert_equal(tkt["key"], v2[i].key)
            assert_equal(result["txid"], v2[i].txid)
            contract_ticket = tkt["contract_ticket"]
            assert_equal(contract_ticket["contract_id"], f'v2-{i + 1}')

        # test "tickets find" rpc command
        # find by primary key
        tkt = self.nodes[0].tickets("find", ticket_type_name, v1[0].key)
        assert_true(isinstance(tkt, dict))
        assert_equal(tkt['ticket']['type'], ticket_type_name)

        # find by ticket subtype
        tickets = self.nodes[1].tickets("find", ticket_type_name, "subtype2")
        # assert that tickets is array
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), 3)
        # check sub types
        for i in range(3):
            tkt = tickets[i]['ticket']
            assert_equal(tkt["sub_type"], "subtype2")

        # test "tickets list" rpc command
        # list all contracts
        tickets = self.nodes[2].tickets("list", ticket_type_name)
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), self.ticket_counter(TicketType.CONTRACT))

        # list contracts by subtype
        tickets = self.nodes[3].tickets("list", ticket_type_name, "subtype1")
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), 3)
        for i in range(3):
            tkt = tickets[i]['ticket']
            assert_equal(tkt["sub_type"], "subtype1")
        tickets = self.nodes[3].tickets("list", ticket_type_name, "subtype2")
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), 3)
        for i in range(3):
            tkt = tickets[i]['ticket']
            assert_equal(tkt["sub_type"], "subtype2")

        # register tickets with secondary key
        v3 = []
        for i in range(3):
            v3.append(self.contract_register(2, f'v3-{i + 1}', 'subtype3', f'seckey-{i+1}', None))
        self.generate_and_sync_inc(1)
        self.inc_ticket_counter(TicketType.CONTRACT, 3)
        
        # "tickets find" should return contract by secondary key
        ticket = self.nodes[1].tickets("find", ticket_type_name, 'seckey-2')
        assert_true(isinstance(ticket, dict))
        tkt = ticket['ticket']
        assert_equal(tkt["sub_type"], "subtype3")
        assert_equal(tkt["key"], v3[1].key)
        assert_equal(tkt["secondary_key"], "seckey-2")

        tickets = self.nodes[2].tickets("list", ticket_type_name, "all")
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), self.ticket_counter(TicketType.CONTRACT))

        # test "tickets findbylabel" rpc command (label is secondary key)
        tickets = self.nodes[0].tickets("findbylabel", ticket_type_name, "seckey-3")
        assert_true(isinstance(tickets, list))
        assert_equal(len(tickets), 1)
        tkt = tickets[0]['ticket']
        assert_equal(tkt["sub_type"], "subtype3")
        assert_equal(tkt["key"], v3[2].key)
        assert_equal(tkt["secondary_key"], "seckey-3")


if __name__ == '__main__':
    TicketContractTest().main()
