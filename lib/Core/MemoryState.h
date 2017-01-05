#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "MemoryTrace.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Support/SHA1.h"

#include <array>
#include <cstdint>

#ifdef MEMORYSTATE_DEBUG
#include <iostream>
#endif

namespace klee {

class MemoryState {

private:
  std::array<std::uint8_t, 20> stateHash = {};
  std::array<std::uint8_t, 20> stackFrameHash = {};
  MemoryTrace trace;
  bool allocasInCurrentStackFrame = false;

  #ifdef MEMORYSTATE_DEBUG
  static std::string Sha1String(const std::array<std::uint8_t, 20> &buffer);
  #endif

  static std::string ExprString(ref<Expr> expr);
  void addUint64ToHash(util::SHA1 &sha1, const std::uint64_t address);
  void addConstantExprToHash(util::SHA1 &sha1, const ConstantExpr &expr);
  void addExprStringToHash(util::SHA1 &sha1, ref<Expr> expr);
  void xorStateHash(const std::array<std::uint8_t, 20> &hash);
  void xorStackFrameHash(const std::array<std::uint8_t, 20> &hash);

public:
  MemoryState() = default;
  MemoryState(const MemoryState&) = default;

  void registerAllocation(const MemoryObject &mo);
  void registerDeallocation(const MemoryObject &mo) {
    #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: DEALLOCATION\n";
    #endif

    registerAllocation(mo);
  }

  void registerWrite(ref<Expr> base, const MemoryObject &mo, const ObjectState &os);
  void unregisterWrite(ref<Expr> base, const MemoryObject &mo, const ObjectState &os) {
    #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: UNREGISTER\n";
    #endif

    registerWrite(base, mo, os);
  }

  void registerConstraint(ref<Expr> condition);

  void registerLocal(const KInstruction *target, ref<Expr> value);
  void unregisterLocal(const KInstruction *target, ref<Expr> value) {
    #ifdef MEMORYSTATE_DEBUG
    std::cout << "MemoryState: UNREGISTER\n";
    #endif

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