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
#include "asn/OCTET_STRING.h"
//#include "../include/cryptoconditions.h"
#include "internal.h"
//#include <cJSON.h>


struct CCType CC_EvalType;


static void evalFingerprint(const CC *cond, uint8_t *out) {

    unsigned char codeHash[32], paramHash[32];
    unsigned char preimage[64];
    sha256(cond->code, cond->codeLength, codeHash);
    sha256(cond->param, cond->paramLength, paramHash);

    strcat(preimage, codeHash);
    strcat(preimage, paramHash);

    sha256(preimage, 64, out);
}


static unsigned long evalCost(const CC *cond) {
    return 1048576;  // Pretty high
}


static CC *evalFromJSON(const cJSON *params, char *err) {
    size_t codeLength;
    unsigned char *code = 0;
    //size_t eval_params_len;

    /*
    cJSON *eval_params_json = cJSON_GetObjectItem(params, "params");
    if (!eval_params_json || !cJSON_IsString(eval_params_json)) {
        strcpy(err, "\"params\" must be a string");
        return NULL;
    }

    // FIXME - Alright 
    // we should check that this is valid hex and use it literally as "cond->param"
    // we are wasting space by converting values to ascii and back
    char* param_string = cJSON_PrintUnformatted( eval_params_json );
    // this adds " to beginning and end?!
    for (int i = 1; i < strlen(param_string)-1; i++) {
        if (!isxdigit(param_string[i]))
        {
            strcpy(err, "\"params\" must be valid hex string");
            return NULL;
        }
    } */


    if (!jsonGetBase64(params, "code", err, &code, &codeLength)) {
        return NULL;
    }
    unsigned char *param_string = NULL;
    size_t param_len;

    if (!jsonGetHex(params, "params", err, &param_string, &param_len)) {
        free(code);
        return NULL;
    }

    CC *cond = cc_new(CC_Eval);
    cond->code = code;
    cond->codeLength = codeLength;
    cond->param = param_string;
    cond->paramLength = strlen(param_string);
    return cond;
}


static void evalToJSON(const CC *cond, cJSON *code) {

    // add code
    unsigned char *b64 = base64_encode(cond->code, cond->codeLength);
    cJSON_AddItemToObject(code, "code", cJSON_CreateString(b64));
    free(b64);
}


static CC *evalFromFulfillment(const Fulfillment_t *ffill) {
    CC *cond = cc_new(CC_Eval);

    EvalFulfillment_t *eval = &ffill->choice.evalSha256;

    OCTET_STRING_t octets = eval->code;
    cond->codeLength = octets.size;
    cond->code = calloc(1,octets.size);
    memcpy(cond->code, octets.buf, octets.size);

    OCTET_STRING_t paramOctets = eval->param;
    cond->paramLength = paramOctets.size;
    cond->param = calloc(1, paramOctets.size);
    memcpy(cond->param, paramOctets.buf, paramOctets.size);
    printf("%s cond->param=%s\n", __func__, cond->param);

    return cond;
}


static Fulfillment_t *evalToFulfillment(const CC *cond) {
    Fulfillment_t *ffill = calloc(1, sizeof(Fulfillment_t));
    ffill->present = Fulfillment_PR_evalSha256;
    EvalFulfillment_t *eval = &ffill->choice.evalSha256;
    OCTET_STRING_fromBuf(&eval->code, cond->code, cond->codeLength);
    if (cond->param)
        OCTET_STRING_fromBuf(&eval->param, cond->param, cond->paramLength);
    printf("%s cond->param=%s\n", __func__, (cond->param ? cond->param : "(null)"));
    return ffill;
}


int evalIsFulfilled(const CC *cond) {
    return 1;
}


static void evalFree(CC *cond) {
    free(cond->code);
    if (cond->param)
        free(cond->param);
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
    condCopy->code = calloc(1, cond->codeLength);
    memcpy(condCopy->code, cond->code, cond->codeLength);
    condCopy->codeLength=cond->codeLength;

    condCopy->param = calloc(1, cond->paramLength);
    memcpy(condCopy->param, cond->param, cond->paramLength);
    condCopy->paramLength=cond->paramLength;

    return (condCopy);
}

struct CCType CC_EvalType = { 15, "eval-sha-256", Condition_PR_evalSha256, 0, &evalFingerprint, &evalCost, &evalSubtypes, &evalFromJSON, &evalToJSON, &evalFromFulfillment, &evalToFulfillment, &evalIsFulfilled, &evalFree };
