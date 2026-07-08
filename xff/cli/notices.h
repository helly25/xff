// SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com)
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_CLI_NOTICES_H_
#define XFF_CLI_NOTICES_H_

#include <string_view>

namespace xff::cli {

// The repo's NOTICE and LICENSE files, compiled into the binary so a single-file release can
// reproduce its own third-party notices (`--help=licenses`) without shipping any accompanying
// files. Each is a verbatim copy generated at build time from the corresponding file (see the
// //xff/cli:notices_gen genrule wrapping it in a raw string literal), so neither can drift.
//
// TODO(marcus@helly25.com): replace the genrule string-wrap with proper resource embedding
// (C++23 #embed) and, at that point, reproduce each third-party dependency's OWN license file
// verbatim (RE2's BSD-3-Clause text, and the archive/pcre extras' texts), not just xff's
// Apache-2.0 LICENSE + the NOTICE manifest.

// The verbatim contents of the repo's NOTICE file (the third-party component manifest).
std::string_view NoticeText();

// The verbatim contents of the repo's LICENSE file (the Apache License, Version 2.0).
std::string_view LicenseText();

}  // namespace xff::cli

#endif  // XFF_CLI_NOTICES_H_
