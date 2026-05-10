#include "lwdb/plugin.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <new>      // std::nothrow

// ------------------------------
// Internal storage structures
// ------------------------------
struct MemTable
{
    std::vector<std::string> col_names;
    std::vector<int>         col_types;
    std::vector<std::vector<std::string>> rows;
};

struct MemStorage
{
    std::unordered_map<std::string, MemTable> tables;
};

// ------------------------------
// Rowset (SELECT view) for a table
// ------------------------------
struct MemRowsetCtx
{
    const MemTable* table;
};

static size_t mem_row_count(void* ctx)
{
    auto* r = static_cast<MemRowsetCtx*>(ctx);
    return r && r->table ? r->table->rows.size() : 0;
}

static size_t mem_col_count(void* ctx)
{
    auto* r = static_cast<MemRowsetCtx*>(ctx);
    return r && r->table ? r->table->col_names.size() : 0;
}

static const char* mem_get(void* ctx, size_t row, size_t col)
{
    auto* r = static_cast<MemRowsetCtx*>(ctx);
    if (!r || !r->table) return nullptr;
    if (row >= r->table->rows.size()) return nullptr;
    if (col >= r->table->rows[row].size()) return nullptr;
    return r->table->rows[row][col].c_str();
}

static void mem_rowset_destroy(void* ctx)
{
    auto* r = static_cast<MemRowsetCtx*>(ctx);
    delete r;
}

static const lwdb_rowset_vtbl_t MEM_ROWSET_VTBL = {
    &mem_row_count,
    &mem_col_count,
    &mem_get,
    &mem_rowset_destroy
};

// ------------------------------
// Storage API functions
// ------------------------------
static void* mem_create()
{
    return new (std::nothrow) MemStorage{};
}

static void mem_destroy(void* storage_ctx)
{
    delete static_cast<MemStorage*>(storage_ctx);
}

static lwdb_status_t mem_create_table(
    void* storage_ctx,
    const char* table_name,
    size_t ncols,
    const char* const* col_names,
    const int* col_types)
{
    if (!storage_ctx || !table_name || !col_names || !col_types || ncols == 0)
        return LWDB_ERR_INVALID_QUERY;

    auto* st = static_cast<MemStorage*>(storage_ctx);
    std::string tname(table_name);

    if (st->tables.find(tname) != st->tables.end())
        return LWDB_ERR_SCHEMA_MISMATCH; // table already exists (simple choice)

    MemTable t;
    t.col_names.reserve(ncols);
    t.col_types.reserve(ncols);

    for (size_t i = 0; i < ncols; ++i)
    {
        if (!col_names[i]) return LWDB_ERR_INVALID_QUERY;
        t.col_names.emplace_back(col_names[i]);
        t.col_types.emplace_back(col_types[i]);
    }

    st->tables.emplace(std::move(tname), std::move(t));
    return LWDB_OK;
}

static lwdb_status_t mem_drop_table(void* storage_ctx, const char* table_name)
{
    if (!storage_ctx || !table_name) return LWDB_ERR_INVALID_QUERY;

    auto* st = static_cast<MemStorage*>(storage_ctx);
    auto it = st->tables.find(table_name);
    if (it == st->tables.end())
        return LWDB_ERR_UNKNOWN_TABLE;

    st->tables.erase(it);
    return LWDB_OK;
}

static lwdb_status_t mem_insert_row(
    void* storage_ctx,
    const char* table_name,
    size_t nvals,
    const char* const* values)
{
    if (!storage_ctx || !table_name || !values) return LWDB_ERR_INVALID_QUERY;

    auto* st = static_cast<MemStorage*>(storage_ctx);
    auto it = st->tables.find(table_name);
    if (it == st->tables.end())
        return LWDB_ERR_UNKNOWN_TABLE;

    MemTable& t = it->second;

    if (nvals != t.col_names.size())
        return LWDB_ERR_SCHEMA_MISMATCH;

    std::vector<std::string> row;
    row.reserve(nvals);

    for (size_t i = 0; i < nvals; ++i)
    {
        row.emplace_back(values[i] ? values[i] : "");
    }

    t.rows.emplace_back(std::move(row));
    return LWDB_OK;
}

static lwdb_status_t mem_select_all(
    void* storage_ctx,
    const char* table_name,
    lwdb_rowset_t* out_rowset)
{
    if (!storage_ctx || !table_name || !out_rowset) return LWDB_ERR_INVALID_QUERY;

    auto* st = static_cast<MemStorage*>(storage_ctx);
    auto it = st->tables.find(table_name);
    if (it == st->tables.end())
        return LWDB_ERR_UNKNOWN_TABLE;

    auto* ctx = new (std::nothrow) MemRowsetCtx{};
    if (!ctx) return LWDB_ERR_NO_MEMORY;

    ctx->table = &it->second;

    out_rowset->ctx  = ctx;
    out_rowset->vtbl = &MEM_ROWSET_VTBL;
    return LWDB_OK;
}

// ------------------------------
// Public plugin getter
// ------------------------------
static const lwdb_storage_api_t MEM_STORAGE_API = {
    LWDB_STORAGE_ABI_VERSION,
    "storage_memory",
    &mem_create,
    &mem_destroy,
    &mem_create_table,
    &mem_drop_table,
    &mem_insert_row,
    &mem_select_all
};

extern "C" const lwdb_storage_api_t* lwdb_storage_memory(void)
{
    return &MEM_STORAGE_API;
}
