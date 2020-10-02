#ifndef ADB_H
#define ADB_H

#include <endian.h>
#include <stdint.h>
#include <sys/types.h>
#include "apk_io.h"

struct adb;
struct adb_obj;
struct adb_trust;
struct adb_verify_ctx;

typedef uint32_t adb_val_t;

#define ADB_TYPE_SPECIAL	0x00000000
#define ADB_TYPE_INT		0x10000000
#define ADB_TYPE_INT_32		0x20000000
#define ADB_TYPE_INT_64		0x30000000
#define ADB_TYPE_BLOB_8		0x80000000
#define ADB_TYPE_BLOB_16	0x90000000
#define ADB_TYPE_BLOB_32	0xa0000000
#define ADB_TYPE_ARRAY		0xd0000000
#define ADB_TYPE_OBJECT		0xe0000000
#define ADB_TYPE_ERROR		0xf0000000
#define ADB_TYPE_MASK		0xf0000000
#define ADB_VALUE_MASK		0x0fffffff
#define ADB_VAL_TYPE(x)		((le32toh(x))&ADB_TYPE_MASK)
#define ADB_VAL_VALUE(x)	((le32toh(x))&ADB_VALUE_MASK)
#define ADB_IS_ERROR(x)		(ADB_VAL_TYPE(x) == ADB_TYPE_ERROR)
#define ADB_VAL(type, val)	(htole32((type) | (val)))
#define ADB_ERROR(val)		ADB_VAL(ADB_TYPE_ERROR, val)

/* ADB_TYPE_SPECIAL */
#define ADB_VAL_NULL		0x00000000
#define ADB_VAL_TRUE		0x00000001
#define ADB_VAL_FALSE		0x00000002

#define ADB_NULL		ADB_VAL(ADB_TYPE_SPECIAL, ADB_VAL_NULL)

/* Generic */
#define ADBI_NUM_ENTRIES	0x00
#define ADBI_FIRST		0x01

/* File Header */
#define ADB_FORMAT_MAGIC	0x2e424441	// ADB.

struct adb_header {
	uint32_t magic;
	uint32_t schema;
};

/* Blocks */
#define ADB_BLOCK_END		-1
#define ADB_BLOCK_ADB		0
#define ADB_BLOCK_SIG		2

#define ADB_BLOCK_TYPE(b)	(le32toh((b)->type_size) >> 30)
#define ADB_BLOCK_SIZE(b)	(le32toh((b)->type_size) & 0x3fffffff)

struct adb_block {
	uint32_t type_size;
};

struct adb_sign_hdr {
	uint8_t sign_ver, hash_alg;
};

struct adb_sign_v0 {
	struct adb_sign_hdr hdr;
	uint8_t id[16];
	uint8_t sig[0];
};

/* Hash algorithms */
#define ADB_HASH_NONE		0x00
#define ADB_HASH_SHA1		0x01
#define ADB_HASH_SHA256		0x02
#define ADB_HASH_SHA512		0x03

/* Block enumeration */
struct adb_block *adb_block_first(apk_blob_t b);
struct adb_block *adb_block_next(struct adb_block *cur, apk_blob_t b);

#define adb_foreach_block(__blk, __adb) \
	for (__blk = adb_block_first(__adb); !IS_ERR_OR_NULL(__blk); __blk = adb_block_next(__blk, __adb))

/* Schema */
#define ADB_KIND_ADB	1
#define ADB_KIND_OBJECT	2
#define ADB_KIND_ARRAY	3
#define ADB_KIND_BLOB	4
#define ADB_KIND_INT	5

#define ADB_ARRAY_ITEM(_t) { { .kind = &(_t).kind } }
#define ADB_FIELD(_i, _n, _t) [(_i)-1] = { .name = _n, .kind = &(_t).kind }

struct adb_object_schema {
	uint8_t kind;
	uint16_t num_fields;

	apk_blob_t (*tostring)(struct adb_obj *, char *, size_t);
	int (*fromstring)(struct adb_obj *, apk_blob_t);
	uint32_t (*get_default_int)(unsigned i);
	int (*compare)(const struct adb_obj *, const struct adb_obj *);
	void (*pre_commit)(struct adb_obj *);

	struct {
		const char *name;
		const uint8_t *kind;
	} fields[];
};

struct adb_scalar_schema {
	uint8_t kind;
	uint8_t multiline : 1;

	apk_blob_t (*tostring)(struct adb*, adb_val_t, char *, size_t);
	adb_val_t (*fromstring)(struct adb*, apk_blob_t);
	int (*compare)(struct adb*, adb_val_t, struct adb*, adb_val_t);
};

struct adb_adb_schema {
	uint8_t kind;
	uint32_t schema_id;
	const struct adb_object_schema *schema;
};

/* Database read interface */
struct adb_w_bucket {
	struct list_head node;
	struct adb_w_bucket_entry {
		uint32_t hash;
		uint32_t offs;
		uint32_t len;
	} entries[40];
};

struct adb {
	apk_blob_t mmap, data, adb;
	struct adb_header hdr;
	size_t num_buckets;
	struct list_head *bucket;
};

struct adb_obj {
	struct adb *db;
	const struct adb_object_schema *schema;
	uint32_t num;
	adb_val_t *obj;
};

/* Container read interface */
int adb_free(struct adb *);
void adb_reset(struct adb *);

