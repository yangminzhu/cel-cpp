#include "eval/public/builtin_func_registrar.h"
#include "eval/public/extension_func_registrar.h"

#include "google/protobuf/util/time_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

namespace {
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using google::protobuf::Arena;

class ExtensionTest : public ::testing::Test {
 protected:
  ExtensionTest() {}

  void SetUp() override {
    ASSERT_TRUE(util::IsOk(RegisterBuiltinFunctions(&registry_)));
    ASSERT_TRUE(util::IsOk(RegisterExtensionFunctions(&registry_)));
  }

  // Helper method to test string startsWith() function
  void TestStringInclusion(absl::string_view func_name,
                           const std::vector<bool>& call_style,
                           const std::string& test_string, const std::string& included,
                           bool result) {
    std::vector<bool> call_styles = {true, false};

    for (auto call_style : call_styles) {
      auto functions = registry_.FindOverloads(
          func_name, call_style,
          {CelValue::Type::kString, CelValue::Type::kString});
      ASSERT_EQ(functions.size(), 1);

      auto func = functions[0];

      std::vector<CelValue> args = {CelValue::CreateString(&test_string),
                                    CelValue::CreateString(&included)};
      CelValue result_value = CelValue::CreateNull();
      google::protobuf::Arena arena;
      absl::Span<CelValue> arg_span(&args[0], args.size());
      auto status = func->Evaluate(arg_span, &result_value, &arena);

      ASSERT_TRUE(util::IsOk(status));
      ASSERT_TRUE(result_value.IsBool());
      ASSERT_EQ(result_value.BoolOrDie(), result);
    }
  }

  void TestStringStartsWith(const std::string& test_string, const std::string& prefix,
                            bool result) {
    TestStringInclusion("startsWith", {true, false}, test_string, prefix,
                        result);
  }

  void TestStringEndsWith(const std::string& test_string, const std::string& prefix,
                          bool result) {
    TestStringInclusion("endsWith", {true, false}, test_string, prefix, result);
  }

  // Helper method to test timestamp() function
  void PerformTimestampConversion(Arena* arena, std::string ts_str,
                                  CelValue* result) {
    auto functions =
        registry_.FindOverloads("timestamp", false, {CelValue::Type::kString});
    ASSERT_EQ(functions.size(), 1);

    auto func = functions[0];

    std::vector<CelValue> args = {CelValue::CreateString(&ts_str)};
    absl::Span<CelValue> arg_span(&args[0], args.size());
    auto status = func->Evaluate(arg_span, result, arena);

    ASSERT_TRUE(util::IsOk(status));
  }

  // Helper method to test duration() function
  void PerformDurationConversion(Arena* arena, std::string ts_str,
                                 CelValue* result) {
    auto functions =
        registry_.FindOverloads("duration", false, {CelValue::Type::kString});
    ASSERT_EQ(functions.size(), 1);

    auto func = functions[0];

    std::vector<CelValue> args = {CelValue::CreateString(&ts_str)};
    absl::Span<CelValue> arg_span(&args[0], args.size());
    auto status = func->Evaluate(arg_span, result, arena);

    ASSERT_TRUE(util::IsOk(status));
  }

