/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <lmdb.h>

#include "knot/modules/dynzone.h"
#include "knot/nameserver/process_query.h"

struct cache
{
	MDB_dbi dbi;
	MDB_env *env;
	mm_ctx_t *pool;
};

struct entry {
	const char *ip;
	const char *threat_code;
	const char *syslog_ip;
	MDB_cursor *cursor;
};

/*                       MDB access                                           */

static int dbase_open(struct cache *cache, const char *handle)
{
	int ret = mdb_env_create(&cache->env);
	if (ret != 0) {
		return ret;
	}

	ret = mdb_env_open(cache->env, handle, 0, 0644);
	if (ret != 0) {
		mdb_env_close(cache->env);
		return ret;
	}

	MDB_txn *txn = NULL;
	ret = mdb_txn_begin(cache->env, NULL, 0, &txn);
	if (ret != 0) {
		mdb_env_close(cache->env);
		return ret;
	}

	ret = mdb_open(txn, NULL, 0, &cache->dbi);
	if (ret != 0) {
		mdb_txn_abort(txn);
		mdb_env_close(cache->env);
		return ret;
	}

	ret = mdb_txn_commit(txn);
	if (ret != 0) {
		mdb_env_close(cache->env);
		return ret;
	}

	return 0;
}

static void dbase_close(struct cache *cache)
{
	mdb_close(cache->env, cache->dbi);
	mdb_env_close(cache->env);
}

/*                       data access                                          */

static MDB_cursor *cursor_acquire(MDB_txn *txn, MDB_dbi dbi)
{
	MDB_cursor *cursor = NULL;

	int ret = mdb_cursor_open(txn, dbi, &cursor);
	if (ret != 0) {
		return NULL;
	}

	return cursor;
}

static void cursor_release(MDB_cursor *cursor)
{
	mdb_cursor_close(cursor);
}

/*                       data serialization                                   */

#define PACKED_LEN(str) (strlen(str) + 1) /* length of packed string including terminal byte */
#define PACKED_ENTRY_LEN(e) \
	(PACKED_LEN(e->ip) + PACKED_LEN(e->threat_code) + PACKED_LEN(e->syslog_ip))
static inline void pack_str(char **stream, const char *str) {
	int len = PACKED_LEN(str);
	memcpy(*stream, str, len);
	*stream += len;
}
static inline char *unpack_str(char **stream) {
	char *ret = *stream;
	*stream += PACKED_LEN(ret);
	return ret;
}

static MDB_val pack_key(const knot_dname_t *name)
{
	MDB_val key = { knot_dname_size(name), (void *)name };
	return key;
}

static int pack_entry(MDB_val *data, struct entry *entry)
{
	char *stream = data->mv_data;
	pack_str(&stream, entry->ip);
	pack_str(&stream, entry->threat_code);
	pack_str(&stream, entry->syslog_ip);

	return KNOT_EOK;
}

static int unpack_entry(MDB_val *data, struct entry *entry)
{
	char *stream = data->mv_data;
	entry->ip = unpack_str(&stream);
	entry->threat_code = unpack_str(&stream);
	entry->syslog_ip = unpack_str(&stream);

	return KNOT_EOK;
}

static int remove_entry(MDB_cursor *cur)
{
	int ret = mdb_cursor_del(cur, 0);
	if (ret != 0) {
		return KNOT_ERROR;
	}

	return KNOT_EOK;
}

/*                       database api                                   */

struct cache *cache_open(const char *handle, unsigned flags, mm_ctx_t *mm)
{
	struct cache *cache = mm_alloc(mm, sizeof(struct cache));
	if (cache == NULL) {
		return NULL;
	}
	memset(cache, 0, sizeof(struct cache));

	int ret = dbase_open(cache, handle);
	if (ret != 0) {
		mm_free(mm, cache);
		return NULL;
	}

	cache->pool = mm;
	return cache;
}

void cache_close(struct cache *cache)
{
	if (cache == NULL) {
		return;
	}

	dbase_close(cache);
	mm_free(cache->pool, cache);
}

int cache_query_fetch(MDB_txn *txn, MDB_dbi dbi, const knot_dname_t *name, struct entry *entry)
{
	MDB_cursor *cursor = cursor_acquire(txn, dbi);
	if (cursor == NULL) {
		return KNOT_ERROR;
	}

	MDB_val key = pack_key(name);
	MDB_val data = { 0, NULL };
	int ret = mdb_cursor_get(cursor, &key, &data, MDB_SET_KEY);
	if (ret != 0) {
		cursor_release(cursor);
		return ret;
	}

	ret = unpack_entry(&data, entry);
	if (ret != 0) {
		cursor_release(cursor);
		return KNOT_ENOENT;
	}

	entry->cursor = cursor;
	return KNOT_EOK;
}

