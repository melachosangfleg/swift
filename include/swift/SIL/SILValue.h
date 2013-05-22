//===--- SILValue.h - Value base class for SIL ------------------*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines the SILValue class.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_VALUE_H
#define SWIFT_SIL_VALUE_H

#include "swift/Basic/Range.h"
#include "swift/SIL/SILType.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"

namespace swift {
  class SILTypeList;
  class Operand;
  class ValueBaseUseIterator;
  class ValueUseIterator;

  enum class ValueKind {
#define VALUE(Id, Parent) Id,
#define VALUE_RANGE(Id, FirstId, LastId) \
    First_##Id = FirstId, Last_##Id = LastId,
#include "swift/SIL/SILNodes.def"
  };

/// ValueBase - This is the base class of the SIL value hierarchy, which
/// represents a runtime computed value.  Things like SILInstruction derive
/// from this.
class ValueBase : public SILAllocated<ValueBase> {
  PointerUnion<SILType, SILTypeList*> TypeOrTypeList;
  Operand *FirstUse = nullptr;
  friend class Operand;
  friend class SILValue;
  
  const ValueKind Kind;

  ValueBase(const ValueBase &) = delete;
  ValueBase &operator=(const ValueBase &) = delete;

protected:
  ValueBase(ValueKind Kind, SILTypeList *TypeList = 0)
    : TypeOrTypeList(TypeList), Kind(Kind) {}
  ValueBase(ValueKind Kind, SILType Ty)
    : TypeOrTypeList(Ty), Kind(Kind) {}
  
public:
  ValueKind getKind() const { return Kind; }

  ArrayRef<SILType> getTypes() const;

  SILType getType(unsigned i) const {
    SILType ty = getTypes()[i];
    return ty;
  }

  bool use_empty() const { return FirstUse == nullptr; }
  inline ValueBaseUseIterator use_begin();
  inline ValueBaseUseIterator use_end();
  inline Range<ValueBaseUseIterator> getUses();

  /// Pretty-print the value.
  void dump() const;
  void print(raw_ostream &OS) const;
  
  /// Pretty-print the value in context, preceded by its operands (if the
  /// value represents the result of an instruction) and followed by its
  /// users.
  void dumpInContext() const;

  static bool classof(const ValueBase *V) { return true; }
};

enum {
  /// The number of bits required to store a ResultNumber.
  /// This is primarily here as a way to allow everything that
  /// depends on it to be easily grepped.
  ValueResultNumberBits = 1
};

/// SILValue - A SILValue is a use of a specific result of an ValueBase.  As
/// such, it is a pair of the ValueBase and the result number being referenced.
class SILValue {
  llvm::PointerIntPair<ValueBase*, ValueResultNumberBits> ValueAndResultNumber;
  
  SILValue(void *p) {
    ValueAndResultNumber =
      decltype(ValueAndResultNumber)::getFromOpaqueValue(p);
  }
  
public:
  SILValue(const ValueBase *V = 0, unsigned ResultNumber = 0)
    : ValueAndResultNumber((ValueBase*)V, ResultNumber) {
    assert(ResultNumber == getResultNumber() && "Overflow");
  }

  ValueBase *getDef() const {
    return ValueAndResultNumber.getPointer();
  }
  ValueBase *operator->() const { return getDef(); }
  unsigned getResultNumber() const { return ValueAndResultNumber.getInt(); }

  SILType getType() const {
    return getDef()->getType(getResultNumber());
  }

  // Comparison.
  bool operator==(SILValue RHS) const {
    return ValueAndResultNumber == RHS.ValueAndResultNumber;
  }
  bool operator!=(SILValue RHS) const { return !(*this == RHS); }
  // Ordering (for std::map).
  bool operator<(SILValue RHS) const {
    return ValueAndResultNumber.getOpaqueValue() <
    RHS.ValueAndResultNumber.getOpaqueValue();
  }

  inline bool use_empty() const;
  inline ValueUseIterator use_begin();
  inline ValueUseIterator use_end();
  inline Range<ValueUseIterator> getUses();

  // Check validity.
  bool isValid() const { return getDef() != nullptr; }
  explicit operator bool() const { return getDef() != nullptr; }
  
  // Use as a pointer-like type.
  void *getOpaqueValue() const {
    return ValueAndResultNumber.getOpaqueValue();
  }
  static SILValue getFromOpaqueValue(void *p) {
    return SILValue(p);
  }
};

/// A formal SIL reference to a value, suitable for use as a stored
/// operand.
class Operand {
  /// The value used as this operand.
  SILValue TheValue;

  /// The next operand in the use-chain.  Note that the chain holds
  /// every use of the current ValueBase, not just those of the
  /// designated result.
  Operand *NextUse = nullptr;

  /// A back-pointer in the use-chain, required for fast patching
  /// of use-chains.
  Operand **Back = nullptr;

