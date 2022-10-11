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

#include "asn/Condition.h"
#include "asn/Fulfillment.h"
#include "asn/EvalFulfillment.h"
//#include "asn/EvalFingerprintContents.h"
#include "asn/OCTET_STRING.h"
//#include "../include/cryptoconditions.h"
#include "internal.h"
//#include <cJSON.h>


struct CCType CC_EvalType;


static void evalFingerprint(const CC *cond, uint8_t *out) {

    /* TODO: enable for generic evals
    if (!cond->includeParamInFP) */
    sha256(cond->code, cond->codeLength, out);
    /* TODO: enable for generic evals
    else {
        uint8_t *msg = malloc(cond->codeLength + cond->paramLength);
        memcpy(msg, cond->code, cond->codeLength);
        memcpy(msg + cond->codeLength, cond->param, cond->paramLength);
        sha256(msg, cond->codeLength+cond->paramLength, out);
        free(msg);
    } */
}


static unsigned long evalCost(const CC *cond) {
    return 1048576;  // Pretty high
}


static CC *evalFromJSON(const cJSON *params, char *err) {
    size_t codeLength;
    unsigned char *code = 0;

    if (!jsonGetBase64(params, "code", err, &code, &codeLength) && !jsonGetHex(params, "codehex", err, &code, &codeLength) ) {
        return NULL;
    }

    /* TODO: enable for generic evals
    unsigned char *param = NULL;
    size_t param_len = 0;

    if (!jsonGetHexOptional(params, "param", err, &param, &param_len)) {
        free(code);
        return NULL;
    }

    int includeParamInFP = 0;
    cJSON *objfp = cJSON_GetObjectItem(params, "includeParamInFP");
    if (objfp) includeParamInFP = !!objfp->valueint; */

    CC *cond = cc_new(CC_Eval);
    cond->code = code;
    cond->codeLength = codeLength;
    /* TODO: enable for generic evals
    cond->param = param;
    cond->paramLength = param_len; */
    /* debug:
    if (cond->param)  {
        unsigned char *hex = cc_hex_encode(cond->param, cond->paramLength);
        printf("%s cond->param=%s\n", __func__, hex);
        free(hex);
    }*/
    // cond->includeParamInFP = includeParamInFP;

    int dontFulfill = 0;
    cJSON *objdf = cJSON_GetObjectItem(params, "dontFulfill");
    if (objdf) cond->dontFulfill = !!objdf->valueint;    

    return cond;
}


static void evalToJSON(const CC *cond, cJSON *code) {

    // now print as hex instead of base64
    //unsigned char *b64 = base64_encode(cond->code, cond->codeLength);
    //cJSON_AddItemToObject(code, "code", cJSON_CreateString(b64));
    //free(b64);

    unsigned char *codehex = cc_hex_encode(cond->code, cond->codeLength);
    cJSON_AddItemToObject(code, "codehex", cJSON_CreateString(codehex));
    free(codehex);

    /* TODO: enable for generic evals
    if (cond->param) {
        unsigned char *hex = cc_hex_encode(cond->param, cond->paramLength);
        cJSON_AddItemToObject(code, "param", cJSON_CreateString(hex));
        free(hex);
    }
    if (cond->includeParamInFP) {
        cJSON_AddItemToObject(code, "includeParamInFP", cJSON_CreateNumber(cond->includeParamInFP));
    } */
}


static CC *evalFromFulfillment(const Fulfillment_t *ffill) {
    CC *cond = cc_new(CC_Eval);

    EvalFulfillment_t *eval = &ffill->choice.evalSha256;

    OCTET_STRING_t octets = eval->code;
    cond->codeLength = octets.size;
    cond->code = calloc(1,octets.size);
    memcpy(cond->code, octets.buf, octets.size);

    /* TODO: enable for generic evals
    cond->param = NULL;
    cond->paramLength = 0;

    if (eval->param)  {
        OCTET_STRING_t paramOctets = *eval->param;
        cond->paramLength = paramOctets.size;
        //unsigned char *hex = cc_hex_encode(paramOctets.buf, paramOctets.size);
        //printf("%s size %ld cond->param=%s\n", __func__, paramOctets.size, hex);
        //free(hex);
        cond->param = calloc(1, paramOctets.size);
        memcpy(cond->param, paramOctets.buf, paramOctets.size);
    }   */

    return cond;
}


static Fulfillment_t *evalToFulfillment(const CC *cond) {
    Fulfillment_t *ffill = calloc(1, sizeof(Fulfillment_t));
    ffill->present = Fulfillment_PR_evalSha256;
    EvalFulfillment_t *eval = &ffill->choice.evalSha256;
    OCTET_STRING_fromBuf(&eval->code, cond->code, cond->codeLength);
    /* TODO: enable for generic evals
    if (cond->param) {
        eval->param = (OCTET_STRING_t*)calloc(1, sizeof(OCTET_STRING_t));
        OCTET_STRING_fromBuf(eval->param, cond->param, cond->paramLength);
        //unsigned char *hex = cc_hex_encode(cond->param, cond->paramLength);
        //printf("%s cond->param=%s\n", __func__, hex);
        //free(hex);
    } */
    return ffill;
}


int evalIsFulfilled(const CC *cond) {
    return 1;
}


static void evalFree(CC *cond) {
    free(cond->code);
    /* TODO enable for generic evals
    if (cond->param)
        free(cond->param); */
}


static uint32_t evalSubtypes(const CC *cond) {
    return 0;
}


/*
 * The JSON api doesn't contain custom verifiers, so a stub method is provided suitable for testing
 */
int jsonVerifyEval(CC *cond, void *context) {
    if (cond->codeLength == 5 && 0 == memcmp(cond->code, "TEST", 4)) {
        return cond->code[4];
    }
    fprintf(stderr, "Cannot verify eval; user function unknown\n");
    return 0;
}


typedef struct CCEvalVerifyData {
    VerifyEval verify;
    void *context;
} CCEvalVerifyData;


int evalVisit(CC *cond, CCVisitor visitor) {
    if (cond->type->typeId != CC_Eval) return 1;
    CCEvalVerifyData *evalData = visitor.context;
    return evalData->verify(cond, evalData->context);
}


int cc_verifyEval(const CC *cond, VerifyEval verify, void *context) {
    CCEvalVerifyData evalData = {verify, context};
    CCVisitor visitor = {&evalVisit, "", 0, &evalData};
    return cc_visit(cond, visitor);
}

static CC* evalCopy(const CC* cond)
{
    CC *condCopy = cc_new(CC_Eval);
    condCopy->code = calloc(cond->codeLength, sizeof(uint8_t));
    memcpy(condCopy->code, cond->code, cond->codeLength);
    condCopy->codeLength=cond->codeLength;

    /* TODO enable for generic evals
    condCopy->param = NULL;
    condCopy->paramLength=cond->paramLength;
    if (cond->paramLength)  {
        condCopy->param = calloc(cond->paramLength, sizeof(uint8_t));
        memcpy(condCopy->param, cond->param, cond->paramLength);
    }*/
    condCopy->dontFulfill = cond->dontFulfill;
    return (condCopy);
}

struct CCType CC_EvalType = { 15, "eval-sha-256", Condition_PR_evalSha256, 0, &evalFingerprint, &evalCost, &evalSubtypes, &evalFromJSON, &evalToJSON, &evalFromFulfillment, &evalToFulfillment, &evalIsFulfilled, &evalFree, &evalCopy };
