/*
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 * 
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************************************
 * SQLExecute()
 * CLI Compliance: ISO 92
 *
 * Author: Martin van Dinther
 * Date  : 30 Aug 2002
 *
 **********************************************************************/

#include "ODBCGlobal.h"
#include "ODBCStmt.h"
#include "ODBCUtil.h"

static struct msql_types {
	char *name;
	int concise_type;
} msql_types[] = {
	{"bigint", SQL_BIGINT},
	{"boolean", SQL_BIT},
	{"character", SQL_CHAR},
	{"date", SQL_TYPE_DATE},
	{"decimal", SQL_DECIMAL},
	{"double", SQL_DOUBLE},
	{"float", SQL_FLOAT},
	{"int", SQL_INTEGER},
	{"mediumint", SQL_INTEGER},
	{"month_interval", SQL_INTERVAL_MONTH},
	{"sec_interval", SQL_INTERVAL_SECOND},
	{"smallint", SQL_SMALLINT},
	{"time", SQL_TYPE_TIME},
	{"timestamp", SQL_TYPE_TIMESTAMP},
	{"tinyint", SQL_TINYINT},
	{"varchar", SQL_VARCHAR},
	{"blob", SQL_BINARY},
	{"datetime", 0},
	{"oid", SQL_GUID},
	{"table", 0},
	{"ubyte", SQL_TINYINT},
	{0, 0},			/* sentinel */
};

SQLRETURN
ODBCInitResult(ODBCStmt *stmt)
{
	int i = 0;
	int nrCols;
	ODBCDescRec *rec;
	MapiHdl hdl;

	hdl = stmt->hdl;
	/* initialize the Result meta data values */
	nrCols = mapi_get_field_count(hdl);
	stmt->currentRow = 0;
	stmt->startRow = 0;
	stmt->rowSetSize = 0;
	stmt->retrieved = 0;
	stmt->currentCol = 0;

	if (nrCols == 0 && mapi_get_row_count(hdl) == 0) {
		stmt->State = PREPARED;
		return SQL_SUCCESS;
	}

	setODBCDescRecCount(stmt->ImplRowDescr, nrCols);
	if (stmt->ImplRowDescr->descRec == NULL) {
		addStmtError(stmt, "HY001", NULL, 0);
		return SQL_ERROR;
	}

	rec = stmt->ImplRowDescr->descRec + 1;
	for (i = 0; i < nrCols; i++) {
		struct msql_types *p;
		struct sql_types *tp;
		char *s;

		rec->sql_desc_auto_unique_value = SQL_FALSE;
		rec->sql_desc_nullable = SQL_NULLABLE_UNKNOWN;
		rec->sql_desc_rowver = SQL_FALSE;
		rec->sql_desc_searchable = SQL_PRED_SEARCHABLE;
		rec->sql_desc_updatable = SQL_ATTR_READONLY;

		s = mapi_get_name(hdl, i);
		/* HACK to compensate for generated column names */
		if (s == NULL || strcmp(s, "single_value") == 0)
			s = "";
		if (*s) {
			rec->sql_desc_unnamed = SQL_NAMED;
			rec->sql_desc_base_column_name = (SQLCHAR *) strdup(s);
			rec->sql_desc_label = (SQLCHAR *) strdup(s);
			rec->sql_desc_name = (SQLCHAR *) strdup(s);
		} else {
			rec->sql_desc_unnamed = SQL_UNNAMED;
			rec->sql_desc_base_column_name = NULL;
			rec->sql_desc_label = NULL;
			rec->sql_desc_name = NULL;
		}

		s = mapi_get_type(hdl, i);
		rec->sql_desc_type_name = (SQLCHAR *) strdup(s);
		for (p = msql_types; p->name; p++) {
			if (strcmp(p->name, s) == 0) {
				for (tp = ODBC_sql_types; tp->concise_type; tp++)
					if (p->concise_type == tp->concise_type)
						break;
				rec->sql_desc_concise_type = tp->concise_type;
				rec->sql_desc_type = tp->type;
				rec->sql_desc_datetime_interval_code = tp->code;
				if (tp->precision != UNAFFECTED)
					rec->sql_desc_precision = tp->precision;
				if (tp->datetime_interval_precision != UNAFFECTED)
					rec->sql_desc_datetime_interval_precision = tp->datetime_interval_precision;
				if (tp->scale != UNAFFECTED)
					rec->sql_desc_scale = tp->scale;
				rec->sql_desc_fixed_prec_scale = tp->fixed;
				rec->sql_desc_num_prec_radix = tp->radix;
				rec->sql_desc_unsigned = tp->radix == 0 ? SQL_TRUE : SQL_FALSE;
				break;
			}
		}

		if (rec->sql_desc_concise_type == SQL_CHAR ||
		    rec->sql_desc_concise_type == SQL_VARCHAR)
			rec->sql_desc_case_sensitive = SQL_TRUE;
		else
			rec->sql_desc_case_sensitive = SQL_FALSE;

		rec->sql_desc_base_table_name = (SQLCHAR *) strdup("");
		rec->sql_desc_local_type_name = (SQLCHAR *) strdup("");
		rec->sql_desc_catalog_name = (SQLCHAR *) strdup("");
		rec->sql_desc_literal_prefix = (SQLCHAR *) strdup("");
		rec->sql_desc_literal_suffix = (SQLCHAR *) strdup("");
		rec->sql_desc_schema_name = (SQLCHAR *) strdup("");
		rec->sql_desc_table_name = (SQLCHAR *) strdup("");

		/* unused fields */
		rec->sql_desc_data_ptr = NULL;
		rec->sql_desc_indicator_ptr = NULL;
		rec->sql_desc_octet_length_ptr = NULL;
		rec->sql_desc_parameter_type = 0;

		/* this call must come after other fields have been
		 * initialized */
		rec->sql_desc_length = 0;
		rec->sql_desc_length = ODBCDisplaySize(rec);
		rec->sql_desc_display_size = rec->sql_desc_length;
		rec->sql_desc_octet_length = rec->sql_desc_length;

		rec++;
	}

	stmt->State = EXECUTED;
	return SQL_SUCCESS;
}

