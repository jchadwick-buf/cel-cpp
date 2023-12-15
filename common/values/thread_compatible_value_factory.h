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

// IWYU pragma: private

#ifndef THIRD_PARTY_CEL_CPP_COMMON_VALUES_THREAD_COMPATIBLE_VALUE_FACTORY_H_
#define THIRD_PARTY_CEL_CPP_COMMON_VALUES_THREAD_COMPATIBLE_VALUE_FACTORY_H_

#include "common/memory.h"
#include "common/type.h"
#include "common/types/thread_compatible_type_factory.h"
#include "common/value.h"
#include "common/value_factory.h"
#include "common/values/value_cache.h"

namespace cel::common_internal {

class ThreadCompatibleValueFactory : public ThreadCompatibleTypeFactory,
                                     public ValueFactory {
 public:
  explicit ThreadCompatibleValueFactory(MemoryManagerRef memory_manager)
      : ThreadCompatibleTypeFactory(memory_manager) {}

  using ThreadCompatibleTypeFactory::GetMemoryManager;

 private:
  ListValue CreateZeroListValueImpl(ListTypeView type) override;

  MapValue CreateZeroMapValueImpl(MapTypeView type) override;

  OptionalValue CreateZeroOptionalValueImpl(OptionalTypeView type) override;

  ListValueCacheMap list_values_;
  MapValueCacheMap map_values_;
  OptionalValueCacheMap optional_values_;
};

}  // namespace cel::common_internal

#endif  // THIRD_PARTY_CEL_CPP_COMMON_VALUES_THREAD_COMPATIBLE_VALUE_FACTORY_H_
