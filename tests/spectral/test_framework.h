#ifndef CORRUPTER_TESTS_SPECTRAL_TEST_FRAMEWORK_H_
#define CORRUPTER_TESTS_SPECTRAL_TEST_FRAMEWORK_H_

#include <iostream>
#include <string>
#include <vector>

namespace corrupter_spec {

struct TestCase {
  std::string name;
  bool (*fn)();
};

void RegisterSmokeTests(std::vector<TestCase>& out);
void RegisterAliasingTests(std::vector<TestCase>& out);
void RegisterFraTests(std::vector<TestCase>& out);
void RegisterThdTests(std::vector<TestCase>& out);
void RegisterClickTests(std::vector<TestCase>& out);
void RegisterFreezeTests(std::vector<TestCase>& out);
void RegisterSmoothingTests(std::vector<TestCase>& out);
void RegisterGoldenHashTests(std::vector<TestCase>& out);

}  // namespace corrupter_spec

#define CORRUPTER_SPEC_INFO(msg)                                  \
  do {                                                            \
    std::cerr << "  INFO: " << msg << " (" << __FILE__ << ":"    \
              << __LINE__ << ")\n";                               \
  } while (0)

#define CORRUPTER_SPEC_FAIL(msg)                                  \
  do {                                                            \
    std::cerr << "  FAIL: " << msg << " (" << __FILE__ << ":"    \
              << __LINE__ << ")\n";                               \
    return false;                                                 \
  } while (0)

#define CORRUPTER_SPEC_REQUIRE(cond, msg)                         \
  do {                                                            \
    if (!(cond)) {                                                \
      std::cerr << "  FAIL: " << msg << " (" << __FILE__ << ":"  \
                << __LINE__ << ")\n";                             \
      return false;                                               \
    }                                                             \
  } while (0)

#endif  // CORRUPTER_TESTS_SPECTRAL_TEST_FRAMEWORK_H_
