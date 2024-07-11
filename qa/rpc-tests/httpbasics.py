#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Copyright (c) 2018-2024 The Pastel Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test rpc http basics
#
import http.client
import urllib.parse

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_nodes, str_to_b64str

MAX_URI_LENGTH = 1024
class HTTPBasicsTest(BitcoinTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 3
        self.setup_clean_chain = False

    def setup_nodes(self):
        return start_nodes(self.num_nodes, self.options.tmpdir)

    def setup_network(self):
        self.nodes = self.setup_nodes()

    def run_test(self):

        #################################################
        # lowlevel check for http persistent connection #
        #################################################
        url = urllib.parse.urlparse(self.nodes[0].url)
        authpair = url.username + ':' + url.password
        headers = {"Authorization": "Basic " + str_to_b64str(authpair)}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert_equal(b'"error":null' in out1, True)
        assert_equal(conn.sock!=None, True) # according to http/1.1 connection must still be open!

        # send 2nd request without closing connection
        conn.request('POST', '/', '{"method": "getchaintips"}', headers)
        out2 = conn.getresponse().read()
        assert_equal(b'"error":null' in out2, True) # must also response with a correct json-rpc message
        assert_equal(conn.sock!=None, True) # according to http/1.1 connection must still be open!
        conn.close()

        # same should be if we add keep-alive because this should be the std. behaviour
        headers = {"Authorization": "Basic " + str_to_b64str(authpair), "Connection": "keep-alive"}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert_equal(b'"error":null' in out1, True)
        assert_equal(conn.sock!=None, True) # according to http/1.1 connection must still be open!

        # send 2nd request without closing connection
        conn.request('POST', '/', '{"method": "getchaintips"}', headers)
        out2 = conn.getresponse().read()
        assert_equal(b'"error":null' in out2, True) # must also response with a correct json-rpc message
        assert_equal(conn.sock!=None, True) # according to http/1.1 connection must still be open!
        conn.close()

        # now do the same with "Connection: close"
        headers = {"Authorization": "Basic " + str_to_b64str(authpair), "Connection":"close"}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert_equal(b'"error":null' in out1, True)
        assert_equal(conn.sock!=None, False) # now the connection must be closed after the response

        # node1 (2nd node) is running with disabled keep-alive option
        urlNode1 = urllib.parse.urlparse(self.nodes[1].url)
        authpair = urlNode1.username + ':' + urlNode1.password
        headers = {"Authorization": "Basic " + str_to_b64str(authpair)}

        conn = http.client.HTTPConnection(urlNode1.hostname, urlNode1.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert_equal(b'"error":null' in out1, True)

        # node2 (third node) is running with standard keep-alive parameters which means keep-alive is on
        urlNode2 = urllib.parse.urlparse(self.nodes[2].url)
        authpair = urlNode2.username + ':' + urlNode2.password
        headers = {"Authorization": "Basic " + str_to_b64str(authpair)}

        conn = http.client.HTTPConnection(urlNode2.hostname, urlNode2.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        out1 = conn.getresponse().read()
        assert_equal(b'"error":null' in out1, True)
        assert_equal(conn.sock!=None, True) # connection must be closed because pasteld should use keep-alive by default

        # Check excessive request size
        conn = http.client.HTTPConnection(urlNode2.hostname, urlNode2.port)
        conn.connect()
        conn.request('GET', '/' + ('x'* (MAX_URI_LENGTH + 100)), '', headers)
        out1 = conn.getresponse()
        assert_equal(out1.status, http.client.REQUEST_URI_TOO_LONG)

if __name__ == '__main__':
    HTTPBasicsTest().main()
