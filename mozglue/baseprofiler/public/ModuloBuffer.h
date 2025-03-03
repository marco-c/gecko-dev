/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ModuloBuffer_h
#define ModuloBuffer_h

#include "mozilla/leb128iterator.h"
#include "mozilla/NotNull.h"
#include "mozilla/PowerOfTwo.h"
#include "mozilla/UniquePtr.h"

#include <limits>
#include <type_traits>

namespace mozilla {

// The ModuloBuffer class is a circular buffer that holds raw byte values, with
// data-read/write helpers.
//
// OffsetT: Type of the internal offset into the buffer of bytes, it should be
// large enough to access all bytes of the buffer. It will also be used as
// Length (in bytes) of the buffer and of any subset. Default uint32_t
// IndexT: Type of the external index, it should be large enough that overflows
// should not happen during the lifetime of the ModuloBuffer.
//
// The basic usage is to create an iterator-like object with `ReaderAt(Index)`
// or `WriterAt(Index)`, and use it to read/write data blobs. Iterators
// automatically manage the wrap-around (through "Modulo", which is effectively
// an AND-masking with the PowerOfTwo buffer size.)
//
// There is zero safety: No thread safety, no checks that iterators may be
// overwriting data that's still to be read, etc. It's up to the caller to add
// adequate checks.
// The intended use is as an underlying buffer for a safer container.
template <typename OffsetT = uint32_t, typename IndexT = uint64_t>
class ModuloBuffer {
 public:
  using Byte = uint8_t;
  static_assert(sizeof(Byte) == 1, "ModuloBuffer::Byte must be 1 byte");
  using Offset = OffsetT;
  static_assert(!std::numeric_limits<Offset>::is_signed,
                "ModuloBuffer::Offset must be an unsigned integral type");
  using Length = Offset;
  using Index = IndexT;
  static_assert(!std::numeric_limits<Index>::is_signed,
                "ModuloBuffer::Index must be an unsigned integral type");
  static_assert(sizeof(Index) >= sizeof(Offset),
                "ModuloBuffer::Index size must >= Offset");

  explicit ModuloBuffer(PowerOfTwo<Length> aLength)
      : mMask(aLength.Mask()), mBuffer(new Byte[aLength.Value()]) {}

  PowerOfTwo<Length> BufferLength() const {
    return PowerOfTwo<Length>(mMask.MaskValue() + 1);
  }

  // All ModuloBuffer operations should be done through this iterator, which has
  // an effectively infinite range. The underlying wrapping-around is hidden.
  // Use `ReaderAt(Index)` or `WriterAt(Index)` to create it.
  //
  // `const Iterator<...>` means the iterator itself cannot change, i.e., it
  // cannot move, and only its const methods are available. Note that these
  // const methods may still be used to modify the buffer contents (e.g.:
  // `operator*()`, `Poke()`).
  //
  // `Iterator</*IsBufferConst=*/true>` means the buffer contents cannot be
  // modified, i.e., write operations are forbidden, but the iterator may still
  // move if non-const itself.
  template <bool IsBufferConst>
  class Iterator {
    // Alias to const- or mutable-`ModuloBuffer` depending on `IsBufferConst`.
    using ConstOrMutableBuffer =
        typename std::conditional<IsBufferConst, const ModuloBuffer,
                                  ModuloBuffer>::type;

