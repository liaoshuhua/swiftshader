//===- subzero/src/IceAssembler.h - Integrated assembler --------*- C++ -*-===//
// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
//
// Modified by the Subzero authors.
//
//===----------------------------------------------------------------------===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Assembler base class.  Instructions are assembled
// by architecture-specific assemblers that derive from this base class.
// This base class manages buffers and fixups for emitting code, etc.
//
//===----------------------------------------------------------------------===//

#ifndef SUBZERO_SRC_ICEASSEMBLER_H
#define SUBZERO_SRC_ICEASSEMBLER_H

#include "IceDefs.h"
#include "IceFixups.h"

namespace Ice {

// Assembler buffers are used to emit binary code. They grow on demand.
class AssemblerBuffer {
  AssemblerBuffer(const AssemblerBuffer &) = delete;
  AssemblerBuffer &operator=(const AssemblerBuffer &) = delete;

public:
  AssemblerBuffer(Assembler &);
  ~AssemblerBuffer();

  // Basic support for emitting, loading, and storing.
  template <typename T> void emit(T Value) {
    assert(hasEnsuredCapacity());
    *reinterpret_cast<T *>(Cursor) = Value;
    Cursor += sizeof(T);
  }

  template <typename T> T load(intptr_t Position) const {
    assert(Position >= 0 &&
           Position <= (size() - static_cast<intptr_t>(sizeof(T))));
    return *reinterpret_cast<T *>(Contents + Position);
  }

  template <typename T> void store(intptr_t Position, T Value) {
    assert(Position >= 0 &&
           Position <= (size() - static_cast<intptr_t>(sizeof(T))));
    *reinterpret_cast<T *>(Contents + Position) = Value;
  }

  // Emit a fixup at the current location.
  void emitFixup(AssemblerFixup *Fixup) { Fixup->set_position(size()); }

  // Get the size of the emitted code.
  intptr_t size() const { return Cursor - Contents; }
  uintptr_t contents() const { return Contents; }

// To emit an instruction to the assembler buffer, the EnsureCapacity helper
// must be used to guarantee that the underlying data area is big enough to
// hold the emitted instruction. Usage:
//
//     AssemblerBuffer buffer;
//     AssemblerBuffer::EnsureCapacity ensured(&buffer);
//     ... emit bytes for single instruction ...

#ifndef NDEBUG
  class EnsureCapacity {
    EnsureCapacity(const EnsureCapacity &) = delete;
    EnsureCapacity &operator=(const EnsureCapacity &) = delete;

  public:
    explicit EnsureCapacity(AssemblerBuffer *Buffer);
    ~EnsureCapacity();

  private:
    AssemblerBuffer *Buffer;
    intptr_t Gap;

    intptr_t computeGap() { return Buffer->capacity() - Buffer->size(); }
  };

  bool HasEnsuredCapacity;
  bool hasEnsuredCapacity() const { return HasEnsuredCapacity; }
#else  // NDEBUG
  class EnsureCapacity {
    EnsureCapacity(const EnsureCapacity &) = delete;
    EnsureCapacity &operator=(const EnsureCapacity &) = delete;

  public:
    explicit EnsureCapacity(AssemblerBuffer *Buffer) {
      if (Buffer->cursor() >= Buffer->limit())
        Buffer->extendCapacity();
    }
  };

  // When building the C++ tests, assertion code is enabled. To allow
  // asserting that the user of the assembler buffer has ensured the
  // capacity needed for emitting, we add a dummy method in non-debug mode.
  bool hasEnsuredCapacity() const { return true; }
#endif // NDEBUG

  // Returns the position in the instruction stream.
  intptr_t getPosition() const { return Cursor - Contents; }

  // Create and track a fixup in the current function.
  AssemblerFixup *createFixup(FixupKind Kind, const Constant *Value);

  const FixupRefList &fixups() const { return Fixups; }

  void setSize(intptr_t NewSize) {
    assert(NewSize <= size());
    Cursor = Contents + NewSize;
  }

private:
  // The limit is set to kMinimumGap bytes before the end of the data area.
  // This leaves enough space for the longest possible instruction and allows
  // for a single, fast space check per instruction.
  static const intptr_t kMinimumGap = 32;

