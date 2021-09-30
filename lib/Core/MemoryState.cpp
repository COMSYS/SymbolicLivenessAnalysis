#include "MemoryState.h"

#include "AddressSpace.h"
#include "ExecutionState.h"
#include "Memory.h"

#include "klee/Module/Cell.h"
#include "klee/Module/InstructionInfoTable.h"
#include "klee/Module/KInstruction.h"
#include "klee/Module/KModule.h"
#include "klee/Support/ErrorHandling.h"
#include "klee/Support/InfiniteLoopDetectionFlags.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

KModule *MemoryState::kmodule = nullptr;
std::vector<llvm::Function *> MemoryState::outputFunctionsWhitelist;
std::vector<llvm::Function *> MemoryState::inputFunctionsBlacklist;
std::vector<llvm::Function *> MemoryState::libraryFunctionsList;
std::vector<llvm::Function *> MemoryState::memoryFunctionsList;

void MemoryState::setKModule(KModule *_kmodule) {
  if (kmodule != nullptr)
    return;

  // whitelist: output functions
  const char *outputFunctions[] = {
      // stdio.h
      "fflush", "fputc", "putc", "fputwc", "putwc", "fputs", "fputws",
      "putchar", "putwchar", "puts", "printf", "fprintf", "sprintf", "snprintf",
      "wprintf", "fwprintf", "swprintf", "vprintf", "vfprintf", "vsprintf",
      "vsnprintf", "vwprintf", "vfwprintf", "vswprintf", "perror",

      // POSIX
      "write"};

  // blacklist: input functions
  const char *inputFunctions[] = {
      // stdio.h
      "fopen", "freopen", "fread", "fgetc", "getc", "fgetwc", "getwc", "fgets",
      "fgetws", "getchar", "getwchar", "gets", "scanf", "fscanf", "sscanf",
      "wscanf", "fwscanf", "swscanf", "vscanf", "vfscanf", "vsscanf", "vwscanf",
      "vfwscanf", "vswscanf", "ftell", "ftello", "fseek", "fseeko", "fgetpos",
      "fsetpos", "feof", "ferror",

      // POSIX
      "open", "creat", "socket", "accept", "socketpair", "pipe", "opendir",
      "dirfd", "fileno", "read", "readv", "pread", "recv", "recvmsg", "lseek",
      "fstat", "fdopen", "ftruncate", "fsync", "fdatasync", "fstatvfs",
      "select", "pselect", "poll", "epoll", "flock", "fcntl", "lockf",

      // dirent.h
      "opendir", "readdir", "readdir_r", "telldir",

      // future POSIX
      "openat", "faccessat", "fstatat", "readlinkat", "fdopendir",

      // LFS
      "fgetpos64", "fopen64", "freopen64", "fseeko64", "fsetpos64", "ftello64",
      "fstat64", "lstat64", "open64", "readdir64", "stat64"};

  // library function that might use heavy loops that we do not want to inspect
  const char *libraryFunctions[] = {
      // string.h
      "memcmp", "memchr", "strcpy", "strncpy", "strcat", "strncat", "strxfrm",
      "strlen", "strcmp", "strncmp", "strcoll", "strchr", "strrchr", "strspn",
      "strcspn", "strpbrk", "strstr",

      // wchar.h
      "wmemcmp", "wmemchr", "wcscpy", "wcsncpy", "wcscat", "wcsncat", "wcsxfrm",
      "wcslen", "wcscmp", "wcsncmp", "wcscoll", "wcschr", "wcsrchr", "wcsspn",
      "wcscspn", "wcspbrk", "wcsstr",

      // GNU
      "mempcpy",

      // POSIX
      "strdup", "strcasecmp", "memccpy", "bzero"};

  // library functions with signature (*dest, _, count) that modify the memory
  // starting from dest for count bytes
  const char *memoryFunctions[] = {"memset",  "memcpy",  "memmove",
                                   "wmemset", "wmemcpy", "wmemmove"};

  initializeFunctionList(_kmodule, outputFunctions, outputFunctionsWhitelist);
  initializeFunctionList(_kmodule, inputFunctions, inputFunctionsBlacklist);
  initializeFunctionList(_kmodule, libraryFunctions, libraryFunctionsList);
  initializeFunctionList(_kmodule, memoryFunctions, memoryFunctionsList);

  kmodule = _kmodule;
}

