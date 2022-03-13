/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
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

#define _GNU_SOURCE 1

#if __linux
#include <sys/syscall.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <windows.h> 
#endif

#include <unistd.h>
#include <pthread.h>

#include "sha256.h"
#include "ripemd160.h"

#include "asn/Condition.h"
#include "asn/Fulfillment.h"
#include "asn/Secp256k1hashFulfillment.h"
#include "asn/Secp256k1hashFingerprintContents.h"
#include "asn/OCTET_STRING.h"
//#include <cJSON.h>
#include "include/secp256k1/include/secp256k1.h"
//#include "../include/cryptoconditions.h"
#include "internal.h"


struct CCType CC_Secp256k1hashType;

/*
static const size_t SECP256K1_PK_SIZE = 33;
static const size_t SECP256K1_SK_SIZE = 32;
static const size_t SECP256K1_SIG_SIZE = 64;
*/
static const size_t SECP256K1_HASH_SIZE = 20;



extern secp256k1_context *ec_ctx_sign, *ec_ctx_verify; // = 0, 
extern pthread_mutex_t cc_secp256k1ContextLock;// = PTHREAD_MUTEX_INITIALIZER;


void lockSign(); /*{
    pthread_mutex_lock(&cc_secp256k1ContextLock);
    if (!ec_ctx_sign) {
        ec_ctx_sign = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    }
    unsigned char ent[32];
#ifdef SYS_getrandom
    int read = syscall(SYS_getrandom, ent, 32, 0);
#else
    FILE *fp = fopen("/dev/urandom", "r");
    int read = (int) fread(&ent, 1, 32, fp);
    fclose(fp);
#endif
    if (read != 32)
    {
        int32_t i;
        for (i=0; i<32; i++)
            ((uint8_t *)ent)[i] = rand();
    }
    if (!secp256k1_context_randomize(ec_ctx_sign, ent)) {
        fprintf(stderr, "Could not randomize secp256k1 context\n");
        exit(-1);
    }
}*/


void unlockSign(); /* {
    pthread_mutex_unlock(&cc_secp256k1ContextLock);
}*/


void initVerify();/* {
    if (!ec_ctx_verify) {
        pthread_mutex_lock(&cc_secp256k1ContextLock);
        if (!ec_ctx_verify)
            ec_ctx_verify = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        pthread_mutex_unlock(&cc_secp256k1ContextLock);
    }
}*/

static void secp256k1hashFingerprint(const CC *cond, uint8_t *out) {
    Secp256k1hashFingerprintContents_t *fp = calloc(1, sizeof(Secp256k1hashFingerprintContents_t));
    if (cond->publicKey)  {
        // make the pubkey hash from the pubkey
        uint8_t pksha256[32];
        uint8_t pkripemd160[SECP256K1_HASH_SIZE];
        sha256(cond->publicKey, SECP256K1_PK_SIZE, pksha256);
        ripemd160(pksha256, sizeof(pksha256), pkripemd160);
        OCTET_STRING_fromBuf(&fp->publicKeyHash, pkripemd160, SECP256K1_HASH_SIZE);
    }
    else {
        OCTET_STRING_fromBuf(&fp->publicKeyHash, cond->publicKeyHash, SECP256K1_HASH_SIZE);
    }
    hashFingerprintContents(&asn_DEF_Secp256k1hashFingerprintContents, fp, out);
}


int secp256k1hashVerify(CC *cond, CCVisitor visitor) {
    if (cond->type->typeId != CC_Secp256k1hashType.typeId) return 1;
    initVerify();

    int rc;

    // parse pubkey
    secp256k1_pubkey pk;
    rc = secp256k1_ec_pubkey_parse(ec_ctx_verify, &pk, cond->publicKey, SECP256K1_PK_SIZE);
    if (rc != 1) return 0;

    // parse siganature
    secp256k1_ecdsa_signature sig;
    rc = secp256k1_ecdsa_signature_parse_compact(ec_ctx_verify, &sig, cond->signature);
    if (rc != 1) return 0;

    // Only accepts lower S signatures
    rc = secp256k1_ecdsa_verify(ec_ctx_verify, &sig, visitor.msg, &pk);
    if (rc != 1) return 0;

    return 1;
}


int cc_secp256k1HashVerifyTreeMsg32(const CC *cond, const unsigned char *msg32) {
    int subtypes = cc_typeMask(cond);
    if (subtypes & (1 << CC_PrefixType.typeId) &&
        subtypes & (1 << CC_Secp256k1hashType.typeId)) {
        // No support for prefix currently, due to pending protocol decision on
        // how to combine message and prefix into 32 byte hash
        return 0;
    }
    CCVisitor visitor = {&secp256k1hashVerify, msg32, 0, NULL};
    int out = cc_visit(cond, visitor);
    return out;
}


