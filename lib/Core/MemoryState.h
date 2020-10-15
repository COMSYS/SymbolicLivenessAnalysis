#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "Memory.h"
#include "MemoryFingerprint.h"
#include "MemoryTrace.h"

#include "klee/Support/InfiniteLoopDetectionFlags.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
}

namespace klee {
class ExecutionState;
class KFunction;
class KInstruction;
class KModule;

class MemoryState {

private:
  MemoryState(const MemoryState &) = default;

  MemoryFingerprint fingerprint;
  MemoryTrace trace;
  const ExecutionState *executionState = nullptr;

  // klee_enable_memory_state() is inserted by KLEE before executing the entry
  // point chosen by the user. Thus, the initialization of (uc)libc or POSIX
  // runtime are not analyzed (we asume them to be free of liveness violations).
  bool disableMemoryState = true;
  bool globalDisableMemoryState = true;

  struct listedFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
  } listedFunction;

  struct libraryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
  } libraryFunction;

  struct memoryFunction {
    bool entered = false;
    llvm::Function *function = nullptr;
    ref<ConstantExpr> address;
    const MemoryObject *mo = nullptr;
    std::size_t bytes = 0;
  } memoryFunction;

  static KModule *kmodule;
  static std::vector<llvm::Function *> outputFunctionsWhitelist;
  static std::vector<llvm::Function *> inputFunctionsBlacklist;
  static std::vector<llvm::Function *> libraryFunctionsList;
  static std::vector<llvm::Function *> memoryFunctionsList;

  template <std::size_t array_size>
  static void initializeFunctionList(KModule *kmodule,
                                     const char* (& functions)[array_size],
                                     std::vector<llvm::Function *> &list);

  static std::string ExprString(ref<Expr> expr);

  bool enterListedFunction(llvm::Function *f);
  void leaveListedFunction();
  bool isInListedFunction(llvm::Function *f) const;

  bool enterLibraryFunction(llvm::Function *f);
  void leaveLibraryFunction();
  bool isInLibraryFunction(llvm::Function *f) const;

  bool enterMemoryFunction(llvm::Function *f, ref<ConstantExpr> address,
    const MemoryObject *mo, const ObjectState *os, std::size_t bytes);
  void leaveMemoryFunction();
  bool isInMemoryFunction(llvm::Function *f) const;

  KInstruction *getKInstruction(const llvm::BasicBlock *bb) const;
  KFunction *getKFunction(const llvm::BasicBlock *bb) const;
  ref<Expr> getArgumentValue(const KFunction *kf, unsigned index) const;
  ref<Expr> getLocalValue(const KInstruction *kinst) const;

  void applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                          const ObjectState &os, std::size_t bytes);

  void updateDisableMemoryState() {
    disableMemoryState = listedFunction.entered || libraryFunction.entered || memoryFunction.entered || globalDisableMemoryState;

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: updating disableMemoryState: "
                   << "(listedFunction: " << listedFunction.entered << " || "
                   << "libraryFunction: " << libraryFunction.entered << " || "
                   << "memoryFunction: " << memoryFunction.entered << " || "
                   << "globalDisable: " << globalDisableMemoryState << ") "
                   << "= " << disableMemoryState << "\n";
    }
  }

public:
  MemoryState() = delete;
  MemoryState& operator=(const MemoryState&) = delete;

  MemoryState(const ExecutionState *state) : executionState(state) {}
  MemoryState(const MemoryState &from, const ExecutionState *state)
    : MemoryState(from) {
    executionState = state;
 }

  void disable() {
    globalDisableMemoryState = true;
    updateDisableMemoryState();
  }

  void enable() {
    globalDisableMemoryState = false;
    updateDisableMemoryState();
  }

  static std::pair<std::size_t, std::size_t> getTraceStructSizes() {
    return MemoryTrace::getTraceStructSizes();
  }

  std::pair<std::size_t, std::size_t> getTraceLength() const {
    return trace.getTraceLength();
  }

  std::pair<std::size_t, std::size_t> getTraceCapacity() const {
    return trace.getTraceCapacity();
  }

  std::size_t getNumberOfEntriesInCurrentStackFrame() const {
    return trace.getNumberOfEntriesInCurrentStackFrame();
  }

  std::size_t getFunctionListsLength() const {
    return MemoryState::outputFunctionsWhitelist.size()
        + MemoryState::inputFunctionsBlacklist.size()
        + MemoryState::libraryFunctionsList.size()
        + MemoryState::memoryFunctionsList.size();
  }

  std::size_t getFunctionListsCapacity() const {
    return MemoryState::outputFunctionsWhitelist.capacity()
        + MemoryState::inputFunctionsBlacklist.capacity()
        + MemoryState::libraryFunctionsList.capacity()
        + MemoryState::memoryFunctionsList.capacity();
  }

  static void setKModule(KModule *kmodule);

  void registerFunctionCall(llvm::Function *f,
                            std::vector<ref<Expr>> &arguments);
  void registerFunctionRet(llvm::Function *f);

  void clearEverything();

  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os, std::size_t bytes);
  void registerWrite(ref<Expr> address, const MemoryObject &mo,
                     const ObjectState &os) {
    registerWrite(address, mo, os, os.size);
  }
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                       const ObjectState &os, std::size_t bytes);
  void unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                                          const ObjectState &os) {
    unregisterWrite(address, mo, os, os.size);
  }
  void unregisterWrite(const MemoryObject &mo, const ObjectState &os) {
    unregisterWrite(mo.getBaseExpr(), mo, os, os.size);
  }

  void registerExternalFunctionCall();

  void registerBasicBlock(const llvm::BasicBlock &bb);

  bool findInfiniteLoopInFunction() const;
  bool findInfiniteRecursion() const;

  void registerPushFrame(const KFunction *kf);
  void registerPopFrame(const llvm::BasicBlock *returningBB,
                        const llvm::BasicBlock *callerBB);

  void dumpTrace(llvm::raw_ostream &out = llvm::errs()) const {
    trace.dumpTrace(out);
  }
};
}

#endif
