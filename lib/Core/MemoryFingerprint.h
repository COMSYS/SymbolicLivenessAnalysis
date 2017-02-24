#ifndef KLEE_MEMORYFINGERPRINT_H
#define KLEE_MEMORYFINGERPRINT_H

#include "klee/Expr.h"
#include "klee/Internal/Support/SHA1.h"

// stub for typeid to use CryptoPP without RTTI
template<typename T> const std::type_info& FakeTypeID(void) {
    assert(0 && "CryptoPP tries to use typeid()");
}
#define typeid(a) FakeTypeID<a>()
#include <cryptopp/sha.h>
#include <cryptopp/blake2.h>
#undef typeid

#include <array>
#include <string>
#include <iomanip>

namespace klee {

class MemoryFingerprint_SHA1;
class MemoryFingerprint_CryptoPP_SHA1;
class MemoryFingerprint_CryptoPP_BLAKE2b;

// Set default implementation
using MemoryFingerprint = MemoryFingerprint_CryptoPP_BLAKE2b;

template<typename Derived, size_t hashSize>
class MemoryFingerprintT {

public:
  typedef std::array<std::uint8_t, hashSize> fingerprint_t;

private:
  Derived& getDerived() {
    return *(static_cast<Derived*>(this));
  }

  fingerprint_t fingerprint = {};
  fingerprint_t fingerprintDelta = {};

  inline void executeXOR(fingerprint_t &dst, fingerprint_t &src) {
    for (std::size_t i = 0; i < hashSize; ++i) {
      dst[i] ^= src[i];
    }
  }

protected:
  // buffer that holds current hash after calling generateHash()
  fingerprint_t buffer = {};


public:
  MemoryFingerprintT() = default;
  MemoryFingerprintT(const MemoryFingerprintT &) = default;

  void applyToFingerprint() {
    getDerived().generateHash();
    executeXOR(fingerprint, buffer);
    getDerived().clearHash();
  }

  void applyToFingerprintAndDelta() {
    getDerived().generateHash();
    executeXOR(fingerprint, buffer);
    executeXOR(fingerprintDelta, buffer);
    getDerived().clearHash();
  }

  fingerprint_t getFingerprint() {
    return fingerprint;
  }

  fingerprint_t getDelta() {
    return fingerprintDelta;
  }

  void removeDelta() {
    executeXOR(fingerprint, fingerprintDelta);
    resetDelta();
  }

  void resetDelta() {
    fingerprintDelta = {};
  }

  void applyDelta(fingerprint_t &delta) {
    executeXOR(fingerprint, delta);
    executeXOR(fingerprintDelta, delta);
  }

  void updateConstantExpr(const ConstantExpr &expr) {
    if (expr.getWidth() <= 64) {
      std::uint64_t constantValue = expr.getZExtValue(64);
      getDerived().updateUint64(constantValue);
    } else {
      const llvm::APInt &value = expr.getAPValue();
      for (std::size_t i = 0; i != value.getNumWords(); i++) {
        std::uint64_t word = value.getRawData()[i];
        getDerived().updateUint64(word);
      }
    }
  }

  static std::string toString(const fingerprint_t &fingerprint) {
    std::stringstream result;
    for (auto iter = fingerprint.cbegin(); iter != fingerprint.cend(); ++iter) {
      result << std::hex << std::setfill('0') << std::setw(2);
      result << static_cast<unsigned int>(*iter);
    }
    return result.str();
  }

  std::string getFingerprintAsString() {
    return toString(fingerprint);
  }

  std::string getDeltaAsString() {
    return toString(fingerprintDelta);
  }
};


class MemoryFingerprint_SHA1 :
public MemoryFingerprintT<MemoryFingerprint_SHA1, 20> {
friend class MemoryFingerprintT<MemoryFingerprint_SHA1, 20>;
private:
  util::SHA1 sha1;
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};

class MemoryFingerprint_CryptoPP_SHA1 :
public MemoryFingerprintT<MemoryFingerprint_CryptoPP_SHA1, CryptoPP::SHA::DIGESTSIZE> {
friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_SHA1, CryptoPP::SHA::DIGESTSIZE>;
private:
  CryptoPP::SHA1 sha1;
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};

class MemoryFingerprint_CryptoPP_BLAKE2b :
public MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32> {
friend class MemoryFingerprintT<MemoryFingerprint_CryptoPP_BLAKE2b, 32>;
private:
  CryptoPP::BLAKE2b blake2b = CryptoPP::BLAKE2b(false, 32);
  void generateHash();
  void clearHash();

public:
  void updateUint8(const std::uint8_t value);
  void updateUint64(const std::uint64_t value);
  void updateExpr(ref<Expr> expr);
};

}

#endif
