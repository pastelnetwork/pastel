#!/usr/bin/env python3
# Copyright (c) 2021 The Pastel developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_true, assert_false
from test_framework.util import start_nodes, wait_and_assert_operationid_status
from decimal import Decimal

my_memo_str = 'c0ffee' # stay awake
my_memo = '633066666565'
my_memo = my_memo + '0'*(1024-len(my_memo))

no_memo = 'f6' + ('0'*1022) # see section 5.5 of the protocol spec

class ListReceivedTest (BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.num_nodes = 4

    def run_test_release(self, height):
        self.generate_and_sync(height+1)
        taddr = self.nodes[1].getnewaddress()
        zaddr1 = self.nodes[1].z_getnewaddress()
        zaddrExt = self.nodes[3].z_getnewaddress()

        self.nodes[0].sendtoaddress(taddr, 4.0)
        self.generate_and_sync_inc(1)

        # Send 1 PSL to zaddr1, 2 PSLs to zaddrExt
        opid = self.nodes[1].z_sendmany(taddr, [
            {'address': zaddr1, 'amount': 1, 'memo': my_memo},
            {'address': zaddrExt, 'amount': 2},
        ])
        txid = wait_and_assert_operationid_status(self.nodes[1], opid)
        self.sync_all()

        # Decrypted transaction details should be correct
        pt = self.nodes[1].z_viewtransaction(txid)
        assert_equal(pt['txid'], txid)
        assert_equal(len(pt['spends']), 0)
        assert_equal(len(pt['outputs']), 2)

        # Output orders can be randomized, so we check the output
        # positions and contents separately
        outputs = []

        assert_equal(pt['outputs'][0]['type'], 'sapling')
        if pt['outputs'][0]['address'] == zaddr1:
            assert_equal(pt['outputs'][0]['outgoing'], False)
            assert_equal(pt['outputs'][0]['memoStr'], my_memo_str)
        else:
            assert_equal(pt['outputs'][0]['outgoing'], True)
        outputs.append({
            'address': pt['outputs'][0]['address'],
            'value': pt['outputs'][0]['value'],
            'valuePsl': pt['outputs'][0]['valuePsl'],
            'memo': pt['outputs'][0]['memo'],
        })

        assert_equal(pt['outputs'][1]['type'], 'sapling')
        if pt['outputs'][1]['address'] == zaddr1:
            assert_equal(pt['outputs'][1]['outgoing'], False)
            assert_equal(pt['outputs'][1]['memoStr'], my_memo_str)
        else:
            assert_equal(pt['outputs'][1]['outgoing'], True)
        outputs.append({
            'address': pt['outputs'][1]['address'],
            'value': pt['outputs'][1]['value'],
            'valuePsl': pt['outputs'][1]['valuePsl'],
            'memo': pt['outputs'][1]['memo'],
        })

        assert({
            'address': zaddr1,
            'value': Decimal('1'),
            'valuePsl': 100000,
            'memo': my_memo,
        } in outputs)
        assert({
            'address': zaddrExt,
            'value': Decimal('2'),
            'valuePsl': 200000,
            'memo': no_memo,
        } in outputs)

        r = self.nodes[1].z_listreceivedbyaddress(zaddr1)
        assert_equal(0, len(r), "Should have received no confirmed note")
        c = self.nodes[1].z_getnotescount()
        assert_equal(0, c['sapling'], "Count of confirmed notes should be 0")

        # No confirmation required, one note should be present
        r = self.nodes[1].z_listreceivedbyaddress(zaddr1, 0)
        assert_equal(1, len(r), "Should have received one (unconfirmed) note")
        assert_equal(txid, r[0]['txid'])
        assert_equal(1, r[0]['amount'])
        assert_equal(100000, r[0]['amountPsl'])
        assert_false(r[0]['change'], "Note should not be change")
        assert_equal(my_memo, r[0]['memo'])
        assert_equal(0, r[0]['confirmations'])
        assert_equal(-1, r[0]['blockindex'])
        assert_equal(0, r[0]['blockheight'])

        c = self.nodes[1].z_getnotescount(0)
        assert_equal(1, c['sapling'], "Count of unconfirmed notes should be 1")

        # Confirm transaction (1 ZEC from taddr to zaddr1)
        self.generate_and_sync(height+3)

        # adjust confirmations
        r[0]['confirmations'] = 1
        # adjust blockindex
        r[0]['blockindex'] = 1
        # adjust height
        r[0]['blockheight'] = height + 3

        # Require one confirmation, note should be present
        assert_equal(r, self.nodes[1].z_listreceivedbyaddress(zaddr1))

        # Generate some change by sending part of zaddr1 to zaddr2
        txidPrev = txid
        zaddr2 = self.nodes[1].z_getnewaddress()
        opid = self.nodes[1].z_sendmany(zaddr1,
            [{'address': zaddr2, 'amount': 0.6}])
        txid = wait_and_assert_operationid_status(self.nodes[1], opid)
        self.sync_all()
        self.generate_and_sync(height+4)

        # Decrypted transaction details should be correct
        pt = self.nodes[1].z_viewtransaction(txid)
        assert_equal(pt['txid'], txid)
        assert_equal(len(pt['spends']), 1)
        assert_equal(len(pt['outputs']), 2)

        assert_equal(pt['spends'][0]['type'], 'sapling')
        assert_equal(pt['spends'][0]['txidPrev'], txidPrev)
        assert_equal(pt['spends'][0]['spend'], 0)
        assert_equal(pt['spends'][0]['outputPrev'], 0)
        assert_equal(pt['spends'][0]['address'], zaddr1)
        assert_equal(pt['spends'][0]['value'], Decimal('1.0'))
        assert_equal(pt['spends'][0]['valuePsl'], 100000)

        # Output orders can be randomized, so we check the output
        # positions and contents separately
        outputs = []

        assert_equal(pt['outputs'][0]['type'], 'sapling')
        assert_equal(pt['outputs'][0]['output'], 0)
        assert_equal(pt['outputs'][0]['outgoing'], False)
        outputs.append({
            'address': pt['outputs'][0]['address'],
            'value': pt['outputs'][0]['value'],
            'valuePsl': pt['outputs'][0]['valuePsl'],
            'memo': pt['outputs'][0]['memo'],
        })

        assert_equal(pt['outputs'][1]['type'], 'sapling')
        assert_equal(pt['outputs'][1]['output'], 1)
        assert_equal(pt['outputs'][1]['outgoing'], False)
        outputs.append({
            'address': pt['outputs'][1]['address'],
            'value': pt['outputs'][1]['value'],
            'valuePsl': pt['outputs'][1]['valuePsl'],
            'memo': pt['outputs'][1]['memo'],
        })

        assert({
            'address': zaddr2,
            'value': Decimal('0.6'),
            'valuePsl': 60000,
            'memo': no_memo,
        } in outputs)
        assert({
            'address': zaddr1,
            'value': Decimal('0.4') - self._fee,
            'valuePsl': 40000 - self._fee * self._coin,
            'memo': no_memo,
        } in outputs)

        # zaddr1 should have a note with change
        r = self.nodes[1].z_listreceivedbyaddress(zaddr1, 0)
        r = sorted(r, key = lambda received: received['amount'])
        assert_equal(2, len(r), "zaddr1 Should have received 2 notes")

        assert_equal(txid, r[0]['txid'])
        assert_equal(Decimal('0.4')-self._fee, r[0]['amount'])
        assert_equal(40000 - self._fee * self._coin, r[0]['amountPsl'])
        assert_true(r[0]['change'], "Note valued at (0.4-"+str(self._fee)+") should be change")
        assert_equal(no_memo, r[0]['memo'])

        # The old note still exists (it's immutable), even though it is spent
        assert_equal(Decimal('1.0'), r[1]['amount'])
        assert_equal(100000, r[1]['amountPsl'])
        assert_false(r[1]['change'], "Note valued at 1.0 should not be change")
        assert_equal(my_memo, r[1]['memo'])

        # zaddr2 should not have change
        r = self.nodes[1].z_listreceivedbyaddress(zaddr2, 0)
        r = sorted(r, key = lambda received: received['amount'])
        assert_equal(1, len(r), "zaddr2 Should have received 1 notes")
        assert_equal(txid, r[0]['txid'])
        assert_equal(Decimal('0.6'), r[0]['amount'])
        assert_equal(60000, r[0]['amountPsl'])
        assert_false(r[0]['change'], "Note valued at 0.6 should not be change")
        assert_equal(no_memo, r[0]['memo'])

        c = self.nodes[1].z_getnotescount(0)
        assert_equal(3, c['sapling'], "Count of unconfirmed notes should be 3(2 in zaddr1 + 1 in zaddr2)")

    def run_test(self):
        self.run_test_release(200)

if __name__ == '__main__':
    ListReceivedTest().main()
