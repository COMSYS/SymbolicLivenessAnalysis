#include "MemoryFingerprint.h"

#include "klee/Internal/Module/KModule.h"
#include "klee/util/ExprPPrinter.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3,5)
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#else
#include "llvm/DebugInfo.h"
#include "llvm/Support/DebugLoc.h"
#endif
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"

namespace klee {

/* MemoryFingerprint_CryptoPP_BLAKE2b */

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint8(const std::uint8_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
  blake2b.Update(&value, 1);
}

void MemoryFingerprint_CryptoPP_BLAKE2b::updateUint64(const std::uint64_t value) {
  static_assert(sizeof(CryptoPP::byte) == sizeof(std::uint8_t));
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
  hash.Update(reinterpret_cast<const CryptoPP::byte*>(ptr), size);
  pos += size;
}


/* MemoryFingerprint_Dummy */

void MemoryFingerprint_Dummy::updateUint8(const std::uint8_t value) {
  if (first) {
    first = false;
  } else {
    current += " ";
  }
  current += std::to_string(static_cast<unsigned>(value));
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
  size_t writes = 0;

  // show individual memory operations in detail: writes (per byte)
  bool showMemoryOperations = false;

  result << "{";

  for (auto it = fingerprint.begin(); it != fingerprint.end(); ++it) {
    std::istringstream item(*it);
    int id;
    item >> id;
    bool output = false;
    switch (id) {
      case 1:
      case 2:
        if (showMemoryOperations) {
          std::uint64_t addr;
          item >> addr;

          result << "Write: ";
          result << addr;
          result << " =";

          if (id == 2) {
            std::string value;
            for (std::string line; std::getline(item, line); ) {
              result << line;
            }
          } else {
            unsigned value;
            item >> value;
            result << " " << value;
          }
          output = true;
        }
        writes++;
        break;
      case 3:
      case 4: {
        std::uintptr_t ptr;

        item >> ptr;
        llvm::Instruction *inst = reinterpret_cast<llvm::Instruction *>(ptr);

        const llvm::DebugLoc &dl = inst->getDebugLoc();
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
        std::string filename = "";
        auto *scope = cast_or_null<llvm::DIScope>(dl.getScope());
        if (scope) {
          filename = scope->getFilename();
        }
#else
        llvm::BasicBlock *bb = inst->getParent();
        llvm::LLVMContext &ctx = bb->getContext();
        llvm::DIScope scope = llvm::DIScope(dl.getScope(ctx));
        auto filename = scope.getFilename();
#endif


        result << "Local: ";
        if (inst->hasName()) {
          result << '%' << inst->getName();
        } else {
          result << "unnamed(@"
                 << reinterpret_cast<std::uintptr_t>(inst)
                 << ')';
        }

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 6)
        if (dl) {
#else
        if (!dl.isUnknown()) {
#endif
          result << " (" << filename;
          result << ":" << dl.getLine();
          result << ")";
        }
        result << " =";

        for (std::string line; std::getline(item, line); ) {
          result << line;
        }
        output = true;
        break;
      }
      case 5:
      case 6: {
        std::uintptr_t ptr;
        std::size_t argumentIndex;

        item >> ptr;
        KFunction *kf = reinterpret_cast<KFunction *>(ptr);
        item >> argumentIndex;
        std::size_t total = kf->function->arg_size();
        result << "Argument: ";
        result << kf->function->getName() << "(";
        for (std::size_t i = 0; i < total; ++i) {
          if (argumentIndex == i) {
            for (std::string line; std::getline(item, line); ) {
              result << line;
            }
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

  if (!showMemoryOperations) {
    result << "} + " << writes << " write(s)";
  } else {
    result << "}";
  }

  return result.str();
}

}
