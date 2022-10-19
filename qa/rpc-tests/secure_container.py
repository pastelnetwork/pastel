#!/usr/bin/env python3
# Copyright (c) 2018-2022 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.
import base64
from decimal import getcontext

from test_framework.util import (
    assert_equal,
    assert_raises_rpc,
    assert_true,
    assert_raises,
    assert_raises_message,
    assert_shows_help,
    start_nodes,
    connect_nodes_bi
)
from pastel_test_framework import PastelTestFramework
from test_framework.authproxy import JSONRPCException
import test_framework.rpc_consts as rpc

getcontext().prec = 16

class SecureContainerTest(PastelTestFramework):
    pastelid1 = None
    id1_lrkey = None
    pastelid2 = None
    pastelid3 = None
    id3_lrkey = None

    def __init__(self):
        super().__init__()
        self.num_nodes = 2

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
                                 extra_args=[[]] * self.num_nodes)
        self.is_network_split = False
        connect_nodes_bi(self.nodes,0,1)

    def run_test(self):
        print("---- Pastel ID tests STARTED ----")
        print(" -pastelid help")
        assert_shows_help(self.nodes[0].pastelid)

        self.nodes[1].getnewaddress()
        taddr1 = self.nodes[1].getnewaddress()

        print(" -pastelid newkey")
        assert_shows_help(self.nodes[0].pastelid, "newkey")
        self.pastelid1, self.id1_lrkey = self.create_pastelid(0)
        self.pastelid2 = self.create_pastelid()[0]
        self.pastelid3, self.id3_lrkey = self.create_pastelid(1)
        print(f"pastelid1: {self.pastelid1}")
        print(f"pastelid2: {self.pastelid2}")
        print(f"pastelid3: {self.pastelid3}")

        # fail if empty passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "passphrase for new key cannot be empty", 
            self.nodes[0].pastelid, "newkey", "")

        # List all internally stored Pastel ID and keys
        print(" -pastelid list")
        # check Pastel IDs on node0
        id_list0 = self.nodes[0].pastelid("list")
        id_list0 = dict((key+str(i), val) for i, k in enumerate(id_list0) for key, val in k.items())
        assert_true(self.pastelid1 in id_list0.values(), f"Pastel ID {self.pastelid1} not in the list")
        assert_true(self.pastelid2 in id_list0.values(), f"Pastel ID {self.pastelid2} not in the list")
        # check Pastel IDs on node1
        id_list1 = self.nodes[1].pastelid("list")
        id_list1 = dict((key+str(i), val) for i, k in enumerate(id_list1) for key, val in k.items())
        assert_true(self.pastelid3 in id_list1.values(), f"Pastel ID {self.pastelid3} not in the list")

        print(" -pastelid sign & verify ed448")
        text_to_sign = "my text to sign"
        # Sign "text" with the internally stored private key associated with the Pastel ID
        # check that signing with existing passphrase works, default algorithm - EdDSA448
        signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.passphrase)["signature"]
        assert_true(signature, "Cannot sign text using existing passphrase. No ed448 signature was created")
        assert_equal(len(base64.b64decode(signature)), 114)
        # Verify text"'s "signature" (EdDSA448) with the Pastel ID
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid1)["verification"]
        assert_equal(result, "OK")
        # Fail to verify EdDSA448 signature with the different key (Pastel ID 2)
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid2)["verification"]
        assert_equal(result, "Failed")
        # Fail to verify modified text (ed448 signature)
        text_to_sign_modified = 'X' + text_to_sign[1:]
        result = self.nodes[0].pastelid("verify", text_to_sign_modified, signature, self.pastelid1)["verification"]
        assert_equal(result, "Failed")
        # try to sign using Pastel ID with invalid passphrase
        assert_raises_rpc(rpc.RPC_MISC_ERROR, self.ERR_READ_PASTELID_FILE,
            self.nodes[1].pastelid, "sign", text_to_sign, self.pastelid1, self.new_passphrase)

        print(" -pastelid sign & verify legroast")
        # Sign with no errors using encoded LegRoast public key
        # returns base64 encoded signature
        lr_signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.passphrase, "legroast")["signature"]
        assert_true(lr_signature, "Cannot sign text using existing passphrase. No LegRoast signature was created")
        assert_equal(len(base64.b64decode(lr_signature)), 14272)
        # Verify text"'s "signature" (LegRoast) with the Pastel ID
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid1, "legroast")["verification"]
        assert_equal(result, "OK")
        # Fail to verify LegRoast signature with the different key (Pastel ID2)
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid2, "legroast")["verification"]
        assert_equal(result, "Failed")
        # Fail to verify modified text (LegRoast signature)
        result = self.nodes[0].pastelid("verify", text_to_sign_modified, lr_signature, self.pastelid1, "legroast")["verification"]
        assert_equal(result, "Failed")
        
        # Sign message on node1 with the LegRoast key associated with pastelid3 
        lr_signature = self.nodes[1].pastelid("sign", text_to_sign, self.pastelid3, self.passphrase, "legroast")["signature"]
        assert_true(lr_signature, "Cannot sign text on node1 with LegRoast key associated with pastelid3. No LegRoast signature was created")
        # ... but verify it on node0 that does not have pastelid3 and we don't have any Pastel ID reg tickets
        assert_raises_rpc(rpc.RPC_MISC_ERROR, "is not stored locally and Pastel ID registration ticket was not found in the blockchain",
            self.nodes[0].pastelid, "verify", text_to_sign, lr_signature, self.pastelid3, "legroast")
        # now let's register pastelid3
        self.generate_and_sync_inc(10)
        # send all utxos from node #3 to addr[0] to make empty balance
        self.nodes[0].sendtoaddress(taddr1, self.nodes[0].getbalance(), "empty node0", "test", True)
        self.generate_and_sync_inc(1)
        # register pastelid3
        txid = self.nodes[1].tickets("register", "id", self.pastelid3, self.passphrase, taddr1)
        assert_true(txid, "pastelid3 registration failed")
        self.generate_and_sync_inc(1)

        # now we should be able to retrieve lr pubkey for pastelid3 on node0
        # but first make sure pastelid3 is not stored locally on node0
        assert_true(self.pastelid3 not in id_list0.values(), f"Pastel ID3 {self.pastelid3} should not be stored on node0")
           # Verify text"'s "signature" (LegRoast) with the Pastel ID3
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid3, "legroast")["verification"]
        assert_equal(result, "OK")
         # Fail to verify LegRoast signature with the different key (Pastel ID1)
        result = self.nodes[0].pastelid("verify", text_to_sign, lr_signature, self.pastelid1, "legroast")["verification"]
        assert_equal(result, "Failed")
        # Fail to verify modified text (LegRoast signature)
        result = self.nodes[0].pastelid("verify", text_to_sign_modified, lr_signature, self.pastelid3, "legroast")["verification"]
        assert_equal(result, "Failed")
       

        print(" -pastelid passwd")
        assert_shows_help(self.nodes[0].pastelid, "passwd")
        # missing new passphrase
        assert_raises(JSONRPCException, self.nodes[0].pastelid, "passwd", self.pastelid1, self.passphrase)
        # empty new passphrase
        assert_raises_message(JSONRPCException, "cannot be empty", 
            self.nodes[0].pastelid, "passwd", self.pastelid1, self.passphrase, "")
        # empty Pastel ID
        assert_raises_message(JSONRPCException, "cannot be empty", 
            self.nodes[0].pastelid, "passwd", "", self.passphrase, self.new_passphrase)
        # empty passphrase
        assert_raises_message(JSONRPCException, "cannot be empty", 
            self.nodes[0].pastelid, "passwd", self.pastelid1, "", self.new_passphrase)
        # change passphrase
        result = self.nodes[0].pastelid("passwd", self.pastelid1, self.passphrase, self.new_passphrase)["result"]
        assert_equal(result, "successful")
        # try to sign text using old passphrase
        assert_raises_message(JSONRPCException, "Failed to decrypt", self.nodes[0].pastelid, "sign", text_to_sign, self.pastelid1, self.passphrase)
        # signing using new passphrase should work
        signature = self.nodes[0].pastelid("sign", text_to_sign, self.pastelid1, self.new_passphrase)["signature"]
        assert_true(signature, "Cannot sign text using existing passphrase. No ed448 signature was created")
        # verify signature
        result = self.nodes[0].pastelid("verify", text_to_sign, signature, self.pastelid1)["verification"]
        assert_equal(result, "OK")
        print("----- Pastel ID tests FINISHED -----")

if __name__ == '__main__':
    SecureContainerTest().main()