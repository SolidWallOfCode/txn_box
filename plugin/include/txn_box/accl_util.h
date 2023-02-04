/** @file
 *  Utility helper for string match accelerator class.
 *
 * Copyright 2020, Verizon Media .
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <iostream>
#include <utility>
#include <stack>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <cassert>

#include <swoc/TextView.h>
#include <swoc/swoc_meta.h>
#include <swoc/MemArena.h>

#include "txn_box/BitSpan.h"

// fwd declarations.
template <class T> class reversed_view;

/** Bit reference.
   *
   * @tparam W Element type, must support bitwise operations.
 */
template < typename W > struct BitRef {
  using self_type = BitRef;

  /// Default constructor - first bit.
  BitRef() = default;

  /** Constructor.
     *
     * @param idx Element index.
     * @param pos Bit position in element.
   */
  BitRef(unsigned idx, unsigned pos) : _idx(idx), _mask(static_cast<W>(1) << pos) { }
  BitRef(self_type const&) = default;

  /// Assignment
  self_type & operator = (self_type const&) = default;

  /// Equality
  bool operator == (self_type const& that) const;
  /// Inequality
  bool operator != (self_type const& that) const;
  /// Ordering.
  bool operator <  (self_type const& that) const;

  /** Apply the reference to data.
   *
   * @param data Source data to check.
   * @return @c true if the referenced bit is set, @c false if not.
   */
  bool apply(W const* data) const { return (data[_idx] & _mask) != 0; }
  template < typename K > bool apply(K const& key) const { return this->apply(key.data()); }

  /// Prefix increment.
  self_type & operator ++ ();

  unsigned _idx = 0; ///< Element index.
  W _mask = 1; ///< Mask for target bit.
};

template <typename W>
bool BitRef<W>::operator<(const BitRef::self_type &that) const
{ return _idx < that._idx || (_idx == that._idx && _mask < that._mask); }

template <typename W>
bool BitRef<W>::operator!=(const BitRef::self_type &that) const
{ return _idx != that._idx || _mask != that._mask; }

template <typename W>
bool BitRef<W>::operator==(const BitRef::self_type &that) const
{ return _idx == that._idx && _mask == that._mask; }

template <typename W>
auto BitRef<W>::operator++() -> self_type &
{
  if (0 == (_mask << 1)) {
    _mask = 1;
    ++_idx;
  }
  return *this;
}

/** PATRICA algorithm implementation using binary trees.
 *
 * This Data Structure allows to search for N key in exactly N nodes, providing a log(N) bit
 * comparision with a single full key comparision per search. The key(or view) is stored in the
 * node, nodes are traversed according to the bits of the key, this implementation does NOT uses the
 * key while traversing, it only stores it for a possible letter reference when the end of the
 * tree/search is reached and the full match needs to be granted.
 *
 * @note We only support insert, full match and prefix match.
 * @note To handle suffix_match please refer to @c reversed_view<T>
 * @tparam Key
 * @tparam Value
*/
template <typename Key, typename Value> class StringMatcher
{
  // Types that have been tested.
  static_assert(swoc::meta::is_any_of_v<Key, std::string, std::string_view, swoc::TextView, reversed_view<swoc::TextView>>,
                 "Type not supported");

  using self_type = StringMatcher;
public:
  // Export template arguments.
  using value_arg  = Value;
  using key_arg   = Key;
  using key_type = key_arg;

  /// Type of elements in the key and node paths.
  using elt_type = std::remove_const_t<std::remove_reference_t<decltype(key_arg()[0])>>;

  static constexpr int32_t UNRANKED = -1;

protected:

  using bit_ref = BitRef<elt_type>;

  /// Node for a key/value.
  /// This is always attached for a branch / decision node.
  struct value_node {
    using self_type = value_node; ///< Self reference type.

    /** Constructor.
     *
     * @param key Key.
     * @param value Value.
     * @param r Rank.
     */
    value_node(key_type key, value_arg const& value, int32_t r = UNRANKED) : _key{key}, _value{value}, _rank{r} {}

    Key _key; ///< Node key.
    Value _value; ///< Value for key.
    int32_t _rank = UNRANKED;  ///< Node rank.
    bool _final_p = true; ///< Final character in match (exact, not prefix).

    value_node()             = delete;
    value_node(self_type const &) = delete;
    value_node &operator=(self_type const &) = delete;
  };
  using value_type = value_node const;

  /// Decision node.
  /// If a decision node has a value, then the bit index should be the first bit past the end of the
  /// key for the value.
  struct branch_node {
    using self_type = branch_node; ///< Self-reference type.

    branch_node(bit_ref ref, elt_type const * path) : _bit(ref), _path(path) {}
    branch_node * next(Key const& key) const { return _bit.apply(key) ? _left : _right; }

    bit_ref _bit; ///< Deciding bit for the branch.

    self_type *_left = nullptr; ///< Left (unset bit)
    self_type *_right = nullptr; ///< Right (set bit)
    elt_type const * _path = nullptr; ///< Key / path for this node.
    value_node * _value = nullptr; ///< Value, if any.

    branch_node()             = delete;
    branch_node(self_type const &) = delete;
    self_type &operator=(self_type const &) = delete;

    ~branch_node() {
      if (_value) { std::destroy_at(&(_value->_value)); }
      if (_left) { std::destroy_at(_left); }
      if (_right) { std::destroy_at(_right); }
    }
  };


public:
  /// Construct a new String Tree object
  StringMatcher();

  /// We have a bunch of memory to clean up after use.
  ~StringMatcher();

  StringMatcher(self_type &&)      = delete;
  StringMatcher(self_type const &) = delete;
  StringMatcher &operator=(self_type const &) = delete;
  StringMatcher &operator=(self_type &&) = delete;

  ///
  /// @brief  Inserts element into the tree, if the container doesn't already contain an element with an equivalent key.
  /// @return true if the k/v was properly inserted, false if not.
  ///
  bool insert(Key const &key, Value const &value, int32_t rank = UNRANKED);

  /** Search the container for the value with @a key.
   *
   * @param key Search key.
   * @return A pointer to the key / value pair structure.
   */
  value_type * find(Key const &key) const noexcept;

private:
  branch_node * _root = nullptr; ///< Root of the nodes.
  swoc::MemArena _arena; ///< Storage for nodes and strings.
};

