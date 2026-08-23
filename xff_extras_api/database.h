// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_MATCHING_MIME_DATABASE_H_
#define XFF_MATCHING_MIME_DATABASE_H_

#include <string_view>
#include <vector>

namespace xff::mime {

struct Database {
  std::string_view name;
  std::string_view json;
};

void RegisterDatabase(Database database);

struct DatabaseRegistrar {
  explicit DatabaseRegistrar(Database database) { RegisterDatabase(database); }
};

std::vector<Database> Databases();

}  // namespace xff::mime

#endif  // XFF_MATCHING_MIME_DATABASE_H_