int adb_m_blob(struct adb *, apk_blob_t, struct adb_trust *);
int adb_m_map(struct adb *, int fd, uint32_t expected_schema, struct adb_trust *);
#define adb_w_init_alloca(db, schema, num_buckets) adb_w_init_dynamic(db, schema, alloca(sizeof(struct list_head[num_buckets])), num_buckets)
#define adb_w_init_tmp(db, size) adb_w_init_static(db, alloca(size), size)
int adb_w_init_dynamic(struct adb *db, uint32_t schema, void *buckets, size_t num_buckets);
int adb_w_init_static(struct adb *db, void *buf, size_t bufsz);

/* Primitive read */
adb_val_t adb_r_root(const struct adb *);
struct adb_obj *adb_r_rootobj(struct adb *a, struct adb_obj *o, const struct adb_object_schema *);
uint32_t adb_r_int(const struct adb *, adb_val_t);
apk_blob_t adb_r_blob(const struct adb *, adb_val_t);
struct adb_obj *adb_r_obj(struct adb *, adb_val_t, struct adb_obj *o, const struct adb_object_schema *);

/* Object read */
static inline uint32_t adb_ro_num(const struct adb_obj *o) { return o->num; }
static inline uint32_t adb_ra_num(const struct adb_obj *o) { return (o->num ?: 1) - 1; }

adb_val_t adb_ro_val(const struct adb_obj *o, unsigned i);
uint32_t adb_ro_int(const struct adb_obj *o, unsigned i);
apk_blob_t adb_ro_blob(const struct adb_obj *o, unsigned i);
struct adb_obj *adb_ro_obj(const struct adb_obj *o, unsigned i, struct adb_obj *);
int adb_ro_cmp(const struct adb_obj *o1, const struct adb_obj *o2, unsigned i);
int adb_ra_find(struct adb_obj *arr, int cur, struct adb *db, adb_val_t val);

/* Primitive write */
void adb_w_root(struct adb *, adb_val_t);
void adb_w_rootobj(struct adb_obj *);
adb_val_t adb_w_blob(struct adb *, apk_blob_t);
adb_val_t adb_w_int(struct adb *, uint32_t);
adb_val_t adb_w_copy(struct adb *, struct adb *, adb_val_t);
adb_val_t adb_w_adb(struct adb *, struct adb *);
adb_val_t adb_w_fromstring(struct adb *, const uint8_t *kind, apk_blob_t);

/* Object write */
#define adb_wo_alloca(o, schema, db) adb_wo_init(o, alloca(sizeof(adb_val_t[(schema)->num_fields])), schema, db)

struct adb_obj *adb_wo_init(struct adb_obj *, adb_val_t *, const struct adb_object_schema *, struct adb *);
void adb_wo_reset(struct adb_obj *);
void adb_wo_resetdb(struct adb_obj *);
adb_val_t adb_w_obj(struct adb_obj *);
adb_val_t adb_w_arr(struct adb_obj *);
adb_val_t adb_wo_fromstring(struct adb_obj *o, apk_blob_t);
adb_val_t adb_wo_val(struct adb_obj *o, unsigned i, adb_val_t);
adb_val_t adb_wo_val_fromstring(struct adb_obj *o, unsigned i, apk_blob_t);
adb_val_t adb_wo_int(struct adb_obj *o, unsigned i, uint32_t);
adb_val_t adb_wo_blob(struct adb_obj *o, unsigned i, apk_blob_t);
adb_val_t adb_wo_obj(struct adb_obj *o, unsigned i, struct adb_obj *);
adb_val_t adb_wo_arr(struct adb_obj *o, unsigned i, struct adb_obj *);
adb_val_t adb_wa_append(struct adb_obj *o, adb_val_t);
adb_val_t adb_wa_append_obj(struct adb_obj *o, struct adb_obj *);
adb_val_t adb_wa_append_fromstring(struct adb_obj *o, apk_blob_t);
void adb_wa_sort(struct adb_obj *);
void adb_wa_sort_unique(struct adb_obj *);

/* Schema helpers */
int adb_s_field_by_name(const struct adb_object_schema *, const char *);

/* Creation */
int adb_c_header(struct apk_ostream *os, struct adb *db);
int adb_c_block(struct apk_ostream *os, uint32_t type, apk_blob_t);
int adb_c_block_copy(struct apk_ostream *os, struct adb_block *b, struct apk_istream *is, struct adb_verify_ctx *);
int adb_c_create(struct apk_ostream *os, struct adb *db, struct adb_trust *t);

/* Trust */
#include <openssl/evp.h>

struct adb_pkey {
	uint8_t		id[16];
	EVP_PKEY	*key;
};

int adb_pkey_init(struct adb_pkey *pkey, EVP_PKEY *key);
void adb_pkey_free(struct adb_pkey *pkey);
int adb_pkey_load(struct adb_pkey *pkey, int dirfd, const char *fn);

struct adb_trust {
	EVP_MD_CTX *mdctx;
	struct list_head trusted_key_list;
	struct list_head private_key_list;
};

struct adb_verify_ctx {
	uint32_t calc;
	uint8_t sha512[64];
};

int adb_trust_init(struct adb_trust *trust, int keysfd, struct apk_string_array *);
void adb_trust_free(struct adb_trust *trust);
int adb_trust_write_signatures(struct adb_trust *trust, struct adb *db, struct adb_verify_ctx *vfy, struct apk_ostream *os);
int adb_trust_verify_signature(struct adb_trust *trust, struct adb *db, struct adb_verify_ctx *vfy, apk_blob_t sigb);

/* Transform existing file */
struct adb_xfrm {
	struct apk_istream *is;
	struct apk_ostream *os;
	struct adb db;
	struct adb_verify_ctx vfy;
};
int adb_c_xfrm(struct adb_xfrm *, int (*cb)(struct adb_xfrm *, struct adb_block *, struct apk_istream *));

#endif
