#include "services.h"
#include "global_structures.h"
#include "Service.h"
#include "utils.h"
#include "NcrackOps.h"
#include <vector>

/* global supported services' lookup table from ncrak-services file */
extern vector <global_service> ServicesTable; 
using namespace std;

static int check_duplicate_services(vector <service_lookup *> services, service_lookup *serv);
static void check_service_option(global_service *temp, char *argname, char *argval);
static int parse_service_argument(char *const arg, char **argname, char **argval);
static int check_duplicate_services(vector <service_lookup *> services, service_lookup *serv);
static global_service parse_services_options(char *const exp);
static int check_supported_services(service_lookup *serv);



/* 
 * Parse service/host/port/options information from target specification
 */
ts_spec
parse_services_target(char *const exp) 
{
	ts_spec temp;
	size_t name_len, host_len, port_len, options_len, tot_len;
	char *s, *h, *p;

	tot_len = strlen(exp);
	memset(&temp, 0, sizeof(temp));
	name_len = port_len = 0;
	p = NULL;

	if ((s = strstr(exp, "://"))) {
		name_len = s - exp;
		temp.service_name = Strndup(exp, name_len);
		s += sizeof("://") - 1;
	} else
		s = exp;

	if ((h = strchr(s, '?'))) {
		if (s == exp)
			fatal("You must specify a service for arguments to be applied to: %s\n", exp);
		if ((p = strchr(s, ':')) && (p < h)) {
			port_len = h - p - 1;
			temp.portno = Strndup(++p, port_len);
		}
		if (p)
			host_len = h - s - port_len - 1;
		else 
			host_len = h - s;

		options_len = exp + tot_len - h;
		temp.service_options = Strndup(++h, options_len);
	} else {
		if ((p = strchr(s, ':'))) {
			if (s == exp)
				fatal("You must specify a service for arguments to be applied to: %s\n", exp);
			port_len = exp + tot_len - p;
			temp.portno = Strndup(++p, port_len);
		}
		if (p)
			host_len = --p - s;
		else 
			host_len = exp + tot_len - s;
	}
	temp.host_expr = Strndup(s, host_len);

	return temp;
}


/*
 * Parsing for -p option
 */
void
parse_services(char *const exp, vector <service_lookup *> &services)
{
	unsigned int nports;
	char *temp, *s;
	service_lookup *serv;

	nports = 0;
	while (1) {
		if (nports == 0)
			temp = strtok(exp, ",");
		else
			temp = strtok(NULL, ",");

		if (temp == NULL)
			break;

		serv = (service_lookup *)safe_zalloc(sizeof(service_lookup));

		if (isdigit(temp[0])) { /* just port number */
			serv->portno = str2port(temp);
		} else {	/* service name and/or port number */
			if ((s = strchr(temp, ':'))) {	/* service name and port number */
				*s = '\0';
				serv->name = strdup(temp);
				serv->portno = str2port(++s);
			} else	/* service name only */
				serv->name = strdup(temp);
		}	
		nports++;

		/* check if service is supported */
		if (check_supported_services(serv))
			continue;

		/* check for duplicate services */
		if (check_duplicate_services(services, serv))
			continue;

		services.push_back(serv);
	}
}


/*
 * Parsing for -m option
 */
void
parse_module_options(char *const exp)
{
	char *name, *options;
	global_service temp;
	vector <global_service>::iterator vi;

	if (!(name = strtok(exp, ":"))) {
		error("No service name specified for option: -m %s . Ignoring...\n", exp);
		return;
	}
	if (!(options = strtok(NULL, ":"))) {
		error("No options specified for module: %s . Ignoring...\n", name);
		return;
	}

	/* retrieve struct with options specified for this module */
	temp = parse_services_options(options);

	for (vi = ServicesTable.begin(); vi != ServicesTable.end(); vi++) {
		if (!strcmp(vi->lookup.name, name))
			break;
	}
	if (vi == ServicesTable.end()) {
		error("Service with name '%s' not supported! Ignoring...\n", name);
		return;
	}

	/* apply any options (non-zero) to global ServicesTable */
	if (temp.timing.connection_limit)
		vi->timing.connection_limit = temp.timing.connection_limit;
	if (temp.timing.auth_limit)
		vi->timing.auth_limit = temp.timing.auth_limit;
	if (temp.timing.connection_delay)
		vi->timing.connection_delay = temp.timing.connection_delay;
	if (temp.timing.retries)
		vi->timing.retries = temp.timing.retries;
	if (temp.misc.ssl)
		vi->misc.ssl = temp.misc.ssl;
}


void
apply_host_options(Service *service, char *const options)
{
	global_service temp;

	/* retrieve struct with options specified for this service */
	temp = parse_services_options(options);

	/* apply any options (non-zero) to this service */
	if (temp.timing.connection_limit)
		service->connection_limit = temp.timing.connection_limit;
	if (temp.timing.auth_limit)
		service->auth_limit = temp.timing.auth_limit;
	if (temp.timing.connection_delay)
		service->connection_delay = temp.timing.connection_delay;
	if (temp.timing.retries)
		service->retries = temp.timing.retries;
	if (temp.misc.ssl)
		service->ssl = temp.misc.ssl;

}


void
apply_service_options(Service *service)
{
	vector <global_service>::iterator vi;

	for (vi = ServicesTable.begin(); vi != ServicesTable.end(); vi++) {
		if (!strcmp(vi->lookup.name, service->name))
			break;
	}
	if (vi == ServicesTable.end())
		fatal("Service with name '%s' not supported!\n", service->name);

	if (!service->portno)
		service->portno = vi->lookup.portno;
	service->connection_limit = vi->timing.connection_limit;
	service->auth_limit = vi->timing.auth_limit;
	service->connection_delay = vi->timing.connection_delay;
	service->retries = vi->timing.retries;
	service->ssl = vi->misc.ssl;

}


