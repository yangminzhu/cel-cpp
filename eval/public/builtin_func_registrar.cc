#include "eval/public/builtin_func_registrar.h"

#include <functional>

#include "google/protobuf/util/time_util.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "eval/eval/container_backed_list_impl.h"
#include "eval/public/cel_builtins.h"
#include "eval/public/cel_function_adapter.h"
#include "re2/re2.h"
#include "google/rpc/code.pb.h"

namespace google {
namespace api {
namespace expr {
namespace runtime {

using google::protobuf::Duration;
using google::protobuf::Timestamp;
using google::protobuf::Arena;
using google::protobuf::util::TimeUtil;

namespace {

// Comparison template functions
template <class Type>
bool Inequal(Arena* arena, Type t1, Type t2) {
  return (t1 != t2);
}

template <class Type>
bool Equal(Arena* arena, Type t1, Type t2) {
  return (t1 == t2);
}

template <class Type>
bool LessThan(Arena* arena, Type t1, Type t2) {
  return (t1 < t2);
}

template <class Type>
bool LessThanOrEqual(Arena* arena, Type t1, Type t2) {
  return (t1 <= t2);
}

template <class Type>
bool GreaterThan(Arena* arena, Type t1, Type t2) {
  return LessThan(arena, t2, t1);
}

template <class Type>
bool GreaterThanOrEqual(Arena* arena, Type t1, Type t2) {
  return LessThanOrEqual(arena, t2, t1);
}

// Duration comparison specializations
template <>
bool Inequal(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator!=(*t1, *t2);
}

template <>
bool Equal(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator==(*t1, *t2);
}

template <>
bool LessThan(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator<(*t1, *t2);
}

template <>
bool LessThanOrEqual(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator<=(*t1, *t2);
}

template <>
bool GreaterThan(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator>(*t1, *t2);
}

template <>
bool GreaterThanOrEqual(Arena* arena, const Duration* t1, const Duration* t2) {
  return operator>=(*t1, *t2);
}

// Timestamp comparison specializations
template <>
bool Inequal(Arena* arena, const Timestamp* t1, const Timestamp* t2) {
  return operator!=(*t1, *t2);
}

template <>
bool Equal(Arena* arena, const Timestamp* t1, const Timestamp* t2) {
  return operator==(*t1, *t2);
}

template <>
bool LessThan(Arena* arena, const Timestamp* t1, const Timestamp* t2) {
  return operator<(*t1, *t2);
}

template <>
bool LessThanOrEqual(Arena* arena, const Timestamp* t1, const Timestamp* t2) {
  return operator<=(*t1, *t2);
}

template <>
bool GreaterThan(Arena* arena, const Timestamp* t1, const Timestamp* t2) {
  return operator>(*t1, *t2);
}

template <>
bool GreaterThanOrEqual(Arena* arena, const Timestamp* t1,
                        const Timestamp* t2) {
  return operator>=(*t1, *t2);
}

// Helper method
// Registers all comparison functions for template parameter type.
template <class Type>
util::Status RegisterComparisonFunctionsForType(
    CelFunctionRegistry* registry) {
  // Inequality
  util::Status status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kInequal, false, Inequal<Type>, registry);
  if (!util::IsOk(status)) return status;

  // Equality
  status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kEqual, false, Equal<Type>, registry);
  if (!util::IsOk(status)) return status;

  // Less than
  status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kLess, false, LessThan<Type>, registry);
  if (!util::IsOk(status)) return status;

  // Less than or Equal
  status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kLessOrEqual, false, LessThanOrEqual<Type>, registry);
  if (!util::IsOk(status)) return status;

  // Greater than
  status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kGreater, false, GreaterThan<Type>, registry);
  if (!util::IsOk(status)) return status;

  // Greater than or Equal
  status = FunctionAdapter<bool, Type, Type>::CreateAndRegister(
      builtin::kGreaterOrEqual, false, GreaterThanOrEqual<Type>, registry);
  if (!util::IsOk(status)) return status;

  return util::OkStatus();
}

// Template functions providing arithmetic operations
template <class Type>
Type Add(Arena* arena, Type v0, Type v1) {
  return v0 + v1;
}

template <class Type>
Type Sub(Arena* arena, Type v0, Type v1) {
  return v0 - v1;
}

template <class Type>
Type Mul(Arena* arena, Type v0, Type v1) {
  return v0 * v1;
}

template <class Type>
CelValue Div(Arena* arena, Type v0, Type v1);

// Division operations for integer types should check for
// division by 0
template <>
CelValue Div<int64_t>(Arena* arena, int64_t v0, int64_t v1) {
  // For integral types, zero check is essential, to avoid
  // floating pointer exception.
  if (v1 == 0) {
    // TODO(issues/25) Which code?
    return CreateErrorValue(arena, "Division by 0",
                            CelError::Code::CelError_Code_UNKNOWN);
  }
  return CelValue::CreateInt64(v0 / v1);
}

