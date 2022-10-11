#!/usr/bin/env python3

from lib import rpclib, tuilib
import os
import time
import json
from slickrpc.exc import RpcException
import configparser

# test creates fungible and non-fungible tokens
# performs transfers and multiple transfers
# then runs assets cc ask/bids tests

header = "komodo assets cc tokenask/bid v2 test\n"
tui_config_name = './tui_assets_orders.ini'

def wait_until_confirmed(rpc, txid) :
    for i in range(60) :
        try :
            txdecoded = rpc.getrawtransaction(txid, 1)
            # print('get.confirmations', txdecoded.get('confirmations'), type(txdecoded.get('confirmations')))
            if not txdecoded.get('confirmations') is None :
                print('confirmations', int(txdecoded.get('confirmations')))
                if int(txdecoded.get('confirmations')) > 0 :
                    return
        except :
            print('getrawtransaction exception')
            pass
        time.sleep(10)
        print('still waiting for confirmation...')

def get_my_address(rpc) :
    gr = rpc.listaddressgroupings()
    total = 0
    for l1 in gr :
        for l2 in l1 :
            for addr in l2 :
                return addr
    return ""

def get_result_error(r):
    if isinstance(r, dict) :
        if r.get('result') == "error" :
            return r.get('error')
    return ''

# check if result is valid tx
def check_tx_result(r):
    if isinstance(r, str) :
        # print("r is str, true")
        return True
    elif isinstance(r, dict) :
        # print("r is dict")
        str_hex = r.get('hex')
        if isinstance(str_hex, str) :
            # print("r has hex, true")
            if str_hex.find('coins') >= 0 or str_hex.find('tokens') >= 0 or str_hex.find('assets') >= 0 :  # try to detect old code returning no coins err as success
                print("bad tx result:", r)
                return False
            else :
                return True
        if r.get('result') == "error" :
            print("bad tx result:", r)
            return False
    # print("r has unknown, true")
    return True
    
def check_txid(txid) :
    if isinstance(txid, str) :
        if len(txid) == 64 :
            return True
    return False

