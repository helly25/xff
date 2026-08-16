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

#include "xff/fuse/fuse_server.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
// sigaction()/raise-with-default need the POSIX header, not <csignal>.
#include <signal.h>  // NOLINT(hicpp-deprecated-headers,modernize-deprecated-headers)
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "fuse_lowlevel.h"  // @libfuse//:fuse3_headers, interface-only
#include "mbo/status/status_macros.h"
#include "xff/fuse/fuse_api.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fuse {
namespace {

// One process talks to one fuse3 library, so the API is resolved once and shared: the C callbacks
// have no capture slot besides userdata (which carries the per-mount state), and this is how they
// reach the reply functions.
const absl::StatusOr<FuseApi>& ResolvedApi() {
  static const absl::StatusOr<FuseApi>& api = *new absl::StatusOr<FuseApi>(FuseApi::Resolve());
  return api;
}

// errno for a status the VFS returned; the mount surfaces VFS answers, it does not reinterpret
// them. Unknown kinds read as EIO - "the filesystem failed", which is what happened.
int ErrnoFor(absl::Status status) {
  switch (status.code()) {
    case absl::StatusCode::kFailedPrecondition:
    case absl::StatusCode::kInvalidArgument: return EINVAL;
    case absl::StatusCode::kNotFound: return ENOENT;
    case absl::StatusCode::kPermissionDenied: return EACCES;
    case absl::StatusCode::kUnimplemented: return ENOTSUP;
    default: return EIO;
  }
}

mode_t TypeBitsFor(vfs::FileType type) {
  switch (type) {
    case vfs::FileType::kBlockDevice: return S_IFBLK;
    case vfs::FileType::kCharDevice: return S_IFCHR;
    case vfs::FileType::kDirectory: return S_IFDIR;
    case vfs::FileType::kFifo: return S_IFIFO;
    case vfs::FileType::kRegular: return S_IFREG;
    case vfs::FileType::kSocket: return S_IFSOCK;
    case vfs::FileType::kSymlink: return S_IFLNK;
    case vfs::FileType::kUnknown: return S_IFREG;
  }
  return S_IFREG;
}

struct stat StatFor(const vfs::Metadata& metadata, fuse_ino_t ino) {
  struct stat out = {};
  out.st_ino = ino;
  unsigned int mode = metadata.mode & 07777U;
  // Containers often store directories without search bits (0644, or 0); under the mount's
  // default_permissions the kernel would then refuse path TRAVERSAL (EACCES on every open under
  // such a directory), so derive x from r the way archive extractors do. Symlink modes are
  // ignored on Linux and conventionally 0777.
  if (metadata.type == vfs::FileType::kDirectory) {
    mode |= (mode & 0444U) >> 2U;
  }
  if (metadata.type == vfs::FileType::kSymlink) {
    mode |= 0777U;
  }
  // The mount is read-only by construction; the bits say so too, whatever the container stored.
  out.st_mode = static_cast<mode_t>((static_cast<unsigned int>(TypeBitsFor(metadata.type)) | mode) & ~0222U);
  out.st_nlink = metadata.nlink == 0 ? 1 : static_cast<nlink_t>(metadata.nlink);
  out.st_uid = ::getuid();
  out.st_gid = ::getgid();
  out.st_size = static_cast<off_t>(metadata.size);
  out.st_blocks = static_cast<blkcnt_t>(metadata.blocks);
  const timespec mtime = absl::ToTimespec(metadata.mtime);
  const timespec atime = absl::ToTimespec(metadata.atime);
  const timespec ctime = absl::ToTimespec(metadata.ctime);
#if defined(__APPLE__)
  out.st_mtimespec = mtime;
  out.st_atimespec = atime;
  out.st_ctimespec = ctime;
#else
  out.st_mtim = mtime;
  out.st_atim = atime;
  out.st_ctim = ctime;
#endif
  return out;
}

// The path of `name` inside `dir`, ASKED OF THE FILESYSTEM rather than assembled here. A backend
// spells its paths its own way - the archive VFS writes a member as `container!member`, not
// `container/member` - so joining with a slash would produce a path only a local filesystem
// recognizes, and every lookup below the root would fail on a real container. ReadDir already
// reports each child's full path in the backend's own vocabulary, which is the only spelling
// guaranteed to work.
std::optional<std::string> ChildPath(const vfs::FileSystem& fs, std::string_view dir, std::string_view name) {
  const absl::StatusOr<std::vector<vfs::Entry>> entries = fs.ReadDir(dir);
  if (!entries.ok()) {
    return std::nullopt;
  }
  const auto found = absl::c_find_if(*entries, [name](const vfs::Entry& entry) { return entry.name == name; });
  if (found == entries->end()) {
    return std::nullopt;
  }
  return found->path;
}

// Attribute/entry cache lifetime handed to the kernel. The view is immutable while mounted (the
// container is already open and read-only), so a long timeout only saves round trips.
constexpr double kCacheSeconds = 60.0;

}  // namespace

