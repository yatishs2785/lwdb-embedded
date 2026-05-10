#pragma once

/* lwdb-embedded
 * Public API – platform independent
 *
 * NOTE:
 *  - No platform headers
 *  - No file system assumptions
 *  - No threading
 *  - Safe for embedded / RTOS
 */

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================
 * Forward Declarations (Opaque)
 * ================================ */

typedef struct lwdb_handle      lwdb_handle_t;
typedef struct lwdb_result      lwdb_result_t;

/* ================================
 * Status / Error Codes
 * ================================ */

typedef enum
{
    LWDB_OK = 0,

    LWDB_ERR_INVALID_QUERY,
    LWDB_ERR_UNKNOWN_TABLE,
    LWDB_ERR_SCHEMA_MISMATCH,
    LWDB_ERR_STORAGE_FAILURE,
    LWDB_ERR_NO_MEMORY,
    LWDB_ERR_NOT_INITIALIZED,

    LWDB_ERR_INTERNAL

} lwdb_status_t;

/* ================================
 * Database Lifecycle
 * ================================ */

/* Create / initialize database instance.
 * Storage backend must be injected separately via plugin.
 */
lwdb_handle_t* lwdb_create(void);

/* Destroys database instance and releases all resources */
void lwdb_destroy(lwdb_handle_t* db);

/* ================================
 * Query Execution (DDL / DML)
 * ================================ */

/* Execute SQL-like command:
 * CREATE, INSERT, DELETE, DROP, etc.
 */
lwdb_status_t lwdb_execute(
    lwdb_handle_t* db,
    const char*    sql
);

/* ================================
 * Query Execution (SELECT)
 * ================================ */

/* Execute SELECT query.
 * Result is stored in lwdb_result_t.
 */
lwdb_status_t lwdb_query(
    lwdb_handle_t*  db,
    const char*     sql,
    lwdb_result_t** out_result
);

/* ================================
 * Query Result Access
 * ================================ */

/* Number of rows in result */
size_t lwdb_result_row_count(const lwdb_result_t* result);

/* Number of columns in result */
size_t lwdb_result_column_count(const lwdb_result_t* result);

/* Get cell value (null‑terminated string).
 * Lifetime valid until lwdb_result_free().
 */
const char* lwdb_result_get(
    const lwdb_result_t* result,
    size_t               row,
    size_t               column
);

/* Free result object */
void lwdb_result_free(lwdb_result_t* result);

/* ================================
 * Optional: Diagnostics
 * ================================ */

/* Get last error message (human readable).
 * Buffer is owned by lwdb.
 */
const char* lwdb_last_error(lwdb_handle_t* db);

#ifdef __cplusplus
}
#endif