// Division operations for integer types should check for
// division by 0
template <>
CelValue Div<uint64_t>(Arena* arena, uint64_t v0, uint64_t v1) {
  // For integral types, zero check is essential, to avoid
  // floating pointer exception.
  if (v1 == 0) {
    // TODO(issues/25) Which code?
    return CreateErrorValue(arena, "Division by 0",
                            CelError::Code::CelError_Code_UNKNOWN);
  }
  return CelValue::CreateUint64(v0 / v1);
}

template <>
CelValue Div<double>(Arena* arena, double v0, double v1) {
  // For double, division will result in +/- inf
  return CelValue::CreateDouble(v0 / v1);
}

// Modulo operation
template <class Type>
CelValue Modulo(Arena* arena, Type value, Type value2);

// Modulo operations for integer types should check for
// division by 0
template <>
CelValue Modulo<int64_t>(Arena* arena, int64_t value, int64_t value2) {
  if (value2 == 0) {
    return CreateErrorValue(arena, "Modulo by 0",
                            CelError::Code::CelError_Code_UNKNOWN);
  }

  return CelValue::CreateInt64(value % value2);
}

template <>
CelValue Modulo<uint64_t>(Arena* arena, uint64_t value, uint64_t value2) {
  if (value2 == 0) {
    return CreateErrorValue(arena, "Modulo by 0",
                            CelError::Code::CelError_Code_UNKNOWN);
  }

  return CelValue::CreateUint64(value % value2);
}

// Helper method
// Registers all arithmetic functions for template parameter type.
template <class Type>
util::Status RegisterArithmeticFunctionsForType(
    CelFunctionRegistry* registry) {
  util::Status status = FunctionAdapter<Type, Type, Type>::CreateAndRegister(
      builtin::kAdd, false, Add<Type>, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<Type, Type, Type>::CreateAndRegister(
      builtin::kSubtract, false, Sub<Type>, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<Type, Type, Type>::CreateAndRegister(
      builtin::kMultiply, false, Mul<Type>, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, Type, Type>::CreateAndRegister(
      builtin::kDivide, false, Div<Type>, registry);
  return status;
}

template <class T>
bool ValueEquals(const CelValue& value, T other);

template <>
bool ValueEquals(const CelValue& value, bool other) {
  return value.IsBool() && (value.BoolOrDie() == other);
}

template <>
bool ValueEquals(const CelValue& value, int64_t other) {
  return value.IsInt64() && (value.Int64OrDie() == other);
}

template <>
bool ValueEquals(const CelValue& value, uint64_t other) {
  return value.IsUint64() && (value.Uint64OrDie() == other);
}

template <>
bool ValueEquals(const CelValue& value, double other) {
  return value.IsDouble() && (value.DoubleOrDie() == other);
}

template <>
bool ValueEquals(const CelValue& value, CelValue::StringHolder other) {
  return value.IsString() && (value.StringOrDie() == other);
}

template <>
bool ValueEquals(const CelValue& value, CelValue::BytesHolder other) {
  return value.IsBytes() && (value.BytesOrDie() == other);
}

// Template function implementing CEL in() function
template <typename T>
bool In(Arena* arena, T value, const CelList* list) {
  int index_size = list->size();

  for (int i = 0; i < index_size; i++) {
    CelValue element = (*list)[i];

    if (ValueEquals<T>(element, value)) {
      return true;
    }
  }

  return false;
}

// Concatenation for StringHolder type.
CelValue::StringHolder ConcatString(Arena* arena, CelValue::StringHolder value1,
                                    CelValue::StringHolder value2) {
  auto concatenated = Arena::Create<std::string>(
      arena, absl::StrCat(value1.value(), value2.value()));
  return CelValue::StringHolder(concatenated);
}

// Concatenation for BytesHolder type.
CelValue::BytesHolder ConcatBytes(Arena* arena, CelValue::BytesHolder value1,
                                  CelValue::BytesHolder value2) {
  auto concatenated = Arena::Create<std::string>(
      arena, absl::StrCat(value1.value(), value2.value()));
  return CelValue::BytesHolder(concatenated);
}

// Concatenation for CelList type.
const CelList* ConcatList(Arena* arena, const CelList* value1,
                          const CelList* value2) {
  std::vector<CelValue> joined_values;

  int size1 = value1->size();
  int size2 = value2->size();
  joined_values.reserve(size1 + size2);

  for (int i = 0; i < size1; i++) {
    joined_values.push_back((*value1)[i]);
  }
  for (int i = 0; i < size2; i++) {
    joined_values.push_back((*value2)[i]);
  }

  auto concatenated =
      Arena::Create<ContainerBackedListImpl>(arena, joined_values);
  return concatenated;
}

// Timestamp
const util::Status FindTimeBreakdown(const Timestamp* timestamp,
                                     absl::string_view tz,
                                     absl::TimeZone::CivilInfo* breakdown) {
  absl::Time instant;
  instant = absl::FromUnixSeconds(timestamp->seconds());
  instant += absl::Nanoseconds(timestamp->nanos());

  absl::TimeZone time_zone;

  if (!tz.empty()) {
    bool found = absl::LoadTimeZone(std::string(tz), &time_zone);
    if (!found) {
      return util::MakeStatus(google::rpc::Code::INVALID_ARGUMENT, "Invalid timezone");
    }
  }

  *breakdown = time_zone.At(instant);
  return util::OkStatus();
}

CelValue GetTimeBreakdownPart(
    Arena* arena, const Timestamp* timestamp, absl::string_view tz,
    const std::function<CelValue(const absl::TimeZone::CivilInfo&)>&
        extractor_func) {
  absl::TimeZone::CivilInfo breakdown;
  auto status = FindTimeBreakdown(timestamp, tz, &breakdown);

  if (!util::IsOk(status)) {
    return CreateErrorValue(arena, status.message(),
                            CelError::Code::CelError_Code_UNKNOWN);
  }

  return extractor_func(breakdown);
}

CelValue CreateTimestampFromString(Arena* arena,
                                   CelValue::StringHolder time_str) {
  Timestamp ts;
  auto result =
      google::protobuf::util::TimeUtil::FromString(std::string(time_str.value()), &ts);
  if (!result) {
    return CreateErrorValue(arena, "String to Timestamp conversion failed",
                            CelError::Code::CelError_Code_INVALID_ARGUMENT);
  }

  return CelValue::CreateTimestamp(
      Arena::Create<Timestamp>(arena, std::move(ts)));
}

CelValue GetFullYear(Arena* arena, const Timestamp* timestamp,
                     absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.year());
      });
}