// The per-mount state the callbacks work on (their userdata). `mutex` guards the inode table
// (`paths`/`inodes`/`next_ino`) and the open-file table (`contents`/`next_fh`); everything else is
// set during Mount and read-only afterwards.
struct FuseServer::Impl {
  const vfs::FileSystem* fs = nullptr;
  std::string root;
  std::string mount_point;
  struct fuse_session* session = nullptr;
  std::thread loop;
  // Set before teardown asks the loop to end, so the thread can tell "we were told to stop" from
  // "the session died on its own" - the second is a real fault the run must hear about, because
  // every read under the mount then fails with ENOTCONN and the child reports a puzzling
  // "Software caused connection abort" instead.
  std::atomic<bool> exiting = false;

  absl::Mutex mutex;
  // ino -> VFS path and back. Ino FUSE_ROOT_ID is `root`; the table only grows (a read-only mount
  // lives for one run, so forget is a no-op and inodes stay stable for the kernel's cache).
  absl::flat_hash_map<fuse_ino_t, std::string> paths ABSL_GUARDED_BY(mutex);
  absl::flat_hash_map<std::string, fuse_ino_t> inodes ABSL_GUARDED_BY(mutex);
  fuse_ino_t next_ino ABSL_GUARDED_BY(mutex) = FUSE_ROOT_ID + 1;
  // Open regular files: handle -> whole content, read once per open (an archive member decode is
  // expensive; the kernel reads in small chunks).
  absl::flat_hash_map<std::uint64_t, std::string> contents ABSL_GUARDED_BY(mutex);
  std::uint64_t next_fh ABSL_GUARDED_BY(mutex) = 1;

  std::string PathOf(fuse_ino_t ino) ABSL_LOCKS_EXCLUDED(mutex) {
    if (ino == FUSE_ROOT_ID) {
      return root;
    }
    const absl::MutexLock lock(mutex);
    const auto found = paths.find(ino);
    return found == paths.end() ? std::string() : found->second;
  }

  fuse_ino_t InodeOf(const std::string& path) ABSL_LOCKS_EXCLUDED(mutex) {
    if (path == root) {
      return FUSE_ROOT_ID;
    }
    const absl::MutexLock lock(mutex);
    const auto [it, inserted] = inodes.try_emplace(path, next_ino);
    if (inserted) {
      paths.emplace(next_ino, path);
      ++next_ino;
    }
    return it->second;
  }
};

