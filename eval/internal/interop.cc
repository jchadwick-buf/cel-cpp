// Copyright 2022 Google LLC
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

#include "eval/internal/interop.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/arena.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "base/internal/message_wrapper.h"
#include "base/type_factory.h"
#include "base/type_manager.h"
#include "base/type_provider.h"
#include "base/types/struct_type.h"
#include "base/value.h"
#include "base/values/list_value.h"
#include "base/values/map_value.h"
#include "base/values/struct_value.h"
#include "eval/public/structs/legacy_type_adapter.h"
#include "eval/public/structs/legacy_type_info_apis.h"
#include "eval/public/unknown_set.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/status_macros.h"

namespace cel::interop_internal {

namespace {

using base_internal::HandleFactory;
using base_internal::InlinedStringViewBytesValue;
using base_internal::InlinedStringViewStringValue;
using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::LegacyTypeInfoApis;
using google::api::expr::runtime::MessageWrapper;
using google::api::expr::runtime::UnknownSet;

class LegacyCelList final : public CelList {
 public:
  explicit LegacyCelList(Handle<ListValue> impl) : impl_(std::move(impl)) {}

  CelValue operator[](int index) const override { return Get(nullptr, index); }

  CelValue Get(google::protobuf::Arena* arena, int index) const override {
    if (arena == nullptr) {
      static const absl::Status* status = []() {
        return new absl::Status(absl::InvalidArgumentError(
            "CelList::Get must be called with google::protobuf::Arena* for "
            "interoperation"));
      }();
      return CelValue::CreateError(status);
    }
    // Do not do this at  home. This is extremely unsafe, and we only do it for
    // interoperation, because we know that references to the below should not
    // persist past the return value.
    extensions::ProtoMemoryManager memory_manager(arena);
    TypeFactory type_factory(memory_manager);
    TypeManager type_manager(type_factory, TypeProvider::Builtin());
    ValueFactory value_factory(type_manager);
    auto value = impl_->Get(value_factory, static_cast<size_t>(index));
    if (!value.ok()) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, value.status()));
    }
    auto legacy_value = ToLegacyValue(arena, *value);
    if (!legacy_value.ok()) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, legacy_value.status()));
    }
    return std::move(legacy_value).value();
  }

  // List size
  int size() const override { return static_cast<int>(impl_->size()); }

  Handle<ListValue> value() const { return impl_; }

 private:
  internal::TypeInfo TypeId() const override {
    return internal::TypeId<LegacyCelList>();
  }

  Handle<ListValue> impl_;
};

class LegacyCelMap final : public CelMap {
 public:
  explicit LegacyCelMap(Handle<MapValue> impl) : impl_(std::move(impl)) {}

  absl::optional<CelValue> operator[](CelValue key) const override {
    return Get(nullptr, key);
  }

