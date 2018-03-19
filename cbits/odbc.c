#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <odbcss.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>
#include <odbcss.h>
#include "msodbcsql.h"

#define FALSE 0
#define TRUE 1
#define MAXBUFLEN 256

// Just a way of grouping together these two dependent resources. It's
// probably not a good idea to free up an environment before freeing a
// database. So, we put them together so that they can be allocated
// and freed atomically.
typedef struct EnvAndDbc {
  SQLHENV *env;
  SQLHDBC *dbc;
  char *error;
} EnvAndDbc;

char *odbc_error(EnvAndDbc *envAndDbc){
  return envAndDbc->error;
}

void odbc_ProcessLogMessages(EnvAndDbc *envAndDbc, SQLSMALLINT plm_handle_type, SQLHANDLE plm_handle, char *logstring, int ConnInd);

////////////////////////////////////////////////////////////////////////////////
// Alloc/dealloc env

SQLHENV *odbc_SQLAllocEnv(){
  SQLHENV *henv = malloc(sizeof *henv);
  *henv = SQL_NULL_HENV;
  RETCODE retcode = SQLAllocHandle (SQL_HANDLE_ENV, NULL, henv);
  if ((retcode != SQL_SUCCESS_WITH_INFO) && (retcode != SQL_SUCCESS)) {
    free(henv);
    return NULL;
  } else {
    return henv;
  }
}

void odbc_SQLFreeEnv(SQLHENV *henv){
  if(henv != NULL && *henv != SQL_NULL_HENV) {
    SQLFreeHandle(SQL_HANDLE_ENV, *henv);
    free(henv);
  }
}

RETCODE odbc_SetEnvAttr(SQLHENV *henv){
  return
    SQLSetEnvAttr(*henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, SQL_IS_INTEGER);
}

////////////////////////////////////////////////////////////////////////////////
// Alloc/dealloc dbc

SQLHDBC *odbc_SQLAllocDbc(SQLHENV *henv){
  SQLHDBC *hdbc = malloc(sizeof *hdbc);
  *hdbc = SQL_NULL_HDBC;
  RETCODE retcode = SQLAllocHandle (SQL_HANDLE_DBC, *henv, hdbc);
  if ((retcode != SQL_SUCCESS_WITH_INFO) && (retcode != SQL_SUCCESS)) {
    free(hdbc);
    return NULL;
  } else {
    return hdbc;
  }
}

