#!/usr/bin/env python3

from lib import rpclib, tuilib
import os
import time
import json
import argparse
from slickrpc.exc import RpcException
import configparser

# test assets impl with mustpay... evals


def get_chain_rpc(config) :

    rpc = None
    with open(config) as file:
        lines = file.readlines()
    rpcuser = ''
    rpcpassword = ''
    rpcport = 0
    for l in lines :
        t = l.split('=')
        if len(t) == 2 : 
            if t[0] == 'rpcuser' :
                rpcuser = t[1].strip()
            elif t[0] == 'rpcpassword' :
                rpcpassword = t[1].strip()
            elif t[0] == 'rpcport' :
                rpcport = int(t[1])

    print('rpcuser', rpcuser, 'rpcpassword', rpcpassword, 'rpcport', rpcport)
    rpc = rpclib.rpc_connect(rpcuser, rpcpassword, int(rpcport))
    rpc.getinfo() # try connect
    print('rpc connected')
    return rpc


def add_normal_utxos(rpc, vins, pubkey,  amount) :
    gr = rpc.listaddressgroupings()
    total = 0
    for l1 in gr :
        for l2 in l1 :
            for addr in l2 :
                print('addr', addr)
                utxos = rpc.getaddressutxos({"addresses":  [ addr ] })
                for u in utxos :
                    if total >= amount :
                        return total
                    total += u['satoshis']
                    vins.append({ "hash": u['txid'], "n": u['outputIndex'] })
                break # use only first address elem
    
    return total

def add_token_utxos(rpc, vins, pubkey, tokenid, amount) :
    total = 0
    tokenutxos = rpc.tokenv2getutxos(tokenid, pubkey, str(amount))  # cc rpc accepts only strings
    for u in tokenutxos :
        if total >= amount :
            return total
        total += u['satoshis']
        vins.append({ "hash": u['hash'], "n": u['n'] })
    
    return total