template <std::size_t array_size>
void MemoryState::initializeFunctionList(KModule *_kmodule,
                                         const char *(&functions)[array_size],
                                         std::vector<llvm::Function *> &list) {
  std::vector<llvm::Function *> tmp;
  for (const char *name : functions) {
    llvm::Function *f = _kmodule->module->getFunction(name);
    if (f == nullptr) {
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: could not find function in module: "
                     << name << "\n";
      }
    } else {
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: found function in module: " << name
                     << "\n";
      }
      tmp.emplace_back(f);
    }
  }
  std::sort(tmp.begin(), tmp.end());
  list = std::move(tmp);
}

void MemoryState::registerFunctionCall(const llvm::Function *f,
                                       std::size_t stackFrame,
                                       std::vector<ref<Expr>> &arguments) {
  if (globalDisableMemoryState) {
    // we only check for global disable and not for shadowed functions
    // as we assume that those will not call any input functions
    return;
  }

  if (std::binary_search(inputFunctionsBlacklist.begin(),
                         inputFunctionsBlacklist.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: blacklisted input function call to "
                   << f->getName() << "()\n";
    }
    clearEverything();
    enterShadowFunction(f, stackFrame);
  } else if (std::binary_search(outputFunctionsWhitelist.begin(),
                                outputFunctionsWhitelist.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterShadowFunction(f, stackFrame);
  } else if (std::binary_search(libraryFunctionsList.begin(),
                                libraryFunctionsList.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: library function call to " << f->getName()
                   << "()\n";
    }
    // we need to register changes to global memory
    enterShadowFunction(f, stackFrame, {}, true);
  } else if (std::binary_search(memoryFunctionsList.begin(),
                                memoryFunctionsList.end(), f)) {
    ConstantExpr *constAddr = dyn_cast<ConstantExpr>(arguments[0]);
    ConstantExpr *constSize = dyn_cast<ConstantExpr>(arguments[2]);

    if (constAddr && constSize) {
      ObjectPair op;
      bool success;
      success = executionState->addressSpace.resolveOne(constAddr, op);

      if (success) {
        const MemoryObject *mo = op.first;
        const ObjectState *os = op.second;

        std::uint64_t count = constSize->getZExtValue(64);
        std::uint64_t addr = constAddr->getZExtValue(64);
        std::uint64_t offset = addr - mo->address;

        if (mo->size >= offset + count) {
          if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
            llvm::errs() << "MemoryState: memory function call to "
                         << f->getName() << "()\n";
          }
          unregisterWrite(constAddr, *mo, *os, count);
          enterShadowFunction(
              f, stackFrame,
              [constAddr, mo, count](MemoryState &ms) {
                const auto *os = ms.executionState->addressSpace.findObject(mo);
                ms.registerWrite(constAddr, *mo, *os, count);
              },
              false);
        }
      }
    }
  }
}

void MemoryState::registerFunctionRet(const llvm::Function *f,
                                      std::size_t stackFrame) {
  if (f == shadowedFunction && stackFrame == shadowedStackFrame) {
    leaveShadowFunction(f, stackFrame);
  }
}