CelValue GetMonth(Arena* arena, const Timestamp* timestamp,
                  absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.month() - 1);
      });
}

CelValue GetDayOfYear(Arena* arena, const Timestamp* timestamp,
                      absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(
            absl::GetYearDay(absl::CivilDay(breakdown.cs)) - 1);
      });
}

CelValue GetDayOfMonth(Arena* arena, const Timestamp* timestamp,
                       absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.day() - 1);
      });
}

CelValue GetDate(Arena* arena, const Timestamp* timestamp,
                 absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.day());
      });
}

CelValue GetDayOfWeek(Arena* arena, const Timestamp* timestamp,
                      absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        absl::Weekday weekday = absl::GetWeekday(absl::CivilDay(breakdown.cs));

        // get day of week from the date in UTC, zero-based, zero for Sunday,
        // based on GetDayOfWeek CEL function definition.
        int weekday_num = static_cast<int>(weekday);
        weekday_num = (weekday_num == 6) ? 0 : weekday_num + 1;
        return CelValue::CreateInt64(weekday_num);
      });
}

CelValue GetHours(Arena* arena, const Timestamp* timestamp,
                  absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.hour());
      });
}

CelValue GetMinutes(Arena* arena, const Timestamp* timestamp,
                    absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.minute());
      });
}

CelValue GetSeconds(Arena* arena, const Timestamp* timestamp,
                    absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(breakdown.cs.second());
      });
}

CelValue GetMilliseconds(Arena* arena, const Timestamp* timestamp,
                         absl::string_view tz) {
  return GetTimeBreakdownPart(
      arena, timestamp, tz, [](const absl::TimeZone::CivilInfo& breakdown) {
        return CelValue::CreateInt64(
            absl::ToInt64Milliseconds(breakdown.subsecond));
      });
}

CelValue CreateDurationFromString(Arena* arena,
                                  CelValue::StringHolder time_str) {
  Duration d;
  auto result =
      google::protobuf::util::TimeUtil::FromString(std::string(time_str.value()), &d);
  if (!result) {
    return CreateErrorValue(arena, "String to Duration conversion failed",
                            CelError::Code::CelError_Code_INVALID_ARGUMENT);
  }

  return CelValue::CreateDuration(Arena::Create<Duration>(arena, std::move(d)));
}

CelValue GetHours(Arena* arena, const Duration* duration) {
  return CelValue::CreateInt64(TimeUtil::DurationToHours(*duration));
}

CelValue GetMinutes(Arena* arena, const Duration* duration) {
  return CelValue::CreateInt64(TimeUtil::DurationToMinutes(*duration));
}

CelValue GetSeconds(Arena* arena, const Duration* duration) {
  return CelValue::CreateInt64(TimeUtil::DurationToSeconds(*duration));
}

CelValue GetMilliseconds(Arena* arena, const Duration* duration) {
  int64_t millis_per_second = 1000L;
  return CelValue::CreateInt64(TimeUtil::DurationToMilliseconds(*duration) %
                               millis_per_second);
}

CelValue RegexMatches(Arena* arena, CelValue::StringHolder target,
                      CelValue::StringHolder regex) {
  RE2 re2(regex.value().data());
  if (!re2.ok()) {
    return CreateErrorValue(arena, "invalid_argument",
                            CelError::INVALID_ARGUMENT);
  }
  return CelValue::CreateBool(RE2::FullMatch(target.value().data(), re2));
}

