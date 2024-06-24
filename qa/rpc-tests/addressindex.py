#!/usr/bin/env python3
# Copyright (c) 2019 The Zcash developers
# Copyright (c) 2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
#
# Test addressindex generation and fetching for insightexplorer or lightwalletd
# 
# RPCs tested here:
#
#   getaddresstxids
#   getaddressbalance
#   getaddressdeltas
#   getaddressutxos
#   getaddressmempool

from binascii import hexlify, unhexlify

from test_framework.test_framework import (
    BitcoinTestFramework,
    node_id_0,
)
from test_framework.util import (
    assert_equal,
    assert_true,
    start_nodes,
    stop_nodes,
    connect_nodes,
    wait_pastelds,
)
from test_framework.script import (
    CScript,
    OP_HASH160,
    OP_EQUAL,
    OP_DUP,
    OP_DROP,
)
from test_framework.mininode import (
    COIN,
    CTransaction,
    CTxIn, CTxOut, COutPoint,
)

class AddressIndexTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 3


    def setup_network(self, split = False):
        # -insightexplorer causes addressindex to be enabled (fAddressIndex = true)
        args = [
            '-debug=rpc',
            '-txindex',
            '-insightexplorer'
        ]
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir, [args] * self.num_nodes)

        for n in range(self.num_nodes):
            connect_nodes(self.nodes[0], n + 1)

        self.is_network_split = False
        self.sync_all()


    def run_test(self):

        # helper functions
        def getaddresstxids(node_index, addresses, start, end):
            return self.nodes[node_index].getaddresstxids({
                'addresses': addresses,
                'start': start,
                'end': end
            })

        def getaddressdeltas(node_index, addresses, start, end, chainInfo=None):
            params = {
                'addresses': addresses,
                'start': start,
                'end': end,
            }
            if chainInfo is not None:
                params.update({'chainInfo': chainInfo})
            return self.nodes[node_index].getaddressdeltas(params)

        # default received value is the balance value
        def check_balance(node_index, address, expected_balance, expected_received=None):
            if isinstance(address, list):
                bal = self.nodes[node_index].getaddressbalance({'addresses': address})
            else:
                bal = self.nodes[node_index].getaddressbalance(address)
            assert_equal(bal['balance'], expected_balance)
            if expected_received is None:
                expected_received = expected_balance
            assert_equal(bal['received'], expected_received)

        # begin test

        self.generate_and_sync_inc(105, node_id_0)
        assert_equal(self.nodes[0].getbalance(), 5 * self._reward)
        assert_equal(self.nodes[1].getblockcount(), 105)
        assert_equal(self.nodes[1].getbalance(), 0)

        # only the oldest 5; subsequent are not yet mature
        unspent_txids = [u['txid'] for u in self.nodes[0].listunspent()]

        # Currently our only unspents are coinbase transactions, choose any one
        tx = self.nodes[0].getrawtransaction(unspent_txids[0], 1)

        # first output is the mining reward with type pay-to-public-key-hash
        addr_p2pkh = tx['vout'][0]['scriptPubKey']['addresses'][0]

        # Check that balances from mining are correct (105 blocks mined); in
        # regtest, all mining rewards from a single call to generate() are sent
        # to the same pair of addresses.
        check_balance(1, addr_p2pkh, 105 * self._reward * COIN)

        # Multiple address arguments, results are the sum
        check_balance(1, [addr_p2pkh], 105 * self._reward * COIN)

        assert_equal(len(self.nodes[1].getaddresstxids(addr_p2pkh)), 105)

        # only the oldest 5 transactions are in the unspent list,
        # dup addresses are ignored
        height_txids = getaddresstxids(1, [addr_p2pkh, addr_p2pkh], 1, 5)
        assert_equal(sorted(height_txids), sorted(unspent_txids))

        # each txid should appear only once
        height_txids = getaddresstxids(1, [addr_p2pkh], 1, 5)
        assert_equal(sorted(height_txids), sorted(unspent_txids))

        # do some transfers, make sure balances are good
        txids_a1 = []
        addr1 = self.nodes[1].getnewaddress()
        expected_total_amount = 0
        expected_deltas = []  # for checking getaddressdeltas (below)
        for i in range(5):
            # first transaction happens at height 105, mined in block 106
            txid = self.nodes[0].sendtoaddress(addr1, i + 1)
            txids_a1.append(txid)
            self.generate_and_sync_inc(1, node_id_0)
            expected_total_amount += i + 1
            expected_deltas.append({
                'height': 106 + i,
                'patoshis': (i + 1) * COIN,
                'txid': txid,
            })
        check_balance(1, addr1, expected_total_amount * COIN)
        assert_equal(sorted(self.nodes[0].getaddresstxids(addr1)), sorted(txids_a1))
        assert_equal(sorted(self.nodes[1].getaddresstxids(addr1)), sorted(txids_a1))

        # Restart all nodes to ensure indices are saved to disk and recovered
        stop_nodes(self.nodes)
        wait_pastelds()
        self.setup_network()

        bal = self.nodes[1].getaddressbalance(addr1)
        assert_equal(bal['balance'], expected_total_amount * COIN)
        assert_equal(bal['received'], expected_total_amount * COIN)
        assert_equal(sorted(self.nodes[0].getaddresstxids(addr1)), sorted(txids_a1))
        assert_equal(sorted(self.nodes[1].getaddresstxids(addr1)), sorted(txids_a1))

        # Send 3 from addr1, but -- subtlety alert! -- addr1 at this
        # time has 4 UTXOs, with values 1, 2, 3, 4. Sending value 3 requires
        # using up the value 4 UTXO, because of the tx fee
        # (the 3 UTXO isn't quite large enough).
        #
        # The txid from sending *from* addr1 is also added to the list of
        # txids associated with that address (test will verify below).

        addr2 = self.nodes[2].getnewaddress()
        txid = self.nodes[1].sendtoaddress(addr2, 3)
        self.sync_all()

        txFeePat = self.nodes[0].gettxfee(txid)["txFeePat"]
        
        # the one tx in the mempool refers to addresses addr1 and addr2,
        # change by default goes back to the sender, so there will be one
        # output to addr1 with a change (4 - 3 - txFee) and one output to addr2
        mempool1 = self.nodes[0].getaddressmempool({'addresses': [addr2, addr1]})
        assert_equal(len(mempool1), 3)

        expected_mempool_values1 = [
            { 'address': addr2, 'patoshis': 3 * COIN},
            { 'address': addr1, 'patoshis': [ -4 * COIN, (4 - 3) * COIN - txFeePat ] },
            { 'address': addr1, 'patoshis': [ -4 * COIN, (4 - 3) * COIN - txFeePat ] },
        ]

        for i, expected in enumerate(expected_mempool_values1):
            # make sure we compare the entry with the correct output index
            assert_equal(mempool1[i]['address'], expected['address'])
            if isinstance(expected['patoshis'], list):
                assert_true(mempool1[i]['patoshis'] in expected['patoshis'])
            else:
                assert_equal(mempool1[i]['patoshis'], expected['patoshis'])
            assert_equal(mempool1[i]['txid'], txid)

        # a single address can be specified as a string (not json object)
        addr1_mempool = self.nodes[0].getaddressmempool(addr1)
        assert_equal(len(addr1_mempool), 2)
        # Don't check the timestamp; it's local to the node, and can mismatch due to propagation delay.
        for idx in range(2):
            del addr1_mempool[idx]['timestamp']
            # first find this entry in the mempool1
            mempool_idx = None
            for idx1, mp_item in enumerate(mempool1):
                if mp_item['address'] == addr1_mempool[idx]['address'] and \
                   mp_item['patoshis'] == addr1_mempool[idx]['patoshis']:
                    mempool_idx = idx1
                    break
            assert_true(mempool_idx is not None, f"entry {idx} not found in mempool1")
            for key in addr1_mempool[idx].keys():
                assert_equal(mempool1[mempool_idx][key], addr1_mempool[idx][key])

        tx = self.nodes[0].getrawtransaction(txid, 1)
        assert_equal(tx['vin'][0]['address'], addr1)
        assert_equal(tx['vin'][0]['value'], 4)
        assert_equal(tx['vin'][0]['valuePat'], 4 * COIN)

        txids_a1.append(txid)
        expected_deltas.append({
            'height': 111,
            'patoshis': (-4) * COIN,
            'txid': txid,
        })
        # change goes back to the sender address
        expected_deltas.append({
            'height': 111,
            'patoshis': (4 - 3) * COIN - txFeePat,
            'txid': txid,
        })
        
        # ensure transaction is included in the next block
        self.generate_and_sync_inc(1, node_id_0)

        # the send to addr2 tx is now in a mined block, no longer in the mempool
        mempool = self.nodes[0].getaddressmempool({'addresses': [addr2, addr1]})
        assert_equal(len(mempool), 0)

        # Test DisconnectBlock() by invalidating the most recent mined block
        tip = self.nodes[1].getchaintips()[0]
        for i in range(self.num_nodes):
            node = self.nodes[i]
            # the value (4 - 1) UTXO is no longer in our balance
            # 1 is the change returned to the original address
            check_balance(i, addr1, 
                (expected_total_amount - 4 + 1) * COIN - txFeePat,
                (expected_total_amount + 1) * COIN - txFeePat)
            check_balance(i, addr2, 3 * COIN)

            assert_equal(node.getblockcount(), 111)
            node.invalidateblock(tip['hash'])
            assert_equal(node.getblockcount(), 110)

            mempool = node.getaddressmempool({'addresses': [addr2, addr1]})
            assert_equal(len(mempool), 3)

            check_balance(i, addr1, expected_total_amount * COIN)
            check_balance(i, addr2, 0)

        # now re-mine the addr1 to addr2 send
        self.generate_and_sync_inc(1, node_id_0)
        for node in self.nodes:
            assert_equal(node.getblockcount(), 111)

        mempool = self.nodes[0].getaddressmempool({'addresses': [addr2, addr1]})
        assert_equal(len(mempool), 0)

        # the value 4 UTXO is no longer in our balance
        check_balance(2, addr1,
            (expected_total_amount - 3) * COIN - txFeePat,
            (expected_total_amount + 1) * COIN - txFeePat)

        # Ensure the change from that transaction appears
        tx = self.nodes[0].getrawtransaction(txid, 1)
        change_vout = list(filter(lambda v: v['valuePat'] != 3 * COIN, tx['vout']))
        change_address = change_vout[0]['scriptPubKey']['addresses'][0]

        # by default, change should be sent to the sender address
        assert_equal(change_address, addr1)

        # test getaddressbalance
        for node in (1, 2):
            bal = self.nodes[node].getaddressbalance(change_address)
            assert_equal((expected_total_amount + 1) * COIN - txFeePat, bal['received'])

        assert_equal(self.nodes[2].getaddresstxids(addr2), [txid])

        # Further checks that limiting by height works

        # various ranges
        for i in range(5):
            height_txids = getaddresstxids(1, [addr1], 106, 106 + i)
            assert_equal(height_txids, txids_a1[0:i+1])

        height_txids = getaddresstxids(1, [addr1], 1, 108)
        assert_equal(height_txids, txids_a1[0:3])

        # Further check specifying multiple addresses
        txids_all = list(txids_a1)
        txids_all += self.nodes[1].getaddresstxids(addr_p2pkh)
        multitxids = self.nodes[1].getaddresstxids({
            'addresses': [addr1, addr_p2pkh]
        })
        # No dups in return list from getaddresstxids
        assert_equal(len(multitxids), len(set(multitxids)))

        # set(txids_all) removes its (expected) duplicates
        assert_equal(set(multitxids), set(txids_all))

        # test getaddressdeltas
        deltas = None
        for node in (1, 2):
            deltas = self.nodes[node].getaddressdeltas({'addresses': [addr1]})
            assert_equal(len(deltas), len(expected_deltas))
            # make sure change entry comes last in deltas
            if deltas[-1]['patoshis'] < 0:
                deltas[-1], deltas[-2] = deltas[-2], deltas[-1]
            for idx, delta in enumerate(deltas):
                assert_equal(delta['address'],  addr1)
                assert_equal(delta['height'],   expected_deltas[idx]['height'])
                assert_equal(delta['patoshis'], expected_deltas[idx]['patoshis'])
                assert_equal(delta['txid'],     expected_deltas[idx]['txid'])

        # 106-111 is the full range (also the default)
        deltas_limited = getaddressdeltas(1, [addr1], 106, 111)
        # make sure change entry comes last in deltas_limited
        if deltas_limited[-1]['patoshis'] < 0:
            deltas_limited[-1], deltas_limited[-2] = deltas_limited[-2], deltas_limited[-1]
        assert_equal(deltas_limited, deltas)

        # only the first element missing
        deltas_limited = getaddressdeltas(1, [addr1], 107, 111)
        if deltas_limited[-1]['patoshis'] < 0:
            deltas_limited[-1], deltas_limited[-2] = deltas_limited[-2], deltas_limited[-1]
        assert_equal(deltas_limited, deltas[1:])

        deltas_limited = getaddressdeltas(1, [addr1], 109, 109)
        if deltas_limited[-1]['patoshis'] < 0:
            deltas_limited[-1], deltas_limited[-2] = deltas_limited[-2], deltas_limited[-1]
        assert_equal(deltas_limited, deltas[3:4])

        # the full range (also the default)
        deltas_info = getaddressdeltas(1, [addr1], 106, 111, chainInfo=True)
        deltas_all = deltas_info['deltas']
        if deltas_all[-1]['patoshis'] < 0:
            deltas_all[-1], deltas_all[-2] = deltas_all[-2], deltas_all[-1]
        assert_equal(deltas_all, deltas)

        # check the additional items returned by chainInfo
        assert_equal(deltas_info['start']['height'], 106)
        block_hash = self.nodes[1].getblockhash(106)
        assert_equal(deltas_info['start']['hash'], block_hash)

        assert_equal(deltas_info['end']['height'], 111)
        block_hash = self.nodes[1].getblockhash(111)
        assert_equal(deltas_info['end']['hash'], block_hash)

        # Test getaddressutxos by comparing results with deltas
        utxos = self.nodes[2].getaddressutxos(addr1)

        # The value 4 note was spent, so won't show up in the utxo list,
        # so for comparison, remove the 4 (and -4 for output) from the
        # deltas list
        deltas = self.nodes[1].getaddressdeltas({'addresses': [addr1]})
        deltas = list(filter(lambda d: abs(d['patoshis']) != 4 * COIN, deltas))
        assert_equal(len(utxos), len(deltas))
        for i, utxo in enumerate(utxos):
            assert_equal(utxo['address'],   addr1)
            assert_equal(utxo['height'],    deltas[i]['height'])
            assert_equal(utxo['patoshis'],  deltas[i]['patoshis'])
            assert_equal(utxo['txid'],      deltas[i]['txid'])

        # Check that outputs with the same address in the same tx return one txid
        # (can't use createrawtransaction() as it combines duplicate addresses)
        # P2SH (pay-to-script-hash) address
        addr = "ttRPGkjEbsZhpew4JQjZZ2UsZhgCsbphb5b"
        addressHash = unhexlify("11695b6cd891484c2d49ec5aa738ec2b2f897777")
        scriptPubKey = CScript([OP_HASH160, addressHash, OP_EQUAL])
        # Add an unrecognized script type to vout[], a legal script that pays,
        # but won't modify the addressindex (since the address can't be extracted).
        # (This extra output has no effect on the rest of the test.)
        scriptUnknown = CScript([OP_HASH160, OP_DUP, OP_DROP, addressHash, OP_EQUAL])
        unspent = list(filter(lambda u: u['amount'] >= 4, self.nodes[0].listunspent()))
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]['txid'], 16), unspent[0]['vout']))]
        tx.vout = [
            CTxOut(1 * COIN, scriptPubKey),
            CTxOut(2 * COIN, scriptPubKey),
            CTxOut(7 * COIN, scriptUnknown),
        ]
        tx = self.nodes[0].signrawtransaction(hexlify(tx.serialize()).decode('utf-8'))
        txid = self.nodes[0].sendrawtransaction(tx['hex'], True)
        self.generate_and_sync_inc(1, node_id_0)

        assert_equal(self.nodes[1].getaddresstxids(addr), [txid])
        check_balance(2, addr, 3 * COIN)


if __name__ == '__main__':
    AddressIndexTest().main()
