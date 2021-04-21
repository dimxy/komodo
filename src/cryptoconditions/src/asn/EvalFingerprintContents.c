/*
 * added by komodo team
 */

#include "EvalFingerprintContents.h"

static asn_TYPE_member_t asn_MBR_EvalFingerprintContents_1[] = {
	{ ATF_NOFLAGS, 0, offsetof(struct EvalFingerprintContents, code),
		(ASN_TAG_CLASS_CONTEXT | (0 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_OCTET_STRING,
		NULL,
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"code"
		},
	{ ATF_NOFLAGS, 0, offsetof(struct EvalFingerprintContents, param),
		(ASN_TAG_CLASS_CONTEXT | (1 << 2)),
		-1,	/* IMPLICIT tag at current level */
		&asn_DEF_OCTET_STRING,
		NULL,
		0,	/* PER is not compiled, use -gen-PER */
		0,
		"param"
		},
};
static const ber_tlv_tag_t asn_DEF_EvalFingerprintContents_tags_1[] = {
	(ASN_TAG_CLASS_UNIVERSAL | (16 << 2))
};
static const asn_TYPE_tag2member_t asn_MAP_EvalFingerprintContents_tag2el_1[] = {
    { (ASN_TAG_CLASS_CONTEXT | (0 << 2)), 0, 0, 0 }, /* code */
    { (ASN_TAG_CLASS_CONTEXT | (1 << 2)), 1, 0, 0 } /* param */
};
static asn_SEQUENCE_specifics_t asn_SPC_EvalFingerprintContents_specs_1 = {
	sizeof(struct EvalFingerprintContents),
	offsetof(struct EvalFingerprintContents, _asn_ctx),
	asn_MAP_EvalFingerprintContents_tag2el_1,
	2,	/* Count of tags in the map */
	0, 0, 0,	/* Optional elements (not needed) */
	-1,	/* Start extensions */
	-1	/* Stop extensions */
};
asn_TYPE_descriptor_t asn_DEF_EvalFingerprintContents = {
	"EvalFingerprintContents",
	"EvalFingerprintContents",
	SEQUENCE_free,
	SEQUENCE_print,
	SEQUENCE_constraint,
	SEQUENCE_decode_ber,
	SEQUENCE_encode_der,
	SEQUENCE_decode_xer,
	SEQUENCE_encode_xer,
	0, 0,	/* No PER support, use "-gen-PER" to enable */
	0,	/* Use generic outmost tag fetcher */
	asn_DEF_EvalFingerprintContents_tags_1,
	sizeof(asn_DEF_EvalFingerprintContents_tags_1)
		/sizeof(asn_DEF_EvalFingerprintContents_tags_1[0]), /* 1 */
	asn_DEF_EvalFingerprintContents_tags_1,	/* Same as above */
	sizeof(asn_DEF_EvalFingerprintContents_tags_1)
		/sizeof(asn_DEF_EvalFingerprintContents_tags_1[0]), /* 1 */
	0,	/* No PER visible constraints */
	asn_MBR_EvalFingerprintContents_1,
	2,	/* Elements count */
	&asn_SPC_EvalFingerprintContents_specs_1	/* Additional specs */
};

