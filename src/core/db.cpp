// src/core/db.cpp

#include "lwdb/lwdb.h"
#include "lwdb/plugin.h"

#include <new>
#include <cstring>
#include <string>
#include <vector>
#include <cctype>

// ================================
// Internal structs (opaque)
// ================================

struct lwdb_handle
{
    const char* last_error;

    // Storage plugin + context
    const lwdb_storage_api_t* storage;
    void* storage_ctx;
};

struct lwdb_result
{
    size_t rows = 0;
    size_t cols = 0;
    std::vector<std::string> cells; // flattened row-major
};

// ================================
// Helpers
// ================================

static void lwdb_set_error(lwdb_handle_t* db, const char* msg)
{
    if (db) db->last_error = msg;
}

static std::string trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, e - b + 1);
}

static std::string tolower_str(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Very small parser helpers (v1) — keep it minimal and safe
static bool starts_with(const std::string& s, const char* pfx)
{
    return s.rfind(pfx, 0) == 0;
}

// Parse "create table T(a int, b string)"
static lwdb_status_t parse_create_table(
    const std::string& sql,
    std::string& out_table,
    std::vector<std::string>& out_cols,
    std::vector<int>& out_types)
{
    auto lower = tolower_str(sql);
    if (!starts_with(lower, "create table "))
        return LWDB_ERR_INVALID_QUERY;

    auto posParen = sql.find('(');
    if (posParen == std::string::npos) return LWDB_ERR_INVALID_QUERY;

    std::string head = trim(sql.substr(0, posParen));
    // head: "create table T"
    size_t lastSpace = head.find_last_of(' ');
    if (lastSpace == std::string::npos) return LWDB_ERR_INVALID_QUERY;

    out_table = trim(head.substr(lastSpace + 1));
    if (out_table.empty()) return LWDB_ERR_INVALID_QUERY;

    auto posClose = sql.find(')', posParen);
    if (posClose == std::string::npos) return LWDB_ERR_INVALID_QUERY;

    std::string fieldsStr = sql.substr(posParen + 1, posClose - posParen - 1);

    out_cols.clear();
    out_types.clear();

    size_t start = 0;
    while (start < fieldsStr.size())
    {
        size_t comma = fieldsStr.find(',', start);
        std::string part = trim(fieldsStr.substr(start, (comma == std::string::npos) ? std::string::npos : (comma - start)));

        if (!part.empty())
        {
            // part: "a int"
            size_t sp = part.find(' ');
            if (sp == std::string::npos) return LWDB_ERR_INVALID_QUERY;

            std::string col = trim(part.substr(0, sp));
            std::string typ = tolower_str(trim(part.substr(sp + 1)));

            int dt = 3; // string default
            if (typ == "int") dt = 1;
            else if (typ == "float") dt = 2;
            else if (typ == "string") dt = 3;

            out_cols.push_back(col);
            out_types.push_back(dt);
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return out_cols.empty() ? LWDB_ERR_INVALID_QUERY : LWDB_OK;
}

// Parse "insert into T value(1;abc;3)"
static lwdb_status_t parse_insert(
    const std::string& sql,
    std::string& out_table,
    std::vector<std::string>& out_vals)
{
    auto lower = tolower_str(sql);
    if (!starts_with(lower, "insert into "))
        return LWDB_ERR_INVALID_QUERY;

    // expect: insert into <table> value(...)
    size_t valPos = lower.find(" value");
    if (valPos == std::string::npos) return LWDB_ERR_INVALID_QUERY;

    std::string left = trim(sql.substr(0, valPos)); // "insert into T"
    size_t lastSpace = left.find_last_of(' ');
    if (lastSpace == std::string::npos) return LWDB_ERR_INVALID_QUERY;
    out_table = trim(left.substr(lastSpace + 1));

    size_t open = sql.find('(', valPos);
    size_t close = sql.find(')', open);
    if (open == std::string::npos || close == std::string::npos) return LWDB_ERR_INVALID_QUERY;

    std::string inside = sql.substr(open + 1, close - open - 1);

    out_vals.clear();
    size_t start = 0;
    while (start <= inside.size())
    {
        size_t sep = inside.find(';', start);
        std::string v = trim(inside.substr(start, (sep == std::string::npos) ? std::string::npos : (sep - start)));
        out_vals.push_back(v);
        if (sep == std::string::npos) break;
        start = sep + 1;
    }

    return out_table.empty() ? LWDB_ERR_INVALID_QUERY : LWDB_OK;
}

// Parse "drop table T"
static lwdb_status_t parse_drop_table(const std::string& sql, std::string& out_table)
{
    auto lower = tolower_str(sql);
    if (!starts_with(lower, "drop table "))
        return LWDB_ERR_INVALID_QUERY;

    out_table = trim(sql.substr(std::strlen("drop table ")));
    return out_table.empty() ? LWDB_ERR_INVALID_QUERY : LWDB_OK;
}

// Parse "select * from T"
static lwdb_status_t parse_select_all(const std::string& sql, std::string& out_table)
{
    auto lower = tolower_str(sql);
    if (!starts_with(lower, "select * from "))
        return LWDB_ERR_INVALID_QUERY;

    out_table = trim(sql.substr(std::strlen("select * from ")));
    return out_table.empty() ? LWDB_ERR_INVALID_QUERY : LWDB_OK;
}

// ================================
// Public API
// ================================

lwdb_handle_t* lwdb_create(void)
{
    lwdb_handle_t* db = new (std::nothrow) lwdb_handle_t{};
    if (!db) return nullptr;

    db->last_error = nullptr;

    // Default to built-in in-memory storage plugin
    db->storage = lwdb_storage_memory();
    db->storage_ctx = db->storage ? db->storage->create() : nullptr;

    if (!db->storage || !db->storage_ctx)
    {
        lwdb_set_error(db, "Failed to initialize memory storage");
        delete db;
        return nullptr;
    }

    return db;
}

void lwdb_destroy(lwdb_handle_t* db)
{
    if (!db) return;

    if (db->storage && db->storage_ctx)
        db->storage->destroy(db->storage_ctx);

    delete db;
}

lwdb_status_t lwdb_execute(lwdb_handle_t* db, const char* sql_c)
{
    if (!db) return LWDB_ERR_NOT_INITIALIZED;
    if (!sql_c || std::strlen(sql_c) == 0)
    {
        lwdb_set_error(db, "Empty query");
        return LWDB_ERR_INVALID_QUERY;
    }

    std::string sql = trim(sql_c);
    std::string lower = tolower_str(sql);

    // CREATE TABLE
    if (starts_with(lower, "create table "))
    {
        std::string tname;
        std::vector<std::string> cols;
        std::vector<int> types;

        lwdb_status_t st = parse_create_table(sql, tname, cols, types);
        if (st != LWDB_OK) return st;

        std::vector<const char*> col_ptrs;
        col_ptrs.reserve(cols.size());
        for (auto& c : cols) col_ptrs.push_back(c.c_str());

        return db->storage->create_table(db->storage_ctx, tname.c_str(),
                                         cols.size(), col_ptrs.data(), types.data());
    }

    // INSERT INTO
    if (starts_with(lower, "insert into "))
    {
        std::string tname;
        std::vector<std::string> vals;

        lwdb_status_t st = parse_insert(sql, tname, vals);
        if (st != LWDB_OK) return st;

        std::vector<const char*> val_ptrs;
        val_ptrs.reserve(vals.size());
        for (auto& v : vals) val_ptrs.push_back(v.c_str());

        return db->storage->insert_row(db->storage_ctx, tname.c_str(),
                                       vals.size(), val_ptrs.data());
    }

    // DROP TABLE
    if (starts_with(lower, "drop table "))
    {
        std::string tname;
        lwdb_status_t st = parse_drop_table(sql, tname);
        if (st != LWDB_OK) return st;
        return db->storage->drop_table(db->storage_ctx, tname.c_str());
    }

    lwdb_set_error(db, "Unsupported command (use CREATE/INSERT/DROP or SELECT via lwdb_query)");
    return LWDB_ERR_INVALID_QUERY;
}

lwdb_status_t lwdb_query(lwdb_handle_t* db, const char* sql_c, lwdb_result_t** out_result)
{
    if (!db) return LWDB_ERR_NOT_INITIALIZED;
    if (!sql_c || !out_result) return LWDB_ERR_INVALID_QUERY;

    std::string sql = trim(sql_c);
    std::string tname;

    lwdb_status_t st = parse_select_all(sql, tname);
    if (st != LWDB_OK) return st;

    lwdb_rowset_t rowset{};
    st = db->storage->select_all(db->storage_ctx, tname.c_str(), &rowset);
    if (st != LWDB_OK) return st;

    lwdb_result_t* r = new (std::nothrow) lwdb_result_t{};
    if (!r)
    {
        rowset.vtbl->destroy(rowset.ctx);
        return LWDB_ERR_NO_MEMORY;
    }

    r->rows = rowset.vtbl->row_count(rowset.ctx);
    r->cols = rowset.vtbl->col_count(rowset.ctx);
    r->cells.reserve(r->rows * r->cols);

    for (size_t i = 0; i < r->rows; ++i)
    {
        for (size_t j = 0; j < r->cols; ++j)
        {
            const char* v = rowset.vtbl->get(rowset.ctx, i, j);
            r->cells.emplace_back(v ? v : "");
        }
    }

    rowset.vtbl->destroy(rowset.ctx);

    *out_result = r;
    return LWDB_OK;
}

size_t lwdb_result_row_count(const lwdb_result_t* result)
{
    return result ? result->rows : 0;
}

size_t lwdb_result_column_count(const lwdb_result_t* result)
{
    return result ? result->cols : 0;
}

const char* lwdb_result_get(const lwdb_result_t* result, size_t row, size_t col)
{
    if (!result) return nullptr;
    if (row >= result->rows || col >= result->cols) return nullptr;

    size_t idx = row * result->cols + col;
    return result->cells[idx].c_str();
}

void lwdb_result_free(lwdb_result_t* result)
{
    delete result;
}

const char* lwdb_last_error(lwdb_handle_t* db)
{
    if (!db || !db->last_error) return "";
    return db->last_error;
}
