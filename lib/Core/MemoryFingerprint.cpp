#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KModule.h"
#include "klee/util/ExprPPrinter.h"

#include "llvm/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/DebugLoc.h"
#include "llvm/Support/raw_ostream.h"

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

std::string MemoryFingerprint_Dummy::toString_impl(MemoryFingerprintT::dummy_t fingerprint) {
  std::string result_str;
  llvm::raw_string_ostream result(result_str);
  size_t allocations = 0;
  size_t writes = 0;

  result << "{";

  for (auto it = fingerprint.begin(); it != fingerprint.end(); ++it) {
    std::istringstream item(*it);
    int id;
    item >> id;
    bool output = false;
    switch (id) {
      case 1:
        allocations++;
        break;
      case 2:
        writes++;
        break;
      case 3: {
        std::intptr_t ptr;
        std::string value;

        item >> ptr;
        llvm::Instruction *inst = reinterpret_cast<llvm::Instruction *>(ptr);

        llvm::DebugLoc dl = inst->getDebugLoc();
        llvm::BasicBlock *bb = inst->getParent();
        llvm::LLVMContext &ctx = bb->getContext();
        llvm::DIScope scope = llvm::DIScope(dl.getScope(ctx));

        result << "Local: ";
        result << inst->getName();
        result << " (" << scope.getFilename();
        result << ":" << dl.getLine();
        result << ")";
        std::getline(item, value);
        result << " =" << value;
        output = true;
        break;
      }
      case 4: {
        result << "Argument: ";
        std::intptr_t ptr;
        std::size_t argumentIndex;
        std::uint64_t value;

        item >> ptr;
        KFunction *kf = reinterpret_cast<KFunction *>(ptr);
        item >> argumentIndex;
        std::size_t total = kf->function->arg_size();
        item >> value;
        result << kf->function->getName() << "(";
        for (std::size_t i = 0; i < total; ++i) {
          if (argumentIndex == i) {
            result << value;
          } else {
            result << "?";
          }
          if (i != total - 1) {
            result << ", ";
          }
        }
        result << ")";
        output = true;
        break;
      }
      default:
        result << *it;
        output = true;
    }
    if (std::next(it) != fingerprint.end() && output) {
      result << ", ";
    }
  }

  result << "} + " << allocations << " allocation(s) + " << writes << " write(s)";

  return result.str();
}



}
