#pragma once

/**
 * This header converts every GTEST in compilation unit into co-routine.
 */

#include <gtest/gtest.h>
#include "common/coro.h"


#undef GTEST_TEST_
#define GTEST_TEST_(test_suite_name, test_name, parent_class, parent_id)      \
  static_assert(sizeof(GTEST_STRINGIFY_(test_suite_name)) > 1,                \
                "test_suite_name must not be empty");                         \
  static_assert(sizeof(GTEST_STRINGIFY_(test_name)) > 1,                      \
                "test_name must not be empty");                               \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                    \
      : public parent_class {                                                 \
   public:                                                                    \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() = default;           \
    ~GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() override = default; \
    GTEST_DISALLOW_COPY_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,   \
                                                           test_name));       \
    GTEST_DISALLOW_MOVE_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,   \
                                                           test_name));       \
                                                                              \
   private:                                                                   \
    void TestBody() override { ag::coro::to_future(CoTestBody()).get(); }     \
    virtual ag::coro::Task<void> CoTestBody() noexcept;                       \
    static ::testing::TestInfo* const test_info_ GTEST_ATTRIBUTE_UNUSED_;     \
  };                                                                          \
                                                                              \
  ::testing::TestInfo* const GTEST_TEST_CLASS_NAME_(test_suite_name,          \
                                                    test_name)::test_info_ =  \
      ::testing::internal::MakeAndRegisterTestInfo(                           \
          #test_suite_name, #test_name, nullptr, nullptr,                     \
          ::testing::internal::CodeLocation(__FILE__, __LINE__), (parent_id), \
          ::testing::internal::SuiteApiResolver<                              \
              parent_class>::GetSetUpCaseOrSuite(__FILE__, __LINE__),         \
          ::testing::internal::SuiteApiResolver<                              \
              parent_class>::GetTearDownCaseOrSuite(__FILE__, __LINE__),      \
          new ::testing::internal::TestFactoryImpl<GTEST_TEST_CLASS_NAME_(    \
              test_suite_name, test_name)>);                                  \
  ag::coro::Task<void> GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::CoTestBody() noexcept

#undef TEST_P
#define TEST_P(test_suite_name, test_name)                                     \
  class GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)                     \
      : public test_suite_name {                                               \
   public:                                                                     \
    GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)() {}                    \
    void TestBody() override { ag::coro::to_future(CoTestBody()).get(); }      \
    virtual ag::coro::Task<void> CoTestBody() noexcept;                        \
                                                                               \
   private:                                                                    \
    static int AddToRegistry() {                                               \
      ::testing::UnitTest::GetInstance()                                       \
          ->parameterized_test_registry()                                      \
          .GetTestSuitePatternHolder<test_suite_name>(                         \
              GTEST_STRINGIFY_(test_suite_name),                               \
              ::testing::internal::CodeLocation(__FILE__, __LINE__))           \
          ->AddTestPattern(                                                    \
              GTEST_STRINGIFY_(test_suite_name), GTEST_STRINGIFY_(test_name),  \
              new ::testing::internal::TestMetaFactory<GTEST_TEST_CLASS_NAME_( \
                  test_suite_name, test_name)>(),                              \
              ::testing::internal::CodeLocation(__FILE__, __LINE__));          \
      return 0;                                                                \
    }                                                                          \
    static int gtest_registering_dummy_ GTEST_ATTRIBUTE_UNUSED_;               \
    GTEST_DISALLOW_COPY_AND_ASSIGN_(GTEST_TEST_CLASS_NAME_(test_suite_name,    \
                                                           test_name));        \
  };                                                                           \
  int GTEST_TEST_CLASS_NAME_(test_suite_name,                                  \
                             test_name)::gtest_registering_dummy_ =            \
      GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::AddToRegistry();     \
  ag::coro::Task<void> GTEST_TEST_CLASS_NAME_(test_suite_name, test_name)::CoTestBody() noexcept

#undef GTEST_FATAL_FAILURE_
#define GTEST_FATAL_FAILURE_(message) \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kFatalFailure)

#undef GTEST_SKIP_
#define GTEST_SKIP_(message) \
  co_return GTEST_MESSAGE_(message, ::testing::TestPartResult::kSkip)
