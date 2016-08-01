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

  void xorHash(const std::array<std::uint8_t, 20> &hash);
  void addAddressToHash(util::SHA1 &sha1, const uint64_t *address);
  void addObjectState(const MemoryObject &mo, const ObjectState &os);
  void addObjectPair(ObjectPair &op) {
    addObjectState(*op.first, *op.second);
  }

public:
  MemoryState() = default;
  MemoryState(const MemoryState&) = default;

  void registerWrite(ExecutionState &state, ref<Expr> base);
  void registerWrite(const MemoryObject &mo, const ObjectState &os) {
    addObjectState(mo, os);
  }
  void registerAllocation(const MemoryObject &mo);
  void registerDeallocation(const MemoryObject &mo);
};
}

#endif