def call_assets_ask(config, numtokens, tokenid, tokenprice) :

    rpc = get_chain_rpc(config)
    getinfo = rpc.getinfo()
    mypk = getinfo['pubkey']
    # use faucet rpc as a helper to obtain my normal address:
    faucetres = rpc.faucetaddress()
    myaddress = faucetres["myaddress"]

    # token param with tokenid
    token_param = "{0}{1}{2}".format('t'.encode('utf-8').hex(), '\1'.encode('utf-8').hex(), tokenid)

    # make mustpaycc for next ask output
    ask_script = rpc.makeccscript(json.dumps({ "vars": [{"VAR10":tokenprice}, {"VAR20": "VINAMOUNT"}], "expr": "VAR20-VAR30/VAR10" }))
    #print('ask_script=', ask_script)

    # make mustpaycc eval param with the load and amount script and the 'self' rule condition 
    # 'self' means that the destination output condition must be the same as the vin condition which is being spent
    mustpaycc_ask_param = rpc.makemustpayccparam('11', ask_script["LoadScript"], ask_script["AmountScript"], 'self', '1')

    # make mustpaycc for spent tokens
    token_script = rpc.makeccscript(json.dumps({ "vars": [{"VAR30":"VOUTAMOUNT"}], "expr": "VAR30" }))
    # print('token_script=', token_script)

    # condition where tokens from the ask to go
    next_token_cond = \
    {
        "type": "threshold-sha-256",
        "threshold": 2,
        "subfulfillments": [
            {
                "type": "eval-sha-256",
                "codehex": "f5",
                "param": token_param
            },
            {
                "type": "secp256k1-sha-256",
                "publicKey": "02deaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaa" # dead pubkey means any pubkey
            }
        ]
    }
    mustpaycc_token_param = rpc.makemustpayccparam('12', token_script["LoadScript"], token_script["AmountScript"], json.dumps(next_token_cond), '1')

    # make mustpaypkh scripts 
    normal_script = rpc.makeccscript(json.dumps({ "vars": [], "expr": "VAR30*VAR10" }))  # normal coins = spent-tokens * token-price
    # print('normal_script=', normal_script)

    # mustpaypkh eval param for paid normal coins for purchased tokens:
    mustpaypkh_normal_param = rpc.makemustpaypkhparam('13', normal_script["LoadScript"], normal_script["AmountScript"], myaddress)

    # add normal and token inputs
    vins = []
    inputs = add_normal_utxos(rpc, vins, mypk, 10000) # txfee
    assert inputs >= 10000, 'insufficent normal inputs'

    ccinputs = add_token_utxos(rpc, vins, mypk, tokenid, numtokens)
    assert ccinputs >= numtokens, 'insufficient token inputs:' + str(ccinputs)

    vouts = []
    vouts.append( \
        {
            "nValue": numtokens,
            "cc":  {
                "type": "threshold-sha-256",
                "threshold": 1,
                "subfulfillments": [
                    {
                        "type": "threshold-sha-256",
                        "threshold": 4,
                        "subfulfillments": [
                            { # mustpaycc ask:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_ask_param
                            },
                            { # mustpaycc next token:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_token_param
                            },
                            { # mustpaypkh paid coins:
                                "type": "eval-sha-256",
                                "codehex": "87",
                                "param": mustpaypkh_normal_param
                            },
                            { # token eval
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param
                            }
                        ]
                    },
                    {   # cancel ask cond
                        "type": "threshold-sha-256",
                        "threshold": 2,
                        "subfulfillments": [
                            { # token cond
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param
                            },
                            { # signature cond
                                "type": "secp256k1-sha-256",
                                "publicKey": mypk
                            }
                        ]
                    }
                ]
            }
        })

    # token change 
    if ccinputs > numtokens :
        vouts.append( \
            {
                "nValue": ccinputs - numtokens,
                "cc": {
                    "type": "threshold-sha-256",
                    "threshold": 2,
                    "subfulfillments": [
                        {
                            "type": "secp256k1-sha-256",
                            "publicKey": mypk
                        },
                        {
                            "type": "eval-sha-256",
                            "codehex": "f5",
                            "param": token_param
                        }
                    ]
                }
            })

    vinccs = []
    # add a fulfillment to spend tokens:
    vinccs.append(\
        {
            "cc": {
                "type": "threshold-sha-256",
                "threshold": 2,
                "subfulfillments": [
                    {
                        "type": "secp256k1-sha-256",
                        "publicKey": mypk
                    },
                    {
                        "type": "eval-sha-256",
                        "codehex": "f5",
                        "param": token_param
                    }
                ]
            },
            "sign": True # sign as we spend a signature cond
        })
            

    tx_json = \
        {
            "vins": vins,
            "vouts": vouts,
            "vinccs": vinccs
        }

    hextx = rpc.createccevaltx(json.dumps(tx_json))
    print('tx=', hextx)


