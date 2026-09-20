#include "TempFile.h"

#include "Security.h"

#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <windows.h>
// aclapi.h must follow windows.h.
#include <aclapi.h>
#include <vector>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace lexicon::http {
namespace {
namespace fs = std::filesystem;
std::unexpected<Error> storage(std::string message) {
  return std::unexpected(Error{Error::Code::Storage, std::move(message)});
}
fs::path utf8Path(const std::string &value) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(value.data()),
                                value.size()));
}

#ifdef _WIN32
// A DACL granting the current user and nobody else. Windows has no mode bits,
// and CreateFileW would otherwise inherit whatever the directory allows, so a
// credentials file in a shared directory would be readable by others.
class OwnerOnlyAcl {
public:
  OwnerOnlyAcl() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      return;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) {
      CloseHandle(token);
      return;
    }
    user_.resize(size);
    const bool read =
        GetTokenInformation(token, TokenUser, user_.data(), size, &size) != 0;
    CloseHandle(token);
    if (!read)
      return;

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(
        reinterpret_cast<TOKEN_USER *>(user_.data())->User.Sid);
    if (SetEntriesInAclW(1, &access, nullptr, &acl_) != ERROR_SUCCESS) {
      acl_ = nullptr;
      return;
    }
    if (!InitializeSecurityDescriptor(&descriptor_,
                                      SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor_, TRUE, acl_, FALSE))
      return;
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = &descriptor_;
    attributes_.bInheritHandle = FALSE;
    valid_ = true;
  }
  ~OwnerOnlyAcl() {
    if (acl_)
      LocalFree(acl_);
  }
  OwnerOnlyAcl(const OwnerOnlyAcl &) = delete;
  OwnerOnlyAcl &operator=(const OwnerOnlyAcl &) = delete;

  bool valid() const { return valid_; }
  SECURITY_ATTRIBUTES *attributes() { return valid_ ? &attributes_ : nullptr; }
  // Replaces the target's DACL and stops it inheriting any further entries.
  bool apply(const fs::path &target) {
    if (!valid_)
      return false;
    return SetNamedSecurityInfoW(
               const_cast<LPWSTR>(target.c_str()), SE_FILE_OBJECT,
               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
               nullptr, nullptr, acl_, nullptr) == ERROR_SUCCESS;
  }

private:
  std::vector<unsigned char> user_;
  PACL acl_ = nullptr;
  SECURITY_DESCRIPTOR descriptor_{};
  SECURITY_ATTRIBUTES attributes_{};
  bool valid_ = false;
};
#else
// Makes the rename itself durable, so a crash cannot leave the directory
// entry pointing at nothing. Filesystems that do not support it are ignored.
void syncDirectory(const fs::path &directory) {
  const int handle =
      ::open(directory.empty() ? "." : directory.c_str(), O_RDONLY | O_CLOEXEC);
  if (handle < 0)
    return;
  ::fsync(handle);
  ::close(handle);
}
#endif
} // namespace

TempFile::~TempFile() { discard(); }

TempFile::TempFile(TempFile &&other) noexcept { *this = std::move(other); }

TempFile &TempFile::operator=(TempFile &&other) noexcept {
  if (this == &other)
    return *this;
  discard();
  path_ = std::exchange(other.path_, {});
#ifdef _WIN32
  handle_ = std::exchange(other.handle_, nullptr);
#else
  descriptor_ = std::exchange(other.descriptor_, -1);
#endif
  return *this;
}