bool StringContains(Arena* arena, CelValue::StringHolder value,
                    CelValue::StringHolder substr) {
  return absl::StrContains(value.value(), substr.value());
}

bool StringEndsWith(Arena* arena, CelValue::StringHolder value,
                    CelValue::StringHolder suffix) {
  return absl::EndsWith(value.value(), suffix.value());
}

bool StringStartsWith(Arena* arena, CelValue::StringHolder value,
                      CelValue::StringHolder prefix) {
  return absl::StartsWith(value.value(), prefix.value());
}

// Creates and registers a map index function.
template <typename T, typename CreateCelValue, typename ToAlphaNum>
util::Status RegisterMapIndexFunction(CelFunctionRegistry* registry,
                                        const CreateCelValue& create_cel_value,
                                        const ToAlphaNum& to_alpha_num) {
  return FunctionAdapter<CelValue, const CelMap*, T>::CreateAndRegister(
      builtin::kIndex, false,
      [&create_cel_value, &to_alpha_num](Arena* arena, const CelMap* cel_map,
                                         T key) -> CelValue {
        auto maybe_value = (*cel_map)[create_cel_value(key)];
        if (!maybe_value.has_value()) {
          CelError* error = Arena::CreateMessage<CelError>(arena);
          // TODO(issues/25) Which code?
          error->set_code(CelError::Code::CelError_Code_NO_SUCH_KEY);
          error->set_message(
              absl::StrCat("Key not found: ", to_alpha_num(key)));
          return CelValue::CreateError(error);
        }
        return maybe_value.value();
      },
      registry);
}

template <typename T, typename CreateCelValue>
util::Status RegisterMapIndexFunction(
    CelFunctionRegistry* registry, const CreateCelValue& create_cel_value) {
  return RegisterMapIndexFunction<T>(registry, create_cel_value,
                                     [](T v) { return absl::StrCat(v); });
}

}  // namespace