/*
 * Signing data
 */
typedef struct CCSecp256k1HashSigningData {
    const unsigned char *pkhash;
    const unsigned char *pk;
    const unsigned char *sk;
    int nSigned;
} CCSecp256k1HashSigningData;


/*
 * Visitor that signs an secp256k1 condition if it has a matching public key
 */
static int secp256k1hashSign(CC *cond, CCVisitor visitor) {
    if (cond->type->typeId != CC_Secp256k1hash) return 1;
    CCSecp256k1HashSigningData *signing = (CCSecp256k1HashSigningData*) visitor.context;

    if (0 != memcmp(cond->publicKeyHash, signing->pkhash, SECP256K1_HASH_SIZE)) return 1;

    secp256k1_ecdsa_signature sig;
    lockSign();
    int rc = secp256k1_ecdsa_sign(ec_ctx_sign, &sig, visitor.msg, signing->sk, NULL, NULL);
    unlockSign();

    if (rc != 1)
    {
        fprintf(stderr,"secp256k1hashSign rc.%d\n",rc);
        return 0;
    }

    if (!cond->publicKey) cond->publicKey = calloc(1, SECP256K1_PK_SIZE);
    memcpy(cond->publicKey, signing->pk, SECP256K1_PK_SIZE); // add signed pk to allow to create fulfillment 
    if (!cond->signature) cond->signature = calloc(1, SECP256K1_SIG_SIZE);
    secp256k1_ecdsa_signature_serialize_compact(ec_ctx_verify, cond->signature, &sig);

    signing->nSigned++;
    return 1;
}


/*
 * Sign secp256k1 conditions in a tree
 */
int cc_signTreeSecp256k1HashMsg32(CC *cond, const unsigned char *privateKey, const unsigned char *msg32) {
    if (cc_typeMask(cond) & (1 << CC_Prefix)) {
        // No support for prefix currently, due to pending protocol decision on
        // how to combine message and prefix into 32 byte hash
        return 0;
    }

    // derive the pubkey
    secp256k1_pubkey spk;
    lockSign();
    int rc = secp256k1_ec_pubkey_create(ec_ctx_sign, &spk, privateKey);
    unlockSign();
    if (rc != 1) {
        fprintf(stderr, "Cryptoconditions couldn't derive secp256k1 pubkey\n");
        return 0;
    }

    // serialize pubkey
    //unsigned char *publicKey = calloc(1, SECP256K1_PK_SIZE);
    unsigned char publicKey[SECP256K1_PK_SIZE];
    size_t ol = SECP256K1_PK_SIZE;
    secp256k1_ec_pubkey_serialize(ec_ctx_verify, publicKey, &ol, &spk, SECP256K1_EC_COMPRESSED);
    if ( 0 )
    {
        int32_t z;
        for (z=0; z<33; z++)
            fprintf(stderr,"%02x",publicKey[z]);
        fprintf(stderr," pubkey\n");
    }
    uint8_t pksha256[32];
    uint8_t pkripemd160[SECP256K1_HASH_SIZE];
    sha256(publicKey, SECP256K1_PK_SIZE, pksha256);
    ripemd160(pksha256, sizeof(pksha256), pkripemd160);

    // sign
    CCSecp256k1HashSigningData signing = {pkripemd160, publicKey, privateKey, 0};
    CCVisitor visitor = {&secp256k1hashSign, msg32, 32, &signing};
    cc_visit(cond, visitor);

    //free(publicKey);
    return signing.nSigned;
}


static unsigned long secp256k1hashCost(const CC *cond) {
    return 131072;
}

static CC *cc_secp256k1hashCondition(const unsigned char *publicKeyHash, const unsigned char *publicKey, const unsigned char *signature) {
    unsigned char *pk = NULL, *sig = NULL;
    unsigned char *pkhash = NULL;

    // Check that pk parses
    initVerify();
    if (publicKey) {
        secp256k1_pubkey spk;
        int rc = secp256k1_ec_pubkey_parse(ec_ctx_verify, &spk, publicKey, SECP256K1_PK_SIZE);
        if (!rc) {
            return NULL;
        }
        pk = calloc(1, SECP256K1_PK_SIZE);
        memcpy(pk, publicKey, SECP256K1_PK_SIZE);
    }

    if (signature) {
        sig = calloc(1, SECP256K1_SIG_SIZE);
        memcpy(sig, signature, SECP256K1_SIG_SIZE);
    }

    if (publicKeyHash) {
        pkhash = calloc(1, SECP256K1_HASH_SIZE);
        memcpy(pkhash, publicKeyHash, SECP256K1_HASH_SIZE);
    }
    if (!pk && !pkhash) return NULL;

    CC *cond = cc_new(CC_Secp256k1hash);
    cond->publicKey = pk;
    cond->signature = sig;
    cond->publicKeyHash = pkhash;
    return cond;
}