  absl::optional<CelValue> Get(google::protobuf::Arena* arena,
                               CelValue key) const override {
    if (arena == nullptr) {
      static const absl::Status* status = []() {
        return new absl::Status(absl::InvalidArgumentError(
            "CelMap::Get must be called with google::protobuf::Arena* for "
            "interoperation"));
      }();
      return CelValue::CreateError(status);
    }
    auto modern_key = FromLegacyValue(arena, key);
    if (!modern_key.ok()) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, modern_key.status()));
    }
    // Do not do this at  home. This is extremely unsafe, and we only do it for
    // interoperation, because we know that references to the below should not
    // persist past the return value.
    extensions::ProtoMemoryManager memory_manager(arena);
    TypeFactory type_factory(memory_manager);
    TypeManager type_manager(type_factory, TypeProvider::Builtin());
    ValueFactory value_factory(type_manager);
    auto modern_value = impl_->Get(value_factory, *modern_key);
    if (!modern_value.ok()) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, modern_value.status()));
    }
    if (!*modern_value) {
      return absl::nullopt;
    }
    auto legacy_value = ToLegacyValue(arena, *modern_value);
    if (!legacy_value.ok()) {
      return CelValue::CreateError(
          google::protobuf::Arena::Create<absl::Status>(arena, legacy_value.status()));
    }
    return std::move(legacy_value).value();
  }

  absl::StatusOr<bool> Has(const CelValue& key) const override {
    // Do not do this at  home. This is extremely unsafe, and we only do it for
    // interoperation, because we know that references to the below should not
    // persist past the return value.
    google::protobuf::Arena arena;
    CEL_ASSIGN_OR_RETURN(auto modern_key, FromLegacyValue(&arena, key));
    return impl_->Has(modern_key);
  }

  int size() const override { return static_cast<int>(impl_->size()); }

  bool empty() const override { return impl_->empty(); }

  absl::StatusOr<const CelList*> ListKeys() const override {
    return ListKeys(nullptr);
  }

  absl::StatusOr<const CelList*> ListKeys(google::protobuf::Arena* arena) const override {
    if (arena == nullptr) {
      return absl::InvalidArgumentError(
          "CelMap::ListKeys must be called with google::protobuf::Arena* for "
          "interoperation");
    }
    // Do not do this at  home. This is extremely unsafe, and we only do it for
    // interoperation, because we know that references to the below should not
    // persist past the return value.
    extensions::ProtoMemoryManager memory_manager(arena);
    TypeFactory type_factory(memory_manager);
    TypeManager type_manager(type_factory, TypeProvider::Builtin());
    ValueFactory value_factory(type_manager);
    CEL_ASSIGN_OR_RETURN(auto list_keys, impl_->ListKeys(value_factory));
    CEL_ASSIGN_OR_RETURN(auto legacy_list_keys,
                         ToLegacyValue(arena, list_keys));
    return legacy_list_keys.ListOrDie();
  }

  Handle<MapValue> value() const { return impl_; }

 private:
  internal::TypeInfo TypeId() const override {
    return internal::TypeId<LegacyCelMap>();
  }

  Handle<MapValue> impl_;
};

}  // namespace

internal::TypeInfo CelListAccess::TypeId(const CelList& list) {
  return list.TypeId();
}

internal::TypeInfo CelMapAccess::TypeId(const CelMap& map) {
  return map.TypeId();
}

Handle<StructType> LegacyStructTypeAccess::Create(uintptr_t message) {
  return base_internal::HandleFactory<StructType>::Make<
      base_internal::LegacyStructType>(message);
}

Handle<StructValue> LegacyStructValueAccess::Create(
    const MessageWrapper& wrapper) {
  return Create(MessageWrapperAccess::Message(wrapper),
                MessageWrapperAccess::TypeInfo(wrapper));
}

Handle<StructValue> LegacyStructValueAccess::Create(uintptr_t message,
                                                    uintptr_t type_info) {
  return base_internal::HandleFactory<StructValue>::Make<
      base_internal::LegacyStructValue>(message, type_info);
}

uintptr_t LegacyStructValueAccess::Message(
    const base_internal::LegacyStructValue& value) {
  return value.msg_;
}

uintptr_t LegacyStructValueAccess::TypeInfo(
    const base_internal::LegacyStructValue& value) {
  return value.type_info_;
}

MessageWrapper LegacyStructValueAccess::ToMessageWrapper(
    const base_internal::LegacyStructValue& value) {
  return MessageWrapperAccess::Make(Message(value), TypeInfo(value));
}

uintptr_t MessageWrapperAccess::Message(const MessageWrapper& wrapper) {
  return wrapper.message_ptr_;
}

uintptr_t MessageWrapperAccess::TypeInfo(const MessageWrapper& wrapper) {
  return reinterpret_cast<uintptr_t>(wrapper.legacy_type_info_);
}

MessageWrapper MessageWrapperAccess::Make(uintptr_t message,
                                          uintptr_t type_info) {
  return MessageWrapper(message,
                        reinterpret_cast<const LegacyTypeInfoApis*>(type_info));
}

MessageWrapper::Builder MessageWrapperAccess::ToBuilder(
    MessageWrapper& wrapper) {
  return wrapper.ToBuilder();
}

base_internal::StringValueRep GetStringValueRep(
    const Handle<StringValue>& value) {
  return value->rep();
}

base_internal::BytesValueRep GetBytesValueRep(const Handle<BytesValue>& value) {
  return value->rep();
}

std::shared_ptr<base_internal::UnknownSetImpl> GetUnknownValueImpl(
    const Handle<UnknownValue>& value) {
  return value->impl_;
}

std::shared_ptr<base_internal::UnknownSetImpl> GetUnknownSetImpl(
    const UnknownSet& unknown_set) {
  return unknown_set.impl_;
}

