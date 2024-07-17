"""
  Copyright 2011 Jeff Garzik

  AuthServiceProxy has the following improvements over python-jsonrpc's
  ServiceProxy class:

  - HTTP connections persist for the life of the AuthServiceProxy object
    (if server supports HTTP/1.1)
  - sends protocol 'version', per JSON-RPC 1.1
  - sends proper, incrementing 'id'
  - sends Basic HTTP authentication headers
  - parses all JSON numbers that look like floats as Decimal
  - uses standard Python json lib

  Previous copyright, from python-jsonrpc/jsonrpc/proxy.py:

  Copyright (c) 2007 Jan-Klaas Kollhof

  This file is part of jsonrpc.

  jsonrpc is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  This software is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this software; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""

import base64
import decimal
from http.client import (
    HTTPConnection,
    HTTPSConnection,
    BadStatusLine,
    RemoteDisconnected,
)
from urllib.parse import urlparse
import logging
import simplejson as json
import time

USER_AGENT = "AuthServiceProxy/0.1"

HTTP_TIMEOUT_IN_SECS = 1000
HTTP_MAX_RETRIES = 5
HTTP_RETRY_DELAY_SECS = 2


log = logging.getLogger("PastelRPC")

class JSONRPCException(Exception):
    def __init__(self, rpc_error):
        try:
            errmsg = '%(message)s (%(code)i)' % rpc_error
        except (KeyError, TypeError):
            errmsg = ''
        super().__init__(errmsg)
        self.error = rpc_error


def EncodeDecimal(o):
    if isinstance(o, decimal.Decimal):
        return str(o)
    raise TypeError(repr(o) + " is not JSON serializable")

class AuthServiceProxy():
    __id_count = 0
    # index in nodes list
    index: int = None

    """ Return node alias based on index in node list.

    Returns:
        str: node alias
    """
    @property
    def alias(self) -> str:
        return str(self.index)
        
        
    def __init__(self, index: int, service_url, service_name=None, timeout=HTTP_TIMEOUT_IN_SECS, connection=None):
        self.__service_url = service_url
        self.__service_name = service_name
        self.__url = urlparse(service_url)
        self.index = index
        (user, passwd) = (self.__url.username, self.__url.password)
        try:
            user = user.encode('utf8')
        except AttributeError:
            pass
        try:
            passwd = passwd.encode('utf8')
        except AttributeError:
            pass
        authpair = user + b':' + passwd
        self.__auth_header = b'Basic ' + base64.b64encode(authpair)

        self.timeout = timeout
        self._set_conn(connection)


    def _set_conn(self, connection=None):
        if self.__url.port is None:
            port = 80
        else:
            port = self.__url.port
        if connection:
            # Callables re-use the connection of the original proxy
            self.__conn = connection
            self.timeout = connection.timeout
        elif self.__url.scheme == 'https':
            self.__conn = HTTPSConnection(self.__url.hostname, port, timeout=self.timeout)
        else:
            self.__conn = HTTPConnection(self.__url.hostname, port, timeout=self.timeout)


    def __getattr__(self, name):
        if name.startswith('__') and name.endswith('__'):
            # Python internal stuff
            raise AttributeError
        if self.__service_name is not None:
            name = "%s.%s" % (self.__service_name, name)
        return AuthServiceProxy(self.index, self.__service_url, name, connection=self.__conn)


    def _request(self, method, path, postdata):
        '''
        Do a HTTP request, with retry if we get disconnected (e.g. due to a timeout).
        This is a workaround for https://bugs.python.org/issue3566 which is fixed in Python 3.5.
        '''
        headers = {'Host': self.__url.hostname,
                   'User-Agent': USER_AGENT,
                   'Authorization': self.__auth_header,
                   'Connection': 'keep-alive',
                   'Content-type': 'application/json'}
        for attempt in range(HTTP_MAX_RETRIES):
            try:
                self.__conn.request(method, path, postdata, headers)
                return self._get_response()
            except (RemoteDisconnected, BadStatusLine, ConnectionRefusedError, BrokenPipeError, ConnectionResetError) as e:
                if attempt < HTTP_MAX_RETRIES - 1:
                    log.warning(f"Attempt {attempt + 1} failed: {e}. Retrying in {HTTP_RETRY_DELAY_SECS} seconds...")
                    self.__conn.close()
                    time.sleep(HTTP_RETRY_DELAY_SECS)
                else:
                    log.error(f"All {HTTP_MAX_RETRIES} attempts failed. Raising exception.")
                    raise
            except Exception as e:
                log.error(f"Unhandled exception: {e}. Raising exception.")
                raise                


    def __call__(self, *args):
        AuthServiceProxy.__id_count += 1

        if self.alias is not None:
            log.debug("[%s] -%s-> %s %s" % (self.alias, AuthServiceProxy.__id_count, self.__service_name, json.dumps(args, default=EncodeDecimal)))
        else:            
            log.debug("-%s-> %s %s" % (AuthServiceProxy.__id_count, self.__service_name, json.dumps(args, default=EncodeDecimal)))
        postdata = json.dumps({'version': '1.1',
                               'method': self.__service_name,
                               'params': args,
                               'id': AuthServiceProxy.__id_count}, default=EncodeDecimal)
        response = self._request('POST', self.__url.path, postdata)
        if response['error'] is not None:
            raise JSONRPCException(response['error'])
        elif 'result' not in response:
            raise JSONRPCException({
                'code': -343, 'message': 'missing JSON-RPC result'})
        else:
            return response['result']


    def _batch(self, rpc_call_list):
        postdata = json.dumps(list(rpc_call_list), default=EncodeDecimal)
        log.debug("--> "+postdata)
        return self._request('POST', self.__url.path, postdata)


    def _get_response(self):
        http_response = self.__conn.getresponse()
        if http_response is None:
            raise JSONRPCException({
                'code': -342, 'message': 'missing HTTP response from server'})

        content_type = http_response.getheader('Content-Type')
        if content_type != 'application/json':
            raise JSONRPCException({
                'code': -342, 'message': 'non-JSON HTTP response with \'%i %s\' from server' % (http_response.status, http_response.reason)})

        responsedata = http_response.read().decode('utf8')
        response = json.loads(responsedata, parse_float=decimal.Decimal)
        if self.alias is not None:
            if "error" in response and response["error"] is None:
                log.debug("[%s] <-%s- %s" % (self.alias, response["id"], json.dumps(response["result"], default=EncodeDecimal)))
            else:
                log.debug("[%s] <-- " % {self.alias, responsedata})
        else:
            if "error" in response and response["error"] is None:
                log.debug("<-%s- %s"%(response["id"], json.dumps(response["result"], default=EncodeDecimal)))
            else:
                log.debug("<-- "+responsedata)
        return response
