#ifndef KLEE_MEMORYSTATE_H
#define KLEE_MEMORYSTATE_H

#include "AddressSpace.h"
#include "klee/Internal/Support/SHA1.h"

#include <array>
#include <cstdint>

namespace klee {
class ExecutionState;

class MemoryState {

private:
  std::array<std::uint8_t, 20> shaBuffer = {};

  static std::string Sha1String(std::array<std::uint8_t, 20> &buffer);
  static std::string ExprString(ref<Expr> expr);
  void addUint64ToHash(util::SHA1 &sha1, const std::uint64_t *address);
  void addExprToHash(util::SHA1 &sha1, ref<Expr> expr);
  void xorHash(const std::array<std::uint8_t, 20> &hash);

public:
  MemoryState() = default;
  MemoryState(const MemoryState&) = default;

  void registerAllocation(const MemoryObject &mo);
  void registerDeallocation(const MemoryObject &mo) {
    registerAllocation(mo);
  }
  void registerWrite(ref<Expr> base, const MemoryObject &mo, const ObjectState &os);
  void registerConstraint(ref<Expr> condition);

  void printHash();
};
}

#endif