void SetUnknownValueImpl(Handle<UnknownValue>& value,
                         std::shared_ptr<base_internal::UnknownSetImpl> impl) {
  value->impl_ = std::move(impl);
}

void SetUnknownSetImpl(google::api::expr::runtime::UnknownSet& unknown_set,
                       std::shared_ptr<base_internal::UnknownSetImpl> impl) {
  unknown_set.impl_ = std::move(impl);
}

absl::StatusOr<Handle<Value>> FromLegacyValue(google::protobuf::Arena* arena,
                                              const CelValue& legacy_value) {
  switch (legacy_value.type()) {
    case CelValue::Type::kNullType:
      return CreateNullValue();
    case CelValue::Type::kBool:
      return CreateBoolValue(legacy_value.BoolOrDie());
    case CelValue::Type::kInt64:
      return CreateIntValue(legacy_value.Int64OrDie());
    case CelValue::Type::kUint64:
      return CreateUintValue(legacy_value.Uint64OrDie());
    case CelValue::Type::kDouble:
      return CreateDoubleValue(legacy_value.DoubleOrDie());
    case CelValue::Type::kString:
      return CreateStringValueFromView(legacy_value.StringOrDie().value());
    case CelValue::Type::kBytes:
      return CreateBytesValueFromView(legacy_value.BytesOrDie().value());
    case CelValue::Type::kMessage: {
      const auto& wrapper = legacy_value.MessageWrapperOrDie();
      return LegacyStructValueAccess::Create(
          MessageWrapperAccess::Message(wrapper),
          MessageWrapperAccess::TypeInfo(wrapper));
    }
    case CelValue::Type::kDuration:
      return CreateDurationValue(legacy_value.DurationOrDie());
    case CelValue::Type::kTimestamp:
      return CreateTimestampValue(legacy_value.TimestampOrDie());
    case CelValue::Type::kList: {
      if (CelListAccess::TypeId(*legacy_value.ListOrDie()) ==
          internal::TypeId<LegacyCelList>()) {
        // Fast path.
        return static_cast<const LegacyCelList*>(legacy_value.ListOrDie())
            ->value();
      }
      return HandleFactory<ListValue>::Make<base_internal::LegacyListValue>(
          reinterpret_cast<uintptr_t>(legacy_value.ListOrDie()));
    }
    case CelValue::Type::kMap: {
      if (CelMapAccess::TypeId(*legacy_value.MapOrDie()) ==
          internal::TypeId<LegacyCelMap>()) {
        // Fast path.
        return static_cast<const LegacyCelMap*>(legacy_value.MapOrDie())
            ->value();
      }
      return HandleFactory<MapValue>::Make<base_internal::LegacyMapValue>(
          reinterpret_cast<uintptr_t>(legacy_value.MapOrDie()));
    } break;
    case CelValue::Type::kUnknownSet: {
      extensions::ProtoMemoryManager memory_manager(arena);
      auto value =
          HandleFactory<UnknownValue>::Make<UnknownValue>(memory_manager);
      SetUnknownValueImpl(value,
                          GetUnknownSetImpl(*legacy_value.UnknownSetOrDie()));
      return value;
    }
    case CelValue::Type::kCelType: {
      extensions::ProtoMemoryManager memory_manager(arena);
      TypeFactory type_factory(memory_manager);
      CEL_ASSIGN_OR_RETURN(
          auto type, TypeProvider::Builtin().ProvideType(
                         type_factory, legacy_value.CelTypeOrDie().value()));
      return HandleFactory<TypeValue>::Make<TypeValue>(type);
    }
    case CelValue::Type::kError:
      return HandleFactory<ErrorValue>::Make<ErrorValue>(
          *legacy_value.ErrorOrDie());
    case CelValue::Type::kAny:
      return absl::InternalError(absl::StrCat(
          "illegal attempt to convert special CelValue type ",
          CelValue::TypeName(legacy_value.type()), " to cel::Value"));
    default:
      break;
  }
  return absl::UnimplementedError(absl::StrCat(
      "conversion from CelValue to cel::Value for type ",
      CelValue::TypeName(legacy_value.type()), " is not yet implemented"));
}

namespace {

struct BytesValueToLegacyVisitor final {
  google::protobuf::Arena* arena;

