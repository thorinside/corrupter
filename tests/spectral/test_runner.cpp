#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "test_framework.h"

int main(int argc, char** argv) {
  using corrupter_spec::TestCase;

  std::vector<TestCase> tests;
  corrupter_spec::RegisterSmokeTests(tests);
  corrupter_spec::RegisterAliasingTests(tests);
  corrupter_spec::RegisterFraTests(tests);
  corrupter_spec::RegisterThdTests(tests);
  corrupter_spec::RegisterClickTests(tests);
  corrupter_spec::RegisterFreezeTests(tests);
  corrupter_spec::RegisterSmoothingTests(tests);

  std::string filter;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--filter=", 0) == 0) {
      filter = a.substr(std::string("--filter=").size());
    }
  }

  int passed = 0;
  int failed = 0;
  int skipped = 0;

  std::cout << "========================================\n";
  std::cout << "  corrupter_dsp_spectral_tests\n";
  std::cout << "========================================\n";

  for (const auto& t : tests) {
    if (!filter.empty() && t.name.find(filter) == std::string::npos) {
      ++skipped;
      continue;
    }
    bool ok = false;
    std::cout << "[ RUN  ] " << t.name << "\n";
    try {
      ok = t.fn();
    } catch (const std::exception& e) {
      std::cerr << "  EXCEPTION: " << e.what() << "\n";
      ok = false;
    } catch (...) {
      std::cerr << "  EXCEPTION: unknown\n";
      ok = false;
    }
    if (ok) {
      ++passed;
      std::cout << "[ PASS ] " << t.name << "\n";
    } else {
      ++failed;
      std::cout << "[ FAIL ] " << t.name << "\n";
    }
  }

  std::cout << "----------------------------------------\n";
  std::cout << "  passed: " << passed
            << "  failed: " << failed
            << "  skipped: " << skipped
            << "  (total: " << tests.size() << ")\n";
  std::cout << "----------------------------------------\n";
  return (failed == 0) ? 0 : 1;
}
