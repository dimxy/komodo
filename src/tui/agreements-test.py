#!/usr/bin/env python3

from ast import Num
from unicodedata import numeric
from lib import rpclib, tuilib
import os
import time
import json
import argparse
from slickrpc.exc import RpcException
import configparser

# test assets impl with mustpay... evals

def check_tx_result(tx) :
    assert tx['result'] != 'error', tx

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


def run_tokentags_tests(rpc1, rpc2, rpc3) :

    for i in range(1) :

        print('starting tokenv2create')
        tokentx = rpc1.tokenv2create('TESTTOK', '0.0001')
        # print('tokentx', tokentx)
        check_tx_result(tokentx)
        tokentxid = rpc1.sendrawtransaction(tokentx['hex'])

        '''
        print('starting tokenv2create')
        tokentx2 = rpc1.tokenv2create('TESTTOK2', '0.0001')
        check_tx_result(tokentx2)
        tokentxid2 = rpc1.sendrawtransaction(tokentx2['hex'])
        '''

        wait_until_confirmed(rpc1, tokentxid)
        # wait_until_confirmed(rpc1, tokentxid2)
        print('token created', tokentxid)
        # print('token 2 created', tokentxid2)


        print('starting tokentagcreate')
        # tagtx = rpc1.tokentagcreate(tokentxid, '10000', 'mytag', '6000', 'mydata', '0', tokentxid2)
        # tagtx = rpc1.tokentagcreate(tokentxid, '9999', 'mytag', '6000', 'mydata', '0')
        tagtx = rpc1.tokentagcreate(tokentxid, '10000', 'mytag', '6000', 'mydata', '0')
        # print('tagtx', tagtx)
        check_tx_result(tagtx)
        tagtxid = rpc1.sendrawtransaction(tagtx['hex'])
        wait_until_confirmed(rpc1, tagtxid)
        print('tokentag created', tagtxid)

        '''
        print('starting tokentagcreate 2')
        tagtx2 = rpc1.tokentagcreate(tokentxid, '10000', 'mytag', '6000', 'mydata2', '0')
        check_tx_result(tagtx2)
        tagtxid2 = rpc1.sendrawtransaction(tagtx2['hex'])
        wait_until_confirmed(rpc1, tagtxid2)
        print('tokentag created', tagtxid2)
        '''

        print('starting tokentagupdate 1')
        updtx1 = rpc1.tokentagupdate(tagtxid, '5001', 'mydata2')
        # updtx1 = rpc1.tokentagupdate(tagtxid, '5001', 'mydata2', tagtxid2) # try spend a different tagtxid2
        # updtx1 = rpc1.tokentagupdate(tokentxid, '5001', 'mydata2')
        # print('updtx1', updtx1)
        check_tx_result(updtx1)
        updtxid1 = rpc1.sendrawtransaction(updtx1['hex'])
        wait_until_confirmed(rpc1, updtxid1)
        print('tokentagupdate 1 created', updtxid1)

        print('starting tokentagupdate 2')
        updtx2 = rpc1.tokentagupdate(tagtxid, '5001',  'mydata3')
        # print('updtx2', updtx2)
        check_tx_result(updtx2)
        updtxid2 = rpc1.sendrawtransaction(updtx2['hex'])
        wait_until_confirmed(rpc1, updtxid2)
        print('tokentagupdate 2 created', updtxid2)

        print('starting tokentagupdate 3')
        updtx3 = rpc1.tokentagupdate(tagtxid, '9997', 'mydata4')
        # print('updtx3', updtx3)
        check_tx_result(updtx3)
        updtxid3 = rpc1.sendrawtransaction(updtx3['hex'])
        wait_until_confirmed(rpc1, updtxid3)
        print('tokentagupdate 3 created', updtxid3)

        print('starting tokentagupdate 4')
        updtx4 = rpc1.tokentagupdate(tagtxid, '9996',  'mydata5')
        # print('updtx4', updtx4)
        check_tx_result(updtx4)
        updtxid4 = rpc1.sendrawtransaction(updtx4['hex'])
        wait_until_confirmed(rpc1, updtxid4)
        print('tokentagupdate 4 created', updtxid4)

        hist = rpc1.tokentaghistory(tagtxid)
        assert len(hist) == 5