/// --------------------------------------------------------------------------------------------------------------------
namespace detail
{
// Just to make the code a bit more readable
static constexpr int BIT_ON{1};

template < typename W > BitRef<W> bit_cmp(W const * lhs, W const * rhs, BitRef<W> idx, BitRef<W> limit) {
  while (idx < limit && idx.apply(lhs) == idx.apply(rhs)) {
    ++idx;
  }
  return idx;
}

# if 0
/// @brief Specialization to deal with suffix and prefix byte getter.
template <typename T>
auto
get_byte(typename T::const_pointer ptr, int byte_number,
         typename std::enable_if_t<std::is_same_v<T, reversed_view<swoc::TextView>>> *val = 0)
{
  typename T::const_pointer byte = ptr - byte_number;
  return *byte;
}

template <typename T>
auto
get_byte(typename T::const_pointer ptr, int byte_number,
         typename std::enable_if_t<!std::is_same_v<T, reversed_view<swoc::TextView>>> *val = 0)
{
  typename T::const_pointer byte = ptr + byte_number;
  return *byte;
}

/// @brief Get a specific bit position from a stream of bytes.
template <typename Key>
static auto
get_bit(Key const &key, BitRef<typename Key::value_type> spot)
{
  return spot.apply(key.data());
}

template <typename Key>
static auto
get_first_diff_bit_position(Key const &lhs, Key const &rhs)
{
  std::size_t byte_count{0};
  auto lhs_iter = std::begin(lhs);
  auto rhs_iter = std::begin(rhs);
  while ((lhs_iter != std::end(lhs) && rhs_iter != std::end(rhs)) && *lhs_iter == *rhs_iter) {
    ++lhs_iter;
    ++rhs_iter;
    ++byte_count;
  }

  std::size_t bit_pos{0};

  for (bit_pos = 0; bit_pos < 8; ++bit_pos) {
    if (get_bit_from_byte(*lhs_iter, bit_pos) != get_bit_from_byte(*rhs_iter, bit_pos)) {
      break;
    }
  }

  return BitRef<typename Key::value_type>{byte_count, bit_pos};
}

# endif

} // namespace detail

/// --------  Implementation -------------

template <typename Key, typename Value> StringMatcher<Key, Value>::StringMatcher()
{
  _root            = _arena.make<branch_node>(bit_ref{}, "");
}

template <typename Key, typename Value> StringMatcher<Key, Value>::~StringMatcher()
{
  std::destroy_at(_root);
}

template <typename Key, typename Value>
bool
StringMatcher<Key, Value>::insert(Key const &key, Value const &value, int32_t rank)
{
  branch_node * parent_node;
  branch_node * search_node = _root; // Current node for search.
  branch_node * child_node = _root->next(key); // Next node to search.
  bit_ref cur_bit_idx, next_bit_idx;
  bit_ref bit_idx_limit{unsigned(key.size()), 0};
  bit_ref bit_diff; //

  if (child_node == nullptr) { // missing root child, just set it.
    auto bnode = _arena.make<branch_node>(bit_idx_limit, key.data());
    bnode->_value = _arena.make<value_node>(key, value, rank);
    (_root->_bit.apply(key) ? _root->_right : _root->_left) = bnode;
    return true;
  }

  // Invariant - there is at least one child node of root on the target side.

  // We wil try to go down the path and get close to the place where we want to insert the new value, then we will
  // follow the logic as like a search miss. Done this way to guarantee @a search_node is never null.
  do {
    parent_node = search_node;
    search_node = child_node;
    next_bit_idx         = std::min(search_node->_bit, bit_idx_limit);
    bit_diff = detail::bit_cmp(key.data(), search_node->_path, cur_bit_idx, next_bit_idx);
    cur_bit_idx = next_bit_idx;
    if (bit_diff < next_bit_idx || next_bit_idx == bit_idx_limit) { // difference or reached end of search key.
      break;
    }
    // Can't update this before the previous check - if that fails then @a key is long enough for @c next.
    child_node  = search_node->next(key);
  } while (child_node);

  if (search_node->_value && key == search_node->_value->_key) { // already present.
    return false;
  } else if (nullptr == child_node) { // past end of existing nodes, add.
    auto bnode = _arena.make<branch_node>(bit_idx_limit, key.data());
    (search_node->_bit.apply(key) ? search_node->_right : search_node->_left) = bnode;
    bnode->_value = _arena.make<value_node>(key, value, rank);
  } else {
    auto &parent_link = (parent_node->_bit.apply(key) ? parent_node->_right : parent_node->_left);
    auto bnode        = _arena.make<branch_node>(bit_diff, key.data());
    (bnode->_bit.apply(key) ? bnode->_right : bnode->_left) = parent_link;
    parent_link                                             = bnode;
    // Add the new value.
    auto vnode                                              = _arena.make<branch_node>(bit_idx_limit, key.data());
    vnode->_value                                           = _arena.make<value_node>(key, value, rank);
    (bnode->_bit.apply(key) ? bnode->_left : bnode->_right) = vnode;
  }
  return true;
}

