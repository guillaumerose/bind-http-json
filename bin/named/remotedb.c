#include <config.h>

#include <string.h>
#include <stdio.h>

#include <isc/mem.h>
#include <isc/print.h>
#include <isc/result.h>
#include <isc/util.h>

#include <dns/sdb.h>

#include <named/globals.h>

#include "remotedb.h"

typedef struct _dbinfo {
	char *url;
} dbinfo_t;

static dns_sdbimplementation_t *remotedb = NULL;

static isc_result_t
remotedb_lookup(const char *zone, const char *name, void *dbdata,
	      dns_sdblookup_t *lookup)
{
	UNUSED(zone);
	UNUSED(dbdata);

	return (ISC_R_FAILURE);
}

static isc_result_t
remotedb_authority(const char *zone, void *dbdata, dns_sdblookup_t *lookup) {
	UNUSED(zone);
	UNUSED(dbdata);

	return (ISC_R_FAILURE);
}

#define STRDUP_OR_FAIL(target, source)				\
	do {							\
		target = isc_mem_strdup(ns_g_mctx, source);	\
		if (target == NULL) {				\
			result = ISC_R_NOMEMORY;		\
			goto cleanup;				\
		}						\
	} while (0);

static isc_result_t
remotedb_create(const char *zone,
		int argc, char **argv,
		void *driverdata, void **dbdata)
{
	dbinfo_t *dbi;
	isc_result_t result;

	UNUSED(zone);
	UNUSED(driverdata);

	if (argc < 1)
		return (ISC_R_FAILURE);

	dbi = isc_mem_get(ns_g_mctx, sizeof(dbinfo_t));
	if (dbi == NULL)
		return (ISC_R_NOMEMORY);

	dbi->url = NULL;
	STRDUP_OR_FAIL(dbi->url, argv[0]);
	
	*dbdata = dbi;

	return (ISC_R_SUCCESS);

cleanup:
	return (result);
}

static dns_sdbmethods_t remotedb_methods = {
	remotedb_lookup,
	remotedb_authority,
	NULL,
	remotedb_create,
	NULL
};

isc_result_t
remotedb_init(void) {
	unsigned int flags;
	flags = DNS_SDBFLAG_RELATIVEOWNER | DNS_SDBFLAG_RELATIVERDATA;
	return (dns_sdb_register("remote", &remotedb_methods, NULL, flags,
				 ns_g_mctx, &remotedb));
}

void
remotedb_clear(void) {
	if (remotedb != NULL)
		dns_sdb_unregister(&remotedb);
}
