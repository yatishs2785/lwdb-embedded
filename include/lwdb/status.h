
#pragma once

namespace lwdb {
enum class Status {
    Ok = 0,
    InvalidQuery,
    UnknownTable,
    StorageError
};
}