    // Implementation note about the strange enable-if's below:
    //   `template <bool NotIBC = !IsBufferConst> enable_if_t<NotIBC>`
    // which intuitively could be simplified to:
    //   `enable_if_t<!IsBufferConst>`
    // The former extra-templated syntax is in fact necessary to delay
    // instantiation of these functions until they are actually needed.
    //
    // If we were just doing `enable_if_t<!IsBufferConst>`, this would only
    // depend on the *class* (`ModuloBuffer<...>::Iterator`), which gets
    // instantiated when a `ModuloBuffer` is created with some template
    // arguments; at that point, all non-templated methods get instantiated, so
    // there's no "SFINAE" happening, and `enable_if_t<...>` is actually doing
    // `typename enable_if<...>::type` on the spot, but there is no `type` if
    // `IsBufferConst` is true, so it just fails right away. E.g.:
    // error: no type named 'type' in 'std::enable_if<false, void>';
    //        'enable_if' cannot be used to disable this declaration
    // note: in instantiation of template type alias 'enable_if_t'
    // > std::enable_if_t<!IsBufferConst> WriteObject(const T& aObject) {
    //       in instantiation of template class
    //       'mozilla::ModuloBuffer<...>::Iterator<true>'
    // > auto it = mb.ReaderAt(1);
    //
    // By adding another template level `template <bool NotIsBufferConst =
    // !IsBufferConst>`, the instantiation is delayed until the function is
    // actually invoked somewhere, e.g. `it.Poke(...);`.
    // So at that invocation point, the compiler looks for a "Poke" name in it,
    // and considers potential template instantiations that could work. The
    // `enable_if_t` is *now* attempted, with `NotIsBufferConst` taking its
    // value from `!IsBufferConst`:
    // - If `IsBufferConst` is false, `NotIsBufferConst` is true,
    // `enable_if<NotIsBufferConst>` does define a `type` (`void` by default),
    // so `enable_if_t` happily becomes `void`, the function exists and may be
    // called.
    // - Otherwise if `IsBufferConst` is true, `NotIsBufferConst` is false,
    // `enable_if<NotIsBufferConst>` does *not* define a `type`, therefore
    // `enable_if_t` produces an error because there is no `type`. Now "SFINAE"
    // happens: This "Substitution Failure Is Not An Error" (by itself)... But
    // then, there are no other functions named "Poke" as requested in the
    // `it.Poke(...);` call, so we are now getting an error (can't find
    // function), as expected because `it` had `IsBufferConst`==true. (But at
    // least the compiler waited until this invocation attempt before outputting
    // an error.)
    //
    // C++ is fun!

   public:
    // Can always copy/assign from the same kind of iterator.
    Iterator(const Iterator& aRhs) = default;
    Iterator& operator=(const Iterator& aRhs) = default;

    // Can implicitly copy an Iterator-to-mutable (reader+writer) to
    // Iterator-to-const (reader-only), but not the reverse.
    template <bool IsRhsBufferConst,
              typename = std::enable_if_t<(!IsRhsBufferConst) && IsBufferConst>>
    MOZ_IMPLICIT Iterator(const Iterator<IsRhsBufferConst>& aRhs)
        : mModuloBuffer(aRhs.mModuloBuffer), mIndex(aRhs.mIndex) {}

    // Can implicitly assign from an Iterator-to-mutable (reader+writer) to
    // Iterator-to-const (reader-only), but not the reverse.
    template <bool IsRhsBufferConst,
              typename = std::enable_if_t<(!IsRhsBufferConst) && IsBufferConst>>
    Iterator& operator=(const Iterator<IsRhsBufferConst>& aRhs) {
      mModuloBuffer = aRhs.mModuloBuffer;
      mIndex = aRhs.mIndex;
      return *this;
    }

    // Current location of the iterator in the `Index` range.
    // Note that due to wrapping, multiple indices may effectively point at the
    // same byte in the buffer.
    Index CurrentIndex() const { return mIndex; }

    // Location comparison in the `Index` range. I.e., two `Iterator`s may look
    // unequal, but refer to the same buffer location.
    // Must be on the same buffer.
    bool operator==(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex == aRhs.mIndex;
    }
    bool operator!=(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex != aRhs.mIndex;
    }
    bool operator<(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex < aRhs.mIndex;
    }
    bool operator<=(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex <= aRhs.mIndex;
    }
    bool operator>(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex > aRhs.mIndex;
    }
    bool operator>=(const Iterator& aRhs) const {
      MOZ_ASSERT(mModuloBuffer == aRhs.mModuloBuffer);
      return mIndex >= aRhs.mIndex;
    }

    // Movement in the `Index` range.
    Iterator& operator++() {
      ++mIndex;
      return *this;
    }
    Iterator& operator--() {
      --mIndex;
      return *this;
    }
    Iterator& operator+=(Length aLength) {
      mIndex += aLength;
      return *this;
    }
    Iterator operator+(Length aLength) const {
      return Iterator(*mModuloBuffer, mIndex + aLength);
    }
    Iterator& operator-=(Length aLength) {
      mIndex -= aLength;
      return *this;
    }
    Iterator operator-(Length aLength) const {
      return Iterator(*mModuloBuffer, mIndex - aLength);
    }

