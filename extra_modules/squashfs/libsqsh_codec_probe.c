// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#include "libsqsh_codec_probe.h"

#include <sqsh_extract_private.h>

int xff_libsqsh_has_extractor(int compression_id) {
  return sqsh__extractor_impl_from_id(compression_id) != 0;
}
