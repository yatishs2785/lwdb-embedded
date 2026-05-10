#pragma once

#include "lwdb/lwdb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LWDB_STORAGE_ABI_VERSION 1

/* A rowset is an abstract view returned by the storage layer for SELECT results.
 * Core will copy data out and then destroy the rowset.
 */
typedef struct lwdb_rowset_vtbl
{
    size_t      (*row_count)(void* ctx);
    size_t      (*col_count)(void* ctx);
    const char* (*get)(void* ctx, size_t row, size_t col);
    void        (*destroy)(void* ctx);

} lwdb_rowset_vtbl_t;

typedef struct lwdb_rowset
{
    void*                    ctx;
    const lwdb_rowset_vtbl_t* vtbl;

} lwdb_rowset_t;

/* Storage plugin API */
typedef struct lwdb_storage_api
{
    unsigned    abi_version;
    const char* name;

    void* (*create)(void);
    void  (*destroy)(void* storage_ctx);

    lwdb_status_t (*create_table)(
        void* storage_ctx,
        const char* table_name,
        size_t ncols,
        const char* const* col_names,
        const int* col_types /* 1=int,2=float,3=string */
    );

    lwdb_status_t (*drop_table)(void* storage_ctx, const char* table_name);

    lwdb_status_t (*insert_row)(
        void* storage_ctx,
        const char* table_name,
        size_t nvals,
        const char* const* values
    );

    lwdb_status_t (*select_all)(
        void* storage_ctx,
        const char* table_name,
        lwdb_rowset_t* out_rowset
    );

} lwdb_storage_api_t;

/* Built-in plugin getter (in-memory storage) */
const lwdb_storage_api_t* lwdb_storage_memory(void);

#ifdef __cplusplus
}
#endif
