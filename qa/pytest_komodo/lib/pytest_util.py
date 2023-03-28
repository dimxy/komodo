import time
import jsonschema
import os
import random
import string
import hashlib
import re
import requests
from lib.komodo_requests import Proxy

class CCInstance:
    def __init__(self, test_params: dict):
        """Base CC Instance class to wrap test_params data"""
        self.rpc = [test_params.get(node).get('rpc') for node in test_params.keys()]
        self.pubkey = [test_params.get(node).get('pubkey') for node in test_params.keys()]
        self.address = [test_params.get(node).get('address') for node in test_params.keys()]
        self.instance = None


class RewardsCC(CCInstance):
    def __init__(self, test_params: dict):
        super().__init__(test_params)
        self.base_plan = None

    def new_rewardsplan(self, proxy, schema=None):
        name = randomstring(4)
        amount = '250'
        apr = '25'
        mindays = '0'
        maxdays = '10'
        mindeposit = '10'
        res = proxy.rewardscreatefunding(name, amount, apr, mindays, maxdays, mindeposit)
        if schema:
            validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, proxy)
        rewardsplan = {
            'fundingtxid': txid,
            'name': name
        }
        if not self.base_plan:
            self.base_plan = rewardsplan
        return rewardsplan

    @staticmethod
    def rewardsinfo_maincheck(proxy, fundtxid, schema):
        res = proxy.rewardsinfo(fundtxid)
        validate_template(res, schema)
        assert res.get('result') == 'success'

    @staticmethod
    def rewardsaddfunding_maincheck(proxy, fundtxid, schema):
        name = proxy.rewardsinfo(fundtxid).get('name')
        amount = proxy.rewardsinfo(fundtxid).get('mindeposit')  # not related to mindeposit here, just to get amount
        res = proxy.rewardsaddfunding(name, fundtxid, amount)
        validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, proxy)

    @staticmethod
    def un_lock_maincheck(proxy, fundtxid, schema):
        name = proxy.rewardsinfo(fundtxid).get('name')
        amount = proxy.rewardsinfo(fundtxid).get('mindeposit')
        res = proxy.rewardslock(name, fundtxid, amount)
        validate_template(res, schema)
        assert res.get('result') == 'success'
        locktxid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(locktxid, proxy)
        print('\nWaiting some time to gain reward for locked funds')
        time.sleep(10)
        res = proxy.rewardsunlock(name, fundtxid, locktxid)
        print(res)
        validate_template(res, schema)
        assert res.get('result') == 'error'  # reward is less than txfee atm


class ChannelsCC(CCInstance):
    def __init__(self, test_params: dict):
        super().__init__(test_params)
        self.base_channel = None

    def new_channel(self, proxy: object, destpubkey: str, numpayments='10',
                    paysize='100000', schema=None, tokenid=None) -> dict:
        if tokenid:
            res = proxy.channelsopen(destpubkey, numpayments, paysize, tokenid)
        else:
            res = proxy.channelsopen(destpubkey, numpayments, paysize)
        if schema:
            validate_template(res, schema)
        open_txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(open_txid, proxy)
        channel = {
            'open_txid': open_txid,
            'number_of_payments': numpayments,
        }
        if tokenid:
            channel.update({'tokenid': tokenid})
        if not self.base_channel:
            self.base_channel = channel
        return channel

    @staticmethod
    def channelslist_get(proxy: object, schema=None) -> str:
        res = proxy.channelslist()
        open_txid = None
        if schema:
            validate_template(res, schema)
        # check dict items returned to find first available channel
        for key in res.keys():
            if validate_tx_pattern(key):
                open_txid = key
                break
        return open_txid


class OraclesCC(CCInstance):
    def __init__(self, test_params: dict):
        super().__init__(test_params)
        self.base_oracle = None

    def new_oracle(self, proxy, schema=None, description="test oracle", o_type=None):
        name = randomstring(8)
        if not o_type:
            o_type = "s"
            res = proxy.oraclescreate(name, description, o_type)
        elif isinstance(o_type, str):
            res = proxy.oraclescreate(name, description, o_type)
        elif isinstance(o_type, list):
            txid = ""
            oracles = []
            for single_o_type in o_type:
                res = proxy.oraclescreate(name, description, single_o_type)
                txid = proxy.sendrawtransaction(res.get('hex'))
                oracles.append({
                                'format': single_o_type,
                                'name': name,
                                'description': description,
                                'oracle_id': txid
                               })
            mine_and_waitconfirms(txid, proxy)
            return oracles
        else:
            raise TypeError("Invalid oracles format: ", o_type)
        if schema:
            validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, proxy)
        oracle = {
            'format': o_type,
            'name': name,
            'description': description,
            'oracle_id': txid
        }
        if not self.base_oracle:
            self.base_oracle = oracle
        return oracle


