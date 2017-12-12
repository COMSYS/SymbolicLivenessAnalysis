#include "AddressSpace.h"
#include "InfiniteLoopDetectionFlags.h"
#include "Memory.h"
#include "MemoryState.h"

#include "klee/ExecutionState.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/ErrorHandling.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace klee {

KModule *MemoryState::listInitializedForKModule = nullptr;
std::vector<llvm::Function *> MemoryState::outputFunctionsWhitelist;
std::vector<llvm::Function *> MemoryState::inputFunctionsBlacklist;
std::vector<llvm::Function *> MemoryState::libraryFunctionsList;

void MemoryState::initializeLists(KModule *kmodule) {
  if (listInitializedForKModule != nullptr) return;

  // whitelist: output functions
  const char* outputFunctions[] = {
    // stdio.h
    "fflush", "fputc", "putc", "fputwc", "putwc", "fputs", "fputws", "putchar",
    "putwchar", "puts", "printf", "fprintf", "sprintf", "snprintf", "wprintf",
    "fwprintf", "swprintf", "vprintf", "vfprintf", "vsprintf", "vsnprintf",
    "vwprintf", "vfwprintf", "vswprintf", "perror",

    // POSIX
    "write"
  };

  // blacklist: input functions
  const char* inputFunctions[] = {
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
    "fstat64", "lstat64", "open64", "readdir64", "stat64"
  };

  // library functions
  const char* libraryFunctions[] = {
    "memset", "memcpy", "memmove"
  };

  initializeFunctionList(kmodule, outputFunctions, outputFunctionsWhitelist);
  initializeFunctionList(kmodule, inputFunctions, inputFunctionsBlacklist);
  initializeFunctionList(kmodule, libraryFunctions, libraryFunctionsList);

  listInitializedForKModule = kmodule;
}

template <std::size_t array_size>
void MemoryState::initializeFunctionList(KModule *kmodule,
                                         const char* (& functions)[array_size],
                                         std::vector<llvm::Function *> &list) {
  std::vector<llvm::Function *> tmp;
  for (const char *name : functions) {
    llvm::Function *f = kmodule->module->getFunction(name);
    if (f == nullptr) {
        llvm::errs() << "MemoryState: could not find function in module: "
                     << name << "\n";
    }
    tmp.emplace_back(f);
  }
  std::sort(tmp.begin(), tmp.end());
  list = std::move(tmp);
}



void MemoryState::registerFunctionCall(KModule *kmodule, llvm::Function *f,
                                       std::vector<ref<Expr>> &arguments) {
  if (globalDisableMemoryState) {
    // we do not need to check for library functions as we assume that those
    // will not call any input or output functions
    return;
  }

  initializeLists(kmodule);
  assert(listInitializedForKModule == kmodule && "can only handle one KModule");


  if (std::binary_search(inputFunctionsBlacklist.begin(),
                         inputFunctionsBlacklist.end(),
                         f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: blacklisted input function call to "
                   << f->getName() << "()\n";
    }
    clearEverything();
    enterListedFunction(f);
  } else if (std::binary_search(outputFunctionsWhitelist.begin(),
                                outputFunctionsWhitelist.end(),
                                f)) {
    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "MemoryState: whitelisted output function call to "
                   << f->getName() << "()\n";
    }
    enterListedFunction(f);
  } else if (std::binary_search(libraryFunctionsList.begin(),
                                libraryFunctionsList.end(),
                                f)) {
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
          enterLibraryFunction(f, constAddr, mo, os, count);
        }
      }
    }
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
  if (disableMemoryState) {
    return;
  }

  ref<ConstantExpr> base = mo.getBaseExpr();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: processing "
                 << (mo.isLocal ? "local " : "global ")
                 << "ObjectState at base address "
                 << ExprString(base) << "\n";
  }

  ref<Expr> offset = mo.getOffsetExpr(address);
  ConstantExpr *concreteOffset = dyn_cast<ConstantExpr>(offset);

  std::uint64_t begin = 0;
  std::uint64_t end = os.size;

  bool isLocal = false;
  MemoryFingerprint::fingerprint_t *externalDelta = nullptr;

  if (mo.isLocal) {
    isLocal = true;
    if (!trace.isAllocaAllocationInCurrentStackFrame(*executionState, mo)) {
      externalDelta = trace.findAllocaAllocationStackFrame(*executionState, mo);
      if (externalDelta == nullptr) {
        // allocation was made in previous stack frame that is not available
        // anymore due to an external function call
        isLocal = false;
      }
    }
  }

  if (!globalAllocationsInCurrentStackFrame && !isLocal)
    globalAllocationsInCurrentStackFrame = true;

  // optimization for concrete offsets: only hash changed indices
  if (concreteOffset) {
    begin = concreteOffset->getZExtValue(64);
    if ((begin + bytes) < os.size) {
      end = begin + bytes;
    }
  }

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
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }
}

