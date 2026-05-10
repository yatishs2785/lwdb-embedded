#pragma once
#include <vector>
#include <string>

namespace lwdb {
struct Result {
    std::vector<std::vector<std::string>> rows;
};
}
