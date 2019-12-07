/** @file
 * Configuration classes.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <array>
#include <vector>

#include <swoc/TextView.h>
#include <swoc/MemArena.h>
#include <swoc/swoc_file.h>

#include "txn_box/common.h"
#include "txn_box/Extractor.h"
#include "txn_box/Directive.h"
#include "txn_box/yaml_util.h"

using TSCont = struct tsapi_cont *;
using TSHttpTxn = struct tsapi_httptxn *;

/// Contains a configuration and configuration helper methods.
/// This is also used to pass information between node parsing during configuration loading.
class Config {
  using self_type = Config; ///< Self reference type.
  using Errata = swoc::Errata;
public:

  /// Full name of the plugin.
  static constexpr swoc::TextView PLUGIN_NAME { "Transaction Tool Box" };
  /// Tag name of the plugin.
  static constexpr swoc::TextView PLUGIN_TAG { "txn_box" };

  static const std::string ROOT_KEY; ///< Root key for plugin configuration.

  /// Type of a view.
  enum class StrType {
    VIEW, ///< Standard view.
    C, ///< C string
  };

  /// Track the state of provided features.
  struct FeatureRefState {
    ValueType _type { STRING }; ///< Type of active feature.
    bool _feature_active_p = false; ///< Feature is active (provided).
    bool _feature_ref_p = false; ///< Feature has been referenced / used.
    bool _rxp_group_ref_p = false; ///< Regular expression capture groups referenced / used.
    unsigned _rxp_group_count = 0; ///< Number of active capture groups - 0 => not active.
    int _rxp_line = -1; ///< Line of the active regular expression.
  };

  /// Global and session variable map.
  using Variables = std::map<swoc::TextView, unsigned>;

  /// Default constructor, makes an empty instance.
  Config();

  /** Parse YAML from @a node to initialize @a this configuration.
   *
   * @param root Root node.
   * @param path Path from root node to the configuration based node.
   * @param hook Default hook for directives.
   * @return Errors, if any.
   *
   * The @a path is an @c ARG_SEP separate list of keys. The value of the last key is the
   * node that is parsed. If the path is a single @c ARG_SEP the root node is parsed.
   *
   * If @a hook is @c Hook::INVALID then the directives must all be @c WHEN directives.
   * Otherwise the directives are put in the @a hook bucket if not @c WHEN. If @c WHEN the
   * directive is unpacked and put in the bucket specified by the @c WHEN.
   *
   * @note Currently only @c Hook::REMAP is used for @a hook to handle the special needs of
   * a remap based configuration.
   *
   */
  Errata parse_yaml(YAML::Node const& root, swoc::TextView path, Hook hook = Hook::INVALID);

  /** Load directives at the top level.
   *
   * @param node Base plugin configuation node.
   * @return Errors, if any.
   *
   * Processing of directives directly in the base node value is handled specially
   * by this method.
   */
  Errata load_top_level_directive(YAML::Node node);

  Errata load_remap_directive(YAML::Node node);

  /** Load / create a directive from a node.
   *
   * @param drtv_node Directive node.
   * @return A new directive instance, or errors if loading failed.
   */
  swoc::Rv<Directive::Handle> parse_directive(YAML::Node const& drtv_node);

  /** Load / create a directive from a node.
   *
   * @param drtv_node Directive node.
   * @param state A reference state to use for the directives in @a node.
   *
   * @return A new directive instance, or errors if loading failed.
   *
   * This is used by directives that provide a feature and contain other directives. The
   * @a state provides information on feature provision.
   */
  swoc::Rv<Directive::Handle> parse_directive(YAML::Node const& drtv_node, FeatureRefState& state);

  /** Parse a node as a feature extractor.
   *
   * @param fmt_node The node with the extractor.
   * @param str_type Standard view or C string.
   * @return The condensed extractor format or errors on failure.
   *
   * This does extensive work for handle the various feature extraction capabilities. This should
   * be bypassed only in extreme cases where very specialized handling is needed. The result of
   * this can be passed to @c Context::extract to get the actual value at runtime.
   *
   * @see Context::extract
   */
  swoc::Rv<Extractor::Format> parse_feature(YAML::Node fmt_node, StrType str_type = StrType::VIEW);

  /** Copy @a text to local storage in this instance.
   *
   * @param text Text to copy.
   * @return The localized copy.
   *
   * Strings in the YAML configuration are transient. If the content needs to be available at
   * run time it must be first localized.
   */
  std::string_view& localize(std::string_view & text);
  swoc::TextView localize(std::string_view const& text) { swoc::TextView tv { text }; return this->localize(tv); }

  /** Localized a format.
   *
   * @param fmt Format to localize.
   * @return @a this
   *
   * Localize all the strings in @a fmt, which is updated in place. If @a fmt is a pure literal
   * it will be condensed in to a single item literal.
   */
  self_type & localize(Extractor::Format & fmt);

  self_type& localize(Feature & feature);