void cache_query_release(struct entry *entry)
{
	mdb_cursor_close(entry->cursor);
	entry->cursor = NULL;
}

int cache_insert(MDB_txn *txn, MDB_dbi dbi, const knot_dname_t *name, struct entry *entry)
{
	MDB_cursor *cursor = cursor_acquire(txn, dbi);
	if (cursor == NULL) {
		return KNOT_ERROR;
	}

	size_t len = PACKED_ENTRY_LEN(entry);
	MDB_val key = pack_key(name);
	MDB_val data = { len, NULL };

	int ret = mdb_cursor_put(cursor, &key, &data, MDB_RESERVE);
	if (ret != 0) {
		return ret;
	}

	ret = pack_entry(&data, entry);

	cursor_release(cursor);
	return ret;
}

int cache_remove(MDB_txn *txn, MDB_dbi dbi, const knot_dname_t *name)
{
	MDB_cursor *cursor = cursor_acquire(txn, dbi);
	if (cursor == NULL) {
		return KNOT_ERROR;
	}

	MDB_val data;
	MDB_val key = pack_key(name);
	int ret = mdb_cursor_get(cursor, &key, &data, MDB_SET);
	if (ret != 0) {
		return KNOT_ENOENT;
	}

	ret = remove_entry(cursor);

	cursor_release(cursor);
	return ret;
}

/*                       module callbacks                                   */

#define DEFAULT_TTL 300
#define DEFAULT_PORT 514
#define SYSLOG_BUFLEN 1024 /* RFC3164, 4.1 message size. */
#define SYSLOG_FACILITY 3  /* System daemon. */
#define MODULE_ERR(msg...) log_error("module 'dcu', " msg)

/*! \brief Safe stream skipping. */
static int stream_skip(char **stream, size_t *maxlen, int nbytes)
{
	/* Error or space limit exceeded. */
	if (nbytes < 0 || nbytes >= *maxlen) {
		return KNOT_ESPACE;
	}

	*stream += nbytes;
	*maxlen -= nbytes;
	return 0;
}

/*! \brief Stream write with constraints checks. */
#define STREAM_WRITE(stream, maxlen, fn, args...) \
	if (stream_skip(&(stream), (maxlen), fn(stream, *(maxlen), args)) != KNOT_EOK) { \
		return KNOT_ESPACE; \
	}

static int dynzone_log_message(char *stream, size_t *maxlen, struct entry *entry, struct query_data *qdata)
{
	struct sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	time_t now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);

	/* Field 1 Timestamp (UTC). */
	STREAM_WRITE(stream, maxlen, strftime, "%Y-%m-%d %H:%M:%S\t", &tm);

	/* Field 2/3 Local, remote address. */
	const struct sockaddr_storage *remote = qdata->param->remote;
	memcpy(&addr, remote, sockaddr_len(remote));
	int client_port = sockaddr_port(&addr);
	sockaddr_port_set(&addr, 0);
	STREAM_WRITE(stream, maxlen, sockaddr_tostr, &addr);
	STREAM_WRITE(stream, maxlen, snprintf, "\t");
	getsockname(qdata->param->socket, (struct sockaddr *)&addr, &addr_len);
	int server_port = sockaddr_port(&addr);
	sockaddr_port_set(&addr, 0);
	STREAM_WRITE(stream, maxlen, sockaddr_tostr, &addr);
	STREAM_WRITE(stream, maxlen, snprintf, "\t");

	/* Field 4/5 Local, remote port. */
	STREAM_WRITE(stream, maxlen, snprintf, "%d\t%d\t", client_port, server_port);

	/* Field 6 Threat ID. */
	STREAM_WRITE(stream, maxlen, snprintf, "%s\t", entry->threat_code);

	/* Field 7 - 13 NULL */
	STREAM_WRITE(stream, maxlen, snprintf, "\t\t\t\t\t\t\t");

	/* Field 14 QNAME */
	char *qname = knot_dname_to_str(knot_pkt_qname(qdata->query));
	STREAM_WRITE(stream, maxlen, snprintf, "%s\t", qname);
	free(qname);

	/* Field 15 Resolution (0 = local, 1 = lookup)*/
	STREAM_WRITE(stream, maxlen, snprintf, "0\t");

	/* Field 16 First IP. */
	STREAM_WRITE(stream, maxlen, snprintf, "%s\t", entry->ip);

	/* Field 17 Connection type. */
	STREAM_WRITE(stream, maxlen, snprintf, "%s\t",
	             net_is_connected(qdata->param->socket) ? "TCP" : "UDP");

	/* Field 18 Query type. */
	char type_str[16] = { '\0' };
	knot_rrtype_to_string(knot_pkt_qtype(qdata->query), type_str, sizeof(type_str));
	STREAM_WRITE(stream, maxlen, snprintf, "%s", type_str);

	return KNOT_EOK;
}

