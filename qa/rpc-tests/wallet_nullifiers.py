#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_true, pasteld_processes, \
    connect_nodes_bi, start_node, start_nodes, wait_and_assert_operationid_status

import time
from decimal import Decimal, getcontext
getcontext().prec = 16

class WalletNullifiersTest (BitcoinTestFramework):

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir,
                           extra_args=[
		[ '-experimentalfeatures',
		  '-developerencryptwallet']] * self.num_nodes)

    def run_test (self):
        self.generate_and_sync_inc(2)

        # add zaddr to node 0
        myzaddr0 = self.nodes[0].z_getnewaddress()

        # send node 0 taddr to zaddr to get out of coinbase
        mytaddr = self.nodes[0].getnewaddress()
        recipients = []
        recipients.append({"address":myzaddr0, "amount": Decimal('10.0') - self._fee}) # utxo amount less fee
        wait_and_assert_operationid_status(self.nodes[0], self.nodes[0].z_sendmany(mytaddr, recipients), timeout=360)

        self.sync_all()
        self.generate_and_sync_inc(1)

        # add zaddr to node 2
        myzaddr = self.nodes[2].z_getnewaddress()

        # import node 2 zaddr into node 1
        myzkey = self.nodes[2].z_exportkey(myzaddr)
        self.nodes[1].z_importkey(myzkey)

        # encrypt node 1 wallet and wait to terminate
        self.nodes[1].encryptwallet("test")
        pasteld_processes[1].wait()

        # restart node 1
        self.nodes[1] = start_node(1, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_all()

        # send node 0 zaddr to note 2 zaddr
        recipients = []
        recipients.append({"address":myzaddr, "amount": Decimal(7.0)})

        wait_and_assert_operationid_status(self.nodes[0], self.nodes[0].z_sendmany(myzaddr0, recipients), timeout=360)

        self.sync_all()
        self.generate_and_sync_inc(1)

        # check zaddr balance
        zsendmanynotevalue = Decimal('7.0')
        assert_equal(self.nodes[2].z_getbalance(myzaddr), zsendmanynotevalue)
        assert_equal(self.nodes[1].z_getbalance(myzaddr), zsendmanynotevalue)

        # add zaddr to node 3
        myzaddr3 = self.nodes[3].z_getnewaddress()

        # send node 2 zaddr to note 3 zaddr
        recipients = []
        recipients.append({"address":myzaddr3, "amount": Decimal(2.0)})

        wait_and_assert_operationid_status(self.nodes[2], self.nodes[2].z_sendmany(myzaddr, recipients), timeout=360)

        self.sync_all()
        self.generate_and_sync_inc(1, 2)

        # check zaddr balance
        zsendmany2notevalue = Decimal('2.0')
        zsendmanyfee = self._fee
        zaddrremaining = zsendmanynotevalue - zsendmany2notevalue - zsendmanyfee
        assert_equal(self.nodes[3].z_getbalance(myzaddr3), zsendmany2notevalue)
        assert_equal(self.nodes[2].z_getbalance(myzaddr), zaddrremaining)

        # Parallel encrypted wallet can cache nullifiers for Sapling received notes
        assert_equal(self.nodes[1].z_getbalance(myzaddr), zaddrremaining)

        # send node 2 zaddr on node 1 to taddr
        # This requires that node 1 be unlocked, which triggers caching of
        # uncached nullifiers.
        self.nodes[1].walletpassphrase("test", 600)
        mytaddr1 = self.nodes[1].getnewaddress()
        recipients = []
        recipients.append({"address":mytaddr1, "amount": Decimal(1.0)})
        
        wait_and_assert_operationid_status(self.nodes[1], self.nodes[1].z_sendmany(myzaddr, recipients), timeout=360)

        self.sync_all()
        self.generate_and_sync_inc(1, 1)

        # check zaddr balance
        # Now that the encrypted wallet has been unlocked, the note nullifiers
        # have been cached and spent notes can be detected. Thus the two wallets
        # are in agreement once more.
        zsendmany3notevalue = Decimal('1.0')
        zaddrremaining2 = zaddrremaining - zsendmany3notevalue - zsendmanyfee
        assert_equal(self.nodes[1].z_getbalance(myzaddr), zaddrremaining2)
        assert_equal(self.nodes[2].z_getbalance(myzaddr), zaddrremaining2)

        # Test viewing keys

        node3mined = self._reward*25
        assert_equal({k: Decimal(v) for k, v in self.nodes[3].z_gettotalbalance().items()}, {
            'transparent': node3mined,
            'private': zsendmany2notevalue,
            'total': node3mined + zsendmany2notevalue,
        })

        # add node 1 address and node 2 viewing key to node 3
        myzvkey = self.nodes[2].z_exportviewingkey(myzaddr)
        self.nodes[3].importaddress(mytaddr1)
        importvk_result = self.nodes[3].z_importviewingkey(myzvkey, 'whenkeyisnew', 1)

        # Check results of z_importviewingkey
        assert_equal(importvk_result["type"], "sapling")
        assert_equal(importvk_result["address"], myzaddr)

        # Check the address has been imported
        assert_equal(myzaddr in self.nodes[3].z_listaddresses(), False)
        assert_equal(myzaddr in self.nodes[3].z_listaddresses(True), True)

        # Node 3 should see the same received notes as node 2; however, there are 2 things:
        # - Some of the notes were change for node 2 but not for node 3.
        # - Each node wallet store transaction time as received. As
        #   `wait_and_assert_operationid_status` is called node 2 and 3 are off by a few seconds.
        # Aside from that the received notes should be the same. So,
        # group by txid and then check that all properties aside from
        # change are equal.
        node2Received = dict([r['txid'], r] for r in self.nodes[2].z_listreceivedbyaddress(myzaddr))
        node3Received = dict([r['txid'], r] for r in self.nodes[3].z_listreceivedbyaddress(myzaddr))
        assert_equal(len(node2Received), len(node2Received))
        for txid in node2Received:
            received2 = node2Received[txid]
            received3 = node3Received[txid]
            # the change field will be omitted for received3, but all other fields should be shared
            assert_true(len(received2) >= len(received3))
            for key in received2:
                # check all the properties except for change and blocktime
                if key != 'change' and key != 'blocktime':
                    assert_equal(received2[key], received3[key])

        # Node 3's balances should be unchanged without explicitly requesting
        # to include watch-only balances
        assert_equal({k: Decimal(v) for k, v in self.nodes[3].z_gettotalbalance().items()}, {
            'transparent': node3mined,
            'private': zsendmany2notevalue,
            'total': node3mined + zsendmany2notevalue,
        })

        # Wallet can cache nullifiers for Sapling notes received by addresses it only has a
        # viewing key for.
        assert_equal({k: Decimal(v) for k, v in self.nodes[3].z_gettotalbalance(1, True).items()}, {
            'transparent': node3mined + Decimal('1.0'),
            'private': zsendmany2notevalue + zaddrremaining2,
            'total': node3mined + Decimal('1.0') + zsendmany2notevalue + zaddrremaining2,
        })

        # Check individual balances reflect the above
        assert_equal(self.nodes[3].z_getbalance(mytaddr1), Decimal('1.0'))
        assert_equal(self.nodes[3].z_getbalance(myzaddr), zaddrremaining2)

if __name__ == '__main__':
    WalletNullifiersTest().main ()