  uintptr_t Contents;
  uintptr_t Cursor;
  uintptr_t Limit;
  // The member variable is named Assemblr to avoid hiding the class Assembler.
  Assembler &Assemblr;
  // List of pool-allocated fixups relative to the current function.
  FixupRefList Fixups;

  uintptr_t cursor() const { return Cursor; }
  uintptr_t limit() const { return Limit; }
  intptr_t capacity() const {
    assert(Limit >= Contents);
    return (Limit - Contents) + kMinimumGap;
  }

  // Compute the limit based on the data area and the capacity. See
  // description of kMinimumGap for the reasoning behind the value.
  static uintptr_t computeLimit(uintptr_t Data, intptr_t Capacity) {
    return Data + Capacity - kMinimumGap;
  }

  void extendCapacity();
};

class Assembler {
  Assembler(const Assembler &) = delete;
  Assembler &operator=(const Assembler &) = delete;

public:
  Assembler()
      : Allocator(), FunctionName(""), IsInternal(false), Preliminary(false),
        Buffer(*this) {}
  virtual ~Assembler() = default;

  // Allocate a chunk of bytes using the per-Assembler allocator.
  uintptr_t allocateBytes(size_t bytes) {
    // For now, alignment is not related to NaCl bundle alignment, since
    // the buffer's GetPosition is relative to the base. So NaCl bundle
    // alignment checks can be relative to that base. Later, the buffer
    // will be copied out to a ".text" section (or an in memory-buffer
    // that can be mprotect'ed with executable permission), and that
    // second buffer should be aligned for NaCl.
    const size_t Alignment = 16;
    return reinterpret_cast<uintptr_t>(Allocator.Allocate(bytes, Alignment));
  }

  // Allocate data of type T using the per-Assembler allocator.
  template <typename T> T *allocate() { return Allocator.Allocate<T>(); }

  // Align the tail end of the function to the required target alignment.
  virtual void alignFunction() = 0;

  // Add nop padding of a particular width to the current bundle.
  virtual void padWithNop(intptr_t Padding) = 0;

  virtual SizeT getBundleAlignLog2Bytes() const = 0;

  virtual const char *getNonExecPadDirective() const = 0;
  virtual llvm::ArrayRef<uint8_t> getNonExecBundlePadding() const = 0;

  // Mark the current text location as the start of a CFG node
  // (represented by NodeNumber).
  virtual void bindCfgNodeLabel(SizeT NodeNumber) = 0;

  virtual bool fixupIsPCRel(FixupKind Kind) const = 0;

  // Return a view of all the bytes of code for the current function.
  llvm::StringRef getBufferView() const;

  const FixupRefList &fixups() const { return Buffer.fixups(); }

  AssemblerFixup *createFixup(FixupKind Kind, const Constant *Value) {
    return Buffer.createFixup(Kind, Value);
  }

  void emitIASBytes(GlobalContext *Ctx) const;
  bool getInternal() const { return IsInternal; }
  void setInternal(bool Internal) { IsInternal = Internal; }
  const IceString &getFunctionName() { return FunctionName; }
  void setFunctionName(const IceString &NewName) { FunctionName = NewName; }
  intptr_t getBufferSize() const { return Buffer.size(); }
  // Roll back to a (smaller) size.
  void setBufferSize(intptr_t NewSize) { Buffer.setSize(NewSize); }
  void setPreliminary(bool Value) { Preliminary = Value; }
  bool getPreliminary() const { return Preliminary; }

private:
  ArenaAllocator<32 * 1024> Allocator;
  // FunctionName and IsInternal are transferred from the original Cfg
  // object, since the Cfg object may be deleted by the time the
  // assembler buffer is emitted.
  IceString FunctionName;
  bool IsInternal;
  // Preliminary indicates whether a preliminary pass is being made
  // for calculating bundle padding (Preliminary=true), versus the
  // final pass where all changes to label bindings, label links, and
  // relocation fixups are fully committed (Preliminary=false).
  bool Preliminary;

protected:
  // Buffer's constructor uses the Allocator, so it needs to appear after it.
  // TODO(jpp): dependencies on construction order are a nice way of shooting
  // yourself in the foot. Fix this.
  AssemblerBuffer Buffer;
};

} // end of namespace Ice

#endif // SUBZERO_SRC_ICEASSEMBLER_H_
