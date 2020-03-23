
def cc_eval(chain, tx, nin, code):
    if code == b"ok":
        return None
    if code == b"fail":
        return "invalid"
    if code == b"return_invalid":
        return True
    raise ValueError()

# FIXME need cc_cli test, this is a dummy
def cc_cli(chain, code):
    if code == b"ok":
        return None
    if code == b"fail":
        return "invalid"
    if code == b"return_invalid":
        return True
    raise ValueError()


def test_get_height(chain):
    return chain.get_height()


def test_get_tx_confirmed(chain):
    return chain.get_tx_confirmed("a")
