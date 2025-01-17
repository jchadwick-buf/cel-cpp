// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// IWYU pragma: private, include "common/value.h"
// IWYU pragma: friend "common/value.h"

// `ParsedMapValue` represents values of the primitive `map` type.
// `ParsedMapValueView` is a non-owning view of `ParsedMapValue`.
// `ParsedMapValueInterface` is the abstract base class of implementations.
// `ParsedMapValue` and `ParsedMapValueView` act as smart pointers to
// `ParsedMapValueInterface`.

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MAP_VALUE_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MAP_VALUE_H_

#include <cstddef>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/any.h"
#include "common/casting.h"
#include "common/json.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value_interface.h"
#include "common/value_kind.h"
#include "common/values/map_value_interface.h"
#include "common/values/values.h"

namespace cel {

class Value;
class ListValue;
class ParsedMapValueInterface;
class ParsedMapValue;
class ValueManager;

// `Is` checks whether `lhs` and `rhs` have the same identity.
bool Is(const ParsedMapValue& lhs, const ParsedMapValue& rhs);

class ParsedMapValueInterface : public MapValueInterface {
 public:
  using alternative_type = ParsedMapValue;

  static constexpr ValueKind kKind = MapValueInterface::kKind;

  absl::StatusOr<size_t> GetSerializedSize(
      AnyToJsonConverter& value_manager) const override;

  absl::Status SerializeTo(AnyToJsonConverter& value_manager,
                           absl::Cord& value) const override;

  virtual absl::Status Equal(ValueManager& value_manager, const Value& other,
                             Value& result) const;

  bool IsZeroValue() const { return IsEmpty(); }

  // Returns `true` if this map contains no entries, `false` otherwise.
  virtual bool IsEmpty() const { return Size() == 0; }

  // Returns the number of entries in this map.
  virtual size_t Size() const = 0;

  // Lookup the value associated with the given key, returning a view of the
  // value. If the implementation is not able to directly return a view, the
  // result is stored in `scratch` and the returned view is that of `scratch`.
  absl::Status Get(ValueManager& value_manager, const Value& key,
                   Value& result) const;

  // Lookup the value associated with the given key, returning a view of the
  // value and a bool indicating whether it exists. If the implementation is not
  // able to directly return a view, the result is stored in `scratch` and the
  // returned view is that of `scratch`.
  absl::StatusOr<bool> Find(ValueManager& value_manager, const Value& key,
                            Value& result) const;

  // Checks whether the given key is present in the map.
  absl::Status Has(ValueManager& value_manager, const Value& key,
                   Value& result) const;

  // Returns a new list value whose elements are the keys of this map.
  virtual absl::Status ListKeys(ValueManager& value_manager,
                                ListValue& result) const = 0;

  // Iterates over the entries in the map, invoking `callback` for each. See the
  // comment on `ForEachCallback` for details.
  virtual absl::Status ForEach(ValueManager& value_manager,
                               ForEachCallback callback) const;

  // By default, implementations do not guarantee any iteration order. Unless
  // specified otherwise, assume the iteration order is random.
  virtual absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator(
      ValueManager& value_manager) const = 0;

 protected:
  Type GetTypeImpl(TypeManager&) const override { return MapType(); }

  // Called by `Find` after performing various argument checks.
  virtual absl::StatusOr<bool> FindImpl(ValueManager& value_manager,
                                        const Value& key,
                                        Value& result) const = 0;

  // Called by `Has` after performing various argument checks.
  virtual absl::StatusOr<bool> HasImpl(ValueManager& value_manager,
                                       const Value& key) const = 0;
};

class ParsedMapValue {
 public:
  using interface_type = ParsedMapValueInterface;

  static constexpr ValueKind kKind = ParsedMapValueInterface::kKind;

  // NOLINTNEXTLINE(google-explicit-constructor)
  ParsedMapValue(Shared<const ParsedMapValueInterface> interface)
      : interface_(std::move(interface)) {}

  // By default, this creates an empty map whose type is `map(dyn, dyn)`. Unless
  // you can help it, you should use a more specific typed map value.
  ParsedMapValue();
  ParsedMapValue(const ParsedMapValue&) = default;
  ParsedMapValue(ParsedMapValue&&) = default;
  ParsedMapValue& operator=(const ParsedMapValue&) = default;
  ParsedMapValue& operator=(ParsedMapValue&&) = default;

  constexpr ValueKind kind() const { return kKind; }

  MapType GetType(TypeManager& type_manager) const {
    return interface_->GetType(type_manager);
  }

  absl::string_view GetTypeName() const { return interface_->GetTypeName(); }

  std::string DebugString() const { return interface_->DebugString(); }

  // See `ValueInterface::GetSerializedSize`.
  absl::StatusOr<size_t> GetSerializedSize(
      AnyToJsonConverter& converter) const {
    return interface_->GetSerializedSize(converter);
  }

  // See `ValueInterface::SerializeTo`.
  absl::Status SerializeTo(AnyToJsonConverter& converter,
                           absl::Cord& value) const {
    return interface_->SerializeTo(converter, value);
  }