def run_tokens_create(rpc1, rpc2, rpc3):

    # set your own two node params
    # DIMXY20 
    # rpc1 = rpclib.rpc_connect("user3088995989", "pass53c05895f37aa1eda9b0de63944275668e1956abb8c38f186132262fe53ae9be7c", 14723)
    # rpc2 = rpclib.rpc_connect("user2898668153", "passe99167496d2bbe43876e60430d52eb37b547fa1085b741a15776454eb126d9a603", 15723)
    # rpc3 = rpclib.rpc_connect("user972794450", "passe7eb16f5c015a53463cc5f27a004854cb76f4ec5c9aece177f01d8b3d13119e445", 16723)

    # DIMXY29
    #rpc1 = rpclib.rpc_connect("user977812407", "pass45a4a6ce9ffafbebd2a478d3858d55c8a4bded74632bb488c8c93164d048237516", 14723)
    #rpc2 = rpclib.rpc_connect("user4228621419", "pass4e51de899bd15c12569d762ded036e5aecd5aeb7be40788a339e72e1fea7359d7e", 15723)
    #rpc3 = rpclib.rpc_connect("user3877003389", "passea3e5a6c7a8a88f5447cc39bc8720cf783bbcef43345b16be9aadc0a1b381006eb", 16723)

    ## for v in ["", "v2"] :
    ## for v in [""] :
    for v in ["v2"] :
        print("creating fungible token 1...")
        result = call_rpc(rpc1, "token"+v+"create", "T1", str(0.000001))  # 100
        assert(check_tx_result(result))
        tokenid1 = rpc1.sendrawtransaction(result['hex'])
        print("created token:", tokenid1)
        # wait_until_confirmed(rpc1, tokenid1)

        print("creating fungible token 2...")
        result = call_rpc(rpc1, "token"+v+"create", "T2", str(0.1))
        assert(check_tx_result(result))
        tokenid2 = rpc1.sendrawtransaction(result['hex'])
        print("created token:", tokenid2)
        # wait_until_confirmed(rpc1, tokenid2)

        print("creating tokel NFT 1 with empty data...")
        result = call_rpc(rpc1, "token"+v+"createtokel", "NFT-00-1", str(0.00000001), "nft tokel empty", '{}')
        assert(check_tx_result(result))
        nft00id1 = rpc1.sendrawtransaction(result['hex'])
        print("created token:", nft00id1)
        # wait_until_confirmed(rpc1, nft00id1)

        print("creating tokel NFT 2 with arbitrary data...")
        result = call_rpc(rpc1, "token"+v+"createtokel", "NFT-00-2", str(0.00000001), "nft tokel arbitrary", '{"arbitrary":"010203"}')
        assert(check_tx_result(result))
        nft00id2 = rpc1.sendrawtransaction(result['hex'])
        print("created token:", nft00id2)
        # wait_until_confirmed(rpc1, nft00id2)

        #  tokel nft data F7 evalcode
        print("creating tokel NFT with royalty 0...")
        result = call_rpc(rpc1, "token"+v+"createtokel", "NFT-F7-1", str(0.00000001), "nft tokel royalty=0", '{"royalty":0}')
        assert(check_tx_result(result))
        nftf7id1 = rpc1.sendrawtransaction(result['hex'])
        print("created token:", nftf7id1)
        # wait_until_confirmed(rpc1, nftf7id1)

        print("creating tokel NFT with royalty 999 on rpc3...")
        result = call_rpc(rpc3, "token"+v+"createtokel", "NFT-F7-2", str(0.00000001), "nft tokel royalty=999", '{"royalty":999}')
        assert(check_tx_result(result))
        nftf7id2 = rpc3.sendrawtransaction(result['hex'])
        print("created token:", nftf7id2)
        # wait_until_confirmed(rpc1, nftf7id2)

        print("creating tokel NFT with royalty 1 on rpc3 (to test dust)...")
        result = call_rpc(rpc3, "token"+v+"createtokel", "NFT-F7-3", str(0.00000001), "nft tokel royalty=1", '{"royalty":1}')
        assert(check_tx_result(result))
        nftf7id3 = rpc3.sendrawtransaction(result['hex'])
        print("created token:", nftf7id3)
        # wait_until_confirmed(rpc1, nftf7id3)

        print("creating tokel NFT with royalty 500 (to test dust)...")
        result = call_rpc(rpc3, "token"+v+"createtokel", "NFT-F7-4", str(0.00000001), "nft tokel royalty=500", '{"royalty":500}')
        assert(check_tx_result(result))
        nftf7id4 = rpc3.sendrawtransaction(result['hex'])
        print("created token:", nftf7id4)
        # wait_until_confirmed(rpc1, nftf7id4)

        print("tokens for tests created okay!")

        # transfer tokens  from rpc3 to rpc1 (to make a different pubkey than tokencreator)
        pubkey1 = rpc1.getinfo()['pubkey']
        call_token_rpc_send_tx(rpc3, "token"+v+"transfer", '', nftf7id2, pubkey1, str(1))
        call_token_rpc_send_tx(rpc3, "token"+v+"transfer", '', nftf7id3, pubkey1, str(1))
        call_token_rpc_send_tx(rpc3, "token"+v+"transfer", '', nftf7id4, pubkey1, str(1))

        # test tokenlist:
        check_tokenlist(rpc1, v, [tokenid1, tokenid2, nft00id1, nft00id2, nftf7id1, nftf7id2, nftf7id3, nftf7id4])
        # test tokenallbalances:
        check_tokenallbalances(rpc1, v, {tokenid1: 100, tokenid2: 1000_0000, nft00id1: 1, nft00id2: 1, nftf7id1: 1, nftf7id2: 1, nftf7id3: 1, nftf7id4: 1} )
        
        # first try transfer tokens to a pk and back, then run assets tests
        print("starting transfer tests for tokenid version=" + v + "...")
        run_transfers(rpc1, rpc2, v, tokenid1, tokenid2, 10)

        
        print("starting transfer tests for nft00id version=" + v + "...")
        run_transfers(rpc1, rpc2, v, nft00id1, nft00id2, 1)
        print("starting transfer tests for nftf7id version=" + v + "...")
        run_transfers(rpc1, rpc2, v, nftf7id1, nftf7id2, 1)
        print("token transfers tests finished okay!")
        time.sleep(3)


        # assets cc tests:
        print("starting assets tests for tokenid1 version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, tokenid1, 10, 8, 0.0001, False)
        print("starting assets tests for nft00id1 version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, nft00id1, 1, 1, 0.0001, True)
        print("starting assets tests for nftf7id1 version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, nftf7id1, 1, 1, 0.0001, True)
        print("starting assets tests for nftf7id2 version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, nftf7id2, 1, 1, 0.0001, True)
        print("starting assets tests for nftf7id3 version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, nftf7id3, 1, 1, 0.0001, True)
        print("starting assets tests for nftf7id4 with unit price creating dust royalty and checking that round correct, version=" + v + "...")
        run_assets_orders(rpc1, rpc2, v, nftf7id4, 1, 1, 0.0000_1001, True)
        print("starting assets order expiration tests for tokenid1 version=" + v + "...")
        run_assets_expired_orders(rpc1, rpc2, v, tokenid1, 10, 8, 0.0001, False)
        print("assets tests finished okay!")
        

        if v == "v2" :  # MofN supported for tokens cc v2 only
            print("running MofN tests for tokens v2:")
            print("starting MofN tests for tokenid1...")
            run_MofN_transfers(rpc1, rpc2, rpc3, tokenid1, 10)
            print("starting MofN tests for nft00id1...")
            run_MofN_transfers(rpc1, rpc2, rpc3, nft00id1, 1)
            print("starting MofN tests for nftf7id1...")
            run_MofN_transfers(rpc1, rpc2, rpc3, nftf7id1, 1)   
            print("token MofN transfer tests finished okay!")
        
    

    print("all token/assets tests finished okay!")
    time.sleep(3)
    exit

def call_rpc(rpc, rpcname, *args) :
    rpcfunc = getattr(rpc, rpcname)
    return rpcfunc(*args)

def call_token_rpc_create_tx(rpc, rpcname, stop_error, *args) :
    retries = 24
    delay = 10
    rpcfunc = getattr(rpc, rpcname)
    for i in range(retries):
        print("calling " + rpcname)
        result = rpcfunc(*args)
        print(rpcname + " create tx result:", result, " arg=", args[0])
        if  check_tx_result(result):
            break
        if stop_error and get_result_error(result) == stop_error :  
            print(rpcname + " retrying stopped because of expected stop error received: " + stop_error)
            return result
        if i < retries-1:
            print("retrying " + rpcname + '...')
            time.sleep(delay)                
    assert(check_tx_result(result))
    print(rpcname + " tx created")
    return result

def call_token_rpc_send_tx(rpc, rpcname, stop_error, *args) :

    for i in range(3) :
        result = call_token_rpc_create_tx(rpc, rpcname, stop_error, *args)
        if check_tx_result(result) :
            try :
                txid = rpc.sendrawtransaction(result['hex'])
                print("sendrawtransaction result: ", txid)
                assert(check_tx_result(txid))            
                print(rpcname + " tx sent")
                return txid
            except RpcException as e :
                if e.message.find('replacement in mempool') >= 0 or e.message.find('missing inputs') >= 0 :
                    print('double spending in mempool', e.message, 'retrying...')
                    pass
                else :
                    assert False, e
        else :
            return result
    assert False, 'sendrawtransaction no more retries'

def call_rpc_retry(rpc, rpcname, stop_error, *args) :
    retries = 36
    delay = 10
    rpcfunc = getattr(rpc, rpcname)
    for i in range(retries):
        print("calling " + rpcname)
        result = rpcfunc(*args)
        # print(rpcname + " result:", result)
        if  check_tx_result(result):
            break
        if stop_error and get_result_error(result) == stop_error :  
            print(rpcname + " retrying stopped because of expected stop error received: " + stop_error)
            return result
        if i < retries-1:
            print("retrying " + rpcname + '...')
            time.sleep(delay)                
    assert(check_tx_result(result))
    # print(rpcname + " finished okay")
    return result

def call_token_rpc(rpc, rpcname, stop_error, *args) :
    if len(args) > 0 :
        print("calling " + rpcname + " for tokenid=", args[0])
    return call_rpc_retry(rpc, rpcname, stop_error, *args)

def run_transfers(rpc1, rpc2, v, tokenid1, tokenid2, amount):
   
    pubkey1 = rpc1.getinfo()['pubkey']
    pubkey2 = rpc2.getinfo()['pubkey']

    # rpc1.setgenerate(True, 2)

    if amount == 1 :
        print("creating token"+v+"transfer tokenid1 amount tx...")
        call_token_rpc_send_tx(rpc1, "token"+v+"transfer", '', tokenid1, pubkey2,  str(amount))
    else :
        # try two transfers
        print("creating token"+v+"transfer tokenid1 amount-1 tx...")
        txid = call_token_rpc_send_tx(rpc1, "token"+v+"transfer", '', tokenid1, pubkey2,  str(amount-1))
        assert(check_txid(txid))   

        print("creating token"+v+"transfer tokenid1 1 tx...")
        txid = call_token_rpc_send_tx(rpc1, "token"+v+"transfer", '', tokenid1, pubkey2,  str(1))
        assert(check_txid(txid))   

    print("creating token"+v+"transfer tokenid1 tx back...")
    txid = call_token_rpc_send_tx(rpc2, "token"+v+"transfer", '', tokenid1, pubkey1,  str(amount))
    assert(check_txid(txid))   

    print("creating token"+v+"transfermany tokenid1 tokenid2 tx...")
    txid = call_token_rpc_send_tx(rpc1, "token"+v+"transfermany", '', tokenid1, tokenid2, pubkey2,  str(amount))
    assert(check_txid(txid))   

    print("creating token"+v+"transfermany tokenid1 tokenid2 tx back...")
    txid = call_token_rpc_send_tx(rpc2, "token"+v+"transfermany", '', tokenid1, tokenid2, pubkey1,  str(amount))
    assert(check_txid(txid))   


def run_MofN_transfers(rpc1, rpc2, rpc3, tokenid, amount):
   
    pubkey1 = rpc1.getinfo()['pubkey']
    pubkey2 = rpc2.getinfo()['pubkey']
    pubkey3 = rpc3.getinfo()['pubkey']


    if  amount > 1 :
        amountback = int(amount / 2) # if not nft send half amount back twice
    else :
        amountback = amount

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # try 1of2
    param = {}
    param["tokenid"] = tokenid
    param["destpubkeys"] = [ pubkey1, pubkey2 ]
    param["M"] = 1
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid amount tx to 1of2 tx...", json.dumps(param))
    tx1of2 = call_token_rpc_create_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    txid = rpc1.sendrawtransaction(tx1of2['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))   
    ccaddr1of2 = rpc1.decoderawtransaction(tx1of2['hex'])['vout'][0]['scriptPubKey']['addresses'][0]
    
    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # spend 1of2 to pk1 or pk2
    param = {}
    param["tokenid"] = tokenid
    param["ccaddressMofN"] = ccaddr1of2
    param["M"] = 1
    param["amount"] = amountback
    if amount > 1 :  # if not nft first send half to pk2 (to test lib cc fulfilment ordering)
        # try firs1 to pk2
        param["destpubkeys"] = [ pubkey2 ]
        print("creating tokenv2transferMofN tokenid1 tx spending 1of2 back to pk2...", json.dumps(param))
        txid = call_token_rpc_send_tx(rpc2, "tokenv2transferMofN", '', json.dumps(param))
        assert(check_txid(txid))   
    else :
        # if nft send 1 back to pk1 for the next test
        param["destpubkeys"] = [ pubkey1 ]
        print("creating tokenv2transferMofN tokenid tx spending 1of2 back to pk1...", json.dumps(param))
        txid = call_token_rpc_send_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
        assert(check_txid(txid))   

    if amount > 1 :  # if not nft send half amount back to pk1
        # wait to prevent adding same inputs while tx is not propagated to this node mempool
        time.sleep(3)   
        param["destpubkeys"] = [ pubkey1 ]
        print("creating tokenv2transferMofN tokenid tx spending 1of2 back to pk1...",json.dumps(param))
        txid = call_token_rpc_send_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
        assert(check_txid(txid))   

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # try 2of2
    param = {}
    param["tokenid"] = tokenid
    param["destpubkeys"] = [ pubkey1, pubkey2 ]
    param["M"] = 2
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid amount tx to 2of2 ...", json.dumps(param))
    tx2of2 = call_token_rpc_create_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    txid = rpc1.sendrawtransaction(tx2of2['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))   
    ccaddr2of2 = rpc1.decoderawtransaction(tx2of2['hex'])['vout'][0]['scriptPubKey']['addresses'][0]

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # spend 2of2 to pk1
    param = {}
    param["tokenid"] = tokenid
    param["ccaddressMofN"] = ccaddr2of2
    param["destpubkeys"] = [ pubkey1 ]
    param["M"] = 1
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid tx spending 2of2 back to pk1...", json.dumps(param))
    partialtx = call_token_rpc(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    assert partialtx['hex'], 'partial tx not created'
    assert partialtx['PartiallySigned'], 'partially signed object'

    try :
        print("trying to sendrawtransaction partially 1 signed tx... ")
        result = rpc2.sendrawtransaction(partialtx['hex'])
        print("sending partially1 signed tx returned:", result)
        assert not result, 'sending partially signed 2of2 tx should return error'
    except RpcException as e :
        print ('got a normal exception (note "FULFILLMENT NOT ENCODED" msg on console is normal)', e.message)
        pass # should be error 

    tx2of2back = call_rpc_retry(rpc2, "addccv2signature", '', partialtx['hex'], partialtx['PartiallySigned'])
    assert tx2of2back['hex'], 'sig 2 not added to tx'
    txid = rpc2.sendrawtransaction(tx2of2back['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))            
    print("tx 2of2 back sent")

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # try 2of3
    param = {}
    param["tokenid"] = tokenid
    param["destpubkeys"] = [ pubkey1, pubkey2, pubkey3 ]
    param["M"] = 2
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid amount tx to 2of3 tx...", json.dumps(param))
    tx2of3 = call_token_rpc_create_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    txid = rpc1.sendrawtransaction(tx2of3['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))   
    ccaddr2of3 = rpc1.decoderawtransaction(tx2of3['hex'])['vout'][0]['scriptPubKey']['addresses'][0]
    
    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # spend 2of3 to pk1
    param = {}
    param["tokenid"] = tokenid
    param["ccaddressMofN"] = ccaddr2of3
    param["M"] = 1
    param["amount"] = amountback
    param["destpubkeys"] = [ pubkey1 ]
    print("creating tokenv2transferMofN tokenid tx spending 2of3 back to pk1...", json.dumps(param))
    partialtx = call_token_rpc(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    assert partialtx['hex'], 'partial tx not created'
    assert partialtx['PartiallySigned'], 'partially signed object'

    try :
        print("trying to sendrawtransaction partially 1 signed tx... ")
        result = rpc2.sendrawtransaction(partialtx['hex'])
        print("sending partially1 signed tx returned:", result)
        assert not result, 'sending partially signed 2of3 tx should return error'
    except RpcException as e :
        print ('got a normal exception (note "FULFILLMENT NOT ENCODED" msg on console is normal)', e.message)
        pass # should be error 


    # add sig 2
    tx2of3back = call_rpc_retry(rpc2, "addccv2signature", '', partialtx['hex'], partialtx['PartiallySigned'])
    assert tx2of3back['hex'], 'sig 2 not added to tx'
    txid = rpc2.sendrawtransaction(tx2of3back['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))            
    print("tx 2of3 back sent")

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # try 3of3
    param = {}
    param["tokenid"] = tokenid
    param["destpubkeys"] = [ pubkey1, pubkey2, pubkey3 ]
    param["M"] = 3
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid amount tx to 3of3 ...", json.dumps(param))
    tx3of3 = call_token_rpc_create_tx(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    txid = rpc1.sendrawtransaction(tx3of3['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))   
    ccaddr3of3 = rpc1.decoderawtransaction(tx3of3['hex'])['vout'][0]['scriptPubKey']['addresses'][0]

    time.sleep(1) # pause to allow tx in mempool to propagate to prevent double spending in mempool               

    # spend 3of3 to pk1
    param = {}
    param["tokenid"] = tokenid
    param["ccaddressMofN"] = ccaddr3of3
    param["destpubkeys"] = [ pubkey1 ]
    param["M"] = 1
    param["amount"] = amount
    print("creating tokenv2transferMofN tokenid tx spending 3of3 back to pk1...", json.dumps(param))
    partialtx1 = call_token_rpc(rpc1, "tokenv2transferMofN", '', json.dumps(param))
    assert partialtx1['hex'], 'partial tx not created'
    assert partialtx1['PartiallySigned'], 'partially signed object'

    try :
        print("trying to sendrawtransaction partially 1 signed tx... ")
        result = rpc2.sendrawtransaction(partialtx1['hex'])
        print("sending partially1 signed tx returned:", result)
        assert not result, 'sending partially signed 3of3 tx should return error'
    except RpcException as e :
        print ('got a normal exception (note "FULFILLMENT NOT ENCODED" msg on console is normal)', e.message)
        pass # should be error 

    # add sig 2
    partialtx2 = call_rpc_retry(rpc2, "addccv2signature", '', partialtx1['hex'], partialtx1['PartiallySigned'])
    assert partialtx2['hex'], 'sig 2 not added to tx'
    assert partialtx2['PartiallySigned'], 'partially signed object'

    try :
        print("trying to sendrawtransaction partially 2 signed tx... ")
        result = rpc2.sendrawtransaction(partialtx2['hex'])
        print("sending partially2 signed tx returned:", result)
        assert not result, 'sending partially signed 3of3 tx should return error'
    except RpcException as e :
        print ('got a normal exception (note "FULFILLMENT NOT ENCODED" msg on console is normal)', e.message)
        pass # should be error 

    # add sig 3
    tx3of3back = call_rpc_retry(rpc3, "addccv2signature", '', partialtx2['hex'], partialtx2['PartiallySigned'])
    assert tx3of3back['hex'], 'sig 3 not added to tx'

    txid = rpc3.sendrawtransaction(tx3of3back['hex'])
    print("sendrawtransaction result: ", txid)
    assert(check_txid(txid))            
    print("tx 3of3 back sent")


def run_assets_orders(rpc1, rpc2, v, tokenid, total, units, unitprice, isnft):

    retries = 24
    delay = 10
   
    askunits = bidunits = units
    # unitprice = 0.0001

    # wait for mempool to empty to get correct balance: 
    retries = 24
    delay = 10
    for i in range(retries):
        print("calling " + "getmempoolinfo...")
        mempoolinfo = rpc1.getmempoolinfo()
        if int(mempoolinfo["size"]) == 0 :
            break
        if i < retries-1:
            print("retrying " + "getmempoolinfo size=" + str(mempoolinfo["size"]) + '...')
            time.sleep(delay)  

    # get initial balance
    result = call_rpc(rpc1, "token"+v+"balance", tokenid)
    assert(check_tx_result(result))
    initial_balance = int(result["balance"])
    print("initial balance for tokenid " + tokenid + " = " + str(initial_balance))

    # create tokenask 1
    print("creating token"+v+"ask tx #1...")
    askid1 = call_token_rpc_send_tx(rpc1, "token"+v+"ask", '', str(total), tokenid, str(unitprice))

    if not isnft :
        # create tokenask 2
        print("creating token"+v+"ask tx #2...")
        askid2 = call_token_rpc_send_tx(rpc1, "token"+v+"ask", '', str(total), tokenid, str(unitprice))

        # create tokenask 3
        print("creating token"+v+"ask tx #3...")
        askid3 = call_token_rpc_send_tx(rpc1, "token"+v+"ask", '', str(total), tokenid, str(unitprice))

    # fill ask 
    if not isnft :
        print("creating token"+v+"fillask tx #1 in backward order...")
        fillaskid1 = call_token_rpc_send_tx(rpc2, "token"+v+"fillask", '', tokenid, askid3, str(askunits))
        print("creating token"+v+"fillask tx #2...")
        fillaskid2 = call_token_rpc_send_tx(rpc2, "token"+v+"fillask", '', tokenid, askid2, str(askunits))
        print("creating token"+v+"fillask tx #3...")
        fillaskid3 = call_token_rpc_send_tx(rpc2, "token"+v+"fillask", '', tokenid, askid1, str(askunits))
    else :
        print("creating token"+v+"fillask tx #1...")
        fillaskid1 = call_token_rpc_send_tx(rpc2, "token"+v+"fillask", '', tokenid, askid1, str(askunits))


    # cancel ask
    print("creating token"+v+"cancelask tx #1...")
    cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelask", 'ask is empty', tokenid, fillaskid1)
    if not isnft :
        print("creating token"+v+"cancelask tx #2...")
        cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelask", 'ask is empty', tokenid, fillaskid2)
        print("creating token"+v+"cancelask tx #3...")
        cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelask", 'ask is empty', tokenid, fillaskid3)

    # create tokenbid to buy back tokens - same amount
    print("creating token"+v+"bid tx #1...")
    bidid1 = call_token_rpc_send_tx(rpc1, "token"+v+"bid", '', str(askunits), tokenid, str(unitprice))
    if not isnft :
        print("creating token"+v+"bid tx #2...")
        bidid2 = call_token_rpc_send_tx(rpc1, "token"+v+"bid", '', str(askunits), tokenid, str(unitprice))
        print("creating token"+v+"bid tx #3...")
        bidid3 = call_token_rpc_send_tx(rpc1, "token"+v+"bid", '', str(askunits), tokenid, str(unitprice))

    # fill bid 
    print("creating token"+v+"fillbid tx #1...")
    fillbidid1 = call_token_rpc_send_tx(rpc2, "token"+v+"fillbid", '', tokenid, bidid1, str(bidunits))
    if not isnft :
        print("creating token"+v+"fillbid tx #2...")
        fillbidid2 = call_token_rpc_send_tx(rpc2, "token"+v+"fillbid", '', tokenid, bidid2, str(bidunits))
        print("creating token"+v+"fillbid tx #3...")
        fillbidid3 = call_token_rpc_send_tx(rpc2, "token"+v+"fillbid", '', tokenid, bidid3, str(bidunits))

    # cancel bid
    print("creating token"+v+"cancelbid tx #1...")
    cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelbid", 'bid is empty', tokenid, fillbidid1)
    if not isnft :
        print("creating token"+v+"cancelbid tx #2...")
        cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelbid", 'bid is empty', tokenid, fillbidid2)
        print("creating token"+v+"cancelbid tx #3...")
        cancelid = call_token_rpc_send_tx(rpc1, "token"+v+"cancelbid", 'bid is empty', tokenid, fillbidid3)

    # create tokenbid to self to test dust
    print("creating token"+v+"bid tx #4...")
    bidid4 = call_token_rpc_send_tx(rpc1, "token"+v+"bid", '', str(1), tokenid, str(0.0000_0600))  # price more than dust

    # fill bid to self with dust price
    print("creating token"+v+"fillbid tx #4 (to check failure on dust unit price)...")
    # enable this if dust is enabled on normals in the chain:
    # fillbidid4 = call_token_rpc_send_tx(rpc1, "token"+v+"fillbid", '', tokenid, bidid4, str(1), str(0.0000_005)) # fill with dust price (<=500sat)
    # if dust not enabled we should get error:
    fillbidid4 = call_token_rpc(rpc1, "token"+v+"fillbid", '', tokenid, bidid4, str(1), str(0.0000_005)) # fill with dust price (<=500sat)
    assert fillbidid4['hex'], "token"+v+"fillbid" + ' tx not created'

    try :
        print("trying to sendrawtransaction tokenfillbid tx with dust... ")
        result = rpc1.sendrawtransaction(fillbidid4['hex'])
        print("sendrawtransaction tokenfillbid tx with dust returned:", result)
        assert not result, 'sending tokenfillbid tx with dust should return error'
    except RpcException as e :
        print ('got a normal exception for tokenfillbid tx with dust', e.message)
        pass # should be error 
        # balance does not change as this is a tx to self

    # check balance after 
    retries = 24
    delay = 10
    for i in range(retries):
        print("calling " + "token"+v+"balance")
        finresult = call_rpc(rpc1, "token"+v+"balance", tokenid)
        assert(check_tx_result(result))
        if int(finresult["balance"]) == initial_balance :
            break
        if i < retries-1:
            print("retrying " + "token"+v+"balance" + '...')
            time.sleep(delay)                

    assert(int(finresult["balance"]) == initial_balance)

def wait_for_height(rpc, ht) :
    # wait for expiration height
    while True :
        time.sleep(30)
        getinfo = rpc.getinfo()
        h1 = getinfo["blocks"]
        if h1 >= ht :
            break
        print ('waiting for expiration height...')
        myaddr = get_my_address(rpc)
        rpc.sendtoaddress(myaddr, 0.1) # this is for on demand chains to advance a chain

# test expiration height in orders
def run_assets_expired_orders(rpc1, rpc2, v, tokenid, total, units, unitprice, isnft):

    askunits = bidunits = units

    getinfo = rpc1.getinfo()
    hoff = 5
    h0 = getinfo["blocks"]

    # create tokenask with expiration height
    print("creating token"+v+"ask with expiration tx #1...")
    askid1 = call_token_rpc_send_tx(rpc1, "token"+v+"ask", '', str(total), tokenid, str(unitprice), str(h0+hoff))

    print("trying to cancel not yet expired order with other pk: token"+v+"cancelask...")
    result = call_token_rpc_create_tx(rpc2, "token"+v+"cancelask", 'ask is empty', tokenid, askid1)
    assert(check_tx_result(result))
    try :
        cancelid = rpc2.sendrawtransaction(result['hex'])
        assert(get_result_error(cancelid))  # should return error for not yet expired order
    except RpcException as e :
        print ('got a normal exception for tokencancelask not expired', e.message)
        pass # exception is a normal execution    

    # wait for expiration height
    wait_for_height(rpc1, h0 + hoff)

    print("trying to fill expired order token"+v+"fillask tx #1...")
    result = call_token_rpc_create_tx(rpc2, "token"+v+"fillask", '', tokenid, askid1, str(askunits))
    assert(check_tx_result(result))
    try :
        fillaskid1 = rpc2.sendrawtransaction(result['hex'])
        print("fillaskid1", fillaskid1)
        print("get_result_error(fillaskid1)", get_result_error(fillaskid1))
        assert(get_result_error(fillaskid1))  # should return error for yet expired order
    except RpcException as e :
        print ('got a normal exception for tokenfillask expired', e.message)
        pass # exception is a normal execution 

    print("trying to cancel not yet expired order with other pk: token"+v+"cancelask...")
    cancelid2 = call_token_rpc_send_tx(rpc2, "token"+v+"cancelask", 'ask is empty', tokenid, askid1)

    # same for tokenbid
    getinfo = rpc1.getinfo()
    h0 = getinfo["blocks"]

    # create tokenbid with expiration height
    print("creating token"+v+"bid with expiration tx #1...")
    bidid1 = call_token_rpc_send_tx(rpc2, "token"+v+"bid", '', str(total), tokenid, str(unitprice), str(h0+hoff))

    print("trying to cancel not yet expired order with other pk: token"+v+"cancelbid...")
    result = call_token_rpc_create_tx(rpc1, "token"+v+"cancelbid", 'bid is empty', tokenid, bidid1)
    assert(check_tx_result(result))
    try :
        cancelid = rpc1.sendrawtransaction(result['hex'])
        assert(get_result_error(cancelid))  # should return error for not yet expired order
    except RpcException as e :
        print ('got a normal exception for tokencancelbid not expired', e.message)
        pass # should be exception     

    # wait for expiration height
    wait_for_height(rpc1, h0 + hoff)

    print("trying to fill expired order token"+v+"fillbid tx #1...")
    result = call_token_rpc_create_tx(rpc1, "token"+v+"fillbid", '', tokenid, bidid1, str(bidunits))
    assert(check_tx_result(result))
    try :
        fillbidid1 = rpc1.sendrawtransaction(result['hex'])
        assert(get_result_error(fillbidid1))  # should return error for yet expired order
    except RpcException as e :
        print ('got a normal exception for tokenfillbid expired', e.message)
        pass # should be exception here 

    print("trying to cancel not yet expired order with other pk: token"+v+"cancelbid...")
    cancelid3 = call_token_rpc_send_tx(rpc1, "token"+v+"cancelbid", 'bid is empty', tokenid, bidid1)

def check_tokenlist(rpc, v, tokenids) :
    rpcname = "token" + v + "list"
    results = {}
    retries = 24
    delay = 10
    for i in range(retries) :
        tokenlist = call_token_rpc(rpc, rpcname, '')
        for tid in tokenids :
            if tid in tokenlist :
                results[tid] = 1
        if len(results) == len(tokenids) :
            break
        time.sleep(delay)
        print('retrying check_tokenlist waiting for txns to mine...')

    assert len(results) == len(tokenids), "not all tokenids found in " + rpcname + " results"
    print('check_tokenlist okay!')

def check_tokenallbalances(rpc, v, checkbalances) :
    rpcname = "token" + v + "allbalances"
    results = {}
    retries = 24
    delay = 10
    for i in range(retries) :
        tokenbalances = call_token_rpc(rpc, rpcname, '')
        # print('tokenbalances', tokenbalances)
        for i, (checktid, checkamount) in enumerate(checkbalances.items()) :
            for tbalance in tokenbalances :
                for tid, amount in tbalance.items() :  # get tokenid and amount from tokenallbalances items like {'1233937': 100000}
                    if tid == checktid :
                        assert amount == checkamount, 'invalid token amount ' + str(amount) + ' returned from ' + rpcname + ' for tokenid ' + checktid
                        results[tid] = 1
        if len(results) == len(checkbalances) :
            break
        time.sleep(delay)
        print('retrying check_tokenallbalances waiting for txns to mine...')
    assert len(results) == len(checkbalances), "not all tokenids have correct amounts in " + rpcname + " results"
    print('check_tokenallbalances okay!')

def get_kmd_rpc(config) :
    rpc = None
    with open(config) as file:
        lines = file.readlines()

    rpcport = 7771
    cookiename = '__cookie__'
    cookie = ''
    for l in lines :
        t = l.split(':')
        if len(t) == 2 : 
            if t[0] == cookiename :
                cookie = t[1]

    rpc = rpclib.rpc_connect(cookiename, cookie, int(rpcport))
    return rpc


def get_as_chain_rpc(config) :
    rpc = None
    with open(config) as file:
        lines = file.readlines()
    rpcuser = ''
    rpcpassword = ''
    rpcport = 0
    for l in lines :
        t = l.split('=')
        # print('t=', t)
        if len(t) == 2 : 
            if t[0] == 'rpcuser' :
                rpcuser = t[1].strip()
            elif t[0] == 'rpcpassword' :
                rpcpassword = t[1].strip()
            elif t[0] == 'rpcport' :
                rpcport = int(t[1])

    print('rpcuser', rpcuser, 'rpcpassword', rpcpassword, 'rpcport', rpcport)
    rpc = rpclib.rpc_connect(rpcuser, rpcpassword, int(rpcport))
    return rpc

def get_chain_rpc(config) :

    rpc = None
    # print('config[-4:]', config[-4:])
    if config[-4:] == 'conf' :
        rpc = get_as_chain_rpc(config)
    elif config[-6:] == 'cookie' :
        rpc = get_kmd_rpc(config)
    else :
        assert False, 'could not find connection params'

    rpc.getinfo() # try connect
    print('rpc connected')
    return rpc

if __name__ == "__main__":
    print("starting assets orders test")
    print("plz first start a chain of three nodes and enter rpc params in the code")
    print("ensure you have at least 10 utxos in the nodes' wallets for batch transaction creation")

    rpc1 = get_chain_rpc('/Users/dimxy/Library/Application Support/Komodo/DIMXY31/DIMXY31.conf')
    rpc2 = get_chain_rpc('/Users/dimxy/repo/tokel/src/DIMXY31/nodes/1/DIMXY31.conf')
    rpc3 = get_chain_rpc('/Users/dimxy/repo/tokel/src/DIMXY31/nodes/2/DIMXY31.conf')


    # time.sleep(2)   
    # makeTuiConfig()
    # main()
    run_tokens_create(rpc1, rpc2, rpc3)