def call_assets_fill_ask(config, asktxid, tokenid, numtokens) :

    rpc = get_chain_rpc(config)
    getinfo = rpc.getinfo()
    mypk = getinfo['pubkey']

    # use faucet rpc as a helper:
    faucetres = rpc.faucetaddress()
    myaddress = faucetres["myaddress"]

    vintx = rpc.getrawtransaction(asktxid, 1)
    vin_amount = vintx['vout'][0]['valueSat']

    mustpaycc_ask_param = rpc.getccevalparam(asktxid, '0', '0x86', '11')
    mustpaycc_token_param = rpc.getccevalparam(asktxid, '0', '0x86', '12')
    mustpaypkh_normal_param = rpc.getccevalparam(asktxid, '0', '0x87', '13')

    # find tokenprice:
    ask_parsed = rpc.parsemustpayccevalparam(mustpaycc_ask_param)
    print('ask_parsed', ask_parsed)
    token_price = ask_parsed['LoadScriptVars']['VAR10']
    normal_amount = token_price * numtokens

    # token param with tokenid
    token_param = "{0}{1}{2}".format('t'.encode('utf-8').hex(), '\1'.encode('utf-8').hex(), tokenid)


    # add inputs
    vins = []
    inputs = add_normal_utxos(rpc, vins, mypk, 10000 + normal_amount ) # txfee + normal coind paid for tokens
    assert inputs >= 10000 + normal_amount, 'insufficent normal inputs'

    vouts = []
    # add next ask
    vouts.append( \
        {
            "nValue": vin_amount - numtokens,
            "cc": {
                "type": "threshold-sha-256",
                "threshold": 4,
                "subfulfillments": [
                    { # mustpaycc ask:
                        "type": "eval-sha-256",
                        "codehex": "86",
                        "param": mustpaycc_ask_param
                    },
                    { # mustpaycc spend token rule:
                        "type": "eval-sha-256",
                        "codehex": "86",
                        "param": mustpaycc_token_param
                    },
                    { # mustpaypkh pay normal rule:
                        "type": "eval-sha-256",
                        "codehex": "87",
                        "param": mustpaypkh_normal_param
                    },
                    { # token paid rule:
                        "type": "eval-sha-256",
                        "codehex": "f5",
                        "param": token_param
                    }
                ]
            }
        })

    # add token spending
    vouts.append( \
        {
            "nValue": numtokens,
            "cc": {
                "type": "threshold-sha-256",
                "threshold": 2,
                "subfulfillments": [
                    {
                        "type": "secp256k1-sha-256",
                        "publicKey": mypk
                    },
                    {
                        "type": "eval-sha-256",
                        "codehex": "f5",
                        "param": token_param
                    }
                ]
            }
        })

    # add output to pay normal amount:
    vouts.append( \
        {
            "nValue": normal_amount,
            "Destination": myaddress
        })

    vinccs = []
    # add probe fulfillment to spend the previous ask:
    vinccs.append(\
        {
            "cc":  {
                "type": "threshold-sha-256",
                "threshold": 1,
                "subfulfillments": [
                    {
                        "type": "threshold-sha-256",
                        "threshold": 4,
                        "subfulfillments": [
                            { # mustpaycc ask:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_ask_param
                            },
                            { # mustpaycc next token:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_token_param
                            },
                            { # mustpaypkh paid coins:
                                "type": "eval-sha-256",
                                "codehex": "87",
                                "param": mustpaypkh_normal_param
                            },
                            {
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param
                            }
                        ]
                    },
                    {
                        # cancel ask cond
                        "type": "threshold-sha-256",
                        "threshold": 2,
                        "subfulfillments": [
                            {
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param
                            },
                            {
                                "type": "secp256k1-sha-256",
                                "publicKey": mypk
                            }
                        ]
                    }
                ]
            },
            "sign": False
        })
            
    vins.append({ "hash": asktxid, "n": 0 })

    tx_json = \
        {
            "vins": vins,
            "vouts": vouts,
            "vinccs": vinccs
        }

    hextx = rpc.createccevaltx(json.dumps(tx_json))
    print('tx=', hextx)

