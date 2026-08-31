// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_MATCHING_LANGUAGE_DATABASE_H_
#define XFF_MATCHING_LANGUAGE_DATABASE_H_

#include <string_view>
#include <vector>

namespace xff::language {

struct Database {
  std::string_view name;
  // Returns process-lifetime JSON. The provider lets a removable database
  // keep compressed bytes in its read-only data section and decode lazily;
  // the core vocabulary does not depend on that database's codec.
  std::string_view (*json)();
};

void RegisterDatabase(Database database);

struct DatabaseRegistrar {
  explicit DatabaseRegistrar(Database database) { RegisterDatabase(database); }
};

std::vector<Database> Databases();

}  // namespace xff::language

#endif  // XFF_MATCHING_LANGUAGE_DATABASE_H_
