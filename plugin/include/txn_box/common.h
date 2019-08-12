/** @file
  * Common types and utilities needed by all compilation units.
  *
  * Copyright 2019, Oath Inc.
  * SPDX-License-Identifier: Apache-2.0
  */

#pragma once

#include <tuple>
#include <variant>

#include <swoc/TextView.h>
#include <swoc/swoc_ip.h>
#include <swoc/Lexicon.h>
#include <swoc/bwf_fwd.h>

// Forward declares
class Config;
class Context;

namespace YAML { class Node; }

/// Separator character for names vs. argument.
static constexpr char ARG_SEP = '.';

/// Supported feature types.
enum FeatureType {
  NIL, ///< No data.
  STRING, ///< View of a string.
  INTEGER, ///< An integer.
  IP_ADDR, ///< IP Address
  BOOLEAN, ///< Boolean.
};

static constexpr std::initializer_list<FeatureType> FeatureType_LIST { NIL, STRING, INTEGER, IP_ADDR, BOOLEAN };

/// Number of feature types.
/// @internal @b MUST update this if @c FeatureType is changed.
/// @internal if @c IndexFor doesn't compile, failure to update is the most likely cause.
static constexpr size_t N_FEATURE_TYPE = FeatureType_LIST.size();

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

/// Feature descriptor storage.
/// @note view types have only the view stored here, the string memory is elsewhere.
using FeatureData = std::variant<std::monostate, FeatureView, intmax_t, swoc::IPAddr, bool>;

/// A mask indicating a set of @c FeatureType.
using FeatureMask = std::bitset<N_FEATURE_TYPE>;

/** Convert a feature @a type to a variant index.
 *
 * @param type Feature type.
 * @return Index in @c FeatureData for that feature type.
 */
inline constexpr unsigned IndexFor(FeatureType type) {
  constexpr std::array<unsigned, N_FEATURE_TYPE> IDX { 0, 1, 2, 3, 4 };
  return IDX[static_cast<unsigned>(type)];
};

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
 * @see FeatureType
 * @see FeatureMask
 */
inline FeatureMask MaskFor(FeatureType type) {
  FeatureMask mask;
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
 * @see FeatureType
 * @see FeatureMask
 */
inline FeatureMask MaskFor(std::initializer_list<FeatureType> const& types) {
  FeatureMask mask;
  for (auto type : types) {
    mask[IndexFor(type)] = true;
  }
  return mask;
}

/// Conversion between @c FeatureType and printable names.
extern swoc::Lexicon<FeatureType> FeatureTypeName;
namespace swoc {
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, FeatureType type);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, FeatureData const &feature);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, FeatureMask const &mask);
}

/// Supported hooks.
enum class Hook {
  INVALID, ///< Invalid hook (default initialization value).
  CREQ, ///< Read Request from user agent.
  PRE_REMAP, ///< Before remap.
  POST_REMAP, ///< After remap.
  PREQ, ///< Send request from proxy to upstream.
  URSP, ///< Read response from upstream.
  PRSP, ///< Send response to user agent from proxy.
  BEGIN = CREQ, ///< Iteration support.
  END = PRSP + 1 ///< Iteration support.
};

/** Convert a @c Hook enumeration to an unsigned value.
 *
 * @param id Enumeration to convert.
 * @return Numeric value of @a id.
 */
inline constexpr unsigned IndexFor(Hook id) {
  return static_cast<unsigned>(id);
}

/// Set of enabled hooks.
using HookMask = std::bitset<IndexFor(Hook::END)>;

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
 * @see FeatureType
 * @see HookMask
 */
inline HookMask MaskFor(std::initializer_list<Hook> const& hooks) {
  HookMask mask;
  for (auto hook : hooks) {
    mask[IndexFor(hook)] = true;
  }
  return mask;
}

/// Make @c tuple_size work for the @c Hook enum.
namespace std {
template<> struct tuple_size<Hook> : public std::integral_constant<size_t,
    IndexFor(Hook::END)> {
};
} // namespace std

/// Name lookup for hook values.
extern swoc::Lexicon<Hook> HookName;
extern swoc::BufferWriter& bwformat(swoc::BufferWriter& w, swoc::bwf::Spec const& spec, Hook hook);

inline FeatureView::self_type FeatureView::Literal(TextView view) { self_type zret { view }; zret._literal_p = true; return zret; }