class TokenCC(CCInstance):
    def __init__(self, test_params: dict):
        super().__init__(test_params)
        self.base_token = None

    def new_token(self, proxy, schema=None):
        name = randomstring(8)
        amount = '0.10000000'
        res = proxy.tokencreate(name, amount, "test token 1")
        if schema:
            validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, proxy)
        token = {
            'tokenid': txid,
            'name': name
        }
        if not self.base_token:
            self.base_token = token
        return token


class DiceCC(CCInstance):
    def __init__(self, test_params: dict):
        super().__init__(test_params)
        self.open_casino = None

    def new_casino(self, proxy, schema=None):
        rpc1 = proxy
        name = randomstring(4)
        funds = '777'
        minbet = '1'
        maxbet = '77'
        maxodds = '10'
        timeoutblocks = '5'
        res = rpc1.dicefund(name, funds, minbet, maxbet, maxodds, timeoutblocks)
        if schema:
            validate_template(res, schema)
        assert res.get('result') == 'success'
        txid = rpc1.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(txid, rpc1)
        casino = {
            'fundingtxid': txid,
            'name': name,
            'minbet': minbet,
            'maxbet': maxbet,
            'maxodds': maxodds
        }
        if not self.open_casino:
            self.open_casino = casino
        return casino

    @staticmethod
    def diceinfo_maincheck(proxy, fundtxid, schema):
        res = proxy.diceinfo(fundtxid)
        validate_template(res, schema)
        assert res.get('result') == 'success'

    @staticmethod
    def diceaddfunds_maincheck(proxy, amount, fundtxid, schema):
        name = proxy.diceinfo(fundtxid).get('name')
        res = proxy.diceaddfunds(name, fundtxid, amount)
        validate_template(res, schema)
        assert res.get('result') == 'success'
        addtxid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(addtxid, proxy)

    @staticmethod
    def dicebet_maincheck(proxy, casino, schema):
        res = proxy.dicebet(casino.get('name'), casino.get('fundingtxid'), casino.get('minbet'), casino.get('maxodds'))
        validate_template(res, schema)
        assert res.get('result') == 'success'
        bettxid = proxy.sendrawtransaction(res.get('hex'))
        mine_and_waitconfirms(bettxid, proxy)
        return bettxid

    @staticmethod
    def dicestatus_maincheck(proxy, casino, bettx, schema):
        res = proxy.dicestatus(casino.get('name'), casino.get('fundingtxid'), bettx)
        validate_template(res, schema)
        assert res.get('result') == 'success'

    @staticmethod
    def dicefinsish_maincheck(proxy, casino, bettx, schema):
        res = proxy.dicefinish(casino.get('name'), casino.get('fundingtxid'), bettx)
        validate_template(res, schema)
        assert res.get('result') == 'success'

    @staticmethod
    def create_entropy(proxy, casino):
        amount = '1'
        for i in range(100):
            res = proxy.diceaddfunds(casino.get('name'), casino.get('fundingtxid'), amount)
            fhex = res.get('hex')
            proxy.sendrawtransaction(fhex)
        checkhex = proxy.diceaddfunds(casino.get('name'), casino.get('fundingtxid'), amount).get('hex')
        tx = proxy.sendrawtransaction(checkhex)
        mine_and_waitconfirms(tx, proxy)


def create_proxy(node_params_dictionary):
    try:
        proxy = Proxy(node_params_dictionary, timeout=120)
    except Exception as e:
        raise Exception("Connection error! Probably no daemon on selected port. Error: ", e)
    return proxy


def env_get(var, default):
    try:
        res = os.environ[var]
    except KeyError:
        res = default
    return res


def get_chainstate(proxy):
    vals = {}
    res = proxy.getinfo()
    vals.update({'synced': res.get('synced')})
    vals.update({'notarized': res.get('notarized')})
    vals.update({'blocks': res.get('blocks')})
    vals.update({'longestchain': res.get('longestchain')})
    return vals


