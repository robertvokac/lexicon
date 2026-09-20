#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lexicon {
enum class BlobIssueType { Healthy, Orphaned, Missing, HashMismatch, InvalidReference, UnexpectedFile };
enum class BlobScanDepth { Structural, FullIntegrity };

struct BlobIssue {
  BlobIssueType type;
  std::string hash;
  std::string relativePath;
  std::uint64_t size = 0;
  std::string detail;
};

struct BlobMaintenanceReport {
  BlobScanDepth depth = BlobScanDepth::Structural;
  std::size_t referenceCount = 0;
  std::size_t referencedBlobCount = 0;
  std::size_t physicalBlobCount = 0;
  std::size_t orphanedBlobCount = 0;
  std::size_t missingBlobCount = 0;
  std::size_t corruptedBlobCount = 0;
  std::uint64_t totalBytes = 0;
  std::uint64_t orphanedBytes = 0;
  std::vector<BlobIssue> issues;
};

struct BlobGarbageCollectionResult {
  std::size_t candidates = 0;
  std::size_t deleted = 0;
  std::uint64_t deletedBytes = 0;
  std::size_t skipped = 0;
  std::size_t failed = 0;
  std::vector<BlobIssue> issues;
};
} // namespace lexicon