void MemoryState::registerLocal(const KInstruction *target, ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  llvm::Instruction *inst = target->inst;
  if (inst->getParent() != basicBlockInfo.bb) {
    updateBasicBlockInfo(inst->getParent());
  }
  llvm::Value *instValue = static_cast<llvm::Value *>(inst);
  if (std::find(basicBlockInfo.liveRegisters.begin(),
                basicBlockInfo.liveRegisters.end(),
                instValue) == basicBlockInfo.liveRegisters.end())
  {
    return;
  }

  registerLocal(inst, value);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: register local %" << target->inst->getName()
                 << ": " << ExprString(value)
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}

void MemoryState::registerLocal(const llvm::Instruction *inst, ref<Expr> value)
{
  if (disableMemoryState) {
    return;
  }

  if (value.isNull()) {
    return;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "registerLocal(%" << inst->getName() << ", value)\n";
  }

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(3);
    fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(inst));
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(4);
    fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(inst));
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintLocalDelta();
}

void MemoryState::registerArgument(const KFunction *kf, unsigned index,
                                   ref<Expr> value) {
  if (disableMemoryState) {
    return;
  }

  if (ConstantExpr *constant = dyn_cast<ConstantExpr>(value)) {
    // concrete value
    fingerprint.updateUint8(5);
    fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateConstantExpr(*constant);
  } else {
    // symbolic value
    fingerprint.updateUint8(6);
    fingerprint.updateUint64(reinterpret_cast<std::intptr_t>(kf));
    fingerprint.updateUint64(index);
    fingerprint.updateExpr(value);
  }

  fingerprint.applyToFingerprintLocalDelta();

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: adding argument " << index << " to function "
                 << reinterpret_cast<std::intptr_t>(kf) << ": "
                 << ExprString(value) << "\n"
                 << " [fingerprint: " << fingerprint.getFingerprintAsString()
                 << "]\n";
  }
}


void MemoryState::updateBasicBlockInfo(const llvm::BasicBlock *bb) {
  if (basicBlockInfo.bb != bb) {
    // save previous BasicBlock to be able to determine incoming edge
    basicBlockInfo.prevbb = basicBlockInfo.bb;

    basicBlockInfo.bb = bb;
    basicBlockInfo.liveRegisters.clear();

    const llvm::Instruction *term = bb->getTerminator();
    if (llvm::MDNode *liveRegisters = term->getMetadata("liveregister.live")) {
      for (std::size_t i = 0; i < liveRegisters->getNumOperands(); ++i) {
        llvm::Value *live = liveRegisters->getOperand(i);
        basicBlockInfo.liveRegisters.push_back(live);
      }
    }
  }
}