  absl::StatusOr<CelValue> operator()(absl::string_view value) const {
    return CelValue::CreateBytesView(value);
  }

  absl::StatusOr<CelValue> operator()(const absl::Cord& value) const {
    return CelValue::CreateBytes(google::protobuf::Arena::Create<std::string>(
        arena, static_cast<std::string>(value)));
  }
};

struct StringValueToLegacyVisitor final {
  google::protobuf::Arena* arena;

  absl::StatusOr<CelValue> operator()(absl::string_view value) const {
    return CelValue::CreateStringView(value);
  }

  absl::StatusOr<CelValue> operator()(const absl::Cord& value) const {
    return CelValue::CreateString(google::protobuf::Arena::Create<std::string>(
        arena, static_cast<std::string>(value)));
  }
};

}  // namespace

absl::StatusOr<CelValue> ToLegacyValue(google::protobuf::Arena* arena,
                                       const Handle<Value>& value) {
  switch (value->kind()) {
    case Kind::kNullType:
      return CelValue::CreateNull();
    case Kind::kError:
      return CelValue::CreateError(google::protobuf::Arena::Create<absl::Status>(
          arena, value.As<ErrorValue>()->value()));
    case Kind::kDyn:
      break;
    case Kind::kAny:
      break;
    case Kind::kType:
      // Should be fine, so long as we are using an arena allocator.
      return CelValue::CreateCelTypeView(
          value.As<TypeValue>()->value()->name());
    case Kind::kBool:
      return CelValue::CreateBool(value.As<BoolValue>()->value());
    case Kind::kInt:
      return CelValue::CreateInt64(value.As<IntValue>()->value());
    case Kind::kUint:
      return CelValue::CreateUint64(value.As<UintValue>()->value());
    case Kind::kDouble:
      return CelValue::CreateDouble(value.As<DoubleValue>()->value());
    case Kind::kString:
      return absl::visit(StringValueToLegacyVisitor{arena},
                         GetStringValueRep(value.As<StringValue>()));
    case Kind::kBytes:
      return absl::visit(BytesValueToLegacyVisitor{arena},
                         GetBytesValueRep(value.As<BytesValue>()));
    case Kind::kEnum:
      break;
    case Kind::kDuration:
      return CelValue::CreateDuration(value.As<DurationValue>()->value());
    case Kind::kTimestamp:
      return CelValue::CreateTimestamp(value.As<TimestampValue>()->value());
    case Kind::kList: {
      if (value.Is<base_internal::LegacyListValue>()) {
        // Fast path.
        return CelValue::CreateList(reinterpret_cast<const CelList*>(
            value.As<base_internal::LegacyListValue>()->value()));
      }
      return CelValue::CreateList(
          google::protobuf::Arena::Create<LegacyCelList>(arena, value.As<ListValue>()));
    }
    case Kind::kMap: {
      if (value.Is<base_internal::LegacyMapValue>()) {
        // Fast path.
        return CelValue::CreateMap(reinterpret_cast<const CelMap*>(
            value.As<base_internal::LegacyMapValue>()->value()));
      }
      return CelValue::CreateMap(
          google::protobuf::Arena::Create<LegacyCelMap>(arena, value.As<MapValue>()));
    }
    case Kind::kStruct: {
      if (!value.Is<base_internal::LegacyStructValue>()) {
        return absl::UnimplementedError(
            "only legacy struct types and values can be used for interop");
      }
      return CelValue::CreateMessageWrapper(MessageWrapperAccess::Make(
          LegacyStructValueAccess::Message(
              *value.As<base_internal::LegacyStructValue>()),
          LegacyStructValueAccess::TypeInfo(
              *value.As<base_internal::LegacyStructValue>())));
    }
    case Kind::kUnknown: {
      auto* legacy_value = google::protobuf::Arena::Create<UnknownSet>(arena);
      SetUnknownSetImpl(*legacy_value,
                        GetUnknownValueImpl(value.As<UnknownValue>()));
      return CelValue::CreateUnknownSet(legacy_value);
    }
    default:
      break;
  }
  return absl::UnimplementedError(
      absl::StrCat("conversion from cel::Value to CelValue for type ",
                   KindToString(value->kind()), " is not yet implemented"));
}

