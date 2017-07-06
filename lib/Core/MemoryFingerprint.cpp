#include "MemoryFingerprint.h"

#include "klee/util/ExprPPrinter.h"

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
  MemoryFingerprint_ostream<util::SHA1> OS(sha1);
  ExprPPrinter::printSingleExpr(OS, expr);
}

void MemoryFingerprint_SHA1::generateHash() {
  sha1.store_result(buffer.begin(), buffer.end());
}

void MemoryFingerprint_SHA1::clearHash() {
  sha1.reset();
}

/* MemoryFingerprint_ostream<util::SHA1> */

template<>
void MemoryFingerprint_ostream<util::SHA1>::write_impl(const char *ptr,
                                                       std::size_t size) {
  static_assert(sizeof(::std::uint8_t) == sizeof(const char));
  hash.update_range(ptr, ptr+size);
  pos += size;
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
  MemoryFingerprint_ostream<CryptoPP::SHA1> OS(sha1);
  ExprPPrinter::printSingleExpr(OS, expr);
}

void MemoryFingerprint_CryptoPP_SHA1::generateHash() {
  sha1.Final(buffer.data());
}

void MemoryFingerprint_CryptoPP_SHA1::clearHash() {
  // not really necessary as Final() already calls this internally
  sha1.Restart();
}

/* MemoryFingerprint_ostream<CryptoPP::SHA1> */

template<>
void MemoryFingerprint_ostream<CryptoPP::SHA1>::write_impl(const char *ptr,
                                                           std::size_t size) {
  hash.Update(reinterpret_cast<const byte*>(ptr), size);
  pos += size;
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
  MemoryFingerprint_ostream<CryptoPP::BLAKE2b> OS(blake2b);
  ExprPPrinter::printSingleExpr(OS, expr);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::generateHash() {
  blake2b.Final(buffer.data());
}

void MemoryFingerprint_CryptoPP_BLAKE2b::clearHash() {
  // not really necessary as Final() already calls this internally
  blake2b.Restart();
}

/* MemoryFingerprint_ostream<CryptoPP::BLAKE2b> */

template<>
void MemoryFingerprint_ostream<CryptoPP::BLAKE2b>::write_impl(const char *ptr,
                                                              std::size_t size) {
  hash.Update(reinterpret_cast<const byte*>(ptr), size);
  pos += size;
}


/* MemoryFingerprint_Dummy */

void MemoryFingerprint_Dummy::updateUint8(const std::uint8_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(value);
}

void MemoryFingerprint_Dummy::updateUint64(const std::uint64_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(value);
}

void MemoryFingerprint_Dummy::updateExpr(ref<Expr> expr) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  llvm::raw_string_ostream ostream(current);
  ExprPPrinter::printSingleExpr(ostream, expr);
  ostream.flush();
}

void MemoryFingerprint_Dummy::generateHash() {
  buffer.insert(current);
}

void MemoryFingerprint_Dummy::clearHash() {
  current = "";
  buffer.clear();
  first = true;
}



}
