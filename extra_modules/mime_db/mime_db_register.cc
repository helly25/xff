// SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/license/notice.h"

namespace xff::mime_db {
namespace {

const license::Registrar kExtensionNotice{{
    .section = "MIME database (@xff_mime_db)",
    .section_lead = true,
    .component = "xff MIME database extra (@xff_mime_db)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger and the MBO Works authors. Licensed under the Apache License, Version 2.0.",
}};

const license::Registrar kMimeDbNotice{{
    .section = "MIME database extension",
    .component = "mime-db 1.54.0",
    .spdx = "MIT",
    .text = "Copyright (c) 2014 Jonathan Ong; Copyright (c) 2015-2022 Douglas Christopher Wilson",
}};

}  // namespace
}  // namespace xff::mime_db