static int dynzone_send_log(int sock, struct sockaddr_storage *dst_addr, struct entry *entry, struct query_data *qdata)
{
	char buf[SYSLOG_BUFLEN];
	char *stream = buf;
	size_t maxlen = sizeof(buf);

	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);

	/* Add facility. */
	STREAM_WRITE(stream, &maxlen, snprintf, "<%u>", SYSLOG_FACILITY);

	/* Current local time (4.3.2)*/
	STREAM_WRITE(stream, &maxlen, strftime, "%b %d %H:%M:%S ", &tm);

	/* Host name / Component. */
	STREAM_WRITE(stream, &maxlen, snprintf, "%s ", conf()->identity);
	STREAM_WRITE(stream, &maxlen, snprintf, "%s[%lu]: ", PACKAGE_NAME, (unsigned long) getpid());

	/* Prepare log message line. */
	int ret = dynzone_log_message(stream, &maxlen, entry, qdata);
	if (ret != KNOT_EOK) {
		return ret;
	}

	/* Send log message line. */
	sendto(sock, buf, sizeof(buf) - maxlen, 0, (struct sockaddr *)dst_addr,
	       sockaddr_len(dst_addr));

	return ret;
}

static int dynzone_synth(knot_pkt_t *pkt, struct entry *entry)
{
	size_t addr_len = 0;
	struct sockaddr_storage addr;
	sockaddr_set(&addr, AF_INET, entry->ip, 0);
	const uint8_t *raw_addr = sockaddr_raw(&addr, &addr_len);

	knot_rrset_t rr;
	knot_rrset_init(&rr, (knot_dname_t *)knot_pkt_qname(pkt), KNOT_RRTYPE_A, KNOT_CLASS_IN);
	int ret = knot_rrset_add_rdata(&rr, raw_addr, addr_len, DEFAULT_TTL, &pkt->mm);
	if (ret != KNOT_EOK) {
		assert(0);
		return ret;
	}

	/*! \note The RR will be cleared for iteration. */
	ret = knot_pkt_put(pkt, COMPR_HINT_QNAME, &rr, 0);
	knot_rrset_clear(&rr, &pkt->mm);
	return ret;
}

static int dynzone_query_txn(MDB_txn *txn, MDB_dbi dbi, knot_pkt_t *pkt, struct query_data *qdata)
{
	struct entry entry;
	int ret = 0;

	/* Find suffix for QNAME. */
	const knot_dname_t *qname = knot_pkt_qname(qdata->query);
	const knot_dname_t *key = qname;
	while(key) {
		ret = cache_query_fetch(txn, dbi, key, &entry);
		if (ret == 0) { /* Found */
			break;
		}

		if (*key == '\0') { /* Last label, not found. */
			return KNOT_ENOENT;
		}

		key = knot_wire_next_label(key, qdata->query->wire);
	}

	/* Synthetize A record to response. */
	ret = dynzone_synth(pkt, &entry);

	/* Send message to syslog. */
	struct sockaddr_storage syslog_addr;
	sockaddr_set(&syslog_addr, AF_INET, entry.syslog_ip, DEFAULT_PORT);
	int sock = net_unbound_socket(AF_INET, &syslog_addr);
	if (sock > 0) {
		dynzone_send_log(sock, &syslog_addr, &entry, qdata);
		close(sock);
	}

	cache_query_release(&entry);
	return ret;
}

static int dynzone_query(int state, knot_pkt_t *pkt, struct query_data *qdata, void *ctx)
{
	if (pkt == NULL || qdata == NULL || ctx == NULL) {
		return NS_PROC_FAIL;
	}

	/* Applicable for A-queries only. */
	if (knot_pkt_qtype(qdata->query) != KNOT_RRTYPE_A) {
		return state;
	}

	struct cache *cache = ctx;

	MDB_txn *txn = NULL;
	int ret = mdb_txn_begin(cache->env, NULL, MDB_RDONLY, &txn);
	if (ret != 0) { /* Can't start transaction, ignore. */
		return state;
	}

	ret = dynzone_query_txn(txn, cache->dbi, pkt, qdata);
	if (ret != 0) { /* Can't find matching zone, ignore. */
		mdb_txn_abort(txn);
		return state;
	}

	mdb_txn_abort(txn);

	return NS_PROC_DONE;
}

int dynzone_load(struct query_plan *plan, struct query_module *self)
{
	struct cache *cache = cache_open(self->param, 0, self->mm);
	if (cache == NULL) {
		MODULE_ERR("couldn't open cache file");
		return KNOT_ENOMEM;
	}

	self->ctx = cache;

	return query_plan_step(plan, QPLAN_BEGIN, dynzone_query, cache);
}

int dynzone_unload(struct query_module *self)
{
	cache_close(self->ctx);
	return KNOT_EOK;
}

