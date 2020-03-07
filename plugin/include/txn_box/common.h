/** @file
  * Common types and utilities needed by all compilation units.
  *
  * Copyright 2019, Oath Inc.
  * SPDX-License-Identifier: Apache-2.0
  */

#pragma once

#include <tuple>
#include <variant>

#include <swoc/swoc_meta.h>
#include <swoc/TextView.h>
#include <swoc/MemSpan.h>
#include <swoc/Errata.h>
#include <swoc/swoc_ip.h>
#include <swoc/Lexicon.h>
#include <swoc/bwf_fwd.h>

// Forward declares
class Config;
class Context;

namespace YAML { class Node; }

/// Generate an @c Errata from a format string and arguments.
template < typename ... Args > swoc::Errata Error(std::string_view const& fmt, Args && ... args) {
  return std::move(swoc::Errata().note_v(swoc::Severity::ERROR, fmt, std::forward_as_tuple(args...)));
}

/// Separate a name and argument for a directive or extractor.
extern swoc::Rv<swoc::TextView> parse_arg(swoc::TextView & key);

/** Data for a feature that is a view / string.
 *
 * This is a @c TextView with a couple of extra flags to indicate the semantic location of the
 * string memory. If neither flag is set, the string data should be presumed to exist in transient
 * transaction memory and is therefore subject to overwriting.
 *
 * This is used by the @c Context to represent string features in order to minimize string
 * copying while providing safe access to (possibly) transient data. This enables copy on use
 * rather than always copying.
 */
class FeatureView : public swoc::TextView {
  using self_type = FeatureView;
  using super_type = swoc::TextView;
public:
  bool _direct_p = false; ///< String is in externally controlled memory.
  bool _literal_p = false; ///< String is in transaction static memory.

  using super_type::super_type; ///< Import constructors.
  using super_type::operator=; ///< Import assignment.

  /** Return a literal view.
   *
   * @param view Text of the literal.
   * @return A @c FeatureView marked as a literal.
   */
  static self_type Literal(TextView view);
};

/// YAML tag type for literal (no feature extraction).
static constexpr swoc::TextView LITERAL_TAG { "literal" };

// Self referential types, forward declared.
struct Cons;
struct Feature;

/// Compact tuple representation, via a @c Memspan.
/// Tuples have a fixed size.
using FeatureTuple = swoc::MemSpan<Feature>;

/** Generic data.
 * Two uses:
 * - Very specialized types that are not general enough to warrant a top level feature type.
 * - Extension types such that non-framework code can have its own feature (sub) type.
 * This should be subclassed.
 */
class Generic {
public:
  swoc::TextView _tag; ///< Sub type identifier.

  Generic(swoc::TextView const& tag) : _tag(tag) {}
  virtual ~Generic() {}
  virtual swoc::TextView description() const { return _tag; }

  /** Extract a non-Generic feature from @a this.
   *
   * @return The equivalent non-Generic feature, or @c NIL_FEATURE if there is no conversion.
   *
   * @note The base implementation returns @c NIL_FEATURE therefore unless conversion is supported,
   * this does not need to be overridden.
   */
  virtual Feature extract() const;

  virtual bool is_nil() const { return false; }
};

/// Enumeration of types of values, e.g. those returned by a feature string or extractor.
/// This includes all of the types of features, plus some "meta" types to describe situations
/// in which the type may not be known at configuration load time.
enum ValueType : int8_t {
  NIL = 0, ///< No data.
  STRING, ///< View of a string.
  INTEGER, ///< Integer.
  IP_ADDR, ///< IP Address
  BOOLEAN, ///< Boolean.
  CONS, ///< Pointer to cons cell.
  TUPLE, ///< Array of features (@c FeatureTuple)
  GENERIC, ///< Extended type.
  NO_VALUE, ///< No value, non-existent feature.
  VARIABLE, ///< Variable / indeterminate type.
  ACTIVE, ///< The active / current feature type.
};

namespace std {
template <> struct tuple_size<ValueType> : public std::integral_constant<size_t, static_cast<size_t>(ValueType::ACTIVE) + 1> {};
}; // namespace std

// *** @c FeatureTypeList and @c FeatureType must be kept in parallel synchronization! ***
/// Type list of feature types.
/// The initial values in @c ValueType must match this list exactly.
using FeatureTypeList = swoc::meta::type_list<std::monostate, FeatureView, intmax_t, swoc::IPAddr, bool, Cons *, FeatureTuple, Generic*>;

