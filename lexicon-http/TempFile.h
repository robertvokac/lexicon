#pragma once
// A server-generated temporary file used to bridge byte-oriented HTTP uploads
// and downloads to the path-based BlobService. Clients never choose the path.
#include "Result.h"

#include <string>

namespace lexicon::http {
class TempFile {
public:
  TempFile() = default;
  ~TempFile();
  TempFile(TempFile &&other) noexcept;
  TempFile &operator=(TempFile &&other) noexcept;
  TempFile(const TempFile &) = delete;
  TempFile &operator=(const TempFile &) = delete;

  // Creates a new empty file with owner-only permissions and a random name in
  // `directory`, which must already exist.
  static Result<TempFile> create(const std::string &directory);

  const std::string &path() const { return path_; }
  bool valid() const { return !path_.empty(); }

  Result<void> write(const char *data, std::size_t length);
  // Flushes and closes the descriptor while keeping the file on disk.
  Result<void> close();
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
