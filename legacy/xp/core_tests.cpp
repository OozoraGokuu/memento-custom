#include "core.h"
#include <iostream>
int main() {
    const auto failed = xp::coreTests();
    for (const auto &name : failed) std::cerr << "FAIL: " << name << '\n';
    std::cout << xp::coreTestCount - failed.size() << '/' << xp::coreTestCount << " portable core checks passed\n";
    return failed.empty() ? 0 : 1;
}
