/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://monetdb.cwi.nl/Legal/MonetDBLicense-1.1.html
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2009 MonetDB B.V.
 * All Rights Reserved.
 */

/**
 * properties
 * Fabian Groffen
 * Simple functions that deal with the property file
 */

#include "sql_config.h"
#include "properties.h"
#include "utils.h"
#include <stdio.h> /* fprintf, fgets */
#include <string.h> /* memcpy */
#include <gdk.h> /* GDKmalloc */

#define MEROPROPFILEHEADER \
	"# DO NOT EDIT THIS FILE - use monetdb(1) to set properties\n" \
	"# This file is used by merovingian and monetdb\n"

static confkeyval _internal_prop_keys[] = {
	{"logdir",  NULL},
	{"shared",  NULL},
	{ NULL,     NULL}
};

/**
 * Returns the currently supported list of properties.  This list can be
 * used to read all values, modify some and write the file back again.
 * The returned list is GDKmalloced, the keys are a pointer to a static
 * copy and hence need not to be freed, e.g. GDKfree after freeConfFile
 * is enough.
 */
confkeyval *
getDefaultProps(void)
{
	confkeyval *ret = GDKmalloc(sizeof(_internal_prop_keys));
	memcpy(ret, _internal_prop_keys, sizeof(_internal_prop_keys));
	return(ret);
}

/**
 * Writes the given key-value list to MEROPROPFILE in the given path.
 * FIXME: report back errors (check for them first)
 */
inline void
writeProps(confkeyval *ckv, char *path)
{
	char file[1024];
	FILE *cnf;

	snprintf(file, 1024, "%s/" MEROPROPFILE, path);
	cnf = fopen(file, "w");

	fprintf(cnf, "%s", MEROPROPFILEHEADER);
	while (ckv->key != NULL) {
		if (ckv->val != NULL)
			fprintf(cnf, "%s=%s\n", ckv->key, ckv->val);
		ckv++;
	}

	fflush(cnf);
	fclose(cnf);
}

/**
 * Read a property file, filling in the requested key-values.
 */
inline void
readProps(confkeyval *ckv, char *path)
{
	char file[1024];
	FILE *cnf;

	snprintf(file, 1024, "%s/" MEROPROPFILE, path);
	cnf = fopen(file, "r");

	readConfFile(ckv, cnf);

	fclose(cnf);
}

/* vim:set ts=4 sw=4 noexpandtab: */
