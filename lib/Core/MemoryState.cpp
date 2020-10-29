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

void MemoryState::registerFunctionCall(llvm::Function *f,
                                       std::vector<ref<Expr>> &arguments) {
  if (globalDisableMemoryState) {
    // we only check for global disable and not for library or listed functions
    // as we assume that those will not call any input or output functions
    return;
  }

  if (std::binary_search(inputFunctionsBlacklist.begin(),
                         inputFunctionsBlacklist.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: blacklisted input function call to "
                   << f->getName() << "()\n";
    }
    clearEverything();
    enterListedFunction(f);
  } else if (std::binary_search(outputFunctionsWhitelist.begin(),
                                outputFunctionsWhitelist.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterListedFunction(f);
  } else if (std::binary_search(libraryFunctionsList.begin(),
                                libraryFunctionsList.end(), f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: library function call to " << f->getName()
                   << "()\n";
    }
    enterLibraryFunction(f);
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
          enterMemoryFunction(f, constAddr, mo, os, count);
        }
      }
    }
  }
}

void MemoryState::registerFunctionRet(llvm::Function *f) {
  if (isInListedFunction(f)) {
    leaveListedFunction();
  } else if (isInLibraryFunction(f)) {
    leaveLibraryFunction();
  } else if (isInMemoryFunction(f)) {
    leaveMemoryFunction();
  }
}

void MemoryState::clearEverything() {
  trace.clear();
  fingerprint.discardEverything();
}

void MemoryState::registerExternalFunctionCall() {
  if (listedFunction.entered) {
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
  if (disableMemoryState && !libraryFunction.entered) {
    // in case of library functions, we need to record changes to global memory
    // and previous allocas
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
  if (disableMemoryState) {
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

  if (libraryFunction.entered) {
    if (isLocal && externalDelta == nullptr) {
      // change is only to be made to allocaDelta of current stack frame
      return;
    }
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

bool MemoryState::enterListedFunction(llvm::Function *f) {
  if (listedFunction.entered) {
    // we can only enter one listed function at a time
    // (we do not need to register additional functions calls by the entered
    // function such as printf)
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering listed function: " << f->getName()
                 << "\n";
  }

  listedFunction.entered = true;
  listedFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::leaveListedFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving listed function: "
                 << listedFunction.function->getName() << "\n";
  }

  listedFunction.entered = false;
  listedFunction.function = nullptr;

  updateDisableMemoryState();
}

bool MemoryState::isInListedFunction(llvm::Function *f) const {
  return (listedFunction.entered && f == listedFunction.function);
}

bool MemoryState::enterLibraryFunction(llvm::Function *f) {
  if (libraryFunction.entered) {
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering library function: " << f->getName()
                 << "\n";
  }

  libraryFunction.entered = true;
  libraryFunction.function = f;

  updateDisableMemoryState();

  return true;
}

void MemoryState::leaveLibraryFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving library function: "
                 << libraryFunction.function->getName() << "\n";
  }

  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  updateDisableMemoryState();
}

bool MemoryState::isInLibraryFunction(llvm::Function *f) const {
  return (libraryFunction.entered && f == libraryFunction.function);
}

bool MemoryState::enterMemoryFunction(llvm::Function *f,
                                      ref<ConstantExpr> address,
                                      const MemoryObject *mo,
                                      const ObjectState *os,
                                      std::size_t bytes) {
  if (memoryFunction.entered) {
    // we can only enter one library function at a time
    klee_warning_once(f, "already entered a memory function");
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering memory function: " << f->getName()
                 << "\n";
  }

  unregisterWrite(address, *mo, *os, bytes);

  memoryFunction.entered = true;
  memoryFunction.function = f;
  memoryFunction.address = address;
  memoryFunction.mo = mo;
  memoryFunction.bytes = bytes;

  updateDisableMemoryState();

  return true;
}

bool MemoryState::isInMemoryFunction(llvm::Function *f) const {
  return (memoryFunction.entered && f == memoryFunction.function);
}

void MemoryState::leaveMemoryFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving memory function: "
                 << memoryFunction.function->getName() << "\n";
  }

  const MemoryObject *mo = memoryFunction.mo;
  const ObjectState *os = executionState->addressSpace.findObject(mo);

  memoryFunction.entered = false;
  memoryFunction.function = nullptr;

  updateDisableMemoryState();

  registerWrite(memoryFunction.address, *mo, *os, memoryFunction.bytes);
}

void MemoryState::registerPushFrame(const llvm::Function *function) {
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

void MemoryState::registerPopFrame(const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

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
