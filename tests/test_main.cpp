#include "test_framework.h"

TEST(framework_smoke) {
    CHECK_EQ(1 + 1, 2);
}

int main() { return testfw::run_all(); }
