#!/usr/bin/env python3
# Copyright (c) 2020 SuperNET developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

# this is a test to check a new index for unspent cc utxos activated with -unspentccindex

import pytest
import time
from slickrpc.exc import RpcException as RPCError
from util import assert_success, assert_error, mine_and_waitconfirms,\
    send_and_mine, rpc_connect, wait_some_blocks, komodo_teardown


@pytest.mark.usefixtures("proxy_connection")
def test_ccindex(test_params):

    # test params inits
    rpc = test_params.get('node1').get('rpc')
    rpc1 = test_params.get('node2').get('rpc')

    pubkey = test_params.get('node1').get('pubkey')
    pubkey1 = test_params.get('node2').get('pubkey')

    is_fresh_chain = test_params.get("is_fresh_chain")

    result = rpc.tokenaddress()
    assert_success(result)
    for x in result.keys():
        if x.find('ddress') > 0:
            assert result[x][0] == 'R'

    # get token cc address for pubkey:
    result = rpc.tokenaddress(pubkey)
    assert_success(result)
    pubkeyTokenCCAddress = ""
    for x in result.keys():
        if x.find('ddress') > 0:
            assert result[x][0] == 'R'
            if x == 'pubkey Tokens CC Address':
                pubkeyTokenCCAddress = result[x]

    # get token cc address for pubkey1:
    result = rpc.tokenaddress(pubkey1)
    assert_success(result)
    pubkey1TokenCCAddress = ""
    for x in result.keys():
        if x.find('ddress') > 0:
            assert result[x][0] == 'R'
            if x == 'pubkey Tokens CC Address':
                pubkey1TokenCCAddress = result[x]

    # there are no tokens created yet
    # TODO: this test conflicts with heir test because token creating for heir
#    if is_fresh_chain:
#        result = rpc.tokenlist()
#        assert result == []

    print("stop mining to test txns in mempool")
    rpc.setgenerate(False)
    rpc1.setgenerate(False)

    print("creating test token...")
    result = rpc.tokencreate("T1", "20", "token for testing cc index")
    assert_success(result)

    tokenid = rpc.sendrawtransaction(result['hex'])

    # check mempool index:
    print("check cc index for tokencreate in mempool...")

    # check one utxo in the index
    result = rpc.listccunspents(pubkeyTokenCCAddress, tokenid)
    # print('type(result)=' + str(type(result)))
    assert(str(type(result)) == "<class 'list'>")
    assert(len(result) == 1)
    assert(result[0]['creationId'] == tokenid)
    assert(result[0]['txhash'] == tokenid)
    assert(result[0]['satoshis'] == 20*100000000)
    assert(result[0]['funcid'].upper() == "C")
    assert(result[0]['blockHeight'] == 0)   # in mempool

    print("check cc index for tokentransfer in mempool...")

    result = rpc.tokentransfer(tokenid, pubkey1, "1")
    assert_success(result)
    txid = rpc.sendrawtransaction(result['hex'])

    # check two utxos in the mempool index

    result = rpc.listccunspents(pubkeyTokenCCAddress, tokenid)
    assert(str(type(result)) == "<class 'list'>")

    # note: once I got assert failed here: 
    # 'E       assert 0 == 1'
    # probably it happened because setgenerate(False) had not finished atm
    assert(len(result) == 1)
    assert(result[0]['creationId'] == tokenid)
    assert(result[0]['txhash'] == txid)
    assert(result[0]['satoshis'] == 20*100000000-1)
    assert(result[0]['funcid'].upper() == "T")
    assert(result[0]['blockHeight'] == 0)   # in mempool

    result = rpc.listccunspents(pubkey1TokenCCAddress, tokenid)
    assert(str(type(result)) == "<class 'list'>")
    assert(len(result) == 1)
    assert(result[0]['creationId'] == tokenid)
    assert(result[0]['txhash'] == txid)
    assert(result[0]['satoshis'] == 1)
    assert(result[0]['funcid'].upper() == "T")
    assert(result[0]['blockHeight'] == 0)   # in mempool

    print("mine txns and check cc index in db...")

    rpc.setgenerate(True, 1)
    while True:
        print("waiting until mempool empty...")
        time.sleep(10)
        mempool = rpc.getrawmempool()
        if len(mempool) == 0:
            break    

    result = rpc.listccunspents(pubkeyTokenCCAddress, tokenid)
    assert(str(type(result)) == "<class 'list'>")
    assert(len(result) == 1)
    assert(result[0]['creationId'] == tokenid)
    assert(result[0]['txhash'] == txid)
    assert(result[0]['satoshis'] == 20*100000000-1)
    assert(result[0]['funcid'].upper() == "T")
    assert(result[0]['blockHeight'] != 0)   # not in mempool

    result = rpc.listccunspents(pubkey1TokenCCAddress, tokenid)
    assert(str(type(result)) == "<class 'list'>")
    assert(len(result) == 1)
    assert(result[0]['creationId'] == tokenid)
    assert(result[0]['txhash'] == txid)
    assert(result[0]['satoshis'] == 1)
    assert(result[0]['funcid'].upper() == "T")
    assert(result[0]['blockHeight'] != 0)   # not in mempool

    print("test cc index okay")

