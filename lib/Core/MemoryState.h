#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "DebugInfiniteLoopDetection.h"
#include "Memory.h"
#include "MemoryFingerprint.h"
#include "MemoryTrace.h"

#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

#include <cstdint>

namespace llvm {
class Function;
}

namespace klee {

class MemoryState {

private:
  MemoryFingerprint fingerprint;
  MemoryTrace trace;
  bool globalAllocationsInCurrentStackFrame = false;

  struct libraryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
    ref<ConstantExpr> address;
    const MemoryObject *mo = nullptr;
    std::size_t bytes = 0;
  } libraryFunction;

  static std::string ExprString(ref<Expr> expr);

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

  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os, std::size_t bytes);
  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os) {
    registerWrite(address, mo, os, os.size);
  }
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                       const ObjectState &os, std::size_t bytes) {
    if (!libraryFunction.entered
      && optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: UNREGISTER\n";
    }

    registerWrite(address, mo, os, bytes);
  }
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                       const ObjectState &os) {
    unregisterWrite(address, mo, os, os.size);
  }

  void registerLocal(const KInstruction *target, ref<Expr> value);
  void unregisterLocal(const KInstruction *target, ref<Expr> value) {
    if (!libraryFunction.entered
      && optionIsSet(DebugInfiniteLoopDetection, STDERR_STATE)) {
      llvm::errs() << "MemoryState: UNREGISTER\n";
    }

    registerLocal(target, value);
  }

  void registerArgument(const KFunction *kf, unsigned index, ref<Expr> value);

  void registerExternalFunctionCall();

  void registerBasicBlock(const KInstruction *inst);

  bool findLoop();

  bool enterLibraryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  bool isInLibraryFunction(llvm::Function *f);
  const MemoryObject *getLibraryFunctionMemoryObject();
  void leaveLibraryFunction(const ObjectState *os);

  void registerPushFrame();
  void registerPopFrame();
};
}

#endif
