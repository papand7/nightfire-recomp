#pragma once
// The Netplay page's rows (src/common/netplay_page.cpp): host, join, the games found on this network and online. Any
// menu can show them: each row a label, its value, help, a kind (auf::ModernKind) and what left (-1), right (1) or A (0)
// does to it.
#include <functional>
#include <string>
#include <vector>

#include "common/auf.h"

namespace auf::netplay_page {
struct Row {
    std::string label, value, help;
    int kind = kModernButton;
    std::function<void(int)> act;
};
std::vector<Row> Rows();  // made afresh each call (the lists as found so far)
}  // namespace auf::netplay_page