namespace {

FuseServer::Impl& ImplOf(fuse_req_t req) {
  return *static_cast<FuseServer::Impl*>(ResolvedApi()->req_userdata(req));
}

// The sessions a fatal signal must ask to exit. Guarded by a mutex on the REGISTRATION side only:
// the handler itself cannot lock, so it walks whatever the vector holds - safe in practice because
// registration happens before serving starts and removal precedes session teardown, and
// fuse_session_exit only sets a flag (the same thing libfuse's own signal handlers do).
absl::Mutex& LiveMutex() {
  static absl::Mutex& mutex = *new absl::Mutex();
  return mutex;
}

std::vector<struct fuse_session*>& LiveSessions() {
  static std::vector<struct fuse_session*>& sessions = *new std::vector<struct fuse_session*>();
  return sessions;
}

void SignalExitAll(int signo) {
  for (struct fuse_session* session : LiveSessions()) {
    ResolvedApi()->session_exit(session);
  }
  // Re-raise with the default action so the process still dies of the signal; the exiting loops
  // unmount on the way out only if the loop thread wins the race, and the per-pid stale sweep
  // covers the rest.
  struct sigaction action = {};
  action.sa_handler = SIG_DFL;
  static_cast<void>(::sigaction(signo, &action, nullptr));
  static_cast<void>(::raise(signo));  // NOLINT(concurrency-mt-unsafe): signal handler, single delivery
}

void InstallSignalHandlersOnce() {
  static const bool kInstalled = [] {
    static constexpr std::array kSignals = std::to_array<int>({SIGHUP, SIGINT, SIGTERM});
    for (const int signo : kSignals) {
      struct sigaction action = {};
      action.sa_handler = &SignalExitAll;
      static_cast<void>(::sigaction(signo, &action, nullptr));
    }
    return true;
  }();
  (void)kInstalled;
}

// Defined with the crash-path helper below; the destructor uses it to wake the loop thread.
void Unmount(std::string_view mount_point);

void OpLookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  const std::string parent_path = impl.PathOf(parent);
  if (parent_path.empty()) {
    api.reply_err(req, ENOENT);
    return;
  }
  const std::optional<std::string> path = ChildPath(*impl.fs, parent_path, name);
  if (!path.has_value()) {
    api.reply_err(req, ENOENT);
    return;
  }
  const absl::StatusOr<vfs::Metadata> metadata = impl.fs->Stat(*path, /*follow_symlinks=*/false);
  if (!metadata.ok()) {
    api.reply_err(req, ErrnoFor(metadata.status()));
    return;
  }
  struct fuse_entry_param entry = {};
  entry.ino = impl.InodeOf(*path);
  entry.attr = StatFor(*metadata, entry.ino);
  entry.attr_timeout = kCacheSeconds;
  entry.entry_timeout = kCacheSeconds;
  api.reply_entry(req, &entry);
}

void OpGetattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* /*file_info*/) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  const std::string path = impl.PathOf(ino);
  if (path.empty()) {
    api.reply_err(req, ENOENT);
    return;
  }
  const absl::StatusOr<vfs::Metadata> metadata = impl.fs->Stat(path, /*follow_symlinks=*/false);
  if (!metadata.ok()) {
    api.reply_err(req, ErrnoFor(metadata.status()));
    return;
  }
  const struct stat attr = StatFor(*metadata, ino);
  api.reply_attr(req, &attr, kCacheSeconds);
}

void OpReaddir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info* /*file_info*/) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  const std::string path = impl.PathOf(ino);
  if (path.empty()) {
    api.reply_err(req, ENOENT);
    return;
  }
  const absl::StatusOr<std::vector<vfs::Entry>> entries = impl.fs->ReadDir(path);
  if (!entries.ok()) {
    api.reply_err(req, ErrnoFor(entries.status()));
    return;
  }
  // Entries carry offsets 1..N+2 ("." and ".." first); a continuation call passes the offset of
  // the last entry it consumed, so emission starts at offsets strictly greater than `off`.
  std::string buf(size, '\0');
  std::size_t used = 0;
  off_t next = 0;
  auto emit = [&](const char* name, const struct stat& attr) {
    ++next;
    if (next <= off) {
      return true;
    }
    const std::size_t entry_size =
        api.add_direntry(req, std::next(buf.data(), static_cast<std::ptrdiff_t>(used)), size - used, name, &attr, next);
    if (entry_size > size - used) {
      return false;
    }
    used += entry_size;
    return true;
  };
  struct stat self = {};
  self.st_ino = ino;
  self.st_mode = S_IFDIR;
  struct stat parent = {};
  parent.st_ino = FUSE_ROOT_ID;
  parent.st_mode = S_IFDIR;
  bool more = emit(".", self) && emit("..", parent);
  for (std::size_t i = 0; more && i < entries->size(); ++i) {
    const vfs::Entry& entry = (*entries)[i];
    struct stat attr = {};
    attr.st_ino = impl.InodeOf(entry.path);
    attr.st_mode = TypeBitsFor(entry.type);
    more = emit(entry.name.c_str(), attr);
  }
  api.reply_buf(req, buf.data(), used);
}

