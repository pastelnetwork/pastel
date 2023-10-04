#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2023 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

# Exercise the listtransactions API

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import check_array_result

from decimal import Decimal, getcontext
getcontext().prec = 16

class ListTransactionsTest(BitcoinTestFramework):

    def run_test(self):
        # Simple send, 0 to 1:
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.sync_all()
        tx0_list = self.nodes[0].listtransactions()
        tx1_list = self.nodes[1].listtransactions()
        check_array_result(tx0_list,
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":0})
        check_array_result(tx1_list,
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":0})
        # mine a block, confirmations should change:
        self.generate_and_sync_inc(1)
        
        tx0_list = self.nodes[0].listtransactions()
        tx1_list = self.nodes[1].listtransactions()
        check_array_result(tx0_list,
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":1})
        check_array_result(tx1_list,
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":1})

        # send to the new address one the same node
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
        tx_list = self.nodes[0].listtransactions()
        check_array_result(tx_list,
                           {"txid": txid, "category": "send"},
                           {"amount":Decimal("-0.2")},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx_list,
                           {"txid": txid, "category": "receive"},
                           {"amount":Decimal("0.2")},
                           {"amount": lambda x: abs(x) > 1})

        # sendmany from node1: twice to self, twice to node2:
        send_to = { self.nodes[0].getnewaddress() : 0.11,
                    self.nodes[1].getnewaddress() : 0.22,
                    self.nodes[0].getaccountaddress("") : 0.33,
                    self.nodes[1].getaccountaddress("") : 0.44 }
        txid = self.nodes[1].sendmany("", send_to)
        self.sync_all()
        tx0_list = self.nodes[0].listtransactions()
        tx1_list = self.nodes[1].listtransactions()
        check_array_result(tx1_list,
                           {"category":"send","amount":Decimal("-0.11")},
                           {"txid":txid},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx0_list,
                           {"category":"receive","amount":Decimal("0.11")},
                           {"txid":txid},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx1_list,
                           {"category":"send","amount":Decimal("-0.22")},
                           {"txid":txid},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx1_list,
                           {"category":"receive","amount":Decimal("0.22")},
                           {"txid":txid} )
        check_array_result(tx1_list,
                           {"category":"send","amount":Decimal("-0.33")},
                           {"txid":txid},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx0_list,
                           {"category":"receive","amount":Decimal("0.33")},
                           {"txid":txid, "account" : ""},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx1_list,
                           {"category":"send","amount":Decimal("-0.44")},
                           {"txid":txid, "account" : ""},
                           {"amount": lambda x: abs(x) > 1})
        check_array_result(tx1_list,
                           {"category":"receive","amount":Decimal("0.44")},
                           {"txid":txid, "account" : ""},
                           {"amount": lambda x: abs(x) > 1})

if __name__ == '__main__':
    ListTransactionsTest().main()