  /// The owner of this operand.
  /// FIXME: this could be space-compressed.
  ValueBase *Owner;

  Operand(ValueBase *owner) : Owner(owner) {}
  Operand(ValueBase *owner, SILValue theValue)
      : TheValue(theValue), Owner(owner) {
    insertIntoCurrent();
  }
  template<unsigned N> friend class FixedOperandList;
  template<unsigned N> friend class TailAllocatedOperandList;

public:
  // Operands are not copyable.
  Operand(const Operand &use) = delete;
  Operand &operator=(const Operand &use) = delete;

  /// Return the current value being used by this operand.
  SILValue get() const { return TheValue; }

  /// Set the current value being used by this operand.
  void set(SILValue newValue) {
    // It's probably not worth optimizing for the case of switching
    // operands on a single value.
    removeFromCurrent();
    TheValue = newValue;
    insertIntoCurrent();
  }

  ~Operand() {
    removeFromCurrent();
  }

  /// Return the user that owns this use.
  ValueBase *getUser() { return Owner; }
  const ValueBase *getUser() const { return Owner; }

private:
  void removeFromCurrent() {
    if (!Back) return;
    (*Back)->NextUse = NextUse;
    if (NextUse) NextUse->Back = Back;
  }

  void insertIntoCurrent() {
    Back = &TheValue->FirstUse;
    NextUse = TheValue->FirstUse;
    if (NextUse) NextUse->Back = &NextUse;
    TheValue->FirstUse = this;
  }

  friend class ValueBaseUseIterator;
  friend class ValueUseIterator;
  template <unsigned N> friend class FixedOperandList;
  template <unsigned N> friend class TailAllocatedOperandList;
};

/// A class which adapts an array of Operands into an array of Values.
///
/// The intent is that this should basically act exactly like
/// ArrayRef except projecting away the Operand-ness.
class OperandValueArrayRef {
  ArrayRef<Operand> Operands;
public:
  explicit OperandValueArrayRef(ArrayRef<Operand> operands)
    : Operands(operands) {}

  /// A simple iterator adapter.
  class iterator {
    const Operand *Ptr;
  public:
    iterator(const Operand *ptr) : Ptr(ptr) {}
    SILValue operator*() const { assert(Ptr); return Ptr->get(); }
    iterator &operator++() { ++Ptr; return *this; }
    iterator operator++(int) { iterator copy = *this; ++Ptr; return copy; }

    friend bool operator==(iterator lhs, iterator rhs) {
      return lhs.Ptr == rhs.Ptr;
    }
    friend bool operator!=(iterator lhs, iterator rhs) {
      return lhs.Ptr != rhs.Ptr;
    }
  };

  iterator begin() const { return iterator(Operands.begin()); }
  iterator end() const { return iterator(Operands.end()); }
  size_t size() const { return Operands.size(); }
  bool empty() const { return Operands.empty(); }
  
  SILValue front() const { return Operands.front().get(); }
  SILValue back() const { return Operands.back().get(); }
  