Result<TempFile> TempFile::create(const std::string &directory) {
  const auto base = directory.empty() ? fs::path(".") : utf8Path(directory);
#ifdef _WIN32
  OwnerOnlyAcl security;
#endif
  for (int attempt = 0; attempt < 32; ++attempt) {
    auto suffix = randomBytes(12);
    if (!suffix)
      return std::unexpected(suffix.error());
    const auto candidate = base / (".lexicon-http-" + base64UrlEncode(*suffix));
    TempFile file;
    file.path_ = candidate.string();
#ifdef _WIN32
    HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0,
                                security.attributes(), CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const auto error = GetLastError();
      file.path_.clear();
      if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
        continue;
      return storage("Cannot create a temporary file.");
    }
    file.handle_ = handle;
#else
    const int descriptor = ::open(candidate.c_str(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
                                      O_CLOEXEC,
                                  0600);
    if (descriptor < 0) {
      file.path_.clear();
      if (errno == EEXIST)
        continue;
      return storage("Cannot create a temporary file.");
    }
    file.descriptor_ = descriptor;
#endif
    return file;
  }
  return storage("Cannot allocate a temporary file name.");
}

Result<void> TempFile::write(const char *data, std::size_t length) {
#ifdef _WIN32
  if (handle_ == nullptr)
    return storage("The temporary file is not open.");
  while (length != 0) {
    DWORD written = 0;
    const auto requested =
        static_cast<DWORD>(length < 1024 * 1024 ? length : 1024 * 1024);
    if (!WriteFile(handle_, data, requested, &written, nullptr) || written == 0)
      return storage("Cannot write the temporary file.");
    data += written;
    length -= written;
  }
#else
  if (descriptor_ < 0)
    return storage("The temporary file is not open.");
  while (length != 0) {
    const auto written = ::write(descriptor_, data, length);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      return storage("Cannot write the temporary file.");
    data += written;
    length -= static_cast<std::size_t>(written);
  }
#endif
  return {};
}

Result<void> TempFile::close() {
#ifdef _WIN32
  if (handle_ == nullptr)
    return {};
  const bool flushed = FlushFileBuffers(handle_) != 0;
  const bool closed = CloseHandle(handle_) != 0;
  handle_ = nullptr;
#else
  if (descriptor_ < 0)
    return {};
  const bool flushed = ::fsync(descriptor_) == 0;
  const bool closed = ::close(descriptor_) == 0;
  descriptor_ = -1;
#endif
  if (!flushed || !closed)
    return storage("Cannot finish the temporary file.");
  return {};
}

Result<void> TempFile::replace(const std::string &targetPath,
                               Protection protection) {
  if (path_.empty())
    return storage("There is no temporary file to install.");
  if (auto closed = close(); !closed)
    return closed;

  const auto source = utf8Path(path_);
  const auto target = utf8Path(targetPath);
#ifdef _WIN32
  // ReplaceFileW is the documented way to swap an existing file in place; it
  // does not create a missing target, so a first write falls back to
  // MoveFileExW, which replaces atomically on the same volume.
  bool installed =
      ReplaceFileW(target.c_str(), source.c_str(), nullptr,
                   REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS,
                   nullptr, nullptr) != 0;
  if (!installed) {
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
      return storage("Cannot install the file.");
    installed = MoveFileExW(source.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  }
  if (!installed)
    return storage("Cannot install the file.");
  path_.clear(); // The temporary file no longer exists under its own name.
  if (protection == Protection::OwnerOnly) {
    // ReplaceFileW keeps the ACL of whatever was there before, which may be
    // an inherited one, so the final state is enforced explicitly.
    OwnerOnlyAcl security;
    if (!security.apply(target))
      return storage("The file was installed, but its permissions could not "
                     "be restricted to the current user.");
  }
#else
  std::error_code error;
  fs::rename(source, target, error);
  if (error)
    return storage("Cannot install the file.");
  path_.clear();
  // The file keeps the 0600 it was created with; rename() replaces the
  // directory entry, not the mode.
  static_cast<void>(protection);
  syncDirectory(target.parent_path());
#endif
  return {};
}

void TempFile::discard() noexcept {
#ifdef _WIN32
  if (handle_ != nullptr) {
    CloseHandle(handle_);
    handle_ = nullptr;
  }
#else
  if (descriptor_ >= 0) {
    ::close(descriptor_);
    descriptor_ = -1;
  }
#endif
  if (!path_.empty()) {
    std::error_code ignored;
    fs::remove(utf8Path(path_), ignored);
    path_.clear();
  }
}
} // namespace lexicon::http
