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
struct CUnspentCCIndexKey {
    uint160 hashBytes;
    uint256 creationid;
    uint256 txhash;
    uint32_t index;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return sizeof(uint160) + sizeof(uint256) + sizeof(uint256) + sizeof(uint32_t) ;
    }
    template<typename Stream>
    void Serialize(Stream& s) const {
        hashBytes.Serialize(s);
        creationid.Serialize(s);
        txhash.Serialize(s);
        ser_writedata32(s, index);
    }
    template<typename Stream>
    void Unserialize(Stream& s) {
        hashBytes.Unserialize(s);
        creationid.Unserialize(s);
        txhash.Unserialize(s);
        index = ser_readdata32(s);
    }

    CUnspentCCIndexKey(uint160 addressHash, uint256 _creationid, uint256 _txid, uint32_t _index) {
        hashBytes = addressHash;
        creationid = _creationid;
        txhash = _txid;
        index = _index;
    }

    CUnspentCCIndexKey() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        creationid.SetNull();
        txhash.SetNull();
        index = 0;
    }
};

// partial key for cc address only
struct CUnspentCCIndexKeyAddr {
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

    CUnspentCCIndexKeyAddr(uint160 addressHash) {
        hashBytes = addressHash;
    }

    CUnspentCCIndexKeyAddr() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
    }
};

// partial key for cc address+creationid
struct CUnspentCCIndexKeyCreationId {
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

    CUnspentCCIndexKeyCreationId(uint160 addressHash, uint256 _creationid) {
        hashBytes = addressHash;
        creationid = _creationid;
    }

    CUnspentCCIndexKeyCreationId() {
        SetNull();
    }

    void SetNull() {
        hashBytes.SetNull();
        creationid.SetNull();
    }
};

// unspent cc index value
struct  CUnspentCCIndexValue {
    CAmount satoshis;
    CScript scriptPubKey;
    CScript opreturn;
    int blockHeight;
    uint8_t evalcode;
    uint8_t funcid;
    uint8_t version;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(satoshis);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(*(CScriptBase*)(&opreturn));
        READWRITE(blockHeight);
        READWRITE(evalcode);
        READWRITE(funcid);
        READWRITE(version);
    }

    CUnspentCCIndexValue(CAmount sats, CScript _scriptPubKey, CScript _opreturn, int32_t _height, uint8_t _evalcode, uint8_t _funcid, uint8_t _version) {
        satoshis = sats;
        scriptPubKey = _scriptPubKey;
        opreturn = _opreturn;
        blockHeight = _height;
        evalcode = _evalcode;
        funcid = _funcid;
        version = _version;
    }

    CUnspentCCIndexValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        scriptPubKey.clear();
        opreturn.clear();
        blockHeight = 0;
        evalcode = 0;
        funcid = 0;
        version = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CUnspentCCIndexKeyCompare
{
    bool operator()(const CUnspentCCIndexKey& a, const CUnspentCCIndexKey& b) const 
    {
        if (a.hashBytes == b.hashBytes) 
            if (a.creationid == b.creationid)
                if (a.txhash == b.txhash)
                    return a.index < b.index;
                else
                    return a.txhash < b.txhash;
            else
                return a.creationid < b.creationid;
        else 
            return a.hashBytes < b.hashBytes;
    }
};

#endif // #ifndef UNSPENTCCINDEX_H
