#pragma once

#include <string>
#include <vector>

// Adapter result structure expected by ModuleE
struct lwdb_result_t
{
    std::vector<std::vector<std::string>> rows;
};

// Adapter API (ModuleE expects these)
void* lwdb_open(const char* dbFile);
void  lwdb_close(void* handle);

bool  lwdb_exec(void* handle, const char* sql);
bool  lwdb_query(void* handle, const char* sql, lwdb_result_t* out);

void  lwdb_result_free(lwdb_result_t* r);
