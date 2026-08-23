// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include <vector>

#include "language_database_api.h"

namespace xff::language {
namespace {

std::vector<Database>& Registry() {
  static std::vector<Database> registry;
  return registry;
}

}  // namespace

void RegisterDatabase(Database database) {
  Registry().push_back(database);
}

std::vector<Database> Databases() {
  return Registry();
}

}  // namespace xff::language
