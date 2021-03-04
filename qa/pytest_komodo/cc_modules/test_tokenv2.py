#!/usr/bin/env python3
# Copyright (c) 2021 SuperNET developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import pytest
from slickrpc.exc import RpcException as RPCError
from basic.pytest_util import validate_template, mine_and_waitconfirms, randomstring, randomhex, validate_raddr_pattern


@pytest.mark.usefixtures("proxy_connection")
class TestTokenCCv2Calls:

    @staticmethod
    def new_token(proxy, schema=None):
        name = randomstring(8)
        amount = '0.10000000'
        res = proxy.tokenv2create(name, amount, "test token 1")
        if schema:
            validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, proxy)
        token = {
            'tokenid': txid,
            'name': name
        }
        return token

    def test_tokenv2create_v2list_v2info(self, test_params):
        v2create_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                'hex': {'type': 'string'},
                'error': {'type': 'string'}
            },
            'required': ['result']
        }

        v2list_schema = {
            'type': 'array',
            'items': {'type': 'string'}
        }

        v2info_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                'tokenid': {'type': 'string'},
                'owner': {'type': 'string'},
                'name': {'type': 'string'},
                'supply': {'type': 'integer'},
                'description': {'type': 'string'},
                'version':  {'type': 'integer'},
                'IsMixed': {'type': 'string'}
            },
            'required': ['result']
        }

        rpc = test_params.get('node1').get('rpc')
        token = self.new_token(rpc, schema=v2create_schema)

        res = rpc.tokenv2list()
        validate_template(res, v2list_schema)

        res = rpc.tokenv2info(token.get('tokenid'))
        validate_template(res, v2info_schema)

    def test_tokenv2address(self, test_params):
        v2assetaddress_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                "GlobalPk Assetsv2 CC Address": {'type': 'string'},
                "GlobalPk Assetsv2 CC Balance": {'type': 'number'},
                "GlobalPk Assetsv2 Normal Address": {'type': 'string'},
                "GlobalPk Assetsv2 Normal Balance": {'type': 'number'},
                "GlobalPk Assetsv2/Tokens CC Address": {'type': 'string'},
                "pubkey Assetsv2 CC Address": {'type': 'string'},
                "pubkey Assetsv2 CC Balance": {'type': 'number'},
                "mypk Assetsv2 CC Address": {'type': 'string'},
                "mypk Assetsv2 CC Balance": {'type': 'number'},
                "mypk Normal Address": {'type': 'string'},
                "mypk Normal Balance": {'type': 'number'}
            },
            'required': ['result']
        }

        v2tokenaddress_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                "GlobalPk Tokensv2 CC Address": {'type': 'string'},
                "GlobalPk Tokensv2 CC Balance": {'type': 'number'},
                "GlobalPk Tokensv2 Normal Address": {'type': 'string'},
                "GlobalPk Tokensv2 Normal Balance": {'type': 'number'},
                "pubkey Tokensv2 CC Address": {'type': 'string'},
                "pubkey Tokensv2 CC Balance": {'type': 'number'},
                "mypk Tokensv2 CC Address": {'type': 'string'},
                "mypk Tokensv2 CC Balance": {'type': 'number'},
                "mypk Normal Address": {'type': 'string'},
                "mypk Normal Balance": {'type': 'number'}
            },
            'required': ['result']
        }

        rpc = test_params.get('node1').get('rpc')
        pubkey = test_params.get('node1').get('pubkey')

        res = rpc.assetsv2address(pubkey)
        validate_template(res, v2assetaddress_schema)

        res = rpc.tokenv2address(pubkey)
        validate_template(res, v2tokenaddress_schema)

    def test_tokenv2transfer_v2balance(self, test_params):
        v2transfer_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                'hex': {'type': 'string'}
            },
            'required': ['result']
        }

        v2balance_schema = {
            'type': 'object',
            'properties': {
                'result': {'type': 'string'},
                'CCaddress': {'type': 'string'},
                'tokenid': {'type': 'string'},
                'balance': {'type': 'integer'}
            },
            'required': ['result']
        }

        rpc1 = test_params.get('node1').get('rpc')
        rpc2 = test_params.get('node2').get('rpc')
        pubkey2 = test_params.get('node2').get('pubkey')

        token1 = self.new_token(rpc1)

        amount = 150
        res = rpc1.tokenv2transfer(token1.get('tokenid'), pubkey2, str(amount))
        validate_template(res, v2transfer_schema)
        txid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, rpc1)

        res = rpc2.tokenv2balance(token1.get('tokenid'), pubkey2)
        validate_template(res, v2balance_schema)
        assert amount == res.get('balance')
        assert token1.get('tokenid') == res.get('tokenid')

    def test_tokenv2ask_v2bid_v2orders(self, test_params):
        v2askbid_schema = {
            'type': 'object',
            'properties': {
                'hex': {'type': 'string'}
            },
            'required': ['hex']
        }

        v2orders_schema = {
            'type': 'array',
            'items': {
                'type': 'object',
                'properties': {
                    'funcid': {'type': 'string'},
                    'txid': {'type': 'string'},
                    'vout':  {'type': 'integer'},
                    'amount': {'type': 'string'},
                    'askamount': {'type': 'string'},
                    'origaddress': {'type': 'string'},
                    'origtokenaddress': {'type': 'string'},
                    'tokenid': {'type': 'string'},
                    'totalrequired': {'type': ['string', 'integer']},
                    'price':  {'type': ['integer', 'number']}
                }
            }
        }

        rpc1 = test_params.get('node1').get('rpc')
        rpc2 = test_params.get('node2').get('rpc')
        pubkey2 = test_params.get('node2').get('pubkey')

        token1 = self.new_token(rpc1)
        amount1 = 150
        amount2 = 100
        price = 0.1

        res = rpc1.tokenv2ask(str(amount1), token1.get('tokenid'), price)
        validate_template(res, v2askbid_schema)
        asktxid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(asktxid, rpc1)

        res = rpc1.tokenv2bid(str(amount2), token1.get('tokenid'), price)
        validate_template(res, v2askbid_schema)
        bidtxid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(bidtxid, rpc1)

        # Check orders and myorders call
        orders1 = rpc2.tokenv2orders(token1.get('tokenid'))
        validate_template(orders1, v2orders_schema)
        orders2 = rpc1.mytokenv2orders(token1.get('tokenid'))
        validate_template(orders2, v2orders_schema)
        assert orders1 == orders2

        # Check fills and cancel calls
        res = rpc2.tokenv2fillask(token1.get('tokenid'), asktxid, str(int(amount1 * 0.5)))
        validate_template(res, v2askbid_schema)
        asktxid2 = rpc2.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(asktxid2, rpc2)

        res = rpc2.tokenv2fillbid(token1.get('tokenid'), bidtxid, str(int(amount2 * 0.5)))
        validate_template(res, v2askbid_schema)
        bidtxid2 = rpc2.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(bidtxid2, rpc2)

        res = rpc1.tokenv2cancelask(token1.get('tokenid'), asktxid2)
        validate_template(res, v2askbid_schema)
        txid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, rpc1)

        res = rpc1.tokenv2cancelbid(token1.get('tokenid'), bidtxid2)
        validate_template(res, v2askbid_schema)
        txid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, rpc1)

        # There should be no orders left after cancelling
        orders = rpc1.mytokenv2orders(token1.get('tokenid'))
        validate_template(orders, v2orders_schema)
        assert len(orders) == 0