//  template < typename T > auto localize(T & data) -> typename std::enable_if<swoc::meta::is_any_of<T, feature_type_for<NIL>, feature_type_for<INTEGER>, feature_type_for<BOOLEAN>, feature_type_for<IP_ADDR>, feature_type_for<CONS>, feature_type_for<TUPLE>>::value, self_type&>::type { return *this; }
//  template < typename T > auto localize(T & data) -> typename std::enable_if_t<swoc::meta::is_any_of<T, feature_type_for<NIL>, feature_type_for<INTEGER>, feature_type_for<BOOLEAN>, feature_type_for<IP_ADDR>, feature_type_for<CONS>, feature_type_for<TUPLE>>::value, self_type&> { return *this; }
  template < typename T > auto localize(T & data) -> EnableForFeatureTypes<T, self_type&> { return *this; }

  /** Allocate config space for an array of @a T.
   *
   * @tparam T Element type.
   * @param count # of elements.
   * @return A span covering the allocated array.
   *
   * This allocates in the config storage. No destructors are called when the config is destructed.
   */
  template < typename T > swoc::MemSpan<T> span(unsigned count) {
    return _arena.alloc(sizeof(T) * count).rebind<T>();
  }

  /** Hook for which the directives are being loaded.
   *
   * @return The current hook.
   */
  Hook current_hook() const;

  ValueType active_feature_type() const { return _feature_state ? _feature_state->_type : NO_VALUE; }

  /** Require regular expression capture vectors to support at least @a n groups.
   *
   * @param n Number of capture groups.
   * @return @a this
   */
  self_type &require_rxp_group_count(unsigned n);


  self_type &reserve_slot(Hook hook) { ++_directive_count[IndexFor(hook)]; return *this; }

  /// Check for top level directives.
  /// @return @a true if there are any top level directives, @c false if not.
  bool has_top_level_directive() const;

  /** Get the top level directives for a @a hook.
   *
   * @param hook The hook identifier.
   * @return A reference to the vector of top level directives.
   */
  std::vector<Directive::Handle> const& hook_directives(Hook hook) const;

  /** Define a directive.
   *
   */
  static swoc::Errata define(swoc::TextView name, HookMask const& hooks, Directive::Worker const& worker, Directive::Options const& opts = Directive::Options {});

protected:
  friend class When;
  friend class Context;

  // Transient properties
  /// Current hook for directives being loaded.
  Hook _hook = Hook::INVALID;

  /// Mark whether there are any top level directives.
  bool _has_top_level_directive_p { false };

  /// Maximum number of capture groups for regular expression matching.
  unsigned _capture_groups = 1;

  /** @defgroup Feature reference tracking.
   * A bit obscure but necessary because the active feature and the active capture groups must
   * be tracked independently because either can be overwritten independent of the other. When
   * directives are processed, a state instance can be passed to track back references. This is
   * checked and the pointers updated to point to that iff the incoming state marks the corresopnding
   * tracking as active. These can therefore point to different states at different levels of
   * recursion, or the same. This allows the tracking to operate in a simple way, updating the data
   * for the specific tracking without having to do value checks.
   */
  /// @{
  FeatureRefState* _feature_state = nullptr; ///< Feature.
  FeatureRefState* _rxp_group_state = nullptr; ///< Regular expression capture groups.
  /// #}

  /// Current amount of shared config storage required.
  size_t _cfg_storage_required = 0;

  /// Current amount of shared context storage required.
  size_t _ctx_storage_required = 0;

  /// Directive info for all directive types.
  std::vector<Directive::CfgInfo> _drtv_info;

  /// A factory that maps from directive names to generator functions (@c Worker instances).
  using Factory = std::unordered_map<std::string_view, std::tuple<HookMask, Directive::Worker, Directive::StaticInfo>>;

  /// The set of defined directives..
  static Factory _factory;

  /// Top level directives for each hook. Always invoked.
  std::array<std::vector<Directive::Handle>, std::tuple_size<Hook>::value> _roots;

  /// Maximum number of directives that can execute in a hook. These are updated during
  /// directive load, if needed. This includes the top level directives.
  std::array<size_t, std::tuple_size<Hook>::value> _directive_count { 0 };

  /// For localizing data at a configuration level, primarily strings.
  swoc::MemArena _arena;

  swoc::Rv<Directive::Handle> load_directive(YAML::Node const& drtv_node);
};

inline Hook Config::current_hook() const { return _hook; }
inline bool Config::has_top_level_directive() const { return _has_top_level_directive_p; }

inline std::vector<Directive::Handle> const &Config::hook_directives(Hook hook) const {
  return _roots[static_cast<unsigned>(hook)];
}

inline Config::self_type &Config::require_rxp_group_count(unsigned n) {
  _capture_groups = std::max(_capture_groups, n);
  return *this;
}