  // Function registry object
  CelFunctionRegistry registry_;
};

// Test string startsWith() function.
TEST_F(ExtensionTest, TestStartsWithFunction) {
  // Empty string, non-empty prefix - never matches.
  EXPECT_NO_FATAL_FAILURE(TestStringStartsWith("", "p", false));
  // Prefix of 0 length - always matches.
  EXPECT_NO_FATAL_FAILURE(TestStringStartsWith("", "", true));
  EXPECT_NO_FATAL_FAILURE(TestStringStartsWith("prefixedString", "", true));
  // Non-empty matching prefix.
  EXPECT_NO_FATAL_FAILURE(
      TestStringStartsWith("prefixedString", "prefix", true));
  // Non-empty mismatching prefix.
  EXPECT_NO_FATAL_FAILURE(TestStringStartsWith("prefixedString", "x", false));
  EXPECT_NO_FATAL_FAILURE(
      TestStringStartsWith("prefixedString", "prefixedString1", false));
}

// Test string startsWith() function.
TEST_F(ExtensionTest, TestEndsWithFunction) {
  // Empty string, non-empty postfix - never matches.
  EXPECT_NO_FATAL_FAILURE(TestStringEndsWith("", "p", false));
  // Postfix of 0 length - always matches.
  EXPECT_NO_FATAL_FAILURE(TestStringEndsWith("", "", true));
  EXPECT_NO_FATAL_FAILURE(TestStringEndsWith("postfixedString", "", true));
  // Non-empty matching postfix.
  EXPECT_NO_FATAL_FAILURE(
      TestStringEndsWith("postfixedString", "String", true));
  // Non-empty mismatching post.
  EXPECT_NO_FATAL_FAILURE(TestStringEndsWith("postfixedString", "x", false));
  EXPECT_NO_FATAL_FAILURE(
      TestStringEndsWith("postfixedString", "1postfixedString", false));
}

// Test timestamp conversion function.
TEST_F(ExtensionTest, TestTimestampFromString) {
  CelValue result = CelValue::CreateNull();

  Arena arena;

  // Valid timestamp - no fractions of seconds.
  EXPECT_NO_FATAL_FAILURE(
      PerformTimestampConversion(&arena, "2000-01-01T00:00:00Z", &result));
  ASSERT_TRUE(result.IsTimestamp());

  const Timestamp* ts = result.TimestampOrDie();
  ASSERT_EQ(ts->seconds(), 946684800L);
  ASSERT_EQ(ts->nanos(), 0);

  // Valid timestamp - with nanoseconds.
  EXPECT_NO_FATAL_FAILURE(
      PerformTimestampConversion(&arena, "2000-01-01T00:00:00.212Z", &result));
  ASSERT_TRUE(result.IsTimestamp());

  ts = result.TimestampOrDie();
  ASSERT_EQ(ts->seconds(), 946684800L);
  ASSERT_EQ(ts->nanos(), 212000000);

  // Valid timestamp - with timezone.
  EXPECT_NO_FATAL_FAILURE(PerformTimestampConversion(
      &arena, "2000-01-01T00:00:00.212-01:00", &result));
  ASSERT_TRUE(result.IsTimestamp());

  ts = result.TimestampOrDie();
  ASSERT_EQ(ts->seconds(), 946688400L);
  ASSERT_EQ(ts->nanos(), 212000000);

  // Invalid timestamp - empty string.
  EXPECT_NO_FATAL_FAILURE(PerformTimestampConversion(&arena, "", &result));
  ASSERT_TRUE(result.IsError());
  ASSERT_EQ(result.ErrorOrDie()->code(),
            CelError::Code::CelError_Code_INVALID_ARGUMENT);

  // Invalid timestamp.
  EXPECT_NO_FATAL_FAILURE(
      PerformTimestampConversion(&arena, "2000-01-01TT00:00:00Z", &result));
  ASSERT_TRUE(result.IsError());
}

// Test duration conversion function.
TEST_F(ExtensionTest, TestDurationFromString) {
  CelValue result = CelValue::CreateNull();

  Arena arena;

  // Valid duration - no fractions of seconds.
  EXPECT_NO_FATAL_FAILURE(PerformDurationConversion(&arena, "1354s", &result));
  ASSERT_TRUE(result.IsDuration());

  const Duration* d = result.DurationOrDie();
  ASSERT_EQ(d->seconds(), 1354L);
  ASSERT_EQ(d->nanos(), 0L);

  // Valid duration - with nanoseconds.
  EXPECT_NO_FATAL_FAILURE(PerformDurationConversion(&arena, "15.11s", &result));
  ASSERT_TRUE(result.IsDuration());

  d = result.DurationOrDie();
  ASSERT_EQ(d->seconds(), 15L);
  ASSERT_EQ(d->nanos(), 110000000L);

  // Invalid duration - empty string.
  EXPECT_NO_FATAL_FAILURE(PerformDurationConversion(&arena, "", &result));
  ASSERT_TRUE(result.IsError());
  ASSERT_EQ(result.ErrorOrDie()->code(),
            CelError::Code::CelError_Code_INVALID_ARGUMENT);

  // Invalid duration.
  EXPECT_NO_FATAL_FAILURE(PerformDurationConversion(&arena, "100", &result));
  ASSERT_TRUE(result.IsError());
}

}  // namespace

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