/** Basic feature data type.
 * This is split out in order to make self-reference work. This is the actual variant, and
 * then @c FeatureData is layered on top. The self referential types (Cons, Tuple) must
 * have a defined type to use in their declaration, which cannot be the same type as the
 * variant. Unfortunately it's not possible to forward declare the variant type itself,
 * therefore the wrapper type is forward declared, the self reference uses that, and then
 * the variant type is pasted in to the forwarded declared type.
 */
using FeatureVariant = FeatureTypeList::template apply<std::variant>;

namespace swoc {
namespace meta {
template < typename GENERATOR, size_t ... IDX> constexpr
std::initializer_list<std::result_of_t<GENERATOR(size_t)>> indexed_init_list(GENERATOR && g, std::index_sequence<IDX...> && idx) { return { g(IDX)... }; }
template < size_t N, typename GENERATOR> constexpr
std::initializer_list<std::result_of_t<GENERATOR(size_t)>> indexed_init_list(GENERATOR && g) { return indexed_init_list(std::forward<GENERATOR>(g), std::make_index_sequence<N>());}

template < typename GENERATOR, size_t ... IDX> constexpr
std::array<std::result_of_t<GENERATOR(size_t)>, sizeof...(IDX)> indexed_array(GENERATOR && g, std::index_sequence<IDX...> && idx) { return std::array<std::result_of_t<GENERATOR(size_t)>, sizeof...(IDX)> { g(IDX)... }; }
template < size_t N, typename GENERATOR> constexpr
std::array<std::result_of_t<GENERATOR(size_t)>, N> indexed_array(GENERATOR && g) { return indexed_array(std::forward<GENERATOR>(g), std::make_index_sequence<N>()); }

} // namespace meta
} // namespace swoc

/** Convert a feature @a type to a variant index.
 *
 * @param type Feature type.
 * @return Index in @c FeatureData for that feature type.
 */
inline constexpr unsigned IndexFor(ValueType type) {
  auto IDX = swoc::meta::indexed_array<std::tuple_size<ValueType>::value>([](unsigned idx) { return idx; });
//  constexpr std::array<unsigned, std::tuple_size<ValueType>::value> IDX = swoc::meta::indexed_init_list<std::tuple_size<ValueType>::value>([](unsigned idx) { return idx; });
  return IDX[static_cast<unsigned>(type)];
};

/** Feature.
 * This is a wrapper on the variant type containing all the distinct feature types.
 * All of these are small and fixed size, any external storage (e.g. the text for a view)
 * is stored separately.
 *
 * @internal This is needed to deal with self-reference in the underlying variant. Some nested
 * types need to refer to @c Feature but the variant itself can't be forward declared. Instead
 * this struct is and is then used as an empty wrapper on the actual variant.
 */
struct Feature : public FeatureVariant {
  using variant_type = FeatureVariant; ///< The base variant type.
  using variant_type::variant_type; ///< Inherit all constructors.
  using self_type = Feature;
  using super_type = FeatureVariant;

  /** @a this as the super type (underlying variant class).
   *
   * @return @a this as a variant.
   *
   * This is used for a few specialized purposes where the standard variant machinery doesn't handle
   * a subclass and the exact variant class must be used.
   */
  variant_type & variant() { return *this; }

  bool is_list() const;

  /** Create a string feature by combining this feature.
   *
   * @param ctx Runtime context.
   * @param glue Separate between features.
   * @return A string feature containing this feature.
   *
   * This is simply a string rendering if @a this is a singleton. If it is a list form then the
   * list elements are rendered, separated by the @a glue. The primary use of this is to force
   * an arbitrary feature to be a string.
   */
  self_type join(Context & ctx, swoc::TextView const& glue) const;
};

/// @cond NO_DOXYGEN
// These are overloads for variant visitors so that other call sites can use @c Feature
// directly without having to reach in to the @c variant_type.
namespace std {
template < typename VISITOR > auto visit(VISITOR&& visitor, Feature & feature) -> decltype(visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type &>(feature))) {
  return visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type &>(feature));
}

template < typename VISITOR > auto visit(VISITOR&& visitor, Feature const& feature) -> decltype(visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type const&>(feature))) {
  return visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type const&>(feature));
}
} // namespace std
/// @endcond

inline ValueType ValueTypeOf(Feature const& f) {
  constexpr std::array<ValueType, FeatureTypeList::size> T { NIL, STRING, INTEGER, IP_ADDR, BOOLEAN, CONS, TUPLE, GENERIC };
  return T[f.index()];
}
/// Nil value feature.
static constexpr Feature NIL_FEATURE;