void MemoryState::clearEverything() {
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerExternalFunctionCall() {
  if (shadowedFunction) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: external function call\n";
  }

  // it is unknown whether control flow is changed by an external function, so
  // we cannot detect infinite loop iterations that started before this call
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerWrite(ref<Expr> address, const MemoryObject &mo,
                                const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !registerGlobalsInShadow) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: registering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address " << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::unregisterWrite(ref<Expr> address, const MemoryObject &mo,
                                  const ObjectState &os, std::size_t bytes) {
  if (disableMemoryState && !registerGlobalsInShadow) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    ref<ConstantExpr> base = mo.getBaseExpr();
    llvm::errs() << "MemoryState: unregistering "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address " << ExprString(base) << "\n";
  }

  applyWriteFragment(address, mo, os, bytes);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::applyWriteFragment(ref<Expr> address, const MemoryObject &mo,
                                     const ObjectState &os, std::size_t bytes) {
  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  bool isLocal = false;
  MemoryFingerprint::fingerprint_t *externalDelta = nullptr;

  if (mo.isLocal) {
    isLocal = true;
    if (!trace.isAllocaAllocationInCurrentStackFrame(*executionState, mo)) {
      externalDelta = trace.getPreviousAllocaDelta(*executionState, mo);
      if (externalDelta == nullptr) {
        // allocation was made in previous stack frame that is not available
        // anymore due to an external function call
        isLocal = false;
      }
    }
  }

  if (shadowedFunction && isLocal && externalDelta == nullptr) {
    // change is only to be made to allocaDelta of current stack frame
    return;
  }

  // optimization for concrete offsets: only hash changed indices
  if (concreteOffset) {
    begin = concreteOffset->getZExtValue(64);
    if ((begin + bytes) < os.size) {
      end = begin + bytes;
    }
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  for (std::uint64_t i = begin; i < end; i++) {
    std::uint64_t baseAddress = base->getZExtValue(64);

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "[+" << i << "] ";
    }

    // add value of byte at offset to fingerprint
    ref<Expr> valExpr = os.read8(i);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(valExpr)) {
      // concrete value
      fingerprint.updateUint8(1);

      // add base address + offset to fingerprint
      fingerprint.updateUint64(baseAddress + i);

      std::uint8_t value = constant->getZExtValue(8);
      fingerprint.updateUint8(value);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "0x";
        llvm::errs().write_hex((int)value);
      }
    } else {
      // symbolic value
      fingerprint.updateUint8(2);

      // add base address + offset to fingerprint
      fingerprint.updateUint64(baseAddress + i);

      fingerprint.updateExpr(valExpr);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << ExprString(valExpr);
      }
    }

    if (isLocal) {
      if (externalDelta == nullptr) {
        // current stack frame
        fingerprint.applyToFingerprintAllocaDelta();
      } else {
        // previous stack frame that is still available
        fingerprint.applyToFingerprintAllocaDelta(*externalDelta);
      }
    } else {
      fingerprint.applyToFingerprint();
    }

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      if (i % 10 == 9) {
        llvm::errs() << "\n";
      } else {
        llvm::errs() << " ";
      }
    }
  }
}

void MemoryState::registerBasicBlock(const llvm::BasicBlock &bb) {
  if (disableMemoryState) {
    return;
  }

  // apply live locals to copy of fingerprint
  auto copy = fingerprint;
  KFunction *kf = getKFunction(&bb);
  for (auto &index : kf->getLiveLocals(bb).args) {
    ref<Expr> value = getArgumentValue(kf, index);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
      // concrete value
      copy.updateUint8(5);
      copy.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
      copy.updateUint64(index);
      copy.updateConstantExpr(*constant);
    } else {
      // symbolic value
      copy.updateUint8(6);
      copy.updateUint64(reinterpret_cast<std::uintptr_t>(kf));
      copy.updateUint64(index);
      copy.updateExpr(value);
    }
    copy.applyToFingerprintLocalDelta();

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Add live argument " << index
                   << " to function " << kf->function->getName() << " = "
                   << ExprString(value)
                   << " [fingerprint: " << copy.getFingerprintAsString()
                   << "]\n";
    }
  }
  for (auto &ki : kf->getLiveLocals(bb).inst) {
    ref<Expr> value = getLocalValue(ki);
    if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
      // concrete value
      copy.updateUint8(3);
      copy.updateUint64(reinterpret_cast<std::uintptr_t>(ki->inst));
      copy.updateConstantExpr(*constant);
    } else {
      // symbolic value
      copy.updateUint8(4);
      copy.updateUint64(reinterpret_cast<std::uintptr_t>(ki->inst));
      copy.updateExpr(value);
    }
    copy.applyToFingerprintLocalDelta();

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: Add live local %" << ki->inst->getName()
                   << " = " << ExprString(value)
                   << " [fingerprint: " << copy.getFingerprintAsString()
                   << "]\n";
    }
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: Register BasicBlock " << bb.getName()
                 << " [fingerprint: " << copy.getFingerprintAsString() << "]\n";
  }

  const KInstruction *inst = getKInstruction(&bb);
  trace.registerBasicBlock(inst, copy.getFingerprint());
}

KInstruction *MemoryState::getKInstruction(const llvm::BasicBlock *bb) const {
  KFunction *kf = getKFunction(bb);
  unsigned entry = kf->basicBlockEntry[const_cast<llvm::BasicBlock *>(bb)];
  return kf->instructions[entry];
}

KFunction *MemoryState::getKFunction(const llvm::BasicBlock *bb) const {
  llvm::Function *f = const_cast<llvm::Function *>(bb->getParent());
  assert(f != nullptr && "failed to retrieve Function for BasicBlock");
  KFunction *kf = kmodule->functionMap[f];
  assert(kf != nullptr && "failed to retrieve KFunction");
  return kf;
}

