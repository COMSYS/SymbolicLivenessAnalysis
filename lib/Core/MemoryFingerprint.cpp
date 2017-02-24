#include "MemoryFingerprint.h"

namespace klee {

/* MemoryFingerprint_SHA1 */

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


/* MemoryFingerprint_CryptoPP_SHA1 */

void MemoryFingerprint_CryptoPP_SHA1::updateUint8(const std::uint8_t value) {
  static_assert(sizeof(byte) == sizeof(std::uint8_t));
  sha1.Update(&value, 1);
}

void MemoryFingerprint_CryptoPP_SHA1::updateUint64(const std::uint64_t value) {
  static_assert(sizeof(byte) == sizeof(std::uint8_t));
  sha1.Update(reinterpret_cast<const std::uint8_t*>(&value), 8);
}

void MemoryFingerprint_CryptoPP_SHA1::updateExpr(ref<Expr> expr) {
  std::string str;
  llvm::raw_string_ostream ostream(str);
  expr->print(ostream);
  ostream.flush();
  sha1.Update(reinterpret_cast<const byte*>(str.c_str()), str.size());
}

void MemoryFingerprint_CryptoPP_SHA1::generateHash() {
  sha1.Final(buffer.data());
}

void MemoryFingerprint_CryptoPP_SHA1::clearHash() {
  // not really necessary as Final() already calls this internally
  sha1.Restart();
}


/* MemoryFingerprint_CryptoPP_BLAKE2b */

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint8(const std::uint8_t value) {
  static_assert(sizeof(byte) == sizeof(std::uint8_t));
  blake2b.Update(&value, 1);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint64(const std::uint64_t value) {
  static_assert(sizeof(byte) == sizeof(std::uint8_t));
  blake2b.Update(reinterpret_cast<const std::uint8_t*>(&value), 8);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateExpr(ref<Expr> expr) {
  std::string str;
  llvm::raw_string_ostream ostream(str);
  expr->print(ostream);
  ostream.flush();
  blake2b.Update(reinterpret_cast<const byte*>(str.c_str()), str.size());
}

void MemoryFingerprint_CryptoPP_BLAKE2b::generateHash() {
  blake2b.Final(buffer.data());
}

void MemoryFingerprint_CryptoPP_BLAKE2b::clearHash() {
  // not really necessary as Final() already calls this internally
  blake2b.Restart();
}

}
