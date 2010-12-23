#include <config.h>

#include <string.h>
#include <stdio.h>

#include <isc/print.h>
#include <isc/result.h>
#include <isc/util.h>

#include <dns/sdb.h>

#include <named/globals.h>

#include "remotedb.h"

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

static dns_sdbmethods_t remotedb_methods = {
	remotedb_lookup,
	remotedb_authority,
	NULL,	/* allnodes */
	NULL,	/* create */
	NULL	/* destroy */
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
