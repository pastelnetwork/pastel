#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test REST interface
#

import struct
import binascii
import json
from io import BytesIO
from codecs import encode
from decimal import Decimal, getcontext
getcontext().prec = 16
import http.client
import urllib.parse

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal, 
    assert_greater_than,
    start_nodes,
    connect_nodes_bi,
)


def deser_uint256(f):
    r = 0
    for i in range(8):
        t = struct.unpack(b"<I", f.read(4))[0]
        r += t << (i * 32)
    return r

# allows simple http get calls
def http_get_call(host, port, path, response_object = 0):
    conn = http.client.HTTPConnection(host, port)
    conn.request('GET', path)

    if response_object:
        return conn.getresponse()

    return conn.getresponse().read().decode("utf-8")

# allows simple http post calls with a request body
def http_post_call(host, port, path, requestdata = '', response_object = 0):
    conn = http.client.HTTPConnection(host, port)
    conn.request('POST', path, requestdata)

    if response_object:
        return conn.getresponse()

    return conn.getresponse().read()

class RESTTest (BitcoinTestFramework):
    FORMAT_SEPARATOR = "."

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 3

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir)
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        url = urllib.parse.urlparse(self.nodes[0].url)
        print("Mining blocks...")

        self.generate_and_sync_inc(1)
        self.generate_and_sync_inc(100, 2)

        assert_equal(self.nodes[0].getbalance(), self._reward)

        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.generate_and_sync_inc(1, 2)
        bb_hash = self.nodes[0].getbestblockhash()

        assert_equal(self.nodes[1].getbalance(), Decimal("0.1")) # balance now should be 0.1 on node 1

        # load the latest 0.1 tx over the REST API
        json_string = http_get_call(url.hostname, url.port, '/rest/tx/'+txid+self.FORMAT_SEPARATOR+"json")
        json_obj = json.loads(json_string)
        vintx = json_obj['vin'][0]['txid'] # get the vin to later check for utxo (should be spent by then)
        # get n of 0.1 outpoint
        n = 0
        for vout in json_obj['vout']:
            if vout['value'] == 0.1:
                n = vout['n']


        #######################################
        # GETUTXOS: query an unspent outpoint #
        #######################################
        json_request = '/checkmempool/'+txid+'-'+str(n)
        json_string = http_get_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)

        #check chainTip response
        assert_equal(json_obj['chaintipHash'], bb_hash)

        #make sure there is one utxo
        assert_equal(len(json_obj['utxos']), 1)
        assert_equal(json_obj['utxos'][0]['value'], 0.1)


        #################################################
        # GETUTXOS: now query an already spent outpoint #
        #################################################
        json_request = '/checkmempool/'+vintx+'-0'
        json_string = http_get_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)

        # check chainTip response
        assert_equal(json_obj['chaintipHash'], bb_hash)

        # make sure there is no utox in the response because this oupoint has been spent
        assert_equal(len(json_obj['utxos']), 0)

        # check bitmap
        assert_equal(json_obj['bitmap'], "0")


        ##################################################
        # GETUTXOS: now check both with the same request #
        ##################################################
        json_request = '/checkmempool/'+txid+'-'+str(n)+'/'+vintx+'-0'
        json_string = http_get_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        assert_equal(len(json_obj['utxos']), 1)
        assert_equal(json_obj['bitmap'], "10")

        # test binary response
        bb_hash = self.nodes[0].getbestblockhash()

        binaryRequest = b'\x01\x02'
        binaryRequest += binascii.unhexlify(txid)
        binaryRequest += struct.pack("i", n);
        binaryRequest += binascii.unhexlify(vintx);
        binaryRequest += struct.pack("i", 0);

        bin_response = http_post_call(url.hostname, url.port, '/rest/getutxos'+self.FORMAT_SEPARATOR+'bin', binaryRequest)
        output = BytesIO()
        output.write(bin_response)
        output.seek(0)
        chainHeight = struct.unpack("i", output.read(4))[0]
        hashFromBinResponse = hex(deser_uint256(output))[2:].zfill(64)

        assert_equal(bb_hash, hashFromBinResponse) # check if getutxo's chaintip during calculation was fine
        assert_equal(chainHeight, 102) # chain height must be 102


        ############################
        # GETUTXOS: mempool checks #
        ############################

        # do a tx and don't sync
        txid = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
        self.generate_and_sync_inc(1)

        json_string = http_get_call(url.hostname, url.port, '/rest/tx/'+txid+self.FORMAT_SEPARATOR+"json")
        json_obj = json.loads(json_string)
        vintx = json_obj['vin'][0]['txid'] # get the vin to later check for utxo (should be spent by then)
        # get n of 0.1 outpoint
        n = 0
        for vout in json_obj['vout']:
            if vout['value'] == 0.1:
                n = vout['n']

        json_request = '/'+txid+'-'+str(n)
        json_string = http_get_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        assert_equal(len(json_obj['utxos']), 0) # there should be an outpoint because it has just added to the mempool

        json_request = '/checkmempool/'+txid+'-'+str(n)
        json_string = http_get_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        assert_equal(len(json_obj['utxos']), 1) # there should be an outpoint because it has just added to the mempool

        # do some invalid requests
        json_request = '{"checkmempool'
        response = http_post_call(url.hostname, url.port, '/rest/getutxos'+self.FORMAT_SEPARATOR+'json', json_request, True)
        assert_equal(response.status, 500) # must be a 500 because we send an invalid json request

        json_request = '{"checkmempool'
        response = http_post_call(url.hostname, url.port, '/rest/getutxos'+self.FORMAT_SEPARATOR+'bin', json_request, True)
        assert_equal(response.status, 500) # must be a 500 because we send an invalid bin request

        response = http_post_call(url.hostname, url.port, '/rest/getutxos/checkmempool'+self.FORMAT_SEPARATOR+'bin', '', True)
        assert_equal(response.status, 500) # must be a 500 because we send an invalid bin request

        # test limits
        json_request = '/checkmempool/'
        for x in range(0, 20):
            json_request += txid+'-'+str(n)+'/'
        json_request = json_request.rstrip("/")
        response = http_post_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json', '', True)
        assert_equal(response.status, 500) # must be a 500 because we exceeding the limits

        json_request = '/checkmempool/'
        for x in range(0, 15):
            json_request += txid+'-'+str(n)+'/'
        json_request = json_request.rstrip("/");
        response = http_post_call(url.hostname, url.port, '/rest/getutxos'+json_request+self.FORMAT_SEPARATOR+'json', '', True)
        assert_equal(response.status, 200) # must be a 500 because we exceeding the limits

        # generate block to not affect upcoming tests
        self.generate_and_sync_inc(1)

        ################
        # /rest/block/ #
        ################

        # Block header length of 177 is:
        # - 108 bytes: regular fields
        # -  32 bytes: nonce
        # -  37 bytes: Equihash solution:
        #              - 1 byte length
        #              - 2^k ((n/(k+1))+1)-bit indices.
        #                For regtest parameters (n = 48, k = 5),
        #                this is 32 9-bit indices

        # check binary format
        response = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+"bin", True)
        assert_equal(response.status, 200)
        assert_greater_than(int(response.getheader('content-length')), 177)
        response_str = response.read()

        # compare with block header
        response_header = http_get_call(url.hostname, url.port, '/rest/headers/1/'+bb_hash+self.FORMAT_SEPARATOR+"bin", True)
        assert_equal(response_header.status, 200)
        assert_equal(int(response_header.getheader('content-length')), 179)
        response_header_str = response_header.read()
        assert_equal(response_str[0:179], response_header_str)

        # check block hex format
        response_hex = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(response_hex.status, 200)
        assert_greater_than(int(response_hex.getheader('content-length')), 354)
        response_hex_str = response_hex.read()
        assert_equal(encode(response_str, "hex_codec")[0:354], response_hex_str[0:354])

        # compare with hex block header
        response_header_hex = http_get_call(url.hostname, url.port, '/rest/headers/1/'+bb_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(response_header_hex.status, 200)
        assert_greater_than(int(response_header_hex.getheader('content-length')), 354)
        response_header_hex_str = response_header_hex.read()
        assert_equal(response_hex_str[0:354], response_header_hex_str[0:354])
        assert_equal(encode(response_header_str, "hex-codec")[0:354], response_header_hex_str[0:354])

        # check json format
        block_json_string = http_get_call(url.hostname, url.port, '/rest/block/'+bb_hash+self.FORMAT_SEPARATOR+'json')
        block_json_obj = json.loads(block_json_string)
        assert_equal(block_json_obj['hash'], bb_hash)

        # compare with json block header
        response_header_json = http_get_call(url.hostname, url.port, '/rest/headers/1/'+bb_hash+self.FORMAT_SEPARATOR+"json", True)
        assert_equal(response_header_json.status, 200)
        response_header_json_str = response_header_json.read().decode('utf-8')
        json_obj = json.loads(response_header_json_str, parse_float=Decimal)
        assert_equal(len(json_obj), 1) # ensure that there is one header in the json response
        assert_equal(json_obj[0]['hash'], bb_hash) # request/response hash should be the same

        # compare with normal RPC block response
        rpc_block_json = self.nodes[0].getblock(bb_hash)
        assert_equal(json_obj[0]['hash'],               rpc_block_json['hash'])
        assert_equal(json_obj[0]['confirmations'],      rpc_block_json['confirmations'])
        assert_equal(json_obj[0]['height'],             rpc_block_json['height'])
        assert_equal(json_obj[0]['version'],            rpc_block_json['version'])
        assert_equal(json_obj[0]['merkleroot'],         rpc_block_json['merkleroot'])
        assert_equal(json_obj[0]['time'],               rpc_block_json['time'])
        assert_equal(json_obj[0]['nonce'],              rpc_block_json['nonce'])
        assert_equal(json_obj[0]['bits'],               rpc_block_json['bits'])
        assert_equal(json_obj[0]['difficulty'],         rpc_block_json['difficulty'])
        assert_equal(json_obj[0]['chainwork'],          rpc_block_json['chainwork'])
        assert_equal(json_obj[0]['previousblockhash'],  rpc_block_json['previousblockhash'])

        # see if we can get 5 headers in one response
        self.generate_and_sync_inc(5, 1)
        response_header_json = http_get_call(url.hostname, url.port, '/rest/headers/5/'+bb_hash+self.FORMAT_SEPARATOR+"json", True)
        assert_equal(response_header_json.status, 200)
        response_header_json_str = response_header_json.read().decode('utf-8')
        json_obj = json.loads(response_header_json_str)
        assert_equal(len(json_obj), 5) # now we should have 5 header objects

        # do tx test
        tx_hash = block_json_obj['tx'][0]['txid'];
        json_string = http_get_call(url.hostname, url.port, '/rest/tx/'+tx_hash+self.FORMAT_SEPARATOR+"json")
        json_obj = json.loads(json_string)
        assert_equal(json_obj['txid'], tx_hash)

        # check hex format response
        hex_string = http_get_call(url.hostname, url.port, '/rest/tx/'+tx_hash+self.FORMAT_SEPARATOR+"hex", True)
        assert_equal(hex_string.status, 200)
        assert_greater_than(int(response.getheader('content-length')), 10)



        # check block tx details
        # let's make 3 tx and mine them on node 1
        txs = []
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1))
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1))
        txs.append(self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1))
        self.sync_all()

        # check that there are exactly 3 transactions in the TX memory pool before generating the block
        json_string = http_get_call(url.hostname, url.port, '/rest/mempool/info'+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        assert_equal(json_obj['size'], 3)
        # the size of the memory pool should be greater than 3x ~100 bytes
        assert_greater_than(json_obj['bytes'], 300)

        # check that there are our submitted transactions in the TX memory pool
        json_string = http_get_call(url.hostname, url.port, '/rest/mempool/contents'+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        for tx in txs:
            assert_equal(tx in json_obj, True)

        # now mine the transactions
        newblockhash = self.nodes[1].generate(1)
        self.sync_all()

        # check if the 3 tx show up in the new block
        json_string = http_get_call(url.hostname, url.port, '/rest/block/'+newblockhash[0]+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        for tx in json_obj['tx']:
            if not 'coinbase' in tx['vin'][0]: # exclude coinbase
                assert_equal(tx['txid'] in txs, True)

        # check the same but without tx details
        json_string = http_get_call(url.hostname, url.port, '/rest/block/notxdetails/'+newblockhash[0]+self.FORMAT_SEPARATOR+'json')
        json_obj = json.loads(json_string)
        for tx in txs:
            assert_equal(tx in json_obj['tx'], True)

        # test rest bestblock
        bb_hash = self.nodes[0].getbestblockhash()

        json_string = http_get_call(url.hostname, url.port, '/rest/chaininfo.json')
        json_obj = json.loads(json_string)
        assert_equal(json_obj['bestblockhash'], bb_hash)

if __name__ == '__main__':
    RESTTest().main()