  SILValue operator[](unsigned i) const { return Operands[i].get(); }
  OperandValueArrayRef slice(unsigned begin, unsigned length) const {
    return OperandValueArrayRef(Operands.slice(begin, length));
  }
};

/// An iterator over all uses of a ValueBase.
class ValueBaseUseIterator : public std::iterator<std::forward_iterator_tag,
                                                  Operand*, ptrdiff_t> {
  Operand *Cur;
public:
  ValueBaseUseIterator() = default;
  explicit ValueBaseUseIterator(Operand *cur) : Cur(cur) {}
  Operand *operator*() const { return Cur; }

  ValueBaseUseIterator &operator++() {
    assert(Cur && "incrementing past end()!");
    Cur = Cur->NextUse;
    return *this;
  }

  ValueBaseUseIterator operator++(int unused) {
    ValueBaseUseIterator copy = *this;
    ++*this;
    return copy;
  }

  friend bool operator==(ValueBaseUseIterator lhs,
                         ValueBaseUseIterator rhs) {
    return lhs.Cur == rhs.Cur;
  }
  friend bool operator!=(ValueBaseUseIterator lhs,
                         ValueBaseUseIterator rhs) {
    return !(lhs == rhs);
  }
};
inline ValueBaseUseIterator ValueBase::use_begin() {
  return ValueBaseUseIterator(FirstUse);
}
inline ValueBaseUseIterator ValueBase::use_end() {
  return ValueBaseUseIterator(nullptr);
}
inline Range<ValueBaseUseIterator> ValueBase::getUses() {
  return { use_begin(), use_end() };
}

/// An iterator over all uses of a specific result of a ValueBase.
class ValueUseIterator  : public std::iterator<std::forward_iterator_tag,
                                               Operand*, ptrdiff_t> {
  llvm::PointerIntPair<Operand*, ValueResultNumberBits> CurAndResultNumber;
public:
  ValueUseIterator() = default;
  explicit ValueUseIterator(Operand *cur, unsigned resultNumber) {
    // Skip past uses with different result numbers.
    while (cur && cur->get().getResultNumber() != resultNumber)
      cur = cur->NextUse;

    CurAndResultNumber.setPointerAndInt(cur, resultNumber);
  }

  Operand *operator*() const { return CurAndResultNumber.getPointer(); }

  ValueUseIterator &operator++() {
    Operand *next = CurAndResultNumber.getPointer();
    assert(next && "incrementing past end()!");

    // Skip past uses with different result numbers.
    while ((next = next->NextUse)) {
      if (next->get().getResultNumber() == CurAndResultNumber.getInt())
        break;
    }

    CurAndResultNumber.setPointer(next);
    return *this;
  }

  ValueUseIterator operator++(int unused) {
    ValueUseIterator copy = *this;
    ++*this;
    return copy;
  }

  friend bool operator==(ValueUseIterator lhs, ValueUseIterator rhs) {
    return lhs.CurAndResultNumber.getPointer()
        == rhs.CurAndResultNumber.getPointer();
  }
  friend bool operator!=(ValueUseIterator lhs, ValueUseIterator rhs) {
    return !(lhs == rhs);
  }
};
inline ValueUseIterator SILValue::use_begin() {
  return ValueUseIterator((*this)->FirstUse, getResultNumber());
}
inline ValueUseIterator SILValue::use_end() {
  return ValueUseIterator(nullptr, 0);
}
inline Range<ValueUseIterator> SILValue::getUses() {
  return { use_begin(), use_end() };
}
inline bool SILValue::use_empty() const {
  SILValue *mthis = const_cast<SILValue*>(this);
  return mthis->use_begin() == mthis->use_end();
}

/// A constant-size list of the operands of an instruction.
template <unsigned N> class FixedOperandList {
  Operand Buffer[N];

  FixedOperandList(const FixedOperandList &) = delete;
  FixedOperandList &operator=(const FixedOperandList &) = delete;

public:
  template <class... T> FixedOperandList(ValueBase *user, T&&...args)
      : Buffer{ { user, std::forward<T>(args) }... } {
    static_assert(sizeof...(args) == N, "wrong number of initializers");
  }

  /// Returns the full list of operands.
  MutableArrayRef<Operand> asArray() {
    return MutableArrayRef<Operand>(Buffer, N);
  }
  ArrayRef<Operand> asArray() const {
    return ArrayRef<Operand>(Buffer, N);
  }

  /// Returns the full list of operand values.
  OperandValueArrayRef asValueArray() const {
    return OperandValueArrayRef(asArray());
  }

  /// Indexes into the full list of operands.
  Operand &operator[](unsigned i) { return asArray()[i]; }
  const Operand &operator[](unsigned i) const { return asArray()[i]; }
};

/// An operator list with a fixed number of known operands
/// (possibly zero) and a dynamically-determined set of extra
/// operands (also possibly zero).  The number of dynamic operands
/// is permanently set at initialization time.
///
/// 'N' is the number of static operands.
///
/// This class assumes that a number of bytes of extra storage have
/// been allocated immediately after it.  This means that this class
/// must always be the final data member in a class.
template <unsigned N> class TailAllocatedOperandList {
  unsigned NumExtra;
  Operand Buffer[N];

  TailAllocatedOperandList(const TailAllocatedOperandList &) = delete;
  TailAllocatedOperandList &operator=(const TailAllocatedOperandList &) =delete;

public:
  /// Given the number of dynamic operands required, returns the
  /// number of bytes of extra storage to allocate.
  static size_t getExtraSize(unsigned numExtra) {
    return sizeof(Operand) * numExtra;
  }

  /// Initialize this operand list.
  ///
  /// The dynamic operands are actually out of order: logically they
  /// will placed after the fixed operands, not before them.  But
  /// the variadic arguments have to come last.
  template <class... T>
  TailAllocatedOperandList(ValueBase *user,
                           ArrayRef<SILValue> dynamicArgs,
                           T&&... fixedArgs)
      : NumExtra(dynamicArgs.size()),
        Buffer{ { user, std::forward<T>(fixedArgs) }... } {
    static_assert(sizeof...(fixedArgs) == N, "wrong number of initializers");

    Operand *dynamicSlot = Buffer + N;
    for (auto value : dynamicArgs) {
      new (dynamicSlot++) Operand(user, value);
    }
  }

  ~TailAllocatedOperandList() {
    for (auto &op : getDynamicAsArray()) {
      op.~Operand();
    }
  }

  /// Returns the full list of operands.
  MutableArrayRef<Operand> asArray() {
    return MutableArrayRef<Operand>(Buffer, N+NumExtra);
  }
  ArrayRef<Operand> asArray() const {
    return ArrayRef<Operand>(Buffer, N+NumExtra);
  }

  /// Returns the full list of operand values.
  OperandValueArrayRef asValueArray() const {
    return OperandValueArrayRef(asArray());
  }

  /// Returns the list of the dynamic operands.
  MutableArrayRef<Operand> getDynamicAsArray() {
    return MutableArrayRef<Operand>(Buffer+N, NumExtra);
  }
  ArrayRef<Operand> getDynamicAsArray() const {
    return ArrayRef<Operand>(Buffer+N, NumExtra);
  }

  /// Returns the list of the dynamic operand values.
  OperandValueArrayRef getDynamicValuesAsArray() const {
    return OperandValueArrayRef(getDynamicAsArray());
  }

  /// Indexes into the full list of operands.
  Operand &operator[](unsigned i) { return asArray()[i]; }
  const Operand &operator[](unsigned i) const { return asArray()[i]; }
};

/// A specialization of TailAllocatedOperandList for zero static operands.
template<> class TailAllocatedOperandList<0> {
  unsigned NumExtra;
  union { // suppress value semantics
    Operand Buffer[1];
  };

  TailAllocatedOperandList(const TailAllocatedOperandList &) = delete;
  TailAllocatedOperandList &operator=(const TailAllocatedOperandList &) =delete;

public:
  static size_t getExtraSize(unsigned numExtra) {
    return sizeof(Operand) * (numExtra > 0 ? numExtra - 1 : 0);
  }

  TailAllocatedOperandList(ValueBase *user, ArrayRef<SILValue> dynamicArgs)
      : NumExtra(dynamicArgs.size()) {

    Operand *dynamicSlot = Buffer;
    for (auto value : dynamicArgs) {
      new (dynamicSlot++) Operand(user, value);
    }
  }

  ~TailAllocatedOperandList() {
    for (auto &op : getDynamicAsArray()) {
      op.~Operand();
    }
  }

  /// Returns the full list of operands.
  MutableArrayRef<Operand> asArray() {
    return MutableArrayRef<Operand>(Buffer, NumExtra);
  }
  ArrayRef<Operand> asArray() const {
    return ArrayRef<Operand>(Buffer, NumExtra);
  }

  /// Returns the full list of operand values.
  OperandValueArrayRef asValueArray() const {
    return OperandValueArrayRef(asArray());
  }

  /// Returns the list of the dynamic operands.
  MutableArrayRef<Operand> getDynamicAsArray() {
    return MutableArrayRef<Operand>(Buffer, NumExtra);
  }
  ArrayRef<Operand> getDynamicAsArray() const {
    return ArrayRef<Operand>(Buffer, NumExtra);
  }

  /// Returns the list of the dynamic operand values.
  OperandValueArrayRef getDynamicValuesAsArray() const {
    return OperandValueArrayRef(getDynamicAsArray());
  }

  /// Indexes into the full list of operands.
  Operand &operator[](unsigned i) { return asArray()[i]; }
  const Operand &operator[](unsigned i) const { return asArray()[i]; }
};

} // end namespace swift


