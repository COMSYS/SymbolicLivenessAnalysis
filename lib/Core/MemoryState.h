#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "DebugInfiniteLoopDetection.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/SHA1.h"

#include <array>
#include <cstdint>

namespace klee {

class MemoryState {

private:
  std::array<std::uint8_t, 20> stateHash = {};
  std::array<std::uint8_t, 20> stackFrameHash = {};
  MemoryTrace trace;
  bool allocasInCurrentStackFrame = false;

  static std::string Sha1String(const std::array<std::uint8_t, 20> &buffer);

  static std::string ExprString(ref<Expr> expr);
  void addUint64ToHash(util::SHA1 &sha1, const std::uint64_t address);
  void addConstantExprToHash(util::SHA1 &sha1, const ConstantExpr &expr);
  void addExprStringToHash(util::SHA1 &sha1, ref<Expr> expr);
  void xorStateHash(const std::array<std::uint8_t, 20> &hash);
  void xorStackFrameHash(const std::array<std::uint8_t, 20> &hash);

public:
  MemoryState() = default;
  MemoryState(const MemoryState &) = default;

  void registerAllocation(const MemoryObject &mo);
  void registerDeallocation(const MemoryObject &mo) {
    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: DEALLOCATION\n";
    }

    registerAllocation(mo);
  }

  void registerWrite(ref<Expr> base, const MemoryObject &mo,
                     const ObjectState &os);
  void unregisterWrite(ref<Expr> base, const MemoryObject &mo,
                       const ObjectState &os) {
    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: UNREGISTER\n";
    }

    registerWrite(base, mo, os);
  }

  void registerConstraint(ref<Expr> condition);

  void registerLocal(const KInstruction *target, ref<Expr> value);
  void unregisterLocal(const KInstruction *target, ref<Expr> value) {
    if (optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: UNREGISTER\n";
    }

    registerLocal(target, value);
  }

  void registerArgument(const KFunction *kf, unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  void registerBasicBlock(const KInstruction *inst);

  bool findLoop();

  void registerPushFrame();
  void registerPopFrame();
};
}

#endif
