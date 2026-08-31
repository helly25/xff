// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
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