void odbc_SQLFreeDbc(SQLHDBC *hdbc){
  if(hdbc != NULL && *hdbc != SQL_NULL_HDBC) {
    SQLFreeHandle(SQL_HANDLE_DBC, *hdbc);
    free(hdbc);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Allocate/dealloc env-and-database

EnvAndDbc *odbc_AllocEnvAndDbc(){
  SQLHENV *env = odbc_SQLAllocEnv();
  if (env == NULL) {
    return NULL;
  } else {
    int retcode = odbc_SetEnvAttr(env);
    if ((retcode != SQL_SUCCESS_WITH_INFO) && (retcode != SQL_SUCCESS)) {
      free(env);
      return NULL;
    } else {
      SQLHDBC *dbc = odbc_SQLAllocDbc(env);
      if (dbc == NULL) {
        return NULL;
      } else {
        EnvAndDbc *envAndDbc = malloc(sizeof *envAndDbc);
        envAndDbc->env = env;
        envAndDbc->dbc = dbc;
        envAndDbc->error = calloc(MAXBUFLEN,1);
        return envAndDbc;
      }
    }
  }
}

void odbc_FreeEnvAndDbc(EnvAndDbc *envAndDbc){
  free(envAndDbc->error);
  odbc_SQLFreeDbc(envAndDbc->dbc);
  odbc_SQLFreeEnv(envAndDbc->env);
  free(envAndDbc);
}

////////////////////////////////////////////////////////////////////////////////
// Connect/disconnect

RETCODE odbc_SQLDriverConnectW(EnvAndDbc *envAndDbc, SQLWCHAR *connString, SQLSMALLINT len){
  SQLSMALLINT ignored = 0;
  RETCODE r = SQLDriverConnectW(
    *(envAndDbc->dbc),
    NULL,
    connString,
    len,
    NULL,
    0,
    &ignored,
    SQL_DRIVER_NOPROMPT);
  if (r == SQL_ERROR)
    odbc_ProcessLogMessages(envAndDbc, SQL_HANDLE_DBC, *(envAndDbc->dbc), "odbc_SQLDriverConnectW", FALSE);
  return r;
}

void odbc_SQLDisconnect(EnvAndDbc *envAndDbc){
  SQLDisconnect(*(envAndDbc->dbc));
}

////////////////////////////////////////////////////////////////////////////////
// Alloc/dealloc statement

SQLHSTMT *odbc_SQLAllocStmt(EnvAndDbc *envAndDbc){
  SQLHSTMT *hstmt = malloc(sizeof *hstmt);
  *hstmt = SQL_NULL_HSTMT;
  RETCODE retcode = SQLAllocHandle (SQL_HANDLE_STMT, *(envAndDbc->dbc), hstmt);
  if ((retcode != SQL_SUCCESS_WITH_INFO) && (retcode != SQL_SUCCESS)) {
    free(hstmt);
    return NULL;
  } else {
    return hstmt;
  }
}

void odbc_SQLFreeStmt(SQLHSTMT *hstmt){
  if(hstmt != NULL && *hstmt != SQL_NULL_HSTMT) {
    SQLFreeHandle(SQL_HANDLE_STMT, *hstmt);
    free(hstmt);
  }
}

////////////////////////////////////////////////////////////////////////////////
// Execute

RETCODE odbc_SQLExecDirectW(EnvAndDbc *envAndDbc, SQLHSTMT *hstmt, SQLWCHAR *stmt, SQLINTEGER len){
  RETCODE r = SQLExecDirectW(*hstmt, stmt, len);
  if (r == SQL_ERROR) odbc_ProcessLogMessages(envAndDbc, SQL_HANDLE_STMT, *hstmt, "odbc_SQLExecDirectW", FALSE);
  return r;
}

////////////////////////////////////////////////////////////////////////////////
// Fetch row

RETCODE odbc_SQLFetch(EnvAndDbc *envAndDbc, SQLHSTMT *hstmt){
  RETCODE r = SQLFetch(*hstmt);
  if (r == SQL_ERROR) odbc_ProcessLogMessages(envAndDbc, SQL_HANDLE_STMT, *hstmt, "odbc_SQLFetch", FALSE);
  return r;
}

RETCODE odbc_SQLMoreResults(EnvAndDbc *envAndDbc, SQLHSTMT *hstmt){
  RETCODE r = SQLMoreResults(*hstmt);
  if (r == SQL_ERROR) odbc_ProcessLogMessages(envAndDbc, SQL_HANDLE_STMT, *hstmt, "odbc_SQLMoreResults", FALSE);
  return r;
}

////////////////////////////////////////////////////////////////////////////////
// Get fields

RETCODE odbc_SQLGetData(EnvAndDbc *envAndDbc,
                        SQLHSTMT *hstmt,
                        SQLUSMALLINT   col,
                        SQLSMALLINT    targetType,
                        SQLPOINTER     buffer,
                        SQLLEN         bufferLen,
                        SQLLEN *       resultLen){
  RETCODE r = SQLGetData(*hstmt, col, targetType, buffer, bufferLen, resultLen);
  if (r == SQL_ERROR) odbc_ProcessLogMessages(envAndDbc, SQL_HANDLE_STMT, *hstmt, "odbc_SQLGetData", FALSE);
  return r;
}

SQLRETURN odbc_SQLDescribeColW(
  SQLHSTMT      *StatementHandle,
  SQLUSMALLINT   ColumnNumber,
  SQLWCHAR *      ColumnName,
  SQLSMALLINT    BufferLength,
  SQLSMALLINT *  NameLengthPtr,
  SQLSMALLINT *  DataTypePtr,
  SQLULEN *      ColumnSizePtr,
  SQLSMALLINT *  DecimalDigitsPtr,
  SQLSMALLINT *  NullablePtr){
  return SQLDescribeColW(
    *StatementHandle,
    ColumnNumber,
    ColumnName,
    BufferLength,
    NameLengthPtr,
    DataTypePtr,
    ColumnSizePtr,
    DecimalDigitsPtr,
    NullablePtr);
}

////////////////////////////////////////////////////////////////////////////////
// Get columns

RETCODE odbc_SQLNumResultCols(SQLHSTMT *hstmt, SQLSMALLINT *cols){
  return SQLNumResultCols(*hstmt, cols);
}

////////////////////////////////////////////////////////////////////////////////
// Logs

void odbc_ProcessLogMessages(EnvAndDbc *envAndDbc, SQLSMALLINT plm_handle_type, SQLHANDLE plm_handle, char *logstring, int ConnInd) {
  RETCODE plm_retcode = SQL_SUCCESS;
  UCHAR plm_szSqlState[MAXBUFLEN] = "";
  SDWORD plm_pfNativeError = 0L;
  SWORD plm_pcbErrorMsg = 0;
  SQLSMALLINT plm_cRecNmbr = 1;
  while (plm_retcode != SQL_NO_DATA_FOUND) {
    plm_retcode = SQLGetDiagRec(plm_handle_type, plm_handle, plm_cRecNmbr,
                                plm_szSqlState, &plm_pfNativeError, (SQLCHAR *)envAndDbc->error,
                                MAXBUFLEN - 1, &plm_pcbErrorMsg);
    plm_cRecNmbr++;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Accessors for DATE_STRUCT

SQLSMALLINT DATE_STRUCT_year(DATE_STRUCT *d){
  return d->year;
}
SQLUSMALLINT DATE_STRUCT_month(DATE_STRUCT *d){
  return d->month;
}
SQLUSMALLINT DATE_STRUCT_day(DATE_STRUCT *d){
  return d->day;
}

////////////////////////////////////////////////////////////////////////////////
// Accessors for SQL_SS_TIME2_STRUCT

SQLUSMALLINT SQL_SS_TIME2_STRUCT_hour(SQL_SS_TIME2_STRUCT *t){
  return t->hour;
}
SQLUSMALLINT SQL_SS_TIME2_STRUCT_minute(SQL_SS_TIME2_STRUCT *t){
  return t->minute;
}
SQLUSMALLINT SQL_SS_TIME2_STRUCT_second(SQL_SS_TIME2_STRUCT *t){
  return t->second;
}
SQLUINTEGER SQL_SS_TIME2_STRUCT_fraction(SQL_SS_TIME2_STRUCT *t){
  return t->fraction;
}