void MemoryState::unregisterConsumedLocals(const llvm::BasicBlock *bb,
                                           bool writeToLocalDelta) {
  // This method is called after the execution of bb to clean up the local
  // delta, but also set locals to NULL within KLEE.
  // The parameter "writeToLocalDelta" can be set to false in order to omit
  // changes to a local delta that will be discarded immediately after.

  updateBasicBlockInfo(bb);

  // holds all locals that were consumed during the execution of bb
  std::vector<llvm::Value *> consumedRegs;

  const llvm::Instruction *ti = bb->getTerminator();
  llvm::MDNode *consumedRegisters = ti->getMetadata("liveregister.consumed");
  if (consumedRegisters != nullptr) {
    for (std::size_t i = 0; i < consumedRegisters->getNumOperands(); ++i) {
      llvm::Value *consumed = consumedRegisters->getOperand(i);
      consumedRegs.push_back(consumed);
      llvm::Instruction *inst = static_cast<llvm::Instruction *>(consumed);
      if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
        llvm::errs() << "MemoryState: consumed by previous basic block: "
                     << inst->getName() << "\n";
      }
      if (writeToLocalDelta) {
        // remove local from local delta
        unregisterLocal(inst);
      }
      // set local within KLEE to NULL to mark it as dead
      clearLocal(inst);
    }
  }

  // handle uses of values by PHI nodes
  for (auto it = bb->begin(), e = bb->end(); it != e; ++it) {
    const llvm::Instruction &i = *it;
    if (i.getOpcode() == llvm::Instruction::PHI) {
      for (auto use = i.op_begin(), e = i.op_end(); use != e; ++use) {
        if (std::find(basicBlockInfo.liveRegisters.begin(),
                      basicBlockInfo.liveRegisters.end(),
                      use->get()) != basicBlockInfo.liveRegisters.end())
        {
          // register is live at the end of the basic block,
          // do not unregister or clear
          continue;
        } else if (std::find(consumedRegs.begin(),
                             consumedRegs.end(),
                             use->get()) != consumedRegs.end())
        {
          // register was consumed during the execution of the basic block,
          // was already cleared and possibly unregistered
          continue;
        } else {
          // register is only consumed by PHI node
          llvm::Instruction *inst = dyn_cast<llvm::Instruction>(use->get());
          if (!inst) continue;
          if (writeToLocalDelta) {
            // remove local from local delta
            unregisterLocal(inst);
          }
          // set local within KLEE to NULL to mark it as dead
          clearLocal(inst);
        }
      }
    } else {
      // http://releases.llvm.org/3.4.2/docs/LangRef.html#phi-instruction
      // "There must be no non-phi instructions between the start of a basic
      //  block and the PHI instructions: i.e. PHI instructions must be first
      //  in a basic block."
      // Thus, we can abort as soon as we encounter a non-phi instruction.
      break;
    }
  }
}


void MemoryState::enterBasicBlock(const llvm::BasicBlock *dst,
                                  const llvm::BasicBlock *src) {
  unregisterConsumedLocals(src);
  updateBasicBlockInfo(dst);
  unregisterKilledLocals(dst, src);

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "live variables at the end of " << dst->getName() << ": {";
    for (std::size_t i = 0; i < basicBlockInfo.liveRegisters.size(); ++i) {
      llvm::Value *liveRegister = basicBlockInfo.liveRegisters.at(i);

      llvm::errs() << "%" << liveRegister->getName();
      if (i + 1 < basicBlockInfo.liveRegisters.size()) {
        llvm::errs() << ", ";
      }
    }
    llvm::errs() << "}\n";
  }
}

void MemoryState::registerEntryBasicBlock(const llvm::BasicBlock *entry) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entry BASICBLOCK" << entry->getName()
                 << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }

  // entry basic blocks have no predecessor from which we would have to clean up
  // using enterBasicBlock
  updateBasicBlockInfo(entry);

  const KInstruction *inst = getKInstruction(entry);
  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