void
clean_spec(ts_spec *spec)
{
	if (spec->service_name) {
		free(spec->service_name);
		spec->service_name = NULL;
	}
	if (spec->host_expr) {
		free(spec->host_expr);
		spec->host_expr = NULL;
	}
	if (spec->service_options) {
		free(spec->service_options);
		spec->service_options = NULL;
	}
	if (spec->portno) {
		free(spec->portno);
		spec->portno = NULL;
	}
}



/**** Helper functions ****/



/*
 * Checks for duplicate services by looking at port number.
 */
static int
check_duplicate_services(vector <service_lookup *> services, service_lookup *serv)
{
	vector <service_lookup *>::iterator vi;

	for (vi = services.begin(); vi != services.end(); vi++) {
		if ((*vi)->portno == serv->portno) {
				error("Ignoring duplicate service: '%s:%hu' . Collides with "
						"'%s:%hu'\n", serv->name, serv->portno,
						(*vi)->name, (*vi)->portno);
			return -1;
		}
	}
	return 0;
}



/*
 * Checks if current service is supported by looking at
 * the global lookup table ServicesTable.
 * If a service name has no port, then a default is assigned.
 * If a port has no service name, then the corresponding service
 * name is assigned. Returns -1 if service is not supported.
 */
static int 
check_supported_services(service_lookup *serv)
{
	vector <global_service>::iterator vi;
	/*
	 * If only port is specified make search based on port.
	 * If only service OR if service:port is specified then
	 * lookup based on service name.
	 */
	if (!serv->name && serv->portno) {
		for (vi = ServicesTable.begin(); vi != ServicesTable.end(); vi++) {
			if (vi->lookup.portno == serv->portno) {
				serv->name = strdup(vi->lookup.name); /* assign service name */
				break;
			}
		}
		if (vi == ServicesTable.end()) {
			error("Service with default port '%hu' not supported! Ignoring...\n"
					"For non-default ports specify <service-name>:<non-default-port>\n",
					serv->portno);
			return -1;
		}
	} else if (serv->name) {
		for (vi = ServicesTable.begin(); vi != ServicesTable.end(); vi++) {
			if (!strcmp(vi->lookup.name, serv->name)) {
				if (!serv->portno) /* assign default port number */
					serv->portno = vi->lookup.portno;
				break;
			}
		}
		if (vi == ServicesTable.end()) {
			error("Service with name '%s' not supported! Ignoring...\n",
					serv->name);
			return -1;
		}
	} else 
		fatal("%s failed due to invalid parsing\n", __func__);

	serv->proto = vi->lookup.proto;  // TODO: check for UDP (duplicate etc)
	return 0;
}


static global_service
parse_services_options(char *const exp)
{
	char *arg, *argname, *argval;
	global_service temp;

	memset(&temp, 0, sizeof(temp));
	arg = argval = argname = NULL;

	/* check if we have only one option */
	if ((arg = strtok(exp, ","))) {
		if (!parse_service_argument(arg, &argname, &argval)) {
			check_service_option(&temp, argname, argval);
			free(argname); argname = NULL;
			free(argval); argval = NULL;
		} 	
	}

	while ((arg = strtok(NULL, ","))) {
		/* Tokenize it on our own since we can't use strtok on 2 different
		 * strings at the same time and strtok_r is unportable
		 */
		if (!parse_service_argument(arg, &argname, &argval)) {
			check_service_option(&temp, argname, argval);
			free(argname); argname = NULL;
			free(argval); argval = NULL;
		}
	}
	return temp;
}


/* 
 * Goes through the available options comparing them with 'argname'
 * and if it finds a match, it applies its value ('argval') into 
 * the struct 'temp'
 */
static void
check_service_option(global_service *temp, char *argname, char *argval)
{
	if (!strcmp("cl", argname))
		temp->timing.connection_limit = Strtoul(argval);
	else if (!strcmp("al", argname))
		temp->timing.auth_limit = Strtoul(argval);
	else if (!strcmp("cd", argname))
		temp->timing.connection_delay = tval2msecs(argval);
	else if (!strcmp("mr", argname))
		temp->timing.retries = Strtoul(argval);
	else //TODO misc options
		error("Unknown service option: %s\n", argname);
}


/* 
 * Helper parsing function. Will parse 'arg' which should be in form:
 * argname=argval and store strings into 'argname' and 'argval'.
 * Returns 0 for success.
 */
static int
parse_service_argument(char *const arg, char **argname, char **argval)
{
	size_t i, arg_len, argname_len, argval_len;

	i = 0;
	arg_len = strlen(arg);
	while (i < arg_len) {
		if (arg[i] == '=')
			break;
		i++;
	}
	if (i == arg_len) {
		error("Invalid option argument (missing '='): %s\n", arg);
		return -1;
	}
	argname_len = i;
	*argname = Strndup(arg, argname_len);

	i++; /* arg[i] now points to '=' */
	while (i < arg_len) {
		if (arg[i] == '\0')
			break;
		i++;
	}
	if (i == argname_len + 1) {
		error("No value specified for option %s\n", *argname);
		free(*argname);
		argname = NULL;
		return -1;
	}
	/* allocate i - name_len - 1('=') */
	argval_len = i - argname_len - 1;
	*argval = Strndup(&arg[i - argval_len], argval_len);

	return 0;
}
