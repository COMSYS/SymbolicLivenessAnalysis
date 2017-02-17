#include "MemoryFingerprint.h"

namespace klee {

void MemoryFingerprint_SHA1::updateUint8(const std::uint8_t value) {
  sha1.update_single(static_cast<std::uint8_t>(value));
}

void MemoryFingerprint_SHA1::updateUint64(const std::uint64_t value) {
  sha1.update_single(static_cast<std::uint8_t>(value >> 56));
  sha1.update_single(static_cast<std::uint8_t>(value >> 48));
  sha1.update_single(static_cast<std::uint8_t>(value >> 40));
  sha1.update_single(static_cast<std::uint8_t>(value >> 32));
  sha1.update_single(static_cast<std::uint8_t>(value >> 24));
  sha1.update_single(static_cast<std::uint8_t>(value >> 16));
  sha1.update_single(static_cast<std::uint8_t>(value >>  8));
  sha1.update_single(static_cast<std::uint8_t>(value));
}

void MemoryFingerprint_SHA1::updateExpr(ref<Expr> expr) {
  std::string str;
  llvm::raw_string_ostream ostream(str);
  expr->print(ostream);
  ostream.flush();
  sha1.update_range(str.begin(), str.end());
}

void MemoryFingerprint_SHA1::generateHash() {
  sha1.store_result(buffer.begin(), buffer.end());
}

void MemoryFingerprint_SHA1::clearHash() {
  sha1.reset();
}

}