def get_notary_stats():
    api = "https://komodostats.com/api/notary/summary.json"
    local = "notary.json"
    data = requests.get(api).json()
    with open(local, 'w') as lf:
        lf.write(str(data))
    return data


def check_notarized(proxy, api_stats, coin, blocktime=60):
    maxblocksdiff = round(1500 / blocktime)
    daemon_stats = proxy.getinfo()
    notarizations = {}
    for item in api_stats:
        if item.get('ac_name') == coin:
            notarizations = item
    if not notarizations:
        raise BaseException("Chain notary data not found")
    if daemon_stats['notarized'] == notarizations['notarized']:
        assert daemon_stats['notarizedhash'] == notarizations['notarizedhash']
        assert daemon_stats['notarizedtxid'] == notarizations['notarizedtxid']
        return True
    elif abs(daemon_stats['notarazied'] - notarizations['notarized']) >= maxblocksdiff:
        return False
    else:
        assert daemon_stats['notarized']
        assert daemon_stats['notarizedhash'] != '0000000000000000000000000000000000000000000000000000000000000000'
        assert daemon_stats['notarizedtxid'] != '0000000000000000000000000000000000000000000000000000000000000000'
        return True


def validate_proxy(env_params_dictionary, proxy, node=0):
    attempts = 0
    while True:  # base connection check
        try:
            getinfo_output = proxy.getinfo()
            print(getinfo_output)
            break
        except Exception as e:
            print("Coennction failed, error: ", e, "\nRetrying")
            attempts += 1
            time.sleep(10)
        if attempts > 15:
            raise ChildProcessError("Node ", node, " does not respond")
    print("IMPORTING PRIVKEYS")
    res = proxy.importprivkey(env_params_dictionary.get('test_wif')[node], '', True)
    print(res)
    assert proxy.validateaddress(env_params_dictionary.get('test_address')[node])['ismine']
    try:
        pubkey = env_params_dictionary.get('test_pubkey')[node]
        assert proxy.getinfo()['pubkey'] == pubkey
    except (KeyError, IndexError):
        print("\nNo -pubkey= runtime parameter specified")
    assert proxy.verifychain()
    time.sleep(15)
    print("\nBalance: " + str(proxy.getbalance()))
    print("Each node should have at least 777 coins to perform CC tests\n")


def enable_mining(proxy):
    cores = os.cpu_count()
    if cores > 2:
        threads_count = cores - 2
    else:
        threads_count = 1
    tries = 0
    while True:
        try:
            proxy.setgenerate(True, threads_count)
            break
        except Exception as e:
            print(e, " Waiting chain startup\n")
            time.sleep(10)
            tries += 1
        if tries > 30:
            raise ChildProcessError("Node did not start correctly, aborting\n")


def mine_and_waitconfirms(txid, proxy, confs_req=2):  # should be used after tx is send
    # we need the tx above to be confirmed in the next block
    attempts = 0
    while True:
        try:
            confirmations_amount = proxy.getrawtransaction(txid, 1)['confirmations']
            if confirmations_amount < confs_req:
                print("\ntx is not confirmed yet! Let's wait a little more")
                time.sleep(5)
            else:
                print("\ntx confirmed")
                return True
        except KeyError as e:
            print("\ntx is in mempool still probably, let's wait a little bit more\nError: ", e)
            time.sleep(6)
            attempts += 1
            if attempts < 100:
                pass
            else:
                print("\nwaited too long - probably tx stuck by some reason")
                return False


def validate_transaction(proxy, txid, conf_req):
    try:
        isinstance(proxy, Proxy)
    except Exception as e:
        raise TypeError("Not a Proxy object, error: " + str(e))
    conf = 0
    while conf < conf_req:
        print("\nWaiting confirmations...")
        resp = proxy.gettransaction(txid)
        conf = resp.get('confirmations')
        time.sleep(2)


