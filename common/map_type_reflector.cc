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

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "common/casting.h"
#include "common/json.h"
#include "common/memory.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/type_kind.h"
#include "common/type_reflector.h"
#include "common/value.h"
#include "common/value_factory.h"
#include "common/value_kind.h"
#include "common/value_manager.h"
#include "internal/dynamic_loader.h"  // IWYU pragma: keep
#include "internal/status_macros.h"

namespace cel {

namespace {

template <typename T>
struct MapValueKeyHash;
template <typename T>
struct MapValueKeyEqualTo;
template <typename K, typename V>
using ValueFlatHashMapFor =
    absl::flat_hash_map<K, V, MapValueKeyHash<K>, MapValueKeyEqualTo<K>>;
template <typename T>
struct MapValueKeyJson;

template <typename T>
struct MapValueKeyHash {
  // Used to enable heterogeneous operations in supporting containers.
  using is_transparent = void;

  size_t operator()(const T& value) const { return absl::HashOf(value); }
};

template <typename T>
struct MapValueKeyEqualTo {
  // Used to enable heterogeneous operations in supporting containers.
  using is_transparent = void;

  bool operator()(const T& lhs, const T& rhs) const { return lhs == rhs; }
};

template <typename T>
struct MapValueKeyLess {
  bool operator()(const T& lhs, const T& rhs) const { return lhs < rhs; }
};

template <>
struct MapValueKeyHash<Value> {
  // Used to enable heterogeneous operations in supporting containers.
  using is_transparent = void;

  size_t operator()(const Value& value) const {
    switch (value.kind()) {
      case ValueKind::kBool:
        return absl::HashOf(ValueKind::kBool, Cast<BoolValue>(value));
      case ValueKind::kInt:
        return absl::HashOf(ValueKind::kInt, Cast<IntValue>(value));
      case ValueKind::kUint:
        return absl::HashOf(ValueKind::kUint, Cast<UintValue>(value));
      case ValueKind::kString:
        return absl::HashOf(ValueKind::kString, Cast<StringValue>(value));
      default:
        ABSL_DLOG(FATAL) << "Invalid map key value: " << value;
        return 0;
    }
  }
};

template <>
struct MapValueKeyEqualTo<Value> {
  // Used to enable heterogeneous operations in supporting containers.
  using is_transparent = void;

