// SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_SQUASHFS_LIBSQSH_CODEC_PROBE_H_
#define XFF_SQUASHFS_LIBSQSH_CODEC_PROBE_H_

#ifdef __cplusplus
extern "C" {
#endif

// Returns whether the linked libsqsh has an extractor for the SquashFS compression identifier.
int xff_libsqsh_has_extractor(int compression_id);

#ifdef __cplusplus
}
#endif

#endif  // XFF_SQUASHFS_LIBSQSH_CODEC_PROBE_H_
