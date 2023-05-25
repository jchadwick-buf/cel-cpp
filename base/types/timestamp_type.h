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

#ifndef THIRD_PARTY_CEL_CPP_BASE_TYPES_TIMESTAMP_TYPE_H_
#define THIRD_PARTY_CEL_CPP_BASE_TYPES_TIMESTAMP_TYPE_H_

#include "absl/log/absl_check.h"
#include "base/kind.h"
#include "base/type.h"

namespace cel {

class TimestampValue;

class TimestampType final
    : public base_internal::SimpleType<TypeKind::kTimestamp> {
 private:
  using Base = base_internal::SimpleType<TypeKind::kTimestamp>;

 public:
  using Base::kKind;

  using Base::kName;

  using Base::Is;

  static const TimestampType& Cast(const Type& type) {
    ABSL_DCHECK(Is(type)) << "cannot cast " << type.name() << " to " << kName;
    return static_cast<const TimestampType&>(type);
  }

  using Base::kind;

  using Base::name;

  using Base::DebugString;

 private:
  CEL_INTERNAL_SIMPLE_TYPE_MEMBERS(TimestampType, TimestampValue);
};

CEL_INTERNAL_SIMPLE_TYPE_STANDALONES(TimestampType);

namespace base_internal {

template <>
struct TypeTraits<TimestampType> {
  using value_type = TimestampValue;
};

}  // namespace base_internal

}  // namespace cel

#endif  // THIRD_PARTY_CEL_CPP_BASE_TYPES_TIMESTAMP_TYPE_H_