#include <config.h>

#include <string.h>
#include <stdio.h>
#include <time.h>

#include <json/json.h>
#include <curl/curl.h>

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

static void 
remotedb_fill_soa(dns_sdblookup_t *lookup, json_object *jobj) 
{
	if (json_type_array != json_object_get_type(jobj)) {
		// printf("Wrong type in SOA field\n");
		return;
	}
	
	if (json_object_array_length(jobj) != 3) {
		// printf("Wrong format (SOA requires 3 fields)\n");
		return;
	}
	
	struct json_object *jmname 	= json_object_array_get_idx(jobj, 0);
	struct json_object *jname 	= json_object_array_get_idx(jobj, 1);
	struct json_object *jserial	= json_object_array_get_idx(jobj, 2);
	
	const char *mname = json_object_get_string(jmname);
	const char *name = json_object_get_string(jname);
	int serial = json_object_get_int(jserial);
	
	dns_sdb_putsoa(lookup, mname, name, serial);
	// printf("type = %s, name = %s, serial = %d\n", mname, name, serial);
}

static void 
remotedb_fill_generic(dns_sdblookup_t *lookup, const char *type, json_object *jobj) 
{
	if (json_type_string != json_object_get_type(jobj)) {
		// printf("Wrong type in generic field\n");
		return;
	}
	
	dns_sdb_putrr(lookup, type, 86400, json_object_get_string(jobj));	
	// printf("type = %s, field = %s\n", type, json_object_get_string(jobj));
}

static void 
remotedb_routing(dns_sdblookup_t *lookup, json_object *jobj) 
{
	struct json_object *jtype, *jfield;
	
	if (json_object_get_type(jobj) != json_type_object) {
		// printf("Wrong type in field\n");
		return;
	}
	
	if ((jtype = json_object_object_get(jobj, "type")) == NULL
		|| (jfield = json_object_object_get(jobj, "field")) == NULL)
		return;
	
	const char *type = json_object_get_string(jtype);
	
	if (strcmp(type, "SOA") == 0) 
		remotedb_fill_soa(lookup, jfield);
	else
		remotedb_fill_generic(lookup, type, jfield);
	
}

static void 
remotedb_curl(char *ptr, size_t size, size_t nmemb, dns_sdblookup_t *lookup)
{
	UNUSED(size);
	UNUSED(nmemb);
	
	json_object * jobj = json_tokener_parse(ptr);
	
	if ((int) jobj < 0) {
		// printf("Invalid json\n");
		return;
	}
	
	if (json_object_object_get(jobj, "data") == NULL) {
		// printf("Simple call\n");
		remotedb_routing(lookup, jobj);
	}
		
	enum json_type type;
	
	json_object_object_foreach(jobj, key, val) {
		type = json_object_get_type(val);
		switch (type) {
			case json_type_array: 
				if (strcmp("data", key) != 0) {
					// printf("Only route with data as key\n");
				} 

				int arraylen = json_object_array_length(val);
				
				int i;
				json_object * jvalue;
				for (i = 0; i < arraylen; i++){
					jvalue = json_object_array_get_idx(val, i);
					remotedb_routing(lookup, jvalue);
				}
				break;
			default: 
				break;
		}
	}
	
	json_object_put(jobj);
}
 
static isc_result_t
remotedb_lookup(const char *zone, const char *name, void *dbdata, dns_sdblookup_t *lookup)
{
	dbinfo_t *dbi = (dbinfo_t *) dbdata;
	
	UNUSED(dbdata);

	// We do not handle $ORIGIN
	if (strcmp(name, "@") == 0)
		return (ISC_R_SUCCESS);
	
	char request[1024] = "";
	sprintf(request, "%s/%s/lookup?name=%s", dbi->url, zone, name);

	CURL *curl;
	CURLcode res;
	
	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, request);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remotedb_curl);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, lookup);
		
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
	}

	return (ISC_R_SUCCESS);
}

static isc_result_t
remotedb_authority(const char *zone, void *dbdata, dns_sdblookup_t *lookup) 
{
	dbinfo_t *dbi = (dbinfo_t *) dbdata;

	UNUSED(zone);
	UNUSED(dbdata);

	char request[1024] = "";
	sprintf(request, "%s/%s/authority", dbi->url, zone);

	CURL *curl;
	CURLcode res;
	
	curl = curl_easy_init();
	if(curl) {
		curl_easy_setopt(curl, CURLOPT_URL, request);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, remotedb_curl);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, lookup);
		
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
	}

	return (ISC_R_SUCCESS);
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
remotedb_create(const char *zone, int argc, char **argv, void *driverdata, void **dbdata)
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
remotedb_init(void) 
{
	unsigned int flags;
	flags = DNS_SDBFLAG_RELATIVEOWNER | DNS_SDBFLAG_RELATIVERDATA;
	return (dns_sdb_register("remote", &remotedb_methods, NULL, flags,
				 ns_g_mctx, &remotedb));
}

void
remotedb_clear(void) 
{
	if (remotedb != NULL)
		dns_sdb_unregister(&remotedb);
}
