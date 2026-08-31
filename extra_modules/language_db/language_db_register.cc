// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/license/notice.h"

namespace xff::language_db {
namespace {

const license::Registrar kExtensionNotice{{
    .section = "Language database (@xff_language_db)",
    .section_lead = true,
    .component = "xff language database extra (@xff_language_db)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.",
}};

const license::Registrar kLinguistNotice{{
    .section = "Language database extension",
    .component = "github-linguist 9.6.0",
    .spdx = "MIT",
    .text = "Copyright (c) 2017 GitHub, Inc.",
}};

}  // namespace
}  // namespace xff::language_db