  bool operator()(const Value& lhs, const Value& rhs) const {
    switch (lhs.kind()) {
      case ValueKind::kBool:
        switch (rhs.kind()) {
          case ValueKind::kBool:
            return Cast<BoolValue>(lhs) == Cast<BoolValue>(rhs);
          case ValueKind::kInt:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kUint:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kString:
            return false;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kInt:
        switch (rhs.kind()) {
          case ValueKind::kInt:
            return Cast<IntValue>(lhs) == Cast<IntValue>(rhs);
          case ValueKind::kBool:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kUint:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kString:
            return false;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kUint:
        switch (rhs.kind()) {
          case ValueKind::kUint:
            return Cast<UintValue>(lhs) == Cast<UintValue>(rhs);
          case ValueKind::kBool:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kInt:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kString:
            return false;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kString:
        switch (rhs.kind()) {
          case ValueKind::kString:
            return Cast<StringValue>(lhs) == Cast<StringValue>(rhs);
          case ValueKind::kBool:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kInt:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kUint:
            return false;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      default:
        ABSL_DLOG(FATAL) << "Invalid map key value: " << lhs;
        return false;
    }
  }
};

template <>
struct MapValueKeyLess<Value> {
  bool operator()(const Value& lhs, const Value& rhs) const {
    switch (lhs.kind()) {
      case ValueKind::kBool:
        switch (rhs.kind()) {
          case ValueKind::kBool:
            return Cast<BoolValue>(lhs) < Cast<BoolValue>(rhs);
          case ValueKind::kInt:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kUint:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kString:
            return true;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kInt:
        switch (rhs.kind()) {
          case ValueKind::kInt:
            return Cast<IntValue>(lhs) < Cast<IntValue>(rhs);
          case ValueKind::kBool:
            return false;
          case ValueKind::kUint:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kString:
            return true;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kUint:
        switch (rhs.kind()) {
          case ValueKind::kUint:
            return Cast<UintValue>(lhs) < Cast<UintValue>(rhs);
          case ValueKind::kBool:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kInt:
            return false;
          case ValueKind::kString:
            return true;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      case ValueKind::kString:
        switch (rhs.kind()) {
          case ValueKind::kString:
            return Cast<StringValue>(lhs) < Cast<StringValue>(rhs);
          case ValueKind::kBool:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kInt:
            ABSL_FALLTHROUGH_INTENDED;
          case ValueKind::kUint:
            return false;
          default:
            ABSL_DLOG(FATAL) << "Invalid map key value: " << rhs;
            return false;
        }
      default:
        ABSL_DLOG(FATAL) << "Invalid map key value: " << lhs;
        return false;
    }
  }
};

template <>
struct MapValueKeyJson<BoolValue> {
  absl::StatusOr<absl::Cord> operator()(BoolValue value) const {
    return TypeConversionError("map<bool, ?>", "google.protobuf.Struct")
        .NativeValue();
  }
};

template <>
struct MapValueKeyJson<IntValue> {
  absl::StatusOr<absl::Cord> operator()(IntValue value) const {
    return TypeConversionError("map<int, ?>", "google.protobuf.Struct")
        .NativeValue();
  }
};

template <>
struct MapValueKeyJson<UintValue> {
  absl::StatusOr<absl::Cord> operator()(UintValue value) const {
    return TypeConversionError("map<uint, ?>", "google.protobuf.Struct")
        .NativeValue();
  }
};

template <>
struct MapValueKeyJson<StringValue> {
  absl::StatusOr<absl::Cord> operator()(const StringValue& value) const {
    return value.NativeCord();
  }
};

template <>
struct MapValueKeyJson<Value> {
  absl::StatusOr<absl::Cord> operator()(const Value& value) const {
    switch (value.kind()) {
      case ValueKind::kBool:
        return MapValueKeyJson<BoolValue>{}(Cast<BoolValue>(value));
      case ValueKind::kInt:
        return MapValueKeyJson<IntValue>{}(Cast<IntValue>(value));
      case ValueKind::kUint:
        return MapValueKeyJson<UintValue>{}(Cast<UintValue>(value));
      case ValueKind::kString:
        return MapValueKeyJson<StringValue>{}(Cast<StringValue>(value));
      default:
        return absl::InternalError(
            absl::StrCat("unexpected map key type: ", value.GetTypeName()));
    }
  }
};

template <typename K, typename V>
class TypedMapValueKeyIterator final : public ValueIterator {
 public:
  explicit TypedMapValueKeyIterator(
      const ValueFlatHashMapFor<K, V>& entries ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : begin_(entries.begin()), end_(entries.end()) {}

  bool HasNext() override { return begin_ != end_; }

  absl::Status Next(ValueManager&, Value& result) override {
    if (ABSL_PREDICT_FALSE(begin_ == end_)) {
      return absl::FailedPreconditionError(
          "ValueIterator::Next() called when "
          "ValueIterator::HasNext() returns false");
    }
    auto key = Value(begin_->first);
    ++begin_;
    result = std::move(key);
    return absl::OkStatus();
  }

 private:
  typename ValueFlatHashMapFor<K, V>::const_iterator begin_;
  const typename ValueFlatHashMapFor<K, V>::const_iterator end_;
};

template <typename K, typename V>
class TypedMapValue final : public ParsedMapValueInterface {
 public:
  using key_type = std::decay_t<decltype(std::declval<K>().GetType(
      std::declval<TypeManager&>()))>;

  TypedMapValue(MapType type,
                absl::flat_hash_map<K, V, MapValueKeyHash<K>,
                                    MapValueKeyEqualTo<K>>&& entries)
      : type_(std::move(type)), entries_(std::move(entries)) {}

  std::string DebugString() const override {
    std::vector<std::pair<K, V>> entries;
    entries.reserve(Size());
    for (const auto& entry : entries_) {
      entries.push_back(std::pair{K{entry.first}, V{entry.second}});
    }
    std::stable_sort(
        entries.begin(), entries.end(),
        [](const std::pair<K, V>& lhs, const std::pair<K, V>& rhs) -> bool {
          return MapValueKeyLess<K>{}(lhs.first, rhs.first);
        });
    return absl::StrCat(
        "{",
        absl::StrJoin(entries, ", ",
                      absl::PairFormatter(absl::StreamFormatter(), ": ",
                                          absl::StreamFormatter())),
        "}");
  }

  bool IsEmpty() const override { return entries_.empty(); }

  size_t Size() const override { return entries_.size(); }

  absl::StatusOr<JsonObject> ConvertToJsonObject(
      AnyToJsonConverter& value_manager) const override {
    JsonObjectBuilder builder;
    builder.reserve(Size());
    for (const auto& entry : entries_) {
      CEL_ASSIGN_OR_RETURN(auto json_key, MapValueKeyJson<K>{}(entry.first));
      CEL_ASSIGN_OR_RETURN(auto json_value,
                           entry.second.ConvertToJson(value_manager));
      if (!builder.insert(std::pair{std::move(json_key), std::move(json_value)})
               .second) {
        return absl::FailedPreconditionError(
            "cannot convert map with duplicate keys to JSON");
      }
    }
    return std::move(builder).Build();
  }

  absl::Status ListKeys(ValueManager& value_manager,
                        ListValue& result) const override {
    CEL_ASSIGN_OR_RETURN(
        auto keys,
        value_manager.NewListValueBuilder(
            value_manager.CreateListType(Cast<key_type>(type_.key()))));
    keys->Reserve(Size());
    for (const auto& entry : entries_) {
      CEL_RETURN_IF_ERROR(keys->Add(entry.first));
    }
    result = std::move(*keys).Build();
    return absl::OkStatus();
  }

  absl::Status ForEach(ValueManager& value_manager,
                       ForEachCallback callback) const override {
    for (const auto& entry : entries_) {
      CEL_ASSIGN_OR_RETURN(auto ok, callback(entry.first, entry.second));
      if (!ok) {
        break;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::Nonnull<ValueIteratorPtr>> NewIterator(
      ValueManager&) const override {
    return std::make_unique<TypedMapValueKeyIterator<K, V>>(entries_);
  }

 protected:
  Type GetTypeImpl(TypeManager&) const override { return type_; }

 private:
  absl::StatusOr<bool> FindImpl(ValueManager&, const Value& key,
                                Value& result) const override {
    if (auto entry = entries_.find(Cast<K>(key)); entry != entries_.end()) {
      result = entry->second;
      return true;
    }
    return false;
  }

  absl::StatusOr<bool> HasImpl(ValueManager&, const Value& key) const override {
    if (auto entry = entries_.find(Cast<K>(key)); entry != entries_.end()) {
      return true;
    }
    return false;
  }

  NativeTypeId GetNativeTypeId() const noexcept override {
    return NativeTypeId::For<TypedMapValue<K, V>>();
  }

  const MapType type_;
  const absl::flat_hash_map<K, V, MapValueKeyHash<K>, MapValueKeyEqualTo<K>>
      entries_;
};

template <typename K, typename V>
class MapValueBuilderImpl final : public MapValueBuilder {
 public:
  using key_type = std::decay_t<decltype(std::declval<K>().GetType(
      std::declval<TypeManager&>()))>;
  using value_type = std::decay_t<decltype(std::declval<V>().GetType(
      std::declval<TypeManager&>()))>;

  static_assert(common_internal::IsValueAlternativeV<K>,
                "K must be Value or one of the Value alternatives");
  static_assert(common_internal::IsValueAlternativeV<V> ||
                    std::is_same_v<ListValue, V> || std::is_same_v<MapValue, V>,
                "V must be Value or one of the Value alternatives");

  MapValueBuilderImpl(MemoryManagerRef memory_manager, MapType type)
      : memory_manager_(memory_manager), type_(std::move(type)) {}

  MapValueBuilderImpl(const MapValueBuilderImpl&) = delete;
  MapValueBuilderImpl(MapValueBuilderImpl&&) = delete;
  MapValueBuilderImpl& operator=(const MapValueBuilderImpl&) = delete;
  MapValueBuilderImpl& operator=(MapValueBuilderImpl&&) = delete;

  absl::Status Put(Value key, Value value) override {
    if (key.Is<ErrorValue>()) {
      return key.As<ErrorValue>().NativeValue();
    }
    if (value.Is<ErrorValue>()) {
      return value.As<ErrorValue>().NativeValue();
    }
    auto inserted =
        entries_.insert({Cast<K>(std::move(key)), Cast<V>(std::move(value))})
            .second;
    if (!inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  bool IsEmpty() const override { return entries_.empty(); }

  size_t Size() const override { return entries_.size(); }

  void Reserve(size_t capacity) override { entries_.reserve(capacity); }

  MapValue Build() && override {
    return ParsedMapValue(memory_manager_.MakeShared<TypedMapValue<K, V>>(
        std::move(type_), std::move(entries_)));
  }

 private:
  MemoryManagerRef memory_manager_;
  MapType type_;
  ValueFlatHashMapFor<K, V> entries_;
};

template <typename V>
class MapValueBuilderImpl<Value, V> final : public MapValueBuilder {
 public:
  using value_type = std::decay_t<decltype(std::declval<V>().GetType(
      std::declval<TypeManager&>()))>;

  static_assert(common_internal::IsValueAlternativeV<V> ||
                    std::is_same_v<ListValue, V> || std::is_same_v<MapValue, V>,
                "V must be Value or one of the Value alternatives");

  MapValueBuilderImpl(MemoryManagerRef memory_manager, MapType type)
      : memory_manager_(memory_manager), type_(std::move(type)) {}

  absl::Status Put(Value key, Value value) override {
    if (key.Is<ErrorValue>()) {
      return key.As<ErrorValue>().NativeValue();
    }
    if (value.Is<ErrorValue>()) {
      return value.As<ErrorValue>().NativeValue();
    }
    auto inserted =
        entries_.insert({std::move(key), Cast<V>(std::move(value))}).second;
    if (!inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  bool IsEmpty() const override { return entries_.empty(); }

  size_t Size() const override { return entries_.size(); }

  void Reserve(size_t capacity) override { entries_.reserve(capacity); }

  MapValue Build() && override {
    return ParsedMapValue(memory_manager_.MakeShared<TypedMapValue<Value, V>>(
        std::move(type_), std::move(entries_)));
  }

 private:
  MemoryManagerRef memory_manager_;
  MapType type_;
  absl::flat_hash_map<Value, V, MapValueKeyHash<Value>,
                      MapValueKeyEqualTo<Value>>
      entries_;
};

template <typename K>
class MapValueBuilderImpl<K, Value> final : public MapValueBuilder {
 public:
  using key_type = std::decay_t<decltype(std::declval<K>().GetType(
      std::declval<TypeManager&>()))>;

  static_assert(common_internal::IsValueAlternativeV<K>,
                "K must be Value or one of the Value alternatives");

  MapValueBuilderImpl(MemoryManagerRef memory_manager, MapType type)
      : memory_manager_(memory_manager), type_(std::move(type)) {}

  absl::Status Put(Value key, Value value) override {
    if (key.Is<ErrorValue>()) {
      return key.As<ErrorValue>().NativeValue();
    }
    if (value.Is<ErrorValue>()) {
      return value.As<ErrorValue>().NativeValue();
    }
    auto inserted =
        entries_.insert({Cast<K>(std::move(key)), std::move(value)}).second;
    if (!inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  bool IsEmpty() const override { return entries_.empty(); }

  size_t Size() const override { return entries_.size(); }

  void Reserve(size_t capacity) override { entries_.reserve(capacity); }

  MapValue Build() && override {
    return ParsedMapValue(memory_manager_.MakeShared<TypedMapValue<K, Value>>(
        std::move(type_), std::move(entries_)));
  }

 private:
  MemoryManagerRef memory_manager_;
  MapType type_;
  absl::flat_hash_map<K, Value, MapValueKeyHash<K>, MapValueKeyEqualTo<K>>
      entries_;
};

template <>
class MapValueBuilderImpl<Value, Value> final : public MapValueBuilder {
 public:
  MapValueBuilderImpl(MemoryManagerRef memory_manager, MapType type)
      : memory_manager_(memory_manager), type_(std::move(type)) {}

  absl::Status Put(Value key, Value value) override {
    if (key.Is<ErrorValue>()) {
      return key.As<ErrorValue>().NativeValue();
    }
    if (value.Is<ErrorValue>()) {
      return value.As<ErrorValue>().NativeValue();
    }
    auto inserted = entries_.insert({std::move(key), std::move(value)}).second;
    if (!inserted) {
      return DuplicateKeyError().NativeValue();
    }
    return absl::OkStatus();
  }

  bool IsEmpty() const override { return entries_.empty(); }

  size_t Size() const override { return entries_.size(); }

  void Reserve(size_t capacity) override { entries_.reserve(capacity); }

  MapValue Build() && override {
    return ParsedMapValue(
        memory_manager_.MakeShared<TypedMapValue<Value, Value>>(
            std::move(type_), std::move(entries_)));
  }

 private:
  MemoryManagerRef memory_manager_;
  MapType type_;
  absl::flat_hash_map<Value, Value, MapValueKeyHash<Value>,
                      MapValueKeyEqualTo<Value>>
      entries_;
};

using LegacyTypeReflector_NewMapValueBuilder =
    absl::StatusOr<Unique<MapValueBuilder>> (*)(ValueFactory&, MapTypeView);

ABSL_CONST_INIT struct {
  absl::once_flag init_once;
  LegacyTypeReflector_NewMapValueBuilder new_map_value_builder = nullptr;
} legacy_type_reflector_vtable;

#if ABSL_HAVE_ATTRIBUTE_WEAK
extern "C" ABSL_ATTRIBUTE_WEAK absl::StatusOr<Unique<MapValueBuilder>>
cel_common_internal_LegacyTypeReflector_NewMapValueBuilder(
    ValueFactory& value_factory, MapTypeView type);
#endif

void InitializeLegacyTypeReflector() {
  absl::call_once(legacy_type_reflector_vtable.init_once, []() -> void {
#if ABSL_HAVE_ATTRIBUTE_WEAK
    legacy_type_reflector_vtable.new_map_value_builder =
        cel_common_internal_LegacyTypeReflector_NewMapValueBuilder;
#else
    internal::DynamicLoader dynamic_loader;
    if (auto new_map_value_builder = dynamic_loader.FindSymbol(
            "cel_common_internal_LegacyTypeReflector_NewMapValueBuilder");
        new_map_value_builder) {
      legacy_type_reflector_vtable.new_map_value_builder =
          *new_map_value_builder;
    }
#endif
  });
}

}  // namespace

absl::StatusOr<Unique<MapValueBuilder>> TypeReflector::NewMapValueBuilder(
    ValueFactory& value_factory, MapTypeView type) const {
  InitializeLegacyTypeReflector();
  auto memory_manager = value_factory.GetMemoryManager();
  if (memory_manager.memory_management() == MemoryManagement::kPooling &&
      legacy_type_reflector_vtable.new_map_value_builder != nullptr) {
    auto status_or_builder =
        (*legacy_type_reflector_vtable.new_map_value_builder)(value_factory,
                                                              type);
    if (status_or_builder.ok()) {
      return std::move(status_or_builder).value();
    }
    if (!absl::IsUnimplemented(status_or_builder.status())) {
      return status_or_builder;
    }
  }
  switch (type.key().kind()) {
    case TypeKind::kBool:
      switch (type.value().kind()) {
        case TypeKind::kBool:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, BoolValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kBytes:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, BytesValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDouble:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, DoubleValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDuration:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, DurationValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kInt:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, IntValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kList:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, ListValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kMap:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, MapValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kNull:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, NullValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kOpaque:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, OpaqueValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kString:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, StringValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kTimestamp:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, TimestampValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kType:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, TypeValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kUint:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, UintValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDyn:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<BoolValue, Value>>(memory_manager,
                                                                 MapType(type));
        default:
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid map value type: ", type.value().DebugString()));
      }
    case TypeKind::kInt:
      switch (type.value().kind()) {
        case TypeKind::kBool:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, BoolValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kBytes:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, BytesValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDouble:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, DoubleValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDuration:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, DurationValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kInt:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, IntValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kList:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, ListValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kMap:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, MapValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kNull:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, NullValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kOpaque:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, OpaqueValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kString:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, StringValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kTimestamp:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, TimestampValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kType:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, TypeValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kUint:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, UintValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDyn:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<IntValue, Value>>(memory_manager,
                                                                MapType(type));
        default:
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid map value type: ", type.value().DebugString()));
      }
    case TypeKind::kUint:
      switch (type.value().kind()) {
        case TypeKind::kBool:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, BoolValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kBytes:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, BytesValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDouble:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, DoubleValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDuration:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, DurationValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kInt:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, IntValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kList:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, ListValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kMap:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, MapValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kNull:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, NullValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kOpaque:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, OpaqueValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kString:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, StringValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kTimestamp:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, TimestampValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kType:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, TypeValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kUint:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, UintValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDyn:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<UintValue, Value>>(memory_manager,
                                                                 MapType(type));
        default:
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid map value type: ", type.value().DebugString()));
      }
    case TypeKind::kString:
      switch (type.value().kind()) {
        case TypeKind::kBool:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, BoolValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kBytes:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, BytesValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDouble:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, DoubleValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDuration:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, DurationValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kInt:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, IntValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kList:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, ListValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kMap:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, MapValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kNull:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, NullValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kOpaque:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, OpaqueValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kString:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, StringValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kTimestamp:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, TimestampValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kType:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, TypeValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kUint:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, UintValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDyn:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<StringValue, Value>>(
                  memory_manager, MapType(type));
        default:
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid map value type: ", type.value().DebugString()));
      }
    case TypeKind::kDyn:
      switch (type.value().kind()) {
        case TypeKind::kBool:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, BoolValue>>(memory_manager,
                                                                 MapType(type));
        case TypeKind::kBytes:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, BytesValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDouble:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, DoubleValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kDuration:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, DurationValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kInt:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, IntValue>>(memory_manager,
                                                                MapType(type));
        case TypeKind::kList:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, ListValue>>(memory_manager,
                                                                 MapType(type));
        case TypeKind::kMap:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, MapValue>>(memory_manager,
                                                                MapType(type));
        case TypeKind::kNull:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, NullValue>>(memory_manager,
                                                                 MapType(type));
        case TypeKind::kOpaque:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, OpaqueValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kString:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, StringValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kTimestamp:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, TimestampValue>>(
                  memory_manager, MapType(type));
        case TypeKind::kType:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, TypeValue>>(memory_manager,
                                                                 MapType(type));
        case TypeKind::kUint:
          return memory_manager
              .MakeUnique<MapValueBuilderImpl<Value, UintValue>>(memory_manager,
                                                                 MapType(type));
        case TypeKind::kDyn:
          return memory_manager.MakeUnique<MapValueBuilderImpl<Value, Value>>(
              memory_manager, MapType(type));
        default:
          return absl::InvalidArgumentError(absl::StrCat(
              "invalid map value type: ", type.value().DebugString()));
      }
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("invalid map key type: ", type.key().DebugString()));
  }
}

namespace common_internal {

absl::StatusOr<Unique<MapValueBuilder>> LegacyTypeReflector::NewMapValueBuilder(
    ValueFactory& value_factory, MapTypeView type) const {
  InitializeLegacyTypeReflector();
  auto memory_manager = value_factory.GetMemoryManager();
  if (memory_manager.memory_management() == MemoryManagement::kPooling &&
      legacy_type_reflector_vtable.new_map_value_builder != nullptr) {
    auto status_or_builder =
        (*legacy_type_reflector_vtable.new_map_value_builder)(value_factory,
                                                              type);
    if (status_or_builder.ok()) {
      return std::move(status_or_builder).value();
    }
    if (!absl::IsUnimplemented(status_or_builder.status())) {
      return status_or_builder;
    }
  }
  return TypeReflector::NewMapValueBuilder(value_factory, type);
}

}  // namespace common_internal

}  // namespace cel