@pytest.mark.usefixtures("proxy_connection")
class TestTokenCCv2:

    def test_rewardsaddress(self, test_params):
        rpc = test_params.get('node1').get('rpc')
        pubkey = test_params.get('node1').get('pubkey')

        res = rpc.assetsv2address(pubkey)
        for key in res.keys():
            if key.find('ddress') > 0:
                assert validate_raddr_pattern(res.get(key))

        res = rpc.tokenv2address(pubkey)
        for key in res.keys():
            if key.find('ddress') > 0:
                assert validate_raddr_pattern(res.get(key))

    @staticmethod
    def bad_calls(proxy, token, pubkey):
        name = token.get('name')
        tokenid = token.get('tokenid')
        # trying to create token with negative supply
        with pytest.raises(RPCError):
            res = proxy.tokenv2create("NUKE", "-1987420", "no bueno supply")
            assert res.get('error')
        # creating token with name more than 32 chars
        res = proxy.tokenv2create("NUKE123456789012345678901234567890", "1987420", "name too long")
        assert res.get('error')
        # getting token balance for non existing tokenid
        res = proxy.tokenv2balance("", pubkey)
        assert res.get('error')
        # no info for invalid tokenid
        res = proxy.tokenv2info(pubkey)
        assert res.get('error')
        # invalid token transfer amount
        randompubkey = randomhex()
        res = proxy.tokenv2transfer(tokenid, randompubkey, "0")
        assert res.get('error')
        # invalid token transfer amount
        res = proxy.tokenv2transfer(tokenid, randompubkey, "-1")
        assert res.get('error')
        # invalid numtokens bid
        res = proxy.tokenv2bid("-1", tokenid, "1")
        assert res.get('error')
        # invalid numtokens bid
        res = proxy.tokenv2bid("0", tokenid, "1")
        assert res.get('error')
        # invalid price bid
        with pytest.raises(RPCError):
            res = proxy.tokenv2bid("1", tokenid, "-1")
            assert res.get('error')
        # invalid price bid
        res = proxy.tokenv2bid("1", tokenid, "0")
        assert res.get('error')
        # invalid tokenid bid
        res = proxy.tokenv2bid("100", "deadbeef", "1")
        assert res.get('error')
        # invalid numtokens ask
        res = proxy.tokenv2ask("-1", tokenid, "1")
        assert res.get('error')
        # invalid numtokens ask
        res = proxy.tokenv2ask("0", tokenid, "1")
        assert res.get('error')
        # invalid price ask
        with pytest.raises(RPCError):
            res = proxy.tokenv2ask("1", tokenid, "-1")
            assert res.get('error')
        # invalid price ask
        res = proxy.tokenv2ask("1", tokenid, "0")
        assert res.get('error')
        # invalid tokenid ask
        res = proxy.tokenv2ask("100", "deadbeef", "1")
        assert res.get('error')

    def test_bad_calls(self, test_params):
        rpc = test_params.get('node1').get('rpc')
        pubkey = test_params.get('node1').get('pubkey')
        token = TestTokenCCv2Calls.new_token(rpc)
        self.bad_calls(rpc, token, pubkey)