    // Distance from `aRef` to here in the `Index` range.
    // May be negative (as 2's complement) if `aRef > *this`.
    Index operator-(const Iterator& aRef) const {
      MOZ_ASSERT(mModuloBuffer == aRef.mModuloBuffer);
      return mIndex - aRef.mIndex;
    }

    // Dereference a single byte (read-only if `IsBufferConst` is true).
    std::conditional_t<IsBufferConst, const Byte&, Byte&> operator*() const {
      return mModuloBuffer->mBuffer[OffsetInBuffer()];
    }

    // Write data (if `IsBufferConst` is false) but don't move iterator.
    template <bool NotIsBufferConst = !IsBufferConst>
    std::enable_if_t<NotIsBufferConst> Poke(const void* aSrc,
                                            Length aLength) const {
      // Don't allow data larger than the buffer.
      MOZ_ASSERT(aLength <= mModuloBuffer->BufferLength().Value());
      // Offset inside the buffer (corresponding to our Index).
      Offset offset = OffsetInBuffer();
      // Compute remaining bytes between this offset and the end of the buffer.
      Length remaining = mModuloBuffer->BufferLength().Value() - offset;
      if (MOZ_LIKELY(remaining >= aLength)) {
        // Enough space to write everything before the end.
        memcpy(&mModuloBuffer->mBuffer[offset], aSrc, aLength);
      } else {
        // Not enough space. Write as much as possible before the end.
        memcpy(&mModuloBuffer->mBuffer[offset], aSrc, remaining);
        // And then continue from the beginning of the buffer.
        memcpy(&mModuloBuffer->mBuffer[0],
               static_cast<const Byte*>(aSrc) + remaining,
               (aLength - remaining));
      }
    }

    // Write object data (if `IsBufferConst` is false) but don't move iterator.
    // Note that this copies bytes from the object, with the intent to read them
    // back later. Restricted to trivially-copyable types, which support this
    // without Undefined Behavior!
    template <typename T, bool NotIsBufferConst = !IsBufferConst>
    std::enable_if_t<NotIsBufferConst> PokeObject(const T& aObject) const {
      static_assert(std::is_trivially_copyable<T>::value,
                    "PokeObject<T> - T must be trivially copyable");
      return Poke(&aObject, sizeof(T));
    }

    // Write data (if `IsBufferConst` is false) and move iterator ahead.
    template <bool NotIsBufferConst = !IsBufferConst>
    std::enable_if_t<NotIsBufferConst> Write(const void* aSrc, Length aLength) {
      Poke(aSrc, aLength);
      mIndex += aLength;
    }

    // Write object data (if `IsBufferConst` is false) and move iterator ahead.
    // Note that this copies bytes from the object, with the intent to read them
    // back later. Restricted to trivially-copyable types, which support this
    // without Undefined Behavior!
    template <typename T, bool NotIsBufferConst = !IsBufferConst>
    std::enable_if_t<NotIsBufferConst> WriteObject(const T& aObject) {
      static_assert(std::is_trivially_copyable<T>::value,
                    "WriteObject<T> - T must be trivially copyable");
      return Write(&aObject, sizeof(T));
    }

    // Number of bytes needed to represent `aValue` in unsigned LEB128.
    template <typename T>
    static unsigned ULEB128Size(T aValue) {
      return ::mozilla::ULEB128Size(aValue);
    }

    // Write number as unsigned LEB128 (if `IsBufferConst` is false) and move
    // iterator ahead.
    template <typename T, bool NotIsBufferConst = !IsBufferConst>
    std::enable_if_t<NotIsBufferConst> WriteULEB128(T aValue) {
      ::mozilla::WriteULEB128(aValue, *this);
    }

    // Read data but don't move iterator.
    void Peek(void* aDst, Length aLength) const {
      // Don't allow data larger than the buffer.
      MOZ_ASSERT(aLength <= mModuloBuffer->BufferLength().Value());
      // Offset inside the buffer (corresponding to our Index).
      Offset offset = OffsetInBuffer();
      // Compute remaining bytes between this offset and the end of the buffer.
      Length remaining = mModuloBuffer->BufferLength().Value() - offset;
      if (MOZ_LIKELY(remaining >= aLength)) {
        // Can read everything we need before the end of the buffer.
        memcpy(aDst, &mModuloBuffer->mBuffer[offset], aLength);
      } else {
        // Read as much as possible before the end of the buffer.
        memcpy(aDst, &mModuloBuffer->mBuffer[offset], remaining);
        // And then continue from the beginning of the buffer.
        memcpy(static_cast<Byte*>(aDst) + remaining, &mModuloBuffer->mBuffer[0],
               (aLength - remaining));
      }
    }