  // See `ValueInterface::Serialize`.
  absl::StatusOr<absl::Cord> Serialize(AnyToJsonConverter& converter) const {
    return interface_->Serialize(converter);
  }

  // See `ValueInterface::GetTypeUrl`.
  absl::StatusOr<std::string> GetTypeUrl(
      absl::string_view prefix = kTypeGoogleApisComPrefix) const {
    return interface_->GetTypeUrl(prefix);
  }

  // See `ValueInterface::ConvertToAny`.
  absl::StatusOr<Any> ConvertToAny(
      AnyToJsonConverter& converter,
      absl::string_view prefix = kTypeGoogleApisComPrefix) const {
    return interface_->ConvertToAny(converter, prefix);
  }

  absl::StatusOr<Json> ConvertToJson(AnyToJsonConverter& converter) const {
    return interface_->ConvertToJson(converter);
  }

  absl::StatusOr<JsonObject> ConvertToJsonObject(
      AnyToJsonConverter& converter) const {
    return interface_->ConvertToJsonObject(converter);
  }

  absl::Status Equal(ValueManager& value_manager, const Value& other,
                     Value& result) const;

  bool IsZeroValue() const { return interface_->IsZeroValue(); }

  bool IsEmpty() const { return interface_->IsEmpty(); }

  size_t Size() const { return interface_->Size(); }

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::Status Get(ValueManager& value_manager, const Value& key,
                   Value& result ABSL_ATTRIBUTE_LIFETIME_BOUND) const;

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::StatusOr<bool> Find(ValueManager& value_manager, const Value& key,
                            Value& result) const;

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::Status Has(ValueManager& value_manager, const Value& key,
                   Value& result) const;

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::Status ListKeys(ValueManager& value_manager, ListValue& result) const;

  // See the corresponding type declaration of `MapValueInterface` for
  // documentation.
  using ForEachCallback = typename MapValueInterface::ForEachCallback;

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::Status ForEach(ValueManager& value_manager,
                       ForEachCallback callback) const;

  // See the corresponding member function of `MapValueInterface` for
  // documentation.
  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator(
      ValueManager& value_manager) const;

  void swap(ParsedMapValue& other) noexcept {
    using std::swap;
    swap(interface_, other.interface_);
  }

  const interface_type& operator*() const { return *interface_; }

  absl::Nonnull<const interface_type*> operator->() const {
    return interface_.operator->();
  }

 private:
  friend struct NativeTypeTraits<ParsedMapValue>;
  friend bool Is(const ParsedMapValue& lhs, const ParsedMapValue& rhs);

  Shared<const ParsedMapValueInterface> interface_;
};

inline void swap(ParsedMapValue& lhs, ParsedMapValue& rhs) noexcept {
  lhs.swap(rhs);
}

inline std::ostream& operator<<(std::ostream& out, const ParsedMapValue& type) {
  return out << type.DebugString();
}

template <>
struct NativeTypeTraits<ParsedMapValue> final {
  static NativeTypeId Id(const ParsedMapValue& type) {
    return NativeTypeId::Of(*type.interface_);
  }

  static bool SkipDestructor(const ParsedMapValue& type) {
    return NativeType::SkipDestructor(type.interface_);
  }
};

template <typename T>
struct NativeTypeTraits<T, std::enable_if_t<std::conjunction_v<
                               std::negation<std::is_same<ParsedMapValue, T>>,
                               std::is_base_of<ParsedMapValue, T>>>>
    final {
  static NativeTypeId Id(const T& type) {
    return NativeTypeTraits<ParsedMapValue>::Id(type);
  }

  static bool SkipDestructor(const T& type) {
    return NativeTypeTraits<ParsedMapValue>::SkipDestructor(type);
  }
};

// MapValue -> MapValueFor<T>
template <typename To, typename From>
struct CastTraits<
    To, From,
    std::enable_if_t<std::conjunction_v<
        std::bool_constant<sizeof(To) == sizeof(absl::remove_cvref_t<From>)>,
        std::bool_constant<alignof(To) == alignof(absl::remove_cvref_t<From>)>,
        std::is_same<ParsedMapValue, absl::remove_cvref_t<From>>,
        std::negation<std::is_same<ParsedMapValue, To>>,
        std::is_base_of<ParsedMapValue, To>>>>
    final {
  static bool Compatible(const absl::remove_cvref_t<From>& from) {
    return SubsumptionTraits<To>::IsA(from);
  }

  static decltype(auto) Convert(From from) {
    // `To` is derived from `From`, `From` is `MapValue`, and `To` has the
    // same size and alignment as `MapValue`. We can just reinterpret_cast.
    return SubsumptionTraits<To>::DownCast(std::move(from));
  }
};

inline ParsedMapValue::ParsedMapValue()
    : ParsedMapValue(common_internal::GetEmptyDynDynMapValue()) {}

inline bool Is(const ParsedMapValue& lhs, const ParsedMapValue& rhs) {
  return lhs.interface_.operator->() == rhs.interface_.operator->();
}

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_PARSED_MAP_VALUE_H_
