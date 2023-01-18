/** @file
 *  Bit set on arbitrary memory span.
 *
 * Copyright 2022, Network Geographics
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <limits>

#include <swoc/MemSpan.h>

namespace txb
{
/** Simple class to emulate @c std::bitset over a variable amount of memory.
 * @c std::bitset doesn't work because the bit set size is a compile time constant.
 * @c std::vector<bool> doesn't work either because it does separate memory allocation.
 * In contrast to these, this class allows mapping an arbitrary previously allocated chunk of
 * memory as a bit set. This enables fitting it in to a @c Row in the @c IPSpace payload where
 * the @c Row data is allocated in a single chunk.
 */
class BitView
{
  using self_type                = BitView;                              ///< Self reference type.
protected:
  template < typename T > using MemSpan = swoc::MemSpan<T>;
  static constexpr unsigned BITS = std::numeric_limits<uint8_t>::digits; ///< # of bits per unit.

public:
  /// Construct from span.
  BitView(MemSpan<void const> const &span)  : _span(const_cast<uint8_t*>(static_cast<uint8_t const*>(span.data())), span.size()) {}

  /** Access a single bit.
   *
   * @param idx Index of bit.
   * @return @c true if the bit is set, @c false if not.
   */
  bool operator[](unsigned idx);

  /// @return The number of bits set.
  unsigned count() const;

  /** First bit difference.
   *
   * @param that Other bit view.
   * @return The index of the first different bit.
   */
//  unsigned fsb_diff(self_type const& that);

protected:
  MemSpan<uint8_t const> _span; ///< Memory for the bits.
};

class BitSpan {
  using self_type = BitSpan;
  template < typename T > using MemSpan = swoc::MemSpan<T>;
  static constexpr unsigned BITS = std::numeric_limits<uint8_t>::digits; ///< # of bits per unit.
protected:
  struct bit_ref;
public:
  /// Construct from chunk of memory.
  BitSpan(MemSpan<void> &span) : _span(span.rebind<uint8_t>()) {}

  /** Set a bit
   *
   * @param idx Bit index.
   * @return @a this.
   */
  self_type &set(unsigned idx);

  /** Reset a bit.
   *
   * @param idx Bit index.
   * @return @a this.
   */
  self_type &reset(unsigned idx);

  /** Reset all bits.
   *
   * @return @a this.
   */
  self_type &reset();

 /** Access a single bit (read only).
  *
  * @param idx Index of bit.
  * @return @c true if the bit is set, @c false if not.
  */
  bool operator[](unsigned idx) const;

  /** Access a single bit.
   *
   * @param idx Index of bit.
   * @return A bit reference which can be modified or checked for value.
   */
  bit_ref operator[](unsigned idx);

  /// @return The number of bits set.
  unsigned count() const;

protected:
  MemSpan<uint8_t> _span; ///< The memory for the bits.

  /// Reference class to make the index operator work.
  /// An instance of this fronts for a particular bit in the bit set.
  struct bit_ref {
    bit_ref(MemSpan<uint8_t> const& span, unsigned idx);

    /// Set the bit to 1.
    bit_ref & set();

    /// Reset the bit to 0.
    bit_ref & reset();

    /** Assign a @c bool to the bit.
     *
     * @param b Value to set.
     * @return @a this
     *
     * The bit is set if @a b is @c true, and reset if @a b is @c false.
     */
    bit_ref & operator=(bool b);

    /** Assign an @c int to the bit.
     *
     * @param v The integer to assign.
     * @return @a this
     *
     * The bit is set to zero if @a v is zero, otherwise it is set to 1.
     */
    bit_ref & operator=(int v);

    /** Allow bit to be used as a boolean.
     *
     * @return @c true if the bit is set, @c false if not.
     */
    explicit operator bool() const;

  private:
    uint8_t * _byte; ///< Byte with bit.
    uint8_t _mask;  ///< Mask for bit.

    friend class BitSpan;
  };

};

inline auto
BitSpan::set(unsigned idx) -> self_type &
{
  bit_ref(_span, idx).set();
  return *this;
}

inline auto
BitSpan::reset(unsigned idx) -> self_type &
{
  bit_ref(_span, idx).reset();
  return *this;
}

inline auto
BitSpan::reset() -> self_type &
{
  memset(_span, 0);
  return *this;
}

inline bool
BitView::operator[](unsigned int idx)
{
  swoc::Scalar<BITS> bidx = swoc::round_down(idx);
  return _span[bidx.count()] & (1 << (idx - bidx));
}

#if 0
unsigned
BitView::fsb_diff(const BitView::self_type &that)
{
  unsigned n = std::min(_span.size(), that._span.size());
  unsigned idx = 0;
  auto lv_span{swoc::MemSpan<const void>{_span.data(), n}};
  auto rv_span{swoc::MemSpan<const void>{that._span.data(), n}};

  // Do initial bits that are before a word boundary.
  auto l_init = lv_span.prefix(lv_span.align<uint64_t>().rebind<std::byte const>();

  // Do full word compares to find the first differing word.
  // If not found, first diff must be in last partial word.
  if ( swoc::Scalar<64, unsigned> wn = swoc::round_down(n) ; n ) {

    auto lw_span = lhs_span.prefix(wn / 8).rebind<const uint64_t>();
    auto rw_span = rhs_span.prefix(wn / 8).rebind<const uint64_t>();
    uint64_t const * l_w = lw_span.data();
    uint64_t const * r_w = rw_span.data();
    for ( unsigned i = 0 ; idx < wn && *l_w == *r_w ; ++idx , ++l_w, ++r_w, idx += 64)
      ;
  } else {

  }

  return idx;
}
#endif

inline bool
BitSpan::operator[](unsigned int idx) const
{
  swoc::Scalar<BITS> bidx = swoc::round_down(idx);
  return _span[bidx.count()] & (1 << (idx - bidx));
}

inline BitSpan::bit_ref::bit_ref(MemSpan<uint8_t> const& span, unsigned idx)
{
  swoc::Scalar<BITS> bidx = swoc::round_down(idx);
  _byte = span.data() + bidx.count();
  _mask = 1 << (idx - bidx);
}

inline auto
BitSpan::bit_ref::operator=(bool b) -> bit_ref&
{
  if (b) {
    *_byte |= _mask;
  } else {
    *_byte &= ~_mask;
  }
  return *this;
}

inline auto
BitSpan::bit_ref::operator=(int v) -> bit_ref &
{
  return *this = (v != 0);
}

inline BitSpan::bit_ref::operator bool() const
{
  return *_byte & _mask;
}

inline auto
BitSpan::bit_ref::set() -> bit_ref &
{
  *_byte |= _mask;
  return *this;
}

inline auto
BitSpan::bit_ref::reset() -> bit_ref &
{
  *_byte &= ~_mask;
  return *this;
}

} // namespce txb