/** Standard cons cell.
 *
 * Used to build up structures that have indeterminate length in the standard cons cell way.
 */
struct Cons {
  Feature _car; ///< Immediate feature.
  Feature _cdr; ///< Next feature.
};

/// A mask indicating a set of @c ValueType.
using FeatureMask = std::bitset<FeatureTypeList::size>;
/// A mask indicating a set of @c ValueType.
using ValueMask = std::bitset<std::tuple_size<ValueType>::value>;

/// Convenience meta-function to convert a @c FeatureData index to the specific feature type.
/// @tparam F ValueType enumeration value.
template < ValueType F > using feature_type_for = std::variant_alternative_t<IndexFor(F), Feature::variant_type>;

/** Create a @c FeatureMask containing a single @a type.
 *
 * @param type Type to put in the mask.
 * @return A mask with only @a type set.
 *
 * This is useful for initializing @c const instance of @c FeatureMask. For example, if the mask
 * should be for @c STRING it would be
 *
 * @code
 *   static const FeatureMask Mask { MaskFor(STRING) };
 * @endcode
 *
 * @see ValueType
 * @see FeatureMask
 */
inline ValueMask MaskFor(ValueType type) {
  ValueMask mask;
  mask[IndexFor(type)] = true;
  return mask;
}

/** Create a @c FeatureMask containing @a types.
 *
 * @param types List of types to put in the mask.
 * @return A mask with the specified @a types set.
 *
 * @a types is an initializer list. For example, if the mask should have @c STRING and @c INTEGER
 * set, it would be
 *
 * @code
 *   static const FeatureMask Mask { MaskFor({ STRING, INTEGER}) };
 * @endcode
 *
 * This is useful for initializing @c const instance of @c FeatureMask.
 *
 * @see ValueType
 * @see FeatureMask
 */
inline ValueMask MaskFor(std::initializer_list<ValueType> const& types) {
  ValueMask mask;
  for (auto type : types) {
    mask[IndexFor(type)] = true;
  }
  return mask;
}

/** Helper template for handling @c Feature variants.
 * @tparam T The type to check against the variant type list.
 * @tparam R The return type of the function / method.
 *
 * This can be used to enable a template method for only the types in the variant.
 * @code
 *   template < typename T > auto operator() (T & t) -> EnableForFeatureTypes<T, void> { ... }
 * @endcode
 * The return type @a R can be fixed (as in this case, it is always @c void ) or it can be dependent
 * on @a T (e.g., @c T& ). This will set a class such that the function operator works for any
 * type in the Feature variant, but not other types. Note that overloads for specific Feature
 * types can be defined before such a template. This is generally done when those types are
 * usable types, with the template for a generic failure response for non-usable types.
 */
template < typename T, typename R > using EnableForFeatureTypes = std::enable_if_t<FeatureTypeList::contains<typename std::decay<T>::type>, R>;

/// Check if @a feature is nil.
inline bool is_nil(Feature const& feature) {
  if (auto gf = std::get_if<GENERIC>(&feature)) {
    return (*gf)->is_nil();
  }
  return feature.index() == IndexFor(NIL);
}
/// Check if @a feature is empty (nil or an empty string).
inline bool is_empty(Feature const& feature) { return IndexFor(NIL) == feature.index() || (IndexFor(STRING) == feature.index() && std::get<IndexFor(STRING)>(feature).empty()); }

/** Get the first element for @a feature.
 *
 * @param feature Feature from which to extract.
 * @return If @a feature is not a sequence, @a feature. Otherwise return the first feature in the
 * sequence.
 *
 */
Feature car(Feature const& feature);

/** Drop the first element in @a feature.
 *
 * @param feature Feature sequence.
 * @return If @a feature is not a sequence, or there are no more elements in @a feature, the @c NIL feature.
 * Otherwise a sequence not containing the first element of @a feature.
 *
 */
Feature & cdr(Feature & feature);

inline void clear(Feature & feature) {
  if (auto gf = std::get_if<GENERIC>(&feature) ; gf) {
    (*gf)->~Generic();
  }
  feature = NIL_FEATURE;
}

static constexpr swoc::TextView ACTIVE_FEATURE_KEY { "..." };
static constexpr swoc::TextView REMAINDER_FEATURE_KEY { "*" };

