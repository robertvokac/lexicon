#include "TempFile.h"

#include "Security.h"

#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <windows.h>
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
  for (int attempt = 0; attempt < 32; ++attempt) {
    auto suffix = randomBytes(12);
    if (!suffix)
      return std::unexpected(suffix.error());
    const auto candidate = base / (".lexicon-http-" + base64UrlEncode(*suffix));
    TempFile file;
    file.path_ = candidate.string();
#ifdef _WIN32
    HANDLE handle =
        CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
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