def validate_template(blocktemplate, schema=''):  # BIP 0022
    blockschema = {
        'type': 'object',
        'required': ['bits', 'curtime', 'height', 'previousblockhash', 'version', 'coinbasetxn'],
        'properties': {
            'capabilities': {'type': 'array',
                             'items': {'type': 'string'}},
            'version': {'type': ['integer', 'number']},
            'previousblockhash': {'type': 'string'},
            'finalsaplingroothash': {'type': 'string'},
            'transactions': {'type': 'array',
                             'items': {'type': 'object'}},
            'coinbasetxn': {'type': 'object',
                            'required': ['data', 'hash', 'depends', 'fee', 'required', 'sigops'],
                            'properties': {
                                'data': {'type': 'string'},
                                'hash': {'type': 'string'},
                                'depends': {'type': 'array'},
                                'fee': {'type': ['integer', 'number']},
                                'sigops': {'type': ['integer', 'number']},
                                'coinbasevalue': {'type': ['integer', 'number']},
                                'required': {'type': 'boolean'}
                            }
                            },
            'longpollid': {'type': 'string'},
            'target': {'type': 'string'},
            'mintime': {'type': ['integer', 'number']},
            'mutable': {'type': 'array',
                        'items': {'type': 'string'}},
            'noncerange': {'type': 'string'},
            'sigoplimit': {'type': ['integer', 'number']},
            'sizelimit': {'type': ['integer', 'number']},
            'curtime': {'type': ['integer', 'number']},
            'bits': {'type': 'string'},
            'height': {'type': ['integer', 'number']}
        }
    }
    if not schema:
        schema = blockschema
    jsonschema.validate(instance=blocktemplate, schema=schema)


# two or more nodes are in sync if they have same number of blocks
def check_synced(*proxies):
    while True:
        tries = 0
        blocks_set = set()
        for proxy in proxies:
            blocks = proxy.getinfo().get('blocks')
            blocks_set.add(blocks)

        if  len(blocks_set) == 1: # all have same 'blocks' value
            print("Synced\n")
            break
        else:
            print("Waiting for sync\nAttempt: ", tries + 1, "\n")
            time.sleep(10)
            tries += 1
            if tries > 120:  # up to 20 minutes
                return False
    return True


def randomstring(length):
    chars = string.ascii_letters
    return ''.join(random.choice(chars) for i in range(length))


def in_99_range(compare, base):
    if compare >= 0.99*base:
        return True
    else:
        return False


def compare_rough(base, comp, limit=30):
    if base >= comp - limit:
        return True
    else:
        return False


def collect_orderids(rpc_response, dict_key):  # see dexp2p tests in modules
    orderids = []
    for item in rpc_response.get(dict_key):
        orderids.append(str(item.get('id')))
    return orderids


def randomhex():  # returns 64 chars long pubkey-like hex string
    chars = string.hexdigits
    return (''.join(random.choice(chars) for i in range(64))).lower()


def write_file(filename):  # creates text file
    lines = 10
    content = ''
    for x in range(lines):
        content += randomhex() + '\n'
    with open(filename, 'w') as f:
        f.write(str('filename\n'))
        f.write(content)
    return True


def write_empty_file(filename: str, size: int):  # creates empty file slightly bigger than size in mb
    if os.path.isfile(filename):
        os.remove(filename)
    with open(filename, 'wb') as f:
        f.seek((size * 1024 * 1025) - 1)
        f.write(b'\0')


def get_size(file):
    if os.path.isfile(file):
        return os.path.getsize(file)
    else:
        raise FileNotFoundError


def get_filehash(file):
    if os.path.isfile(file):
        with open(file, "rb") as f:
            bytez = f.read()  # read entire file as bytes
            fhash = hashlib.sha256(bytez).hexdigest()
        return str(fhash)
    else:
        raise FileNotFoundError


def validate_tx_pattern(txid):
    if not isinstance(txid, str):
        return False
    pattern = re.compile('[0-9a-f]{64}')
    if pattern.match(txid):
        return True
    else:
        return False


def validate_raddr_pattern(addr):
    if not isinstance(addr, str):
        return False
    address_pattern = re.compile(r"R[a-zA-Z0-9]{33}\Z")
    if address_pattern.match(addr):
        return True
    else:
        return False


def wait_blocks(rpc_connection, blocks_to_wait):
    init_height = int(rpc_connection.getinfo()["blocks"])
    while True:
        current_height = int(rpc_connection.getinfo()["blocks"])
        height_difference = current_height - init_height
        if height_difference < blocks_to_wait:
            print("Waiting for more blocks")
            time.sleep(5)
        else:
            return True