template <typename Key, typename Value>
auto
StringMatcher<Key, Value>::find(Key const &key) const noexcept -> value_node const *
{
  auto search_node = _root->next(key);
  bit_ref bit_idx = search_node->_bit;
  value_node * candidate = nullptr;
  size_t key_idx = 0;
  size_t ksize = key.size();

  while (search_node) {
    bit_idx     = search_node->_bit;
    if (auto vnode = search_node->_value ; vnode) {
      // walk the key characters to verify they match the search key.
      auto klimit = std::min(ksize, vnode->_key.size());
      while (key_idx < klimit) {
        if (key[key_idx] != key[key_idx]) return nullptr;
        ++key_idx;
      }
      if (vnode->_final_p) {
        if (vnode->_key.size() == key.size()) {
          return (candidate && candidate->_rank < vnode->_rank) ? candidate : vnode;
        }
      } else { // prefix - track the best ranking prefix.
        if (! candidate || candidate->_rank > vnode->_rank) {
          candidate = vnode;
        }
      }
    }
    search_node = search_node->next(key);
  };

  return candidate;
}

/// --------------------------------------------------------------------------------------------------------------------

///
/// @brief Wrapper class to "view" a string_view/TextView as reversed view
///
/// @tparam View Original string view that needs to be stored in reverse gear.
///
template <typename View> class reversed_view
{
  /// haven't test more than this types.
  static_assert(swoc::meta::is_any_of_v<View, std::string, std::string_view, swoc::TextView>, "Type not supported");

public:
  using const_pointer = typename View::const_pointer;
  using pointer       = typename View::pointer;
  using value_type    = typename View::value_type;

  using iterator = typename View::reverse_iterator;
  using const_iterator = typename View::const_reverse_iterator;

# if 0
  // Define our own iterator which show a reverse view of the original.
  struct iterator {
    explicit iterator(typename View::reverse_iterator iter) : _iter(iter) {}
    const typename View::value_type &
    operator*() const
    {
      return *_iter;
    }

    // TODO: Work on some operator to provide a better api (operator+, operator+=, etc)
    iterator &
    operator++()
    {
      ++_iter;
      return *this;
    }

    iterator
    operator++(int)
    {
      iterator it(*this);
      operator++();
      return it;
    }

    bool
    operator==(iterator const &lhs) const
    {
      return _iter == lhs._iter;
    }

    bool
    operator!=(iterator const &lhs) const
    {
      return _iter != lhs._iter;
    }

  private:
    typename View::reverse_iterator _iter;
  };
# endif

  reversed_view() noexcept = default;

  explicit reversed_view(View view) noexcept : _view(view) {}

  bool operator==(View const &v) const noexcept
  {
    return v == _view;
  }

  bool
  empty() const noexcept
  {
    return _view.empty();
  }

  iterator
  begin() const noexcept
  {
    return _view.rbegin();
  }

  iterator
  end() const noexcept
  {
    return _view.rend();
  }

  typename View::size_type
  size() const noexcept
  {
    return _view.size();
  }

  value_type operator [] (size_t idx) const { return _view[_view.size() - idx - 1]; }
  value_type & operator [] (size_t idx) { return _view[_view.size() - idx - 1]; }

  // for debugging purpose we may need to log it.
  template <typename T> friend std::ostream &operator<<(std::ostream &os, reversed_view<T> const &v);
  template <typename T> friend bool operator==(reversed_view<T> const &lhs, reversed_view<T> const &rhs);

private:
  View _view;
};

template <typename View>
std::ostream &
operator<<(std::ostream &os, reversed_view<View> const &v)
{
  std::for_each(v._view.rbegin(), v._view.rend(), [&os](typename View::value_type c) { os << c; });
  return os;
}

template <typename View>
bool
operator==(reversed_view<View> const &lhs, reversed_view<View> const &rhs)
{
  return lhs._view == rhs._view;
}