void OpOpen(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  if ((static_cast<unsigned int>(file_info->flags) & static_cast<unsigned int>(O_ACCMODE)) != O_RDONLY) {
    api.reply_err(req, EROFS);
    return;
  }
  const std::string path = impl.PathOf(ino);
  if (path.empty()) {
    api.reply_err(req, ENOENT);
    return;
  }
  // Whole-content read at open: archive member decode is sequential and expensive, while the
  // kernel asks in small chunks; the open handle owns the bytes until release.
  absl::StatusOr<std::string> content = impl.fs->ReadContent(path);
  if (!content.ok()) {
    api.reply_err(req, ErrnoFor(content.status()));
    return;
  }
  {
    const absl::MutexLock lock(impl.mutex);
    file_info->fh = impl.next_fh++;
    impl.contents.emplace(file_info->fh, *std::move(content));
  }
  file_info->keep_cache = 1;
  api.reply_open(req, file_info);
}

void OpRead(fuse_req_t req, fuse_ino_t /*ino*/, size_t size, off_t off, struct fuse_file_info* file_info) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  const absl::MutexLock lock(impl.mutex);
  const auto found = impl.contents.find(file_info->fh);
  if (found == impl.contents.end()) {
    api.reply_err(req, EBADF);
    return;
  }
  const std::string& content = found->second;
  const auto offset = static_cast<std::size_t>(off);
  if (offset >= content.size()) {
    api.reply_buf(req, content.data(), 0);
    return;
  }
  api.reply_buf(
      req, std::next(content.data(), static_cast<std::ptrdiff_t>(offset)), std::min(size, content.size() - offset));
}

void OpRelease(fuse_req_t req, fuse_ino_t /*ino*/, struct fuse_file_info* file_info) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  {
    const absl::MutexLock lock(impl.mutex);
    impl.contents.erase(file_info->fh);
  }
  api.reply_err(req, 0);
}

void OpReadlink(fuse_req_t req, fuse_ino_t ino) {
  const FuseApi& api = *ResolvedApi();
  FuseServer::Impl& impl = ImplOf(req);
  const std::string path = impl.PathOf(ino);
  if (path.empty()) {
    api.reply_err(req, ENOENT);
    return;
  }
  const absl::StatusOr<std::string> target = impl.fs->ReadLink(path);
  if (!target.ok()) {
    api.reply_err(req, ErrnoFor(target.status()));
    return;
  }
  api.reply_readlink(req, target->c_str());
}

// Callbacks bind by NAME into the FETCHED header's ops struct, so a layout change in a future
// pinned libfuse release is a compile error here, never memory corruption.
const struct fuse_lowlevel_ops& ServerOps() {
  static const struct fuse_lowlevel_ops kOps = [] {
    struct fuse_lowlevel_ops ops = {};
    ops.lookup = &OpLookup;
    ops.getattr = &OpGetattr;
    ops.readdir = &OpReaddir;
    ops.open = &OpOpen;
    ops.read = &OpRead;
    ops.release = &OpRelease;
    ops.readlink = &OpReadlink;
    return ops;
  }();
  return kOps;
}

}  // namespace

