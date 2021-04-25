#!/usr/bin/env python3

from lib import rpclib, tuilib
import os
import time

header = "komodo assets cc tokenask/bid v2 test\n"

def get_result_error(r):
    if isinstance(r, dict) :
        if r.get('result') == "error" :
            return r.get('error')
    return ''

def check_result(r):
    if isinstance(r, str) :
        # print("r is str, true")
        return True
    elif isinstance(r, dict) :
        # print("r is dict")
        if isinstance(r.get('hex'), str) :
            # print("r has hex, true")
            return True
        if r.get('result') == "error" :
            # print("r has error, false")
            return False
    # print("r has unknown, true")
    return True
    
def run_tokens_create(rpc):

    # DIMXY20 
    rpc1 = rpclib.rpc_connect("user1875608407", "passb64b66f9e1debbd3257e39976ba5a43e1c6515a8ced77f426b41ba954f0a562f66", 14723)
    rpc2 = rpclib.rpc_connect("user306071284", "pass965dde8333f6c574c138d787a6ecdc22fad32b236e3580b62b2b0a2241d1489a04", 15723)

    #tokenid = "b08775cf3b3b371f97256b427af005d5573a2868106a8c4dd4e8c76e87d476d7" # fungible
    #tokenid = "3b0003561fb98b332452e71208f13e4379fa6987577c3dcd2aef6199b54111ae" # NFT 0 royalty
    #tokenid = "03b0018da699af63a40595cc93b5e3b097a88225e51e767efbe1425aa4698a65" # NFT 1/1000 royalty - spent
    #tokenid = "9f051912b13b707b64b65bb179649901449f7ae78d74e78a813dffb2cf7705f8" # NFT eval=00

    print("creating fungible token...")
    result = rpc1.tokenv2create("T1", str(0.1))
    assert(check_result(result))
    tokenid = rpc1.sendrawtransaction(result['hex'])
    print("created token:", tokenid)
    run_assets_orders(rpc1, rpc2, tokenid)

    print("creating NFT with 00...")
    result = rpc1.tokenv2create("NFT1", str(0.00000001), "nft eval 00", "00010203")
    assert(check_result(result))
    tokenid = rpc1.sendrawtransaction(result['hex'])
    print("created token:", tokenid)
    run_assets_orders(rpc1, rpc2, tokenid)

    print("creating NFT with F7, no royaly, with arbitary data...")
    result = rpc1.tokenv2create("NFT1", str(0.00000001), "nft eval=f7 arbitrary=hello", "F70101ee020d687474703a2f2f6d792e6f7267040568656c6c6f")
    assert(check_result(result))
    tokenid = rpc1.sendrawtransaction(result['hex'])
    print("created token:", tokenid)
    run_assets_orders(rpc1, rpc2, tokenid)

    print("creating NFT with F7 and royaly 0xAA...")
    result = rpc1.tokenv2create("NFT1", str(0.00000001), "nft eval=f7 roaylty=AA", "F70101ee020d687474703a2f2f6d792e6f726703AA")
    assert(check_result(result))
    tokenid = rpc1.sendrawtransaction(result['hex'])
    print("created token:", tokenid)
    run_assets_orders(rpc1, rpc2, tokenid)
    print("tests finished okay")
    time.sleep(3)
    exit

