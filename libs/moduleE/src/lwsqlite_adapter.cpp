#include "lwsqlite_adapter.h"

extern "C" {
#include "lwdb/lwdb.h"
}

void* lwdb_open(const char* /*dbFile*/)
{
    // Current lwdb-embedded v1 uses in-memory storage by default.
    // dbFile is accepted for future file-storage plugin support.
    return static_cast<void*>(lwdb_create());
}

void lwdb_close(void* handle)
{
    if (!handle) return;
    lwdb_destroy(static_cast<lwdb_handle_t*>(handle));
}

bool lwdb_exec(void* handle, const char* sql)
{
    if (!handle || !sql) return false;
    lwdb_status_t st = lwdb_execute(static_cast<lwdb_handle_t*>(handle), sql);
    return st == LWDB_OK;
}

bool lwdb_query(void* handle, const char* sql, lwdb_result_t* out)
{
    if (!handle || !sql || !out) return false;

    lwdb_result_t* result = nullptr;
    lwdb_status_t st = lwdb_query(static_cast<lwdb_handle_t*>(handle), sql, &result);
    if (st != LWDB_OK || !result) return false;

    const size_t rows = lwdb_result_row_count(result);
    const size_t cols = lwdb_result_column_count(result);

    out->rows.clear();
    out->rows.reserve(rows);

    for (size_t r = 0; r < rows; ++r)
    {
        std::vector<std::string> row;
        row.reserve(cols);

        for (size_t c = 0; c < cols; ++c)
        {
            const char* v = lwdb_result_get(result, r, c);
            row.emplace_back(v ? v : "");
        }

        out->rows.emplace_back(std::move(row));
    }

    lwdb_result_free(result);
    return true;
}

void lwdb_result_free(lwdb_result_t* r)
{
    if (!r) return;
    r->rows.clear();
}
