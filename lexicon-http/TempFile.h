#pragma once
// A server-generated temporary file used to bridge byte-oriented HTTP uploads
// and downloads to the path-based BlobService, and to install small documents
// such as the credentials file atomically. Clients never choose the path.
#include "Result.h"

#include <string>

namespace lexicon::http {
class TempFile {
public:
  // How the installed file is protected. POSIX always creates the temporary
  // file with mode 0600, which rename() carries over to the target; OwnerOnly
  // additionally replaces the target's Windows DACL with one that grants the
  // current user alone, because an inherited directory ACL is not a
  // replacement for 0600.
  enum class Protection { Inherit, OwnerOnly };

  TempFile() = default;
  ~TempFile();
  TempFile(TempFile &&other) noexcept;
  TempFile &operator=(TempFile &&other) noexcept;
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;

  // Creates a new empty file with a random, unpredictable name in `directory`,
  // which must already exist. Creation is exclusive, so an existing file or a
  // symbolic link planted at the path is never opened or followed.
  static Result<TempFile> create(const std::string &directory);

  const std::string &path() const { return path_; }
  bool valid() const { return !path_.empty(); }

  Result<void> write(const char *data, std::size_t length);
  // Flushes and closes the descriptor while keeping the file on disk.
  Result<void> close();
  // Flushes the contents to disk and then atomically installs the file at
  // `targetPath`, replacing whatever was there. A reader either sees the old
  // document or the new one, never a partial write. The temporary file is
  // given up on success.
  Result<void> replace(const std::string &targetPath,
                       Protection protection = Protection::Inherit);
  // Closes the descriptor and deletes the file.
  void discard() noexcept;

private:
  std::string path_;
#ifdef _WIN32
  void *handle_ = nullptr;
#else
  int descriptor_ = -1;
#endif
};
} // namespace lexicon::http