Handle<NullValue> CreateNullValue() {
  return HandleFactory<NullValue>::Make<NullValue>();
}

Handle<BoolValue> CreateBoolValue(bool value) {
  return HandleFactory<BoolValue>::Make<BoolValue>(value);
}

Handle<IntValue> CreateIntValue(int64_t value) {
  return HandleFactory<IntValue>::Make<IntValue>(value);
}

Handle<UintValue> CreateUintValue(uint64_t value) {
  return HandleFactory<UintValue>::Make<UintValue>(value);
}

Handle<DoubleValue> CreateDoubleValue(double value) {
  return HandleFactory<DoubleValue>::Make<DoubleValue>(value);
}

Handle<StringValue> CreateStringValueFromView(absl::string_view value) {
  return HandleFactory<StringValue>::Make<InlinedStringViewStringValue>(value);
}

Handle<BytesValue> CreateBytesValueFromView(absl::string_view value) {
  return HandleFactory<BytesValue>::Make<InlinedStringViewBytesValue>(value);
}

Handle<DurationValue> CreateDurationValue(absl::Duration value) {
  return HandleFactory<DurationValue>::Make<DurationValue>(value);
}

Handle<TimestampValue> CreateTimestampValue(absl::Time value) {
  return HandleFactory<TimestampValue>::Make<TimestampValue>(value);
}

Handle<Value> LegacyValueToModernValueOrDie(
    google::protobuf::Arena* arena, const google::api::expr::runtime::CelValue& value) {
  auto modern_value = FromLegacyValue(arena, value);
  CHECK_OK(modern_value);  // Crash OK
  return std::move(modern_value).value();
}

Handle<Value> LegacyValueToModernValueOrDie(
    MemoryManager& memory_manager,
    const google::api::expr::runtime::CelValue& value) {
  return LegacyValueToModernValueOrDie(
      extensions::ProtoMemoryManager::CastToProtoArena(memory_manager), value);
}

google::api::expr::runtime::CelValue ModernValueToLegacyValueOrDie(
    google::protobuf::Arena* arena, const Handle<Value>& value) {
  auto legacy_value = ToLegacyValue(arena, value);
  CHECK_OK(legacy_value);  // Crash OK
  return std::move(legacy_value).value();
}

google::api::expr::runtime::CelValue ModernValueToLegacyValueOrDie(
    MemoryManager& memory_manager, const Handle<Value>& value) {
  return ModernValueToLegacyValueOrDie(
      extensions::ProtoMemoryManager::CastToProtoArena(memory_manager), value);
}

}  // namespace cel::interop_internal

