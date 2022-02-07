/*
 * added by komodo team
 */

#ifndef	_EvalFingerprintContents_H_
#define	_EvalFingerprintContents_H_


#include "asn_application.h"

/* Including external dependencies */
#include "OCTET_STRING.h"
#include "constr_SEQUENCE.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EvalFingerprintContents */
typedef struct EvalFingerprintContents {
	OCTET_STRING_t	 code;
	OCTET_STRING_t	 param;
	
	/* Context for parsing across buffer boundaries */
	asn_struct_ctx_t _asn_ctx;
} EvalFingerprintContents_t;

/* Implementation */
extern asn_TYPE_descriptor_t asn_DEF_EvalFingerprintContents;

#ifdef __cplusplus
}
#endif

#endif	/* _EvalFingerprintContents_H_ */
#include "asn_internal.h"