SQLRETURN
SQLExecute_(ODBCStmt *stmt)
{
	MapiHdl hdl;
	MapiMsg msg;

	/* check statement cursor state, query should be prepared */
	if (stmt->State != PREPARED) {
		/* 24000 = Invalid cursor state */
		addStmtError(stmt, "24000", NULL, 0);
		return SQL_ERROR;
	}

	/* internal state correctness checks */
	assert(stmt->ImplRowDescr->descRec == NULL);

	assert(stmt->Dbc);
	assert(stmt->Dbc->mid);
	hdl = stmt->hdl;
	assert(hdl);

	/* Have the server execute the query */
	msg = mapi_execute(hdl);
	switch (msg) {
	case MOK:
		break;
	case MTIMEOUT:
		/* 08S01 Communication link failure */
		addStmtError(stmt, "08S01", mapi_error_str(stmt->Dbc->mid), 0);
		return SQL_ERROR;
	default:
		/* General error */
		addStmtError(stmt, "HY000", mapi_error_str(stmt->Dbc->mid), 0);
		return SQL_ERROR;
	}

	/* now get the result data and store it to our internal data structure */

	return ODBCInitResult(stmt);
}

SQLRETURN SQL_API
SQLExecute(SQLHSTMT hStmt)
{
#ifdef ODBCDEBUG
	ODBCLOG("SQLExecute " PTRFMT "\n", PTRFMTCAST hStmt);
#endif

	if (!isValidStmt((ODBCStmt *) hStmt))
		return SQL_INVALID_HANDLE;

	clearStmtErrors((ODBCStmt *) hStmt);

	return SQLExecute_((ODBCStmt *) hStmt);
}