# cancel ask
def call_assets_cancel_ask(config, asktxid, tokenid) :

    rpc = get_chain_rpc(config)
    getinfo = rpc.getinfo()
    mypk = getinfo['pubkey']

    # use faucet rpc as a helper:
    faucetres = rpc.faucetaddress()
    myaddress = faucetres["myaddress"]

    vintx = rpc.getrawtransaction(asktxid, 1)
    vin_amount = vintx['vout'][0]['valueSat']

    # get eval params from the ask vin
    mustpaycc_ask_param = rpc.getccevalparam(asktxid, '0', '0x86', '11')
    mustpaycc_token_param = rpc.getccevalparam(asktxid, '0', '0x86', '12')
    mustpaypkh_normal_param = rpc.getccevalparam(asktxid, '0', '0x87', '13')

    # find tokenprice:
    ask_parsed = rpc.parsemustpayccevalparam(mustpaycc_ask_param)
    print('ask_parsed', ask_parsed)
    token_price = ask_parsed['LoadScriptVars']['VAR10']

    # token param with tokenid
    token_param = "{0}{1}{2}".format('t'.encode('utf-8').hex(), '\1'.encode('utf-8').hex(), tokenid)


    # add inputs
    vins = []
    inputs = add_normal_utxos(rpc, vins, mypk, 10000) # txfee 
    assert inputs >= 10000, 'insufficent normal inputs for txfee'

    vouts = []

    # add token spent from ask
    vouts.append( \
        {
            "nValue": vin_amount,
            "cc": {
                "type": "threshold-sha-256",
                "threshold": 2,
                "subfulfillments": [
                    {
                        "type": "secp256k1-sha-256",
                        "publicKey": mypk
                    },
                    {
                        "type": "eval-sha-256",
                        "codehex": "f5",
                        "param": token_param
                    }
                ]
            }
        })

    vinccs = []
    # add probe fulfillment to spend previous ask:
    vinccs.append( \
        {
            "cc":  {
                "type": "threshold-sha-256",
                "threshold": 1,
                "subfulfillments": [
                    {
                        "type": "threshold-sha-256",
                        "threshold": 4,
                        "subfulfillments": [
                            { # mustpaycc ask:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_ask_param,
                                "dontFulfill": 1
                            },
                            { # mustpaycc next token:
                                "type": "eval-sha-256",
                                "codehex": "86",
                                "param": mustpaycc_token_param,
                                "dontFulfill": 1
                            },
                            { # mustpaypkh paid coins:
                                "type": "eval-sha-256",
                                "codehex": "87",
                                "param": mustpaypkh_normal_param,
                                "dontFulfill": 1
                            },
                            { # token eval
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param,
                                "dontFulfill": 1
                            }
                        ]
                    },
                    {   # cancel ask cond
                        "type": "threshold-sha-256",
                        "threshold": 2,
                        "subfulfillments": [
                            {
                                "type": "eval-sha-256",
                                "codehex": "f5",
                                "param": token_param
                            },
                            {  
                                "type": "secp256k1-sha-256",
                                "publicKey": mypk
                            }
                        ]
                    }
                ]
            },
            "sign": True  # need to sign secp256k1 cond
        })
            
    # spend ask utxo
    vins.append({ "hash": asktxid, "n": 0 })

    tx_json = \
        {
            "vins": vins,
            "vouts": vouts,
            "vinccs": vinccs
        }

    hextx = rpc.createccevaltx(json.dumps(tx_json))
    print('tx=', hextx)


if __name__ == "__main__":
    print("Starting assets generic eval demo")
    parser = argparse.ArgumentParser(epilog="Plz first start a chain of two or three nodes and use the path to one of your nodes' config files to run a test method")
    parser.add_argument("configfile", help='path to node config file')
    parser.add_argument("method", help='assets rpc method like assetsask assetsfillask assetsbid etc')
    parser.add_argument("--numtokens", type=int, help='number of tokens')
    parser.add_argument("--tokenid", help='token id in hex')
    parser.add_argument("--price", type=float, help='token price in coins' )
    parser.add_argument("--txid", help='txid of ask or bid tx' )
    args = parser.parse_args()
    print(args)
    if args.method == 'assetsask':
        if not args.numtokens or not args.tokenid or not args.price :
            print('for', args.method, ' numtokens tokenid price are needed')
            exit(1)
        call_assets_ask(args.configfile, args.numtokens, args.tokenid, int(args.price*1_0000_0000))
    elif args.method == 'assetsfillask':
        if not args.txid or not args.tokenid or not args.numtokens :
            print('for', args.method, ' txid tokenid numtokens are needed')
            exit(1)
        call_assets_fill_ask(args.configfile, args.txid, args.tokenid, args.numtokens)
    elif args.method == 'assetscancelask':
        if not args.txid or not args.tokenid :
            print('for', args.method, ' txid tokenid are needed')
            exit(1)
        call_assets_cancel_ask(args.configfile, args.txid, args.tokenid)
    else :
        print(parser.format_help())