namespace cel::base_internal {

namespace {

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::LegacyTypeInfoApis;
using google::api::expr::runtime::MessageWrapper;
using google::api::expr::runtime::ProtoWrapperTypeOptions;
using interop_internal::FromLegacyValue;
using interop_internal::LegacyStructValueAccess;
using interop_internal::MessageWrapperAccess;
using interop_internal::ToLegacyValue;

}  // namespace

absl::string_view MessageTypeName(uintptr_t msg) {
  if ((msg & kMessageWrapperTagMask) != kMessageWrapperTagMask) {
    // For google::protobuf::MessageLite, this is actually LegacyTypeInfoApis.
    return reinterpret_cast<const LegacyTypeInfoApis*>(msg)->GetTypename(
        MessageWrapper());
  }
  return reinterpret_cast<const google::protobuf::Message*>(msg & kMessageWrapperPtrMask)
      ->GetDescriptor()
      ->full_name();
}

void MessageValueHash(uintptr_t msg, uintptr_t type_info,
                      absl::HashState state) {
  // Getting rid of hash, do nothing.
}

bool MessageValueEquals(uintptr_t lhs_msg, uintptr_t lhs_type_info,
                        const Value& rhs) {
  if (!LegacyStructValue::Is(rhs)) {
    return false;
  }
  return reinterpret_cast<const LegacyTypeInfoApis*>(lhs_type_info)
      ->GetAccessApis(MessageWrapperAccess::Make(lhs_msg, lhs_type_info))
      ->IsEqualTo(
          MessageWrapperAccess::Make(lhs_msg, lhs_type_info),
          LegacyStructValueAccess::ToMessageWrapper(
              static_cast<const base_internal::LegacyStructValue&>(rhs)));
}

absl::StatusOr<bool> MessageValueHasFieldByNumber(uintptr_t msg,
                                                  uintptr_t type_info,
                                                  int64_t number) {
  return absl::UnimplementedError(
      "legacy struct values do not support looking up fields by number");
}

absl::StatusOr<bool> MessageValueHasFieldByName(uintptr_t msg,
                                                uintptr_t type_info,
                                                absl::string_view name) {
  auto wrapper = MessageWrapperAccess::Make(msg, type_info);
  return reinterpret_cast<const LegacyTypeInfoApis*>(type_info)
      ->GetAccessApis(wrapper)
      ->HasField(name, wrapper);
}

absl::StatusOr<Handle<Value>> MessageValueGetFieldByNumber(
    uintptr_t msg, uintptr_t type_info, ValueFactory& value_factory,
    int64_t number) {
  return absl::UnimplementedError(
      "legacy struct values do not supported looking up fields by number");
}

absl::StatusOr<Handle<Value>> MessageValueGetFieldByName(
    uintptr_t msg, uintptr_t type_info, ValueFactory& value_factory,
    absl::string_view name) {
  auto wrapper = MessageWrapperAccess::Make(msg, type_info);
  CEL_ASSIGN_OR_RETURN(
      auto legacy_value,
      reinterpret_cast<const LegacyTypeInfoApis*>(type_info)
          ->GetAccessApis(wrapper)
          ->GetField(name, wrapper, ProtoWrapperTypeOptions::kUnsetNull,
                     value_factory.memory_manager()));
  return FromLegacyValue(extensions::ProtoMemoryManager::CastToProtoArena(
                             value_factory.memory_manager()),
                         legacy_value);
}

absl::StatusOr<Handle<Value>> LegacyListValueGet(uintptr_t impl,
                                                 ValueFactory& value_factory,
                                                 size_t index) {
  auto* arena = extensions::ProtoMemoryManager::CastToProtoArena(
      value_factory.memory_manager());
  return FromLegacyValue(arena, reinterpret_cast<const CelList*>(impl)->Get(
                                    arena, static_cast<int>(index)));
}

size_t LegacyListValueSize(uintptr_t impl) {
  return reinterpret_cast<const CelList*>(impl)->size();
}

bool LegacyListValueEmpty(uintptr_t impl) {
  return reinterpret_cast<const CelList*>(impl)->empty();
}

size_t LegacyMapValueSize(uintptr_t impl) {
  return reinterpret_cast<const CelMap*>(impl)->size();
}

bool LegacyMapValueEmpty(uintptr_t impl) {
  return reinterpret_cast<const CelMap*>(impl)->empty();
}

absl::StatusOr<Handle<Value>> LegacyMapValueGet(uintptr_t impl,
                                                ValueFactory& value_factory,
                                                const Handle<Value>& key) {
  auto* arena = extensions::ProtoMemoryManager::CastToProtoArena(
      value_factory.memory_manager());
  CEL_ASSIGN_OR_RETURN(auto legacy_key, ToLegacyValue(arena, key));
  auto legacy_value =
      reinterpret_cast<const CelMap*>(impl)->Get(arena, legacy_key);
  if (!legacy_value.has_value()) {
    return Handle<Value>();
  }
  return FromLegacyValue(arena, *legacy_value);
}

absl::StatusOr<bool> LegacyMapValueHas(uintptr_t impl,
                                       const Handle<Value>& key) {
  google::protobuf::Arena arena;
  CEL_ASSIGN_OR_RETURN(auto legacy_key, ToLegacyValue(&arena, key));
  return reinterpret_cast<const CelMap*>(impl)->Has(legacy_key);
}

absl::StatusOr<Handle<ListValue>> LegacyMapValueListKeys(
    uintptr_t impl, ValueFactory& value_factory) {
  auto* arena = extensions::ProtoMemoryManager::CastToProtoArena(
      value_factory.memory_manager());
  CEL_ASSIGN_OR_RETURN(auto legacy_list_keys,
                       reinterpret_cast<const CelMap*>(impl)->ListKeys(arena));
  CEL_ASSIGN_OR_RETURN(
      auto list_keys,
      FromLegacyValue(arena, CelValue::CreateList(legacy_list_keys)));
  return list_keys.As<ListValue>();
}

}  // namespace cel::base_internal
