// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <vector>

#include "xff/matching/mime/database.h"

namespace xff::mime {
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

}  // namespace xff::mime