void MemoryState::registerBasicBlock(const llvm::BasicBlock *dst,
                                     const llvm::BasicBlock *src) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: BASICBLOCK " << dst->getName()
                 << " (coming from " << src->getName() << ")"
                 << " [fingerprint: "
                 << fingerprint.getFingerprintAsString() << "]\n";
  }

  assert(dst == basicBlockInfo.bb && "basic block was not properly entered!");

  const KInstruction *inst = getKInstruction(dst);
  trace.registerBasicBlock(inst, fingerprint.getFingerprint());
}

void MemoryState::unregisterKilledLocals(const llvm::BasicBlock *dst,
                                         const llvm::BasicBlock *src) {
  // kill registers based on incoming edge (edge from src to dst)

  // liveregister.killed:
  //
  // +-- edges
  // |
  // (
  //   (precedingBasicBlock1, (killedRegister1, killedRegister2)), // edge 1
  //   (precedingBasicBlock2, (killedRegister3, killedRegister3))  // edge 2
  // ) |                      |
  //   +-- edge               +-- kills

  const llvm::Instruction *inst = &*dst->begin();
  if (llvm::MDNode *edges = inst->getMetadata("liveregister.killed")) {
    llvm::MDNode *edge = nullptr;
    for (std::size_t i = 0; i < edges->getNumOperands(); ++i) {
      llvm::Value *edgeValue = edges->getOperand(i);
      if ((edge = dyn_cast<llvm::MDNode>(edgeValue))) {
        assert(edge->getNumOperands() == 2 &&
          "MemoryState: liveregister.killed metadata in wrong shape");
        llvm::Value *bbValue = edge->getOperand(0);
        llvm::BasicBlock *bb = dyn_cast<llvm::BasicBlock>(bbValue);
        assert(bb != nullptr && "MemoryState: liveregister.killed metadata"
          "does not reference valid basic block");
        if (bb != src) {
          // wrong edge: no evaluation of registers to kill, go to next edge
          continue;
        }

        // correct edge was found, loop can be terminated
        break;
      }
    }

    if (edge != nullptr) {
      // unregister and clear locals that are not live at the end of dst
      llvm::Value *killsValue = edge->getOperand(1);
      llvm::MDNode *killsNode = dyn_cast<llvm::MDNode>(killsValue);
      for (std::size_t j = 0; j < killsNode->getNumOperands(); ++j) {
        llvm::Value *kill = killsNode->getOperand(j);
        llvm::Instruction *inst = static_cast<llvm::Instruction *>(kill);
        if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
          llvm::errs() << "MemoryState: not live anymore: "
                       << inst->getName() << "\n";
        }
        // remove local from local delta
        unregisterLocal(inst);
        // set local within KLEE to NULL to mark it as dead
        clearLocal(inst);
      }
    }

  }
}

KInstruction *MemoryState::getKInstruction(const llvm::BasicBlock* bb) {
  KFunction *kf = executionState->stack.back().kf;
  unsigned entry = kf->basicBlockEntry[const_cast<llvm::BasicBlock *>(bb)];
  return kf->instructions[entry];
}

KInstruction *MemoryState::getKInstruction(const llvm::Instruction* inst) {
  // FIXME: ugly hack
  llvm::BasicBlock *bb = const_cast<llvm::BasicBlock *>(inst->getParent());
  if (bb != nullptr) {
    KFunction *kf = executionState->stack.back().kf;
    if (kf != nullptr) {
      unsigned entry = kf->basicBlockEntry[bb];
      while ((entry + 1) < kf->numInstructions
             && kf->instructions[entry]->inst != inst)
      {
        entry++;
      }
      return kf->instructions[entry];
    }
  }
  return nullptr;
}

ref<Expr> MemoryState::getLocalValue(const KInstruction *kinst) {
  return executionState->stack.back().locals[kinst->dest].value;
}

ref<Expr> MemoryState::getLocalValue(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    return getLocalValue(kinst);
  }
  return nullptr;
}