absl::StatusOr<std::unique_ptr<FuseServer>> FuseServer::Mount(
    const vfs::FileSystem& fs,
    std::string root,
    std::string mount_point) {
  const absl::StatusOr<FuseApi>& api = ResolvedApi();
  MBO_RETURN_IF_ERROR(api.status());
  while (root.size() > 1 && root.back() == '/') {
    root.pop_back();
  }
  auto server = std::unique_ptr<FuseServer>(new FuseServer());
  server->impl_->fs = &fs;
  server->impl_->root = std::move(root);
  server->impl_->mount_point = std::move(mount_point);
  // "ro" tells the kernel what the missing write callbacks already enforce; default_permissions
  // lets the kernel check the (write-stripped) mode bits itself.
  std::string arg0 = "xff";
  std::string arg1 = "-o";
  std::string arg2 = "ro,default_permissions";
  std::array<char*, 3> argv = {arg0.data(), arg1.data(), arg2.data()};
  struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(argv.size()), argv.data());
  server->impl_->session = api->session_new(&args, &ServerOps(), sizeof(ServerOps()), server->impl_.get());
  // session_new copies what it needs and leaves the (now heap-allocated) argv to us, success or
  // not; libfuse's own examples free it here.
  api->opt_free_args(&args);
  if (server->impl_->session == nullptr) {
    return absl::InternalError("fuse_session_new failed");
  }
  if (api->session_mount(server->impl_->session, server->impl_->mount_point.c_str()) != 0) {
    api->session_destroy(server->impl_->session);
    server->impl_->session = nullptr;
    return absl::InternalError(absl::StrCat("FUSE mount failed at '", server->impl_->mount_point, "'"));
  }
  InstallSignalHandlersOnce();
  {
    const absl::MutexLock lock(LiveMutex());
    LiveSessions().push_back(server->impl_->session);
  }
  server->impl_->loop = std::thread([impl = server->impl_.get(), &api] {
    const int result = api->session_loop(impl->session);
    if (!impl->exiting.load()) {
      std::cerr << absl::StreamFormat(
          "xff: the FUSE session serving '%s' ended on its own (fuse_session_loop returned %d); reads under"
          " that mount will fail from here on\n",
          impl->mount_point, result);
    }
  });
  return server;
}

FuseServer::FuseServer() : impl_(std::make_unique<Impl>()) {}

FuseServer::~FuseServer() {
  if (impl_ == nullptr || impl_->session == nullptr) {
    return;
  }
  const FuseApi& api = *ResolvedApi();
  {
    const absl::MutexLock lock(LiveMutex());
    std::erase(LiveSessions(), impl_->session);
  }
  impl_->exiting.store(true);
  api.session_exit(impl_->session);
  // Waking the loop is the delicate part: it sits in a blocking read on the kernel channel, and
  // the exit flag alone is only checked between requests. Unmounting OUT OF PROCESS makes the
  // kernel end that read, so the loop returns on its own and every later call happens after the
  // join, single-threaded. Calling fuse_session_unmount here instead would close the channel fd
  // from this thread while the loop is still reading it - a real close/read race (ThreadSanitizer
  // reports exactly that), not merely a theoretical one.
  Unmount(impl_->mount_point);
  impl_->loop.join();
  api.session_unmount(impl_->session);
  api.session_destroy(impl_->session);
}

std::string_view FuseServer::MountPoint() const {
  return impl_->mount_point;
}

namespace {

// Unmounts `mount_point` with the platform's helper, out of process. Best effort by design: a
// still-busy mount stays for the stale sweep, and an already-gone one makes the helper complain
// harmlessly.
void Unmount(std::string_view mount_point) {
  std::string target(mount_point);
#if defined(__linux__)
  std::string arg0 = "fusermount3";
  std::string arg1 = "-uz";
#else
  std::string arg0 = "umount";
  std::string arg1 = "-f";
#endif
  // posix_spawnp, not system(): no shell, and the argv is ours.
  std::array<char*, 4> argv = {arg0.data(), arg1.data(), target.data(), nullptr};
  pid_t pid = 0;
  if (::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), nullptr) != 0) {
    return;
  }
  int wait_status = 0;
  ::waitpid(pid, &wait_status, 0);
}

}  // namespace

void CrashUnmount(std::string_view mount_point) {
  Unmount(mount_point);
}

}  // namespace xff::fuse