def run_agreements_tests(rpc1, rpc2, rpc3) :

    for i in range(1) :

        print('starting agreementcreate')
        agreementtx = rpc1.agreementcreate('034777b18effce6f7a849b72de8e6810bf7a7e050274b3782e1b5a13d0263a44dc', 'myagree', 'memo', '0.22', '0.11', '0.002', '025f97b6c42409e8e69eb2fdab281219aafe15169deec801ee621c63cc1ba0bb8c')
        # print('agreementtx', agreementtx)
        check_tx_result(agreementtx)
        agreementtxid = rpc1.sendrawtransaction(agreementtx['hex'])
        wait_until_confirmed(rpc2, agreementtxid)
        print('agreement offer created', agreementtxid)

        rpc_accept = rpc2
        print('starting agreementaccept')
        accepttx = rpc_accept.agreementaccept(agreementtxid)
        # print('accepttx', accepttx)
        check_tx_result(accepttx)
        accepttxid = rpc_accept.sendrawtransaction(accepttx['hex'])
        wait_until_confirmed(rpc_accept, accepttxid)
        print('agreement accepted', accepttxid)

        print('starting agreementamend')
        amendtx = rpc1.agreementamend(accepttxid, 'myagree2', 'memo2', '0.23', '0.12', '0.003', '025f97b6c42409e8e69eb2fdab281219aafe15169deec801ee621c63cc1ba0bb8c')
        # print('amendtx', amendtx)
        check_tx_result(amendtx)
        amendtxid = rpc1.sendrawtransaction(amendtx['hex'])
        wait_until_confirmed(rpc2, amendtxid)
        print('agreement amend offer created', amendtxid)

        print('starting agreementaccept for amended')
        accepttx2 = rpc2.agreementaccept(amendtxid)
        # print('accepttx2', accepttx2)
        check_tx_result(accepttx2)
        accepttxid2 = rpc2.sendrawtransaction(accepttx2['hex'])
        wait_until_confirmed(rpc1, accepttxid2)
        print('amended agreement accepted', accepttxid2)


        if i == 1 :
            '''
            # cannot dispute amended agreement:
            print('starting agreementdispute')
            disputetx = rpc1.agreementdispute(accepttxid, 'mydispute', '1', accepttxid2) # add fake accepttxid2 to spend event vin
            # print('disputetx', disputetx)
            check_tx_result(disputetx)
            disputetxid = rpc1.sendrawtransaction(disputetx['hex'])
            wait_until_confirmed(rpc3, disputetxid)
            print('dispute started')

            print('starting agreementresolve')
            resolvetx = rpc3.agreementresolve(disputetxid, '0.01')
            # print('resolvetx', resolvetx)
            check_tx_result(resolvetx)
            resolvetxid = rpc3.sendrawtransaction(resolvetx['hex'])
            wait_until_confirmed(rpc3, resolvetxid)
            print('dispute resolved')
            '''

            # try for amended agreement
            print('starting agreementdispute 2 for amended')
            disputetx2 = rpc1.agreementdispute(accepttxid2, 'mydispute amended')
            # print('disputetx2', disputetx2)
            check_tx_result(disputetx2)
            disputetxid2 = rpc1.sendrawtransaction(disputetx2['hex'])
            wait_until_confirmed(rpc3, disputetxid2)
            print('dispute 2 started')


            print('starting agreementresolve 2 for amended')
            resolvetx2 = rpc3.agreementresolve(disputetxid2, '0.01')
            # print('resolvetx2', resolvetx2)
            check_tx_result(resolvetx2)
            resolvetxid2 = rpc3.sendrawtransaction(resolvetx2['hex'])
            wait_until_confirmed(rpc3, resolvetxid2)
            print('dispute 2 resolved')

        else :
            '''
            # failed bcz amended
            print('starting agreementclose')
            closetx = rpc1.agreementclose(accepttxid, 'myclose', 'memo', '0.02')
            # print('closetx', closetx)
            check_tx_result(closetx)
            closetxid = rpc1.sendrawtransaction(closetx['hex'])
            wait_until_confirmed(rpc1, closetxid)
            print('agreement close offer created')

            print('starting agreementaccept for close')
            acceptclosetx = rpc2.agreementaccept(closetxid, amendtxid)  # add amend offer txid to create probe cond
            # print('acceptclosetx', acceptclosetx)
            check_tx_result(acceptclosetx)
            acceptclosetxid = rpc2.sendrawtransaction(acceptclosetx['hex'])
            wait_until_confirmed(rpc1, acceptclosetxid)
            print('agreement close accepted', acceptclosetxid)
            '''

            print('starting agreementclose 2 for amended')
            closetx2 = rpc1.agreementclose(accepttxid2, 'myclose2', 'memo2', '0.02')
            # print('closetx2', closetx2)
            check_tx_result(closetx2)
            closetxid2 = rpc1.sendrawtransaction(closetx2['hex'])
            wait_until_confirmed(rpc1, closetxid2)
            print('agreement 2 close offer created')

            print('starting agreementaccept for close amended')
            acceptclosetx2 = rpc2.agreementaccept(closetxid2)
            # print('acceptclosetx2', acceptclosetx2)
            check_tx_result(acceptclosetx2)
            acceptclosetxid2 = rpc2.sendrawtransaction(acceptclosetx2['hex'])
            wait_until_confirmed(rpc1, acceptclosetxid2)
            print('agreement close amended accepted', acceptclosetxid2)            

            '''
            print('starting agreementclose 3 for already closed amended')
            closetx3 = rpc1.agreementclose(accepttxid2, 'myclose3', 'memo2', '0.022')
            # print('closetx3', closetx3)
            check_tx_result(closetx3)
            closetxid3 = rpc1.sendrawtransaction(closetx3['hex'])
            wait_until_confirmed(rpc1, closetxid3)
            print('agreement 2(2) close offer created')

            # failed with inputs already spent
            print('starting agreementaccept for close amended (closed)')
            acceptclosetx3 = rpc2.agreementaccept(closetxid3)
            # print('acceptclosetx3', acceptclosetx3)
            check_tx_result(acceptclosetx3)
            acceptclosetxid3 = rpc2.sendrawtransaction(acceptclosetx3['hex'])
            wait_until_confirmed(rpc1, acceptclosetxid3)
            print('agreement 2(2) close amended accepted', acceptclosetxid3)   
            '''


if __name__ == "__main__":
    print("Starting agreements/tokentags tests...")
    parser = argparse.ArgumentParser(epilog="Plz first start a chain of three nodes and use the path to one of your nodes' config files to run a test method")
    parser.add_argument("test", help='test name etc')
    args = parser.parse_args()
    # print(args)

    rpc1 = get_chain_rpc('/Users/dimxy//Library/Application Support/Komodo/VL02/VL02.conf')
    rpc2 = get_chain_rpc('/Users/dimxy/repo/komodo-vleppo/src/nodes/1/VL02.conf')
    rpc3 = get_chain_rpc('/Users/dimxy/repo/komodo-vleppo/src/nodes/2/VL02.conf')

    if args.test == 'agreements' :
        run_agreements_tests(rpc1, rpc2, rpc3)
        print("Agreements tests finished")
    elif args.test == 'tokentags' :
        run_tokentags_tests(rpc1, rpc2, rpc3)
        print("Tokentags tests finished")
    else :
        print("Unknown test name")


