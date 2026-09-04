// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/html.h"

#include <string>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/html_backend.h"

namespace xff::cli {

std::string HtmlReference() {
  HtmlBackend backend;
  RenderDocument(BuildReference(Audience::kPublished), backend);
  return backend.Take();
}

}  // namespace xff::cli