static CC *secp256k1hashFromJSON(const cJSON *params, char *err) {
    CC *cond = NULL;
    unsigned char *pkhash = NULL;
    unsigned char *pk = NULL, *sig = NULL;
    size_t pkhashSize;
    size_t pkSize, sigSize;

    // try get pk
    jsonGetHex(params, "publicKey", err, &pk, &pkSize);
    // try get sig
    jsonGetHexOptional(params, "signature", err, &sig, &sigSize);
    if (sig && SECP256K1_SIG_SIZE != sigSize) {
        strcpy(err, "signature has incorrect length");
        goto END;
    }

    jsonGetHex(params, "publicKeyHash", err, &pkhash, &pkhashSize);
    if (pkhash && pkhashSize != SECP256K1_HASH_SIZE) { strcpy(err, "invalid public key hash"); goto END; }

    if (!pkhash && !pk) { strcpy(err, "invalid public key or hash"); goto END; }

    cond = cc_secp256k1hashCondition(pkhash, pk, sig);
    if (!cond) {
        strcpy(err, "invalid secp256k1hash data");
    }
END:
    if (pkhash) free(pkhash);
    if (pk) free(pk);
    if (sig) free(sig);

    return cond;
}

static void secp256k1hashToJSON(const CC *cond, cJSON *params) {
    if (cond->publicKeyHash) {
        jsonAddHex(params, "publicKeyHash", cond->publicKeyHash, SECP256K1_HASH_SIZE);
    }
    if (cond->publicKey) {
        jsonAddHex(params, "publicKey", cond->publicKey, SECP256K1_PK_SIZE);
    }
    if (cond->signature) {
        jsonAddHex(params, "signature", cond->signature, SECP256K1_SIG_SIZE);
    }
}


static CC *secp256k1hashFromFulfillment(const Fulfillment_t *ffill, FulfillmentFlags _flags) {
    return cc_secp256k1hashCondition(NULL,
                                     ffill->choice.secp256k1Sha256.publicKey.buf,
                                     ffill->choice.secp256k1Sha256.signature.buf);
}


static Fulfillment_t *secp256k1hashToFulfillment(const CC *cond, FulfillmentFlags _flags) {
    if (!cond->signature || !cond->publicKey) {
        return NULL;
    }

    Fulfillment_t *ffill = calloc(1, sizeof(Fulfillment_t));
    ffill->present = Fulfillment_PR_secp256k1hashSha256;
    Secp256k1Fulfillment_t *sec = &ffill->choice.secp256k1hashSha256;

    OCTET_STRING_fromBuf(&sec->publicKey, cond->publicKey, SECP256K1_PK_SIZE);
    OCTET_STRING_fromBuf(&sec->signature, cond->signature, SECP256K1_SIG_SIZE);
    return ffill;
}


int secp256k1hashIsFulfilled(const CC *cond) {
    return cond->signature != NULL;
}

static void secp256k1hashFree(CC *cond) {
    if (cond->publicKey)
        free(cond->publicKey);
    if (cond->publicKeyHash)
        free(cond->publicKeyHash);
    if (cond->signature) {
        free(cond->signature);
    }
}

static CC* secp256k1hashCopy(const CC* cond)
{
    CC *condCopy = cc_new(CC_Secp256k1hash);
    if (cond->publicKey) {
        condCopy->publicKey = calloc(1, SECP256K1_PK_SIZE);
        memcpy(condCopy->publicKey, cond->publicKey, SECP256K1_PK_SIZE);
    }
    if (cond->publicKeyHash) {
        condCopy->publicKeyHash = calloc(1, SECP256K1_HASH_SIZE);
        memcpy(condCopy->publicKeyHash, cond->publicKeyHash, SECP256K1_HASH_SIZE);
    }
    if (cond->signature) {
        condCopy->signature = calloc(1, SECP256K1_SIG_SIZE);
        memcpy(condCopy->signature, cond->signature, SECP256K1_SIG_SIZE);
    }
    condCopy->dontFulfill = cond->dontFulfill;
    return (condCopy);
}


static uint32_t secp256k1hashSubtypes(const CC *cond) {
    return 0;
}


struct CCType CC_Secp256k1hashType = { 6, "secp256k1hash-sha-256", Condition_PR_secp256k1hashSha256, 0, &secp256k1hashFingerprint, &secp256k1hashCost, &secp256k1hashSubtypes, &secp256k1hashFromJSON, &secp256k1hashToJSON, &secp256k1hashFromFulfillment, &secp256k1hashToFulfillment, &secp256k1hashIsFulfilled, &secp256k1hashFree, &secp256k1hashCopy };