    // Read data into an object but don't move iterator.
    // Note that this overwrites `aObject` with bytes from the buffer.
    // Restricted to trivially-copyable types, which support this without
    // Undefined Behavior!
    template <typename T>
    void PeekIntoObject(T& aObject) const {
      static_assert(std::is_trivially_copyable<T>::value,
                    "PeekIntoObject<T> - T must be trivially copyable");
      Peek(&aObject, sizeof(T));
    }

    // Read data as an object but don't move iterator.
    // Note that this creates an default `T` first, and then overwrites it with
    // bytes from the buffer. Restricted to trivially-copyable types, which
    // support this without Undefined Behavior!
    template <typename T>
    T PeekObject() const {
      static_assert(std::is_trivially_copyable<T>::value,
                    "PeekObject<T> - T must be trivially copyable");
      T object;
      PeekIntoObject(object);
      return object;
    }

    // Read data and move iterator ahead.
    void Read(void* aDst, Length aLength) {
      Peek(aDst, aLength);
      mIndex += aLength;
    }

    // Read data into an object and move iterator ahead.
    // Note that this overwrites `aObject` with bytes from the buffer.
    // Restricted to trivially-copyable types, which support this without
    // Undefined Behavior!
    template <typename T>
    void ReadIntoObject(T& aObject) {
      static_assert(std::is_trivially_copyable<T>::value,
                    "ReadIntoObject<T> - T must be trivially copyable");
      Read(&aObject, sizeof(T));
    }

    // Read data as an object and move iterator ahead.
    // Note that this creates an default `T` first, and then overwrites it with
    // bytes from the buffer. Restricted to trivially-copyable types, which
    // support this without Undefined Behavior!
    template <typename T>
    T ReadObject() {
      static_assert(std::is_trivially_copyable<T>::value,
                    "ReadObject<T> - T must be trivially copyable");
      T object;
      ReadIntoObject(object);
      return object;
    }

    // Read an unsigned LEB128 number and move iterator ahead.
    template <typename T>
    T ReadULEB128() {
      return ::mozilla::ReadULEB128<T>(*this);
    }

   private:
    // Only a ModuloBuffer can instantiate its iterator.
    friend class ModuloBuffer;

    Iterator(ConstOrMutableBuffer& aBuffer, Index aIndex)
        : mModuloBuffer(WrapNotNull(&aBuffer)), mIndex(aIndex) {}

    // Convert the Iterator's mIndex into an offset inside the byte buffer.
    Offset OffsetInBuffer() const {
      return static_cast<Offset>(mIndex) & mModuloBuffer->mMask;
    }

    // ModuloBuffer that this Iterator operates on.
    // Using a non-null pointer instead of a reference, to allow re-assignment
    // of an Iterator variable.
    NotNull<ConstOrMutableBuffer*> mModuloBuffer;

    // Position of this iterator in the wider `Index` range. (Will be wrapped
    // around as needed when actually accessing bytes from the buffer.)
    Index mIndex;
  };

  // Shortcut to iterator to const (read-only) data.
  using Reader = Iterator<true>;
  // Shortcut to iterator to non-const (read/write) data.
  using Writer = Iterator<false>;

  // Create an iterator to const data at the given index.
  Reader ReaderAt(Index aIndex) const { return Reader(*this, aIndex); }

  // Create an iterator to non-const data at the given index.
  Writer WriterAt(Index aIndex) { return Writer(*this, aIndex); }

#ifdef DEBUG
  void Dump() const {
    Length len = BufferLength().Value();
    if (len > 128) {
      len = 128;
    }
    for (Length i = 0; i < len; ++i) {
      printf("%02x ", mBuffer[i]);
    }
    printf("\n");
  }
#endif  // DEBUG

 private:
  // Mask used to convert an index to an offset in `mBuffer`
  const PowerOfTwoMask<Offset> mMask;

  // Buffer data.
  UniquePtr<Byte[]> mBuffer;
};

}  // namespace mozilla

#endif  // ModuloBuffer_h
