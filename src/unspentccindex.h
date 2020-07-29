/******************************************************************************
 * Copyright © 2014-2010 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef UNSPENTCCINDEX_H
#define UNSPENTCCINDEX_H

#include "uint256.h"
#include "amount.h"

// unspent cc index key
struct CAddressUnspentCCKey {
    uint160 hashBytes;
    uint256 creationid;
    uint8_t funcid;
    uint8_t version;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return sizeof(uint160) + sizeof(uint256) + sizeof(uint8_t) + sizeof(uint8_t);
    }
    template<typename Stream>
    void Serialize(Stream& s) const {
        hashBytes.Serialize(s);
        creationid.Serialize(s);
        ser_writedata8(s, funcid);
        ser_writedata8(s, version);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        hashBytes.Unserialize(s);
        creationid.Unserialize(s);
        funcid = ser_readdata8(s);
        version = ser_readdata8(s);
    }

    CAddressUnspentCCKey(uint160 addressHash, uint256 txid, uint8_t _funcid, uint8_t _version) {
        hashBytes = addressHash;
        creationid = txid;
        funcid = _funcid;
        version = _version;
    }

    CAddressUnspentCCKey() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        creationid.SetNull();
        funcid = 0;
        version = 0;
    }
};

// partial key for cc address only
struct CAddressUnspentIteratorCCKeyAddr {
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return sizeof(uint160);
    }
    template<typename Stream>
    void Serialize(Stream& s) const {
        hashBytes.Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        hashBytes.Unserialize(s);
    }

    CAddressUnspentIteratorCCKeyAddr(uint160 addressHash) {
        hashBytes = addressHash;
    }

    CAddressUnspentIteratorCCKeyAddr() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
    }
};

// partial key for cc address+creationid
struct CAddressUnspentIteratorCCKeyCreationId {
    uint160 hashBytes;
    uint256 creationid;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return sizeof(uint160) + sizeof(uint256);
    }
    template<typename Stream>
    void Serialize(Stream& s) const {
        hashBytes.Serialize(s);
        creationid.Serialize(s);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        hashBytes.Unserialize(s);
        creationid.Unserialize(s);
    }

    CAddressUnspentIteratorCCKeyCreationId(uint160 addressHash, uint256 txid) {
        hashBytes = addressHash;
        creationid = txid;
    }

    CAddressUnspentIteratorCCKeyCreationId() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        creationid.SetNull();
    }
};

// unspent cc index value
struct  CAddressUnspentCCValue {
    uint256 txhash;
    uint32_t index;
    CAmount satoshis;
    CScript scriptPubKey;
    CScript opreturn;
    int blockHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(txhash);
        READWRITE(index);
        READWRITE(satoshis);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(*(CScriptBase*)(&opreturn));
        READWRITE(blockHeight);
    }

    CAddressUnspentCCValue(uint256 _txid, uint32_t _index, CAmount sats, CScript _scriptPubKey, CScript _opreturn, int32_t height) {
        txhash = _txid;
        index = _index;
        satoshis = sats;
        scriptPubKey = _scriptPubKey;
        opreturn = _opreturn;
        blockHeight = height;
    }

    CAddressUnspentCCValue() {
        SetNull();
    }

    void SetNull() {
        txhash.SetNull();
        index = 0;
        satoshis = -1;
        scriptPubKey.clear();
        opreturn.clear();
        blockHeight = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

#endif // #ifndef UNSPENTCCINDEX_H
