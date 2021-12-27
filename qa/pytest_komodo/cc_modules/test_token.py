#!/usr/bin/env python3
# Copyright (c) 2020 SuperNET developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php.

import pytest
import time
from slickrpc.exc import RpcException as RPCError
from util import assert_success, assert_error, mine_and_waitconfirms,\
    send_and_mine, rpc_connect, wait_some_blocks, komodo_teardown


def call_token_rpc(rpc, rpcname, *args) :
    rpcfunc = getattr(rpc, rpcname)
    return rpcfunc(*args)



@pytest.mark.usefixtures("proxy_connection")
def test_token(test_params):

    # test params inits
    rpc = test_params.get('node1').get('rpc')
    rpc1 = test_params.get('node2').get('rpc')

    pubkey = test_params.get('node1').get('pubkey')
    pubkey1 = test_params.get('node2').get('pubkey')

    is_fresh_chain = test_params.get("is_fresh_chain")

    for v in ["", "v2"] :

        result = call_token_rpc(rpc, "token"+v+"address")
        assert_success(result)
        for x in result.keys():
            if x.find('ddress') > 0:
                assert result[x][0] == 'R'

        result = call_token_rpc(rpc, "token"+v+"address", pubkey)
        assert_success(result)
        for x in result.keys():
            if x.find('ddress') > 0:
                assert result[x][0] == 'R'

        result = call_token_rpc(rpc, "assetsaddress")
        assert_success(result)
        for x in result.keys():
            if x.find('ddress') > 0:
                assert result[x][0] == 'R'

        result = call_token_rpc(rpc, "assetsaddress", pubkey)
        assert_success(result)
        for x in result.keys():
            if x.find('ddress') > 0:
                assert result[x][0] == 'R'

        # there are no tokens created yet
        # TODO: this test conflicts with heir test because token creating for heir
    #    if is_fresh_chain:
    #        result = rpc.tokenlist()
    #        assert result == []

        print("making invalid token" + v + "create(s)...")
        # trying to create token with negative supply
        with pytest.raises(RPCError):
            result = call_token_rpc(rpc, "token"+v+"create", "NUKE", "-1987420", "no bueno supply")
            assert_error(result)

        # creating token with name more than 32 chars
        result = call_token_rpc(rpc, "token"+v+"create", "NUKE123456789012345678901234567890", "1987420", "name too long")
        assert_error(result)

        print("making valid token" + v + "create...")
        # creating valid token
        result = call_token_rpc(rpc, "token"+v+"create", "DUKE", "1987.420", "Duke's custom token")
        assert_success(result)

        tokenid = send_and_mine(result['hex'], rpc)

        result = call_token_rpc(rpc, "token"+v+"list")
        assert tokenid in result

        # there are no token orders yet
        if is_fresh_chain:
            result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
            assert result == []

        # getting token balance for non existing tokenid
        result = call_token_rpc(rpc, "token"+v+"balance", pubkey)
        assert_error(result)

        # get token balance for token with pubkey
        result = call_token_rpc(rpc, "token"+v+"balance", tokenid, pubkey)
        assert_success(result)
        assert result['balance'] == 198742000000
        assert result['tokenid'] ==  tokenid

        # get token balance for token without pubkey
        result = call_token_rpc(rpc, "token"+v+"balance", tokenid)
        assert_success(result)
        assert result['balance'] == 198742000000
        assert result['tokenid'] == tokenid

        # this is not a valid assetid
        result = call_token_rpc(rpc, "token"+v+"info", pubkey)
        assert_error(result)

        # check tokeninfo for valid token
        result = call_token_rpc(rpc, "token"+v+"info", tokenid)
        assert_success(result)
        assert result['tokenid'] == tokenid
        assert result['owner'] == pubkey
        assert result['name'] == "DUKE"
        assert result['supply'] == 198742000000
        assert result['description'] == "Duke's custom token"

        print("making invalid token" + v + "ask(s)...")

        # invalid numtokens ask
        result = call_token_rpc(rpc, "token"+v+"ask", "-1", tokenid, "1")
        assert_error(result)

        # invalid numtokens ask
        result = call_token_rpc(rpc, "token"+v+"ask", "0", tokenid, "1")
        assert_error(result)

        # invalid price ask
        with pytest.raises(RPCError):
            result = call_token_rpc(rpc, "token"+v+"ask", "1", tokenid, "-1")
            assert_error(result)

        # invalid price ask
        result = call_token_rpc(rpc, "token"+v+"ask", "1", tokenid, "0")
        assert_error(result)

        # invalid tokenid ask
        result = call_token_rpc(rpc, "token"+v+"ask", "100", "deadbeef", "1")
        assert_error(result)

        print("making valid token" + v + "ask...")
        # valid ask
        tokenask = call_token_rpc(rpc, "token"+v+"ask", "100", tokenid, "7.77")
        assert tokenask['hex'], "token"+v+"ask tx not created"
        tokenaskid = send_and_mine(tokenask['hex'], rpc)
        result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
        order = result[0]
        assert order, "found order"

        print("making invalid token" + v + "fillask(s)...")
        # invalid ask fillunits
        result = call_token_rpc(rpc, "token"+v+"fillask", tokenid, tokenaskid, "0")
        assert_error(result)

        # invalid ask fillunits
        result = call_token_rpc(rpc, "token"+v+"fillask", tokenid, tokenaskid, "-777")
        assert_error(result)

        # too many ask fillunits
        result = call_token_rpc(rpc, "token"+v+"fillask", tokenid, tokenaskid, "777")
        assert_error(result)

        print("making valid token" + v + "fillask...")
        # valid ask fillunits
        fillask = call_token_rpc(rpc, "token"+v+"fillask", tokenid, tokenaskid, "100")
        assert fillask['hex'], "token"+v+"fillask tx not created"
        result = send_and_mine(fillask['hex'], rpc)
        txid = result[0]
        assert txid, "found txid"

        # should be no token orders
        result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
        assert result == []

        print("making valid token" + v + "ask to test cancelask...")
        # checking ask cancellation
        testask = call_token_rpc(rpc, "token"+v+"ask", "100", tokenid, "7.77")
        assert testask['hex'], "test token"+v+"ask tx not created"
        testorderid = send_and_mine(testask['hex'], rpc)
        # from other node (ensuring that second node have enough balance to cover txfee
        # to get the actual error - not "not enough balance" one
        rpc.sendtoaddress(rpc1.getnewaddress(), 1)
        time.sleep(10)  # to ensure transactions are in different blocks
        rpc.sendtoaddress(rpc1.getnewaddress(), 1)
        wait_some_blocks(rpc, 2)
        result = rpc1.getbalance()
        assert result > 0.1

        print("making invalid node token" + v + "cancelask(s)...")
        badcancel = call_token_rpc(rpc1, "token"+v+"cancelask", tokenid, testorderid)
        try :
            rpc1.sendrawtransaction(badcancel["hex"])   # checked at consensus
            assert False, "order should not be cancelled with another pk"  # should be exception
        except RPCError :
            pass # exception is normal here

        print("making valid node token" + v + "cancelask...")
        # from valid node
        cancelask = call_token_rpc(rpc, "token"+v+"cancelask", tokenid, testorderid)
        assert cancelask['hex'], "token"+v+"cancelask tx not created"
        send_and_mine(cancelask["hex"], rpc)

        # TODO: should be no ask in orders - bad test
        if is_fresh_chain:
            result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
            assert result == []

        print("making invalid token" + v + "bid(s)...")
        # invalid numtokens bid
        result = call_token_rpc(rpc, "token"+v+"bid", "-1", tokenid, "1")
        assert_error(result)

        # invalid numtokens bid
        result = call_token_rpc(rpc, "token"+v+"bid", "0", tokenid, "1")
        assert_error(result)

        # invalid price bid
        with pytest.raises(RPCError):
            result = call_token_rpc(rpc, "token"+v+"bid", tokenid, "-1")
            assert_error(result)

        # invalid price bid
        result = call_token_rpc(rpc, "token"+v+"bid", "1", tokenid, "0")
        assert_error(result)

        # invalid tokenid bid
        result = call_token_rpc(rpc, "token"+v+"bid", "100", "deadbeef", "1")
        assert_error(result)

        print("making valid token" + v + "bid...")
        # valid bid
        tokenbid = call_token_rpc(rpc, "token"+v+"bid", "100", tokenid, "10")
        assert tokenbid['hex'], "token"+v+"bid tx not created"
        tokenbidid = send_and_mine(tokenbid['hex'], rpc)
        result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
        order = result[0]
        assert order, "found order"

        print("making invalid token" + v + "fillbid(s)...")
        # invalid bid fillunits
        result = call_token_rpc(rpc, "token"+v+"fillbid", tokenid, tokenbidid, "0")
        assert_error(result)

        # invalid bid fillunits
        result = call_token_rpc(rpc, "token"+v+"fillbid", tokenid, tokenbidid, "-777")
        assert_error(result)

        # invalid bid fillunits, too many
        result = call_token_rpc(rpc, "token"+v+"fillbid", tokenid, tokenbidid, "1000")
        assert_error(result)

        # valid bid fillunits
        print("making valid token" + v + "fillbid...")
        fillbid = call_token_rpc(rpc, "token"+v+"fillbid", tokenid, tokenbidid, "100")
        assert fillbid['hex'], "token"+v+"fillbid tx not created"
        result = send_and_mine(fillbid['hex'], rpc)
        txid = result[0]
        assert txid, "found txid"

        # should be no token orders
        # TODO: should be no bid in orders - bad test
        if is_fresh_chain:
            result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
            assert result == []

        print("making valid token" + v + "bid to test cancelbid...")
        # checking bid cancellation
        testorder = call_token_rpc(rpc, "token"+v+"bid", "100", tokenid, "7.77")
        assert testorder['hex'], "test token"+v+"bid tx not created"
        testorderid = send_and_mine(testorder['hex'], rpc)

        # from other node
        result = rpc1.getbalance()
        assert result > 0.1

        print("making invalid node token" + v + "cancelbid...")
        result = call_token_rpc(rpc1, "token"+v+"cancelbid", tokenid, testorderid)
        #TODO: not throwing error now on tx generation
        #assert_error(result)

        print("making valid node token" + v + "cancelbid...")
        # from valid node
        cancelbid = call_token_rpc(rpc, "token"+v+"cancelbid", tokenid, testorderid)
        assert cancelbid['hex'], "test token"+v+"cancelbid tx not created"
        send_and_mine(cancelbid["hex"], rpc)
        result = call_token_rpc(rpc, "token"+v+"orders", tokenid)
        assert result == []

        print("making invalid token" + v + "transfer(s)...")
        # invalid token transfer amount (have to add status to CC code!)
        randompubkey = "021a559101e355c907d9c553671044d619769a6e71d624f68bfec7d0afa6bd6a96"
        result = call_token_rpc(rpc, "token"+v+"transfer", tokenid, randompubkey, "0")
        assert_error(result)

        # invalid token transfer amount (have to add status to CC code!)
        result = call_token_rpc(rpc, "token"+v+"transfer", tokenid, randompubkey, "-1")
        assert_error(result)

        print("making valid token" + v + "transfer...")
        # valid token transfer
        sendtokens = call_token_rpc(rpc, "token"+v+"transfer", tokenid, randompubkey, "1")
        assert sendtokens['hex'], "token"+v+"transfer tx not created"
        send_and_mine(sendtokens["hex"], rpc)
        result = call_token_rpc(rpc, "token"+v+"balance", tokenid, randompubkey)
        assert result["balance"] == 1

        print("making valid token" + v + "transfermany...")
        # valid token transfer
        sendtokens = call_token_rpc(rpc, "token"+v+"transfermany", tokenid, randompubkey, "1")
        assert sendtokens['hex'], "token"+v+"transfermany tx not created"
        send_and_mine(sendtokens["hex"], rpc)
        result = call_token_rpc(rpc, "token"+v+"balance", tokenid, randompubkey)
        assert result["balance"] == 2