ref<Expr> MemoryState::getArgumentValue(const KFunction *kf,
                                        unsigned index) const {
  return executionState->stack.back().locals[kf->getArgRegister(index)].value;
}

ref<Expr> MemoryState::getLocalValue(const KInstruction *kinst) const {
  return executionState->stack.back().locals[kinst->dest].value;
}

bool MemoryState::findInfiniteLoopInFunction() const {
  if (disableMemoryState) {
    // we do not want to find infinite loops in library or output functions
    return false;
  }

  bool result = trace.findInfiniteLoopInFunction();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    if (result) {
      trace.dumpTrace();
    }
  }

  return result;
}

bool MemoryState::findInfiniteRecursion() const {
  if (disableMemoryState) {
    // we do not want to find infinite loops in library or output functions
    return false;
  }

  bool result = trace.findInfiniteRecursion();

  if (DebugInfiniteLoopDetection.isSet(STDERR_TRACE)) {
    if (result) {
      trace.dumpTrace();
    }
  }

  return result;
}

void MemoryState::enterShadowFunction(
    const llvm::Function *f, std::size_t stackFrame,
    std::function<void(MemoryState &)> &&callback, bool registerGlobals) {
  if (shadowedFunction) {
    return; // only one function can be entered at a time
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering shadowed function: " << f->getName()
                 << "\n";
  }

  shadowedFunction = f;
  shadowedStackFrame = stackFrame;
  shadowCallback = std::move(callback);
  registerGlobalsInShadow = registerGlobals;
  updateDisableMemoryState();
}

void MemoryState::leaveShadowFunction(const llvm::Function *f,
                                      std::size_t stackFrame) {
  assert(f == shadowedFunction);
  assert(stackFrame == shadowedStackFrame);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving shadowed function: "
                 << shadowedFunction->getName() << "\n";
  }

  if (shadowCallback) {
    shadowCallback(*this);
    shadowCallback = {};
  }
  shadowedFunction = nullptr;
  shadowedStackFrame = 0;
  registerGlobalsInShadow = false;
  updateDisableMemoryState();
}

void MemoryState::registerPushFrame(const llvm::Function *function,
                                    std::size_t stackFrame) {

  if (disableMemoryState && stackFrame != shadowedStackFrame) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME (" << function->getName()
                 << ")\n";
  }

  trace.registerEndOfStackFrame(function, fingerprint.getLocalDelta(),
                                fingerprint.getAllocaDelta());

  // make locals and arguments "invisible"
  fingerprint.discardLocalDelta();
  // record alloca allocations and changes for this new stack frame separately
  // from those of other stack frames (without removing the latter)
  fingerprint.applyAndResetAllocaDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(std::size_t stackFrame,
                                   const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

  if (disableMemoryState && stackFrame != shadowedStackFrame) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME ("
                 << returningBB->getParent()->getName() << " returning to "
                 << callerBB->getParent()->getName() << ")\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    MemoryTrace::StackFrameEntry sfe = trace.popFrame();

    // remove locals and arguments of stack frame that is to be left
    fingerprint.discardLocalDelta();
    // set local delta to fingerprint local delta of stack frame that is to be
    // entered to make locals and arguments "visible" again
    fingerprint.setLocalDelta(sfe.fingerprintLocalDelta);

    // remove allocas allocated in stack frame that is to be left
    fingerprint.discardAllocaDelta();
    // initialize alloca delta with previous fingerprint alloca delta which
    // contains information on allocas allocated in the stack frame that is to
    // be entered
    fingerprint.setAllocaDeltaToPreviousValue(sfe.fingerprintAllocaDelta);

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "reapplying local delta: "
                   << fingerprint.getLocalDeltaAsString()
                   << "\nreapplying alloca delta: "
                   << fingerprint.getAllocaDeltaAsString()
                   << "\nFingerprint: " << fingerprint.getFingerprintAsString()
                   << "\n";
    }
  } else {
    // no stackframe left to pop

    // We need to clear the trace to prevent mixing stack frames
    trace.clear();
    fingerprint.discardEverything();

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "no stackframe left in trace\n";
    }
  }
}

std::string MemoryState::ExprString(ref<Expr> expr) {
  std::string result;
  llvm::raw_string_ostream ostream(result);
  expr->print(ostream);
  ostream.flush();
  return result;
}

} // namespace klee