namespace llvm {
  // A SILValue casts like a ValueBase*.
  template<> struct simplify_type<const ::swift::SILValue> {
    typedef ::swift::ValueBase *SimpleType;
    static SimpleType getSimplifiedValue(::swift::SILValue Val) {
      return Val.getDef();
    }
  };
  template<> struct simplify_type< ::swift::SILValue>
    : public simplify_type<const ::swift::SILValue> {};

  // Values hash just like pointers.
  template<> struct DenseMapInfo<swift::SILValue> {
    static swift::SILValue getEmptyKey() {
      return llvm::DenseMapInfo<swift::ValueBase*>::getEmptyKey();
    }
    static swift::SILValue getTombstoneKey() {
      return llvm::DenseMapInfo<swift::ValueBase*>::getTombstoneKey();
    }
    static unsigned getHashValue(swift::SILValue V) {
      return DenseMapInfo<swift::ValueBase*>::getHashValue(V.getDef());
    }
    static bool isEqual(swift::SILValue LHS, swift::SILValue RHS) {
      return LHS == RHS;
    }
  };
  
  // SILValue is a PointerLikeType.
  template<> class PointerLikeTypeTraits<::swift::SILValue> {
    using SILValue = ::swift::SILValue;
  public:
    static void *getAsVoidPointer(SILValue v) {
      return v.getOpaqueValue();
    }
    static SILValue getFromVoidPointer(void *p) {
      return SILValue::getFromOpaqueValue(p);
    }
    
    enum { NumLowBitsAvailable = 2 - swift::ValueResultNumberBits };
  };
}  // end namespace llvm

#endif