util::Status RegisterBuiltinFunctions(CelFunctionRegistry* registry) {
  // logical NOT
  util::Status status = FunctionAdapter<bool, bool>::CreateAndRegister(
      builtin::kNot, false,
      [](Arena* arena, bool value) -> bool { return !value; }, registry);
  if (!util::IsOk(status)) return status;

  // Negation group
  status = FunctionAdapter<int64_t, int64_t>::CreateAndRegister(
      builtin::kNeg, false,
      [](Arena* arena, int64_t value) -> int64_t { return -value; }, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<double, double>::CreateAndRegister(
      builtin::kNeg, false,
      [](Arena* arena, double value) -> double { return -value; }, registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<bool>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<int64_t>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<uint64_t>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<double>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<CelValue::StringHolder>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<CelValue::BytesHolder>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<const Duration*>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterComparisonFunctionsForType<const Timestamp*>(registry);
  if (!util::IsOk(status)) return status;

  // Logical AND
  // This implementation is used when short-circuiting is off.
  status = FunctionAdapter<bool, bool, bool>::CreateAndRegister(
      builtin::kAnd, false,
      [](Arena* arena, bool value1, bool value2) -> bool {
        return value1 && value2;
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: one of arguments is error.
  status = FunctionAdapter<CelValue, const CelError*, bool>::CreateAndRegister(
      builtin::kAnd, false,
      [](Arena* arena, const CelError* value1, bool value2) {
        return (value2) ? CelValue::CreateError(value1)
                        : CelValue::CreateBool(false);
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: one of arguments is error.
  status = FunctionAdapter<CelValue, bool, const CelError*>::CreateAndRegister(
      builtin::kAnd, false,
      [](Arena* arena, bool value1, const CelError* value2) {
        return (value1) ? CelValue::CreateError(value2)
                        : CelValue::CreateBool(false);
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: both arguments are errors.
  status = FunctionAdapter<const CelError*, const CelError*, const CelError*>::
      CreateAndRegister(builtin::kAnd, false,
                        [](Arena* arena, const CelError* value1,
                           const CelError* value2) { return value1; },
                        registry);
  if (!util::IsOk(status)) return status;

  // Logical OR
  // This implementation is used when short-circuiting is off.
  status = FunctionAdapter<bool, bool, bool>::CreateAndRegister(
      builtin::kOr, false,
      [](Arena* arena, bool value1, bool value2) -> bool {
        return value1 || value2;
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: one of arguments is error.
  status = FunctionAdapter<CelValue, const CelError*, bool>::CreateAndRegister(
      builtin::kOr, false,
      [](Arena* arena, const CelError* value1, bool value2) {
        return (value2) ? CelValue::CreateBool(true)
                        : CelValue::CreateError(value1);
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: one of arguments is error.
  status = FunctionAdapter<CelValue, bool, const CelError*>::CreateAndRegister(
      builtin::kOr, false,
      [](Arena* arena, bool value1, const CelError* value2) {
        return (value1) ? CelValue::CreateBool(true)
                        : CelValue::CreateError(value2);
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Special case: both arguments are errors.
  status = FunctionAdapter<const CelError*, const CelError*, const CelError*>::
      CreateAndRegister(builtin::kOr, false,
                        [](Arena* arena, const CelError* value1,
                           const CelError* value2) { return value1; },
                        registry);
  if (!util::IsOk(status)) return status;

  // Ternary operator
  // This implementation is used when short-circuiting is off.
  status =
      FunctionAdapter<CelValue, bool, CelValue, CelValue>::CreateAndRegister(
          builtin::kTernary, false,
          [](Arena* arena, bool cond, CelValue value1, CelValue value2) {
            return (cond) ? value1 : value2;
          },
          registry);
  if (!util::IsOk(status)) return status;

  // Ternary operator
  // Special case: condition is error
  status = FunctionAdapter<CelValue, const CelError*, CelValue, CelValue>::
      CreateAndRegister(
          builtin::kTernary, false,
          [](Arena* arena, const CelError* error, CelValue value1,
             CelValue value2) { return CelValue::CreateError(error); },
          registry);
  if (!util::IsOk(status)) return status;

  // Strictness
  status = FunctionAdapter<bool, bool>::CreateAndRegister(
      builtin::kNotStrictlyFalse, false,
      [](Arena* arena, bool value) -> bool { return value; }, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, const CelError*>::CreateAndRegister(
      builtin::kNotStrictlyFalse, false,
      [](Arena* arena, const CelError* error) -> bool { return true; },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, bool>::CreateAndRegister(
      builtin::kNotStrictlyFalseDeprecated, false,
      [](Arena* arena, bool value) -> bool { return value; }, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, const CelError*>::CreateAndRegister(
      builtin::kNotStrictlyFalseDeprecated, false,
      [](Arena* arena, const CelError* error) -> bool { return true; },
      registry);
  if (!util::IsOk(status)) return status;

  // String size
  auto string_size_func = [](Arena* arena,
                             CelValue::StringHolder value) -> int64_t {
    return value.value().size();
  };
  // receiver style = true/false
  // Support global and receiver style size() operations on strings.
  status = FunctionAdapter<int64_t, CelValue::StringHolder>::CreateAndRegister(
      builtin::kSize, true, string_size_func, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<int64_t, CelValue::StringHolder>::CreateAndRegister(
      builtin::kSize, false, string_size_func, registry);
  if (!util::IsOk(status)) return status;

  // Bytes size
  auto bytes_size_func = [](Arena* arena,
                            CelValue::BytesHolder value) -> int64_t {
    return value.value().size();
  };
  // receiver style = true/false
  // Support global and receiver style size() operations on bytes.
  status = FunctionAdapter<int64_t, CelValue::BytesHolder>::CreateAndRegister(
      builtin::kSize, true, bytes_size_func, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<int64_t, CelValue::BytesHolder>::CreateAndRegister(
      builtin::kSize, false, bytes_size_func, registry);
  if (!util::IsOk(status)) return status;

  // List Index
  status = FunctionAdapter<CelValue, const CelList*, int64_t>::CreateAndRegister(
      builtin::kIndex, false,
      [](Arena* arena, const CelList* cel_list, int64_t index) -> CelValue {
        if (index < 0 || index >= cel_list->size()) {
          // TODO(issues/25) Which code?
          return CreateErrorValue(arena,
                                  absl::StrCat("Index error: index=", index,
                                               " size=", cel_list->size()),
                                  CelError::Code::CelError_Code_UNKNOWN);
        }
        return (*cel_list)[index];
      },
      registry);
  if (!util::IsOk(status)) return status;

  // List size
  auto list_size_func = [](Arena* arena, const CelList* cel_list) -> int64_t {
    return (*cel_list).size();
  };
  // receiver style = true/false
  // Support both the global and receiver style size() for lists.
  status = FunctionAdapter<int64_t, const CelList*>::CreateAndRegister(
      builtin::kSize, true, list_size_func, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<int64_t, const CelList*>::CreateAndRegister(
      builtin::kSize, false, list_size_func, registry);
  if (!util::IsOk(status)) return status;

  // List in operator: @in
  status = FunctionAdapter<bool, bool, const CelList*>::CreateAndRegister(
      builtin::kIn, false, In<bool>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, int64_t, const CelList*>::CreateAndRegister(
      builtin::kIn, false, In<int64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, uint64_t, const CelList*>::CreateAndRegister(
      builtin::kIn, false, In<uint64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, double, const CelList*>::CreateAndRegister(
      builtin::kIn, false, In<double>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::StringHolder, const CelList*>::
      CreateAndRegister(builtin::kIn, false, In<CelValue::StringHolder>,
                        registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::BytesHolder, const CelList*>::
      CreateAndRegister(builtin::kIn, false, In<CelValue::BytesHolder>,
                        registry);
  if (!util::IsOk(status)) return status;

  // List in operator: _in_ (deprecated)
  // Bindings preserved for backward compatibility with stored expressions.
  status = FunctionAdapter<bool, bool, const CelList*>::CreateAndRegister(
      builtin::kInDeprecated, false, In<bool>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, int64_t, const CelList*>::CreateAndRegister(
      builtin::kInDeprecated, false, In<int64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, uint64_t, const CelList*>::CreateAndRegister(
      builtin::kInDeprecated, false, In<uint64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, double, const CelList*>::CreateAndRegister(
      builtin::kInDeprecated, false, In<double>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::StringHolder, const CelList*>::
      CreateAndRegister(builtin::kInDeprecated, false,
                        In<CelValue::StringHolder>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::BytesHolder, const CelList*>::
      CreateAndRegister(builtin::kInDeprecated, false,
                        In<CelValue::BytesHolder>, registry);
  if (!util::IsOk(status)) return status;

  // List in() function (deprecated)
  // Bindings preserved for backward compatibility with stored expressions.
  status = FunctionAdapter<bool, bool, const CelList*>::CreateAndRegister(
      builtin::kInFunction, false, In<bool>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, int64_t, const CelList*>::CreateAndRegister(
      builtin::kInFunction, false, In<int64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, uint64_t, const CelList*>::CreateAndRegister(
      builtin::kInFunction, false, In<uint64_t>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, double, const CelList*>::CreateAndRegister(
      builtin::kInFunction, false, In<double>, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::StringHolder, const CelList*>::
      CreateAndRegister(builtin::kInFunction, false, In<CelValue::StringHolder>,
                        registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<bool, CelValue::BytesHolder, const CelList*>::
      CreateAndRegister(builtin::kInFunction, false, In<CelValue::BytesHolder>,
                        registry);
  if (!util::IsOk(status)) return status;

  // Map Index
  status = RegisterMapIndexFunction<CelValue::StringHolder>(
      registry,
      [](CelValue::StringHolder v) { return CelValue::CreateString(v); },
      [](CelValue::StringHolder v) { return v.value(); });
  if (!util::IsOk(status)) return status;

  status = RegisterMapIndexFunction<int64_t>(registry, CelValue::CreateInt64);
  if (!util::IsOk(status)) return status;

  status = RegisterMapIndexFunction<uint64_t>(registry, CelValue::CreateUint64);
  if (!util::IsOk(status)) return status;

  status = RegisterMapIndexFunction<bool>(registry, CelValue::CreateBool);
  if (!util::IsOk(status)) return status;

  // Map size
  auto map_size_func = [](Arena* arena, const CelMap* cel_map) -> int64_t {
    return (*cel_map).size();
  };
  // receiver style = true/false
  status = FunctionAdapter<int64_t, const CelMap*>::CreateAndRegister(
      builtin::kSize, true, map_size_func, registry);
  if (!util::IsOk(status)) return status;
  status = FunctionAdapter<int64_t, const CelMap*>::CreateAndRegister(
      builtin::kSize, false, map_size_func, registry);
  if (!util::IsOk(status)) return status;

  // Map in operator: @in
  status = FunctionAdapter<bool, CelValue::StringHolder, const CelMap*>::
      CreateAndRegister(
          builtin::kIn, false,
          [](Arena* arena, CelValue::StringHolder key,
             const CelMap* cel_map) -> bool {
            return (*cel_map)[CelValue::CreateString(key)].has_value();
          },
          registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, int64_t, const CelMap*>::CreateAndRegister(
      builtin::kIn, false,
      [](Arena* arena, int64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateInt64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, uint64_t, const CelMap*>::CreateAndRegister(
      builtin::kIn, false,
      [](Arena* arena, uint64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateUint64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  // Map in operators: _in_ (deprecated).
  // Bindings preserved for backward compatibility with stored expressions.
  status = FunctionAdapter<bool, int64_t, const CelMap*>::CreateAndRegister(
      builtin::kInDeprecated, false,
      [](Arena* arena, int64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateInt64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, uint64_t, const CelMap*>::CreateAndRegister(
      builtin::kInDeprecated, false,
      [](Arena* arena, uint64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateUint64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, CelValue::StringHolder, const CelMap*>::
      CreateAndRegister(
          builtin::kInDeprecated, false,
          [](Arena* arena, CelValue::StringHolder key,
             const CelMap* cel_map) -> bool {
            return (*cel_map)[CelValue::CreateString(key)].has_value();
          },
          registry);
  if (!util::IsOk(status)) return status;

  // Map in() function (deprecated)
  status = FunctionAdapter<bool, CelValue::StringHolder, const CelMap*>::
      CreateAndRegister(
          builtin::kInFunction, false,
          [](Arena* arena, CelValue::StringHolder key,
             const CelMap* cel_map) -> bool {
            return (*cel_map)[CelValue::CreateString(key)].has_value();
          },
          registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, int64_t, const CelMap*>::CreateAndRegister(
      builtin::kInFunction, false,
      [](Arena* arena, int64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateInt64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<bool, uint64_t, const CelMap*>::CreateAndRegister(
      builtin::kInFunction, false,
      [](Arena* arena, uint64_t key, const CelMap* cel_map) -> bool {
        return (*cel_map)[CelValue::CreateUint64(key)].has_value();
      },
      registry);
  if (!util::IsOk(status)) return status;

  // basic Arithmetic functions for numeric types
  status = RegisterArithmeticFunctionsForType<int64_t>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterArithmeticFunctionsForType<uint64_t>(registry);
  if (!util::IsOk(status)) return status;

  status = RegisterArithmeticFunctionsForType<double>(registry);
  if (!util::IsOk(status)) return status;

  // Special arithmetic operators for Timestamp and Duration
  status = FunctionAdapter<CelValue, const Timestamp*, const Duration*>::
      CreateAndRegister(builtin::kAdd, false,
                        [](Arena* arena, const Timestamp* t1,
                           const Duration* d2) -> CelValue {
                          Timestamp* tmp =
                              Arena::CreateMessage<Timestamp>(arena);
                          tmp->CopyFrom(*t1 + *d2);
                          return CelValue::CreateTimestamp(tmp);
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*, const Timestamp*>::
      CreateAndRegister(builtin::kAdd, false,
                        [](Arena* arena, const Duration* d2,
                           const Timestamp* t1) -> CelValue {
                          Timestamp* tmp =
                              Arena::CreateMessage<Timestamp>(arena);
                          tmp->CopyFrom(*t1 + *d2);
                          return CelValue::CreateTimestamp(tmp);
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*, const Duration*>::
      CreateAndRegister(
          builtin::kAdd, false,
          [](Arena* arena, const Duration* d1, const Duration* d2) -> CelValue {
            Duration* tmp = Arena::CreateMessage<Duration>(arena);
            tmp->CopyFrom(*d1 + *d2);
            return CelValue::CreateDuration(tmp);
          },
          registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, const Duration*>::
      CreateAndRegister(builtin::kSubtract, false,
                        [](Arena* arena, const Timestamp* t1,
                           const Duration* d2) -> CelValue {
                          Timestamp* tmp =
                              Arena::CreateMessage<Timestamp>(arena);
                          tmp->CopyFrom(*t1 - *d2);
                          return CelValue::CreateTimestamp(tmp);
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, const Timestamp*>::
      CreateAndRegister(builtin::kSubtract, false,
                        [](Arena* arena, const Timestamp* t1,
                           const Timestamp* t2) -> CelValue {
                          Duration* tmp = Arena::CreateMessage<Duration>(arena);
                          tmp->CopyFrom(*t1 - *t2);
                          return CelValue::CreateDuration(tmp);
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*, const Duration*>::
      CreateAndRegister(
          builtin::kSubtract, false,
          [](Arena* arena, const Duration* d1, const Duration* d2) -> CelValue {
            Duration* tmp = Arena::CreateMessage<Duration>(arena);
            tmp->CopyFrom(*d1 - *d2);
            return CelValue::CreateDuration(tmp);
          },
          registry);
  if (!util::IsOk(status)) return status;

  // Concat group
  status =
      FunctionAdapter<CelValue::StringHolder, CelValue::StringHolder,
                      CelValue::StringHolder>::CreateAndRegister(builtin::kAdd,
                                                                 false,
                                                                 ConcatString,
                                                                 registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<CelValue::BytesHolder, CelValue::BytesHolder,
                      CelValue::BytesHolder>::CreateAndRegister(builtin::kAdd,
                                                                false,
                                                                ConcatBytes,
                                                                registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<const CelList*, const CelList*,
                      const CelList*>::CreateAndRegister(builtin::kAdd, false,
                                                         ConcatList, registry);
  if (!util::IsOk(status)) return status;

  // Global matches function.
  status = FunctionAdapter<
      CelValue, CelValue::StringHolder,
      CelValue::StringHolder>::CreateAndRegister(builtin::kRegexMatch, false,
                                                 RegexMatches, registry);
  if (!util::IsOk(status)) return status;

  // Receiver-style matches function.
  status = FunctionAdapter<
      CelValue, CelValue::StringHolder,
      CelValue::StringHolder>::CreateAndRegister(builtin::kRegexMatch, true,
                                                 RegexMatches, registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringContains, false, StringContains,
                            registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringContains, true, StringContains,
                            registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringEndsWith, false, StringEndsWith,
                            registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringEndsWith, true, StringEndsWith,
                            registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringStartsWith, false, StringStartsWith,
                            registry);
  if (!util::IsOk(status)) return status;

  status =
      FunctionAdapter<bool, CelValue::StringHolder, CelValue::StringHolder>::
          CreateAndRegister(builtin::kStringStartsWith, true, StringStartsWith,
                            registry);
  if (!util::IsOk(status)) return status;

  // Modulo
  status = FunctionAdapter<CelValue, int64_t, int64_t>::CreateAndRegister(
      builtin::kModulo, false, Modulo<int64_t>, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, uint64_t, uint64_t>::CreateAndRegister(
      builtin::kModulo, false, Modulo<uint64_t>, registry);
  if (!util::IsOk(status)) return status;

  // Timestamp
  //
  // timestamp() conversion from string..
  status = FunctionAdapter<CelValue, CelValue::StringHolder>::CreateAndRegister(
      builtin::kTimestamp, false, CreateTimestampFromString, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kFullYear, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetFullYear(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kFullYear, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetFullYear(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kMonth, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetMonth(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kMonth, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetMonth(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kDayOfYear, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetDayOfYear(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kDayOfYear, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetDayOfYear(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kDayOfMonth, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetDayOfMonth(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kDayOfMonth, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetDayOfMonth(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kDate, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetDate(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kDate, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetDate(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kDayOfWeek, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetDayOfWeek(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kDayOfWeek, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetDayOfWeek(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kHours, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetHours(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kHours, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetHours(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kMinutes, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetMinutes(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kMinutes, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetMinutes(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kSeconds, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetSeconds(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kSeconds, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetSeconds(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*, CelValue::StringHolder>::
      CreateAndRegister(builtin::kMilliseconds, true,
                        [](Arena* arena, const Timestamp* ts,
                           CelValue::StringHolder tz) -> CelValue {
                          return GetMilliseconds(arena, ts, tz.value());
                        },
                        registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Timestamp*>::CreateAndRegister(
      builtin::kMilliseconds, true,
      [](Arena* arena, const Timestamp* ts) -> CelValue {
        return GetMilliseconds(arena, ts, "");
      },
      registry);
  if (!util::IsOk(status)) return status;

  // type conversion to int
  // TODO(issues/26): To return errors on loss of precision
  // (overflow/underflow) by returning StatusOr<RawType>.
  status = FunctionAdapter<int64_t, const Timestamp*>::CreateAndRegister(
      builtin::kInt, false,
      [](Arena* arena, const Timestamp* t) {
        return TimeUtil::TimestampToSeconds(*t);
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<int64_t, double>::CreateAndRegister(
      builtin::kInt, false, [](Arena* arena, double v) { return (int64_t)v; },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<int64_t, bool>::CreateAndRegister(
      builtin::kInt, false, [](Arena* arena, bool v) { return (int64_t)v; },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<int64_t, uint64_t>::CreateAndRegister(
      builtin::kInt, false, [](Arena* arena, uint64_t v) { return (int64_t)v; },
      registry);
  if (!util::IsOk(status)) return status;

  // duration

  // duration() conversion from string..
  status = FunctionAdapter<CelValue, CelValue::StringHolder>::CreateAndRegister(
      builtin::kDuration, false, CreateDurationFromString, registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*>::CreateAndRegister(
      builtin::kHours, true,
      [](Arena* arena, const Duration* d) -> CelValue {
        return GetHours(arena, d);
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*>::CreateAndRegister(
      builtin::kMinutes, true,
      [](Arena* arena, const Duration* d) -> CelValue {
        return GetMinutes(arena, d);
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*>::CreateAndRegister(
      builtin::kSeconds, true,
      [](Arena* arena, const Duration* d) -> CelValue {
        return GetSeconds(arena, d);
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue, const Duration*>::CreateAndRegister(
      builtin::kMilliseconds, true,
      [](Arena* arena, const Duration* d) -> CelValue {
        return GetMilliseconds(arena, d);
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue::StringHolder, int64_t>::CreateAndRegister(
      builtin::kString, false,
      [](Arena* arena, int64_t value) -> CelValue::StringHolder {
        return CelValue::StringHolder(
            Arena::Create<std::string>(arena, absl::StrCat(value)));
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue::StringHolder, uint64_t>::CreateAndRegister(
      builtin::kString, false,
      [](Arena* arena, uint64_t value) -> CelValue::StringHolder {
        return CelValue::StringHolder(
            Arena::Create<std::string>(arena, absl::StrCat(value)));
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue::StringHolder, double>::CreateAndRegister(
      builtin::kString, false,
      [](Arena* arena, double value) -> CelValue::StringHolder {
        return CelValue::StringHolder(
            Arena::Create<std::string>(arena, absl::StrCat(value)));
      },
      registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue::StringHolder, CelValue::BytesHolder>::
      CreateAndRegister(
          builtin::kString, false,
          [](Arena* arena,
             CelValue::BytesHolder value) -> CelValue::StringHolder {
            return CelValue::StringHolder(
                Arena::Create<std::string>(arena, std::string(value.value())));
          },
          registry);
  if (!util::IsOk(status)) return status;

  status = FunctionAdapter<CelValue::StringHolder, CelValue::StringHolder>::
      CreateAndRegister(
          builtin::kString, false,
          [](Arena* arena, CelValue::StringHolder value)
              -> CelValue::StringHolder { return value; },
          registry);
  if (!util::IsOk(status)) return status;

  return util::OkStatus();
}

}  // namespace runtime
}  // namespace expr
}  // namespace api
}  // namespace google
