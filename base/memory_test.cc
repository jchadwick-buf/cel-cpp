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

#include "base/memory.h"

#include <type_traits>
#include <vector>

#include "internal/testing.h"

namespace cel {
namespace {

struct NotTriviallyDestuctible final {
  ~NotTriviallyDestuctible() { Delete(); }

  MOCK_METHOD(void, Delete, (), ());
};

TEST(GlobalMemoryManager, NotTriviallyDestuctible) {
  auto managed = MakeUnique<NotTriviallyDestuctible>(MemoryManager::Global());
  EXPECT_CALL(*managed, Delete());
}

TEST(ArenaMemoryManager, NotTriviallyDestuctible) {
  auto memory_manager = ArenaMemoryManager::Default();
  {
    // Destructor is called when UniqueRef is destructed, not on MemoryManager
    // destruction.
    auto managed = MakeUnique<NotTriviallyDestuctible>(*memory_manager);
    EXPECT_CALL(*managed, Delete());
  }
}

TEST(Allocator, Global) {
  std::vector<int, Allocator<int>> vector(
      Allocator<int>{MemoryManager::Global()});
  vector.push_back(0);
  vector.resize(64, 0);
}

TEST(Allocator, Arena) {
  auto memory_manager = ArenaMemoryManager::Default();
  std::vector<int, Allocator<int>> vector(Allocator<int>{*memory_manager});
  vector.push_back(0);
  vector.resize(64, 0);
}

}  // namespace
}  // namespace cel