/// Conversion between @c ValueType and printable names.
extern swoc::Lexicon<ValueType> ValueTypeNames;

class TupleIterator : public Generic {
  using self_type = TupleIterator;
  using super_type = Generic;
public:
  static constexpr swoc::TextView TAG { "ITERATOR" };

  TupleIterator() : super_type{TAG} {}
  virtual ~TupleIterator() {}

  /** The value type of the tuple elements.
   *
   * @return The type of each element if the iteration is homogenous, @c ACTIVE if it is not.
   */
  virtual ValueType value_type() const { return ACTIVE; }

  /// @return @c true if the iterator has a value, @c false if at end.
  explicit virtual operator bool () const { return false; }

  virtual void advance() = 0;

  /// Restart iteration
  virtual self_type & rewind() = 0;

  /// Iteration key, to distinguish the area of iteration.
  virtual swoc::TextView iter_tag() const = 0;

};

// BufferWriter support.
namespace swoc {
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ValueType type);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Feature const &feature);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ValueMask const &mask);
}

/// Supported hooks.
enum class Hook {
  INVALID, ///< Invalid hook (default initialization value).
  CREQ, ///< Read Request from user agent.
  PRE_REMAP, ///< Before remap.
  REMAP, ///< Remap (special).
  POST_REMAP, ///< After remap.
  PREQ, ///< Send request from proxy to upstream.
  URSP, ///< Read response from upstream.
  PRSP, ///< Send response to user agent from proxy.
  CLOSE, ///< Transaction close.
};

/// Make @c tuple_size work for the @c Hook enum.
namespace std {
template<> struct tuple_size<Hook> : public std::integral_constant<size_t, static_cast<size_t>(Hook::CLOSE)+1> {};
}; // namespace std

/** Convert a @c Hook enumeration to an unsigned value.
 *
 * @param id Enumeration to convert.
 * @return Numeric value of @a id.
 */
inline constexpr unsigned IndexFor(Hook id) {
  return static_cast<unsigned>(id);
}

/// Set of enabled hooks.
using HookMask = std::bitset<std::tuple_size<Hook>::value>;

/** Create a @c HookMask containing a single @a type.
 *
 * @param hook Enumeration value for the hook to mark.
 * @return A mask with only @a type set.
 *
 * This is useful for initializing @c const instance of @c HookMask. For example, if the mask
 * should be for @c PRE_REMAP it would be
 *
 * @code
 *   static const HookMask Mask { MaskFor(Hook::PRE_REMAP) };
 * @endcode
 */
inline HookMask MaskFor(Hook hook) {
  HookMask mask;
  mask[IndexFor(hook)] = true;
  return mask;
}

/** Create a @c HookMask containing @a types.
 *
 * @param hooks Enumeration values to hooks to mark.
 * @return A mask with the specified @a types set.
 *
 * @a types is an initializer list. For example, if the mask should have @c CREQ and @c PREQ
 * set, it would be
 *
 * @code
 *   static const HookMask Mask { MaskFor({ Hook::CREQ, Hook::PREQ}) };
 * @endcode
 *
 * This is useful for initializing @c const instance of @c HookMask.
 *
 * @see ValueType
 * @see HookMask
 */
inline HookMask MaskFor(std::initializer_list<Hook> const& hooks) {
  HookMask mask;
  for (auto hook : hooks) {
    mask[IndexFor(hook)] = true;
  }
  return mask;
}

/// Name lookup for hook values.
extern swoc::Lexicon<Hook> HookName;
extern swoc::BufferWriter& bwformat(swoc::BufferWriter& w, swoc::bwf::Spec const& spec, Hook hook);

/** Create a feature that is a literal string of @a view.
 *
 * @param view Soure string.
 * @return A literal feature that is the same as @a view.
 */
inline FeatureView FeatureView::Literal(TextView view) { self_type zret { view }; zret._literal_p = true; return zret; }

/// Conversion enumeration for checking boolean strings.
enum BoolTag {
  INVALID = -1,
  False = 0,
  True = 1,
};
/// Mapping of strings to boolean values.
/// This is for handling various synonymns in a consistent manner.
extern swoc::Lexicon<BoolTag> BoolNames;

/// Container for global data.
struct Global {
  swoc::Errata _preload_errata;
  int TxnArgIdx = -1;

  void reserve_txn_arg();

  // -- Reserved keys -- //
  /// Standard name for nested directives and therefore reserved globally.
  static constexpr swoc::TextView DO_KEY = "do";

};

/// Global data.
extern Global G;