def run_assets_orders(rpc1, rpc2, tokenid):

    retries = 24
    delay = 10
   
    total = 1 
    askunits = 1
    bidunits = 1
    unitprice = 0.0001
    # rpc1.setgenerate(True, 2)

    for i in range(1):
        try:
            name = "none"
        except KeyboardInterrupt:
            break
        else:
            # create tokenask
            for i in range(retries):
                print("creating tokenv2ask tx...")
                result = rpc1.tokenv2ask(str(total), tokenid, str(unitprice))
                print("create tx result:", result)
                if  check_result(result):
                    break
                if i < retries-1:
                    print("retrying")
                    time.sleep(delay)                
            assert(check_result(result))
            print("ask tx created")
            askid = rpc1.sendrawtransaction(result['hex'])
            print("send result:", askid)
            assert(check_result(askid))            
            print("ask tx sent")

            # fill ask 
            print("creating tokenv2fillask tx...")
            for i in range(retries):
                result = rpc2.tokenv2fillask(tokenid, askid, str(askunits))
                print("tokenv2fillask result:", result)
                if  check_result(result):
                    break
                if i < retries-1:
                    print("retrying")
                    time.sleep(delay)
            assert(check_result(result))
            print("fill ask tx created")
            askid = rpc2.sendrawtransaction(result['hex'])
            assert(check_result(askid))
            print("fill ask tx sent", askid)

            # cancel ask
            print("creating tokenv2cancelask tx...")
            for i in range(retries):
                result = rpc1.tokenv2cancelask(tokenid, askid)
                print("cancel ask result:", result)
                if check_result(result):
                    break
                if get_result_error(result) == 'ask is empty' :  # stop after 2 retries if NFT (no cancel)
                    break
                if i < retries-1:
                    print("retrying")
                    time.sleep(delay)
            if get_result_error(result) == 'ask is empty' :   # for NFT cancel should be error
                assert(not check_result(result))
                print("cancel bid tx not created")
            else :
                assert(check_result(result))
                print("cancel ask tx created")
                cancelid = rpc1.sendrawtransaction(result['hex'])
                assert(check_result(cancelid))
                print("cancel ask tx sent", cancelid)
            
            # create tokenbid to buy back tokens 
            print("creating tokenv2bid tx...")
            for i in range(retries):
                result = rpc1.tokenv2bid(str(askunits), tokenid, str(unitprice))
                print("create tx result:", result)
                if  check_result(result):
                    break
                if i < retries-1:
                    print("retrying")
                    time.sleep(delay)
            assert(check_result(result))
            print("bid tx created")
            bidid = rpc1.sendrawtransaction(result['hex'])
            print("send result:", bidid)
            assert(check_result(bidid))
            print("bid tx sent")

            # fill bid 
            print("creating tokenv2fillbid tx...")
            for i in range(retries):
                result = rpc2.tokenv2fillbid(tokenid, bidid, str(bidunits))
                print("create tx result:", result)
                if check_result(result) :
                    break
                if i < retries-1 :
                    print("retrying fill bid")
                    time.sleep(delay)
            assert(check_result(result))
            print("fill bid tx created")
            bidid = rpc2.sendrawtransaction(result['hex'])
            assert(check_result(bidid))
            print("fill bid tx sent", bidid)

            # cancel bid
            print("creating tokenv2cancelbid tx...")
            for i in range(retries):
                result = rpc1.tokenv2cancelbid(tokenid, bidid)
                print("create tx result:", result)
                if check_result(result) :
                    break
                if get_result_error(result) == 'bid is empty' :  # stop after 2 retries if NFT
                    break
                if i < retries-1 :
                    print("retrying")
                    time.sleep(delay)
            if get_result_error(result) == 'bid is empty' :   # for NFT cancel should be error
                assert(not check_result(result))
                print("cancel bid tx not created")
            else:
                assert(check_result(result))
                print("cancel bid tx created")
                cancelid = rpc1.sendrawtransaction(result['hex'])
                assert(check_result(cancelid))
                print("cancel bid tx sent", cancelid)


menuItems = [
    {"run assets orders": run_tokens_create},
    {"Exit": exit}
]


def main():
    while True:
        os.system('clear')
        print(tuilib.colorize(header, 'pink'))
        print(tuilib.colorize('CLI version 0.2\n', 'green'))
        for item in menuItems:
            print(tuilib.colorize("[" + str(menuItems.index(item)) + "] ", 'blue') + list(item.keys())[0])
        choice = input(">> ")
        try:
            if int(choice) < 0:
                raise ValueError
            # Call the matching function
            if list(menuItems[int(choice)].keys())[0] == "Exit":
                list(menuItems[int(choice)].values())[0]()
            else:
                list(menuItems[int(choice)].values())[0](rpc_connection)
        except (ValueError, IndexError):
            pass


if __name__ == "__main__":
    print("starting assets orders test")
    rpc_connection = ""
    main()