void MemoryState::clearLocal(const KInstruction *kinst) {
  executionState->stack.back().locals[kinst->dest].value = nullptr;
  assert(getLocalValue(kinst).isNull());
}

void MemoryState::clearLocal(const llvm::Instruction *inst) {
  KInstruction *kinst = getKInstruction(inst);
  if (kinst != nullptr) {
    clearLocal(kinst);
  }
  assert(getLocalValue(inst).isNull());
}


bool MemoryState::findLoop() {
  if (disableMemoryState) {
    // we do not want to find infinite loops in library or output functions
    return false;
  }

  bool result = trace.findLoop();

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
    llvm::errs() << "MemoryState: entering output function: "
                 << f->getName() << "\n";
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

bool MemoryState::isInListedFunction(llvm::Function *f) {
  return (listedFunction.entered && f == listedFunction.function);
}

bool MemoryState::enterLibraryFunction(llvm::Function *f,
  ref<ConstantExpr> address, const MemoryObject *mo, const ObjectState *os,
  std::size_t bytes) {
  if (libraryFunction.entered) {
    // we can only enter one library function at a time
    klee_warning_once(f, "already entered a library function");
    return false;
  }

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: entering library function: "
                 << f->getName() << "\n";
  }

  unregisterWrite(address, *mo, *os, bytes);

  libraryFunction.entered = true;
  libraryFunction.function = f;
  libraryFunction.address = address;
  libraryFunction.mo = mo;
  libraryFunction.bytes = bytes;

  updateDisableMemoryState();

  return true;
}

bool MemoryState::isInLibraryFunction(llvm::Function *f) {
  return (libraryFunction.entered && f == libraryFunction.function);
}

void MemoryState::leaveLibraryFunction() {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: leaving library function: "
                 << libraryFunction.function->getName() << "\n";
  }

  const MemoryObject *mo = libraryFunction.mo;
  const ObjectState *os = executionState->addressSpace.findObject(mo);

  libraryFunction.entered = false;
  libraryFunction.function = nullptr;

  updateDisableMemoryState();

  registerWrite(libraryFunction.address, *mo, *os, libraryFunction.bytes);
}

void MemoryState::registerPushFrame(const KFunction *kf) {
  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: PUSHFRAME\n";
  }

  trace.registerEndOfStackFrame(kf,
                                fingerprint.getLocalDelta(),
                                fingerprint.getAllocaDelta(),
                                globalAllocationsInCurrentStackFrame);

  // make locals and arguments "invisible"
  fingerprint.discardLocalDelta();
  // record alloca allocations and changes for this new stack frame separately
  // from those of other stack frames (without removing the latter)
  fingerprint.applyAndResetAllocaDelta();

  // reset stack frame specific information
  globalAllocationsInCurrentStackFrame = false;

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }
}

void MemoryState::registerPopFrame(const llvm::BasicBlock *returningBB,
                                   const llvm::BasicBlock *callerBB) {
  // IMPORTANT: has to be called prior to state.popFrame()

  if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
    llvm::errs() << "MemoryState: POPFRAME\n"
                 << "Fingerprint: " << fingerprint.getFingerprintAsString()
                 << "\n";
  }

  if (trace.getNumberOfStackFrames() > 0) {
    // Even though the local delta is removed in the next step, we have to clear
    // consumed locals within KLEE to be able to determine which variable has
    // already been registered during another call to the function we are
    // currently leaving.
    unregisterConsumedLocals(returningBB, false);

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

    updateBasicBlockInfo(callerBB);

    globalAllocationsInCurrentStackFrame = sfe.globalAllocation;

    if (DebugInfiniteLoopDetection.isSet(STDERR_STATE)) {
      llvm::errs() << "reapplying local delta: "
                   << fingerprint.getLocalDeltaAsString()
                   << "\nreapplying alloca delta: "
                   << fingerprint.getAllocaDeltaAsString()
                   << "\nGlobal Alloc: " << globalAllocationsInCurrentStackFrame
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

}
