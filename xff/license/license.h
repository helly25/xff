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

#ifndef XFF_LICENSE_LICENSE_H_
#define XFF_LICENSE_LICENSE_H_

#include <string>
#include <string_view>

#include "xff/license/notice.h"  // Notice, Register, Registrar, Notices() (the registration seam)

namespace xff::license {

// xff's own license, the Apache License 2.0, verbatim (xff holds the copyright). Reproduced so a
// single-file binary is self-contained; the text is generated from the repo LICENSE file (see the
// //xff/license:license_text_gen genrule), which stays canonical.
std::string_view LicenseText();

// The assembled third-party NOTICE: an xff attribution header followed by each registered component
// (sorted). Reproduced by `--help=notice`; the repo NOTICE file is kept equal to it.
//
// TODO(marcus@helly25.com): reproduce each dependency's OWN license file verbatim (RE2's BSD-3
// text, the extras' texts) via C++23 #embed, rather than the name + SPDX id + copyright line here.
std::string NoticeText();

}  // namespace xff::license

#endif  // XFF_LICENSE_LICENSE_H_
