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

#ifndef THIRD_PARTY_CEL_CPP_BASE_TYPES_LIST_TYPE_H_
#define THIRD_PARTY_CEL_CPP_BASE_TYPES_LIST_TYPE_H_

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "base/internal/data.h"
#include "base/kind.h"
#include "base/type.h"

namespace cel {

class MemoryManager;

// ListType represents a list type. A list is a sequential container where each
// element is the same type.
class ListType : public Type {
 public:
  static constexpr Kind kKind = Kind::kList;

  static bool Is(const Type& type) { return type.kind() == kKind; }

  Kind kind() const { return kKind; }

  absl::string_view name() const { return KindToString(kind()); }

  std::string DebugString() const;

  void HashValue(absl::HashState state) const;

  bool Equals(const Type& other) const;

  // Returns the type of the elements in the list.
  const Persistent<Type>& element() const;

 private:
  friend class MemoryManager;
  friend class TypeFactory;
  friend class base_internal::PersistentTypeHandle;
  friend class base_internal::LegacyListType;
  friend class base_internal::ModernListType;

  ListType() = default;
};

CEL_INTERNAL_TYPE_DECL(ListType);

namespace base_internal {

// LegacyListType is used by LegacyListValue for compatibility with the legacy
// API. It's element is always the dynamic type regardless of whether the the
// expression is checked or not.
class LegacyListType final : public ListType, public InlineData {
 public:
  // Returns the type of the elements in the list.
  const Persistent<Type>& element() const;

 private:
  friend class MemoryManager;
  friend class TypeFactory;
  friend class cel::ListType;
  friend class base_internal::PersistentTypeHandle;
  template <size_t Size, size_t Align>
  friend class AnyData;

  static constexpr uintptr_t kMetadata =
      base_internal::kStoredInline | base_internal::kTriviallyCopyable |
      base_internal::kTriviallyDestructible |
      (static_cast<uintptr_t>(kKind) << base_internal::kKindShift);

  LegacyListType() : ListType(), InlineData(kMetadata) {}
};

class ModernListType final : public ListType, public HeapData {
 public:
  // Returns the type of the elements in the list.
  const Persistent<Type>& element() const { return element_; }

 private:
  friend class cel::MemoryManager;
  friend class TypeFactory;
  friend class cel::ListType;
  friend class base_internal::PersistentTypeHandle;

  explicit ModernListType(Persistent<Type> element);

  const Persistent<Type> element_;
};

}  // namespace base_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_TYPES_LIST_TYPE_H_
