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

/// Separator character for names vs. argument.
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
using FeatureTypeList = swoc::meta::type_list<std::monostate, FeatureView, intmax_t, swoc::IPAddr, bool, Cons *, FeatureTuple>;

/** Basic feature data type.
 * This is split out in order to make self-reference work. This is the actual variant, and
 * then @c FeatureData is layered on top. The self referential types (Cons, Tuple) must
 * have a defined type to use in their declaration, which cannot be the same type as the
 * variant. Unfortunately it's not possible to forward declare the variant type itself,
 * therefore the wrapper type is forward declared, the self reference uses that, and then
 * the variant type is pasted in to the forwarded declared type.
 */
using FeatureVariant = FeatureTypeList::template apply<std::variant>;

/** Convert a feature @a type to a variant index.
 *
 * @param type Feature type.
 * @return Index in @c FeatureData for that feature type.
 */
inline constexpr unsigned IndexFor(ValueType type) {
  constexpr std::array<unsigned, FeatureTypeList::size> IDX {0, 1, 2, 3, 4, 5, 6 };
  return IDX[static_cast<unsigned>(type)];
};

/** Feature data.
 * This is a wrapper on the variant type containing all the distinct feature types.
 * All of these are small and fixed size, any external storage (e.g. the text for a view)
 * is stored separately.
 */
struct Feature : public FeatureVariant {
  using variant_type = FeatureVariant; ///< The base variant type.
  using variant_type::variant_type; ///< Inherit all constructors.
};

/// @cond NO_DOXYGEN
// These are overloads for variant visitors so that other call sites can use @c FeatureData
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
inline bool is_nil(Feature const& feature) { return feature.index() == IndexFor(NIL); }
/// Check if @a feature is empty (nil or an empty string).
inline bool is_empty(Feature const& feature) { return IndexFor(NIL) == feature.index() || (IndexFor(STRING) == feature.index() && std::get<IndexFor(STRING)>(feature).empty()); }

/** Get the first element for @a feature.
 *
 * @param feature Feature from which to extract.
 * @return If @a feature is not a sequence, @a feature. Otherwise return the first feature in the
 * sequence.
 *
 */
Feature const& car(Feature const& feature);
/** Drop the first element in @a feature.
 *
 * @param feature Feature sequence.
 * @return If @a feature is not a sequence, or there are no more elements in @a feature, the @c NIL feature.
 * Otherwise a sequence not containing the first element of @a feature.
 *
 */
Feature cdr(Feature const& feature);

/// Conversion between @c ValueType and printable names.
extern swoc::Lexicon<ValueType> ValueTypeNames;

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
};

/// Global data.
extern Global G;
