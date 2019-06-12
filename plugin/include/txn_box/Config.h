/*
   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

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

  static constexpr swoc::TextView PLUGIN_NAME { "Transaction Tool Box" };
  static constexpr swoc::TextView PLUGIN_TAG { "txn_box" };

  static const std::string ROOT_KEY; ///< Root key for plugin configuration.

  /// Track the state of provided features.
  struct FeatureRefState {
    FeatureType _type { STRING }; ///< Type of active feature.
    bool _feature_active_p = false; ///< Feature is active (provided).
    bool _feature_ref_p = false; ///< Feature has been referenced / used.
    bool _rxp_group_ref_p = false; ///< Regular expression capture groups referenced / used.
    unsigned _rxp_group_count = 0; ///< Number of active capture groups - 0 => not active.
    int _rxp_line = -1; ///< Line of the active regular expression.
  };

  Config() = default;

  /** Load the configuration from the file @a file_path.
   *
   * @param file_path Path to configuration file.
   * @return Errors, if any.
   */
  Errata load_file(swoc::file::path const& file_path);

  /** Load directives at the top level.
   *
   * @param node Base plugin configuation node.
   * @return Errors, if any.
   *
   * Processing of directives directly in the base node value is handled specially
   * by this method.
   */
  Errata load_top_level_directive(YAML::Node node);

  /** Load / create a directive from a node.
   *
   * @param drtv_node Directive node.
   * @return A new directive instance, or errors if loading failed.
   */
  swoc::Rv<Directive::Handle> load_directive(YAML::Node drtv_node);

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
  swoc::Rv<Directive::Handle> load_directive(YAML::Node drtv_node, FeatureRefState& state);

  /** Parse a string as a feature extractor.
   *
   * @param fmt_node The node with the extractor.
   * @return The condensed extractor format or errors on failure.
   *
   * This must be called to parse extractors, rather than direct comparison because this does a
   * lot of required checks on the input.
   */
  swoc::Rv<Extractor::Format> parse_feature(YAML::Node fmt_node);

  /** Copy @a text to local storage in this instance.
   *
   * @param text Text to copy.
   * @return The localized copy.
   *
   * Strings in the YAML configuration are transient. If the content needs to be available at
   * run time it must be first localized.
   */
  swoc::TextView localize(swoc::TextView text);

  /** Localized a format.
   *
   * @param fmt Format to localize.
   * @return @a this
   *
   * Localize all the strings in @a fmt, which is updated in place. If @a fmt is a pure literal
   * it will be condensed in to a single item literal.
   */
  self_type & localize(Extractor::Format & fmt);

  /** Require regular expression capture vectors to support at least @a n groups.
   *
   * @param n Number of capture groups.
   * @return @a this
   */
  self_type &require_capture_count(unsigned n);

  /// Check for top level directives.
  /// @return @a true if there are any top level directives, @c false if not.
  bool has_top_level_directive() const;

  /** Get the top level directives for a @a hook.
   *
   * @param hook The hook identifier.
   * @return A reference to the vector of top level directives.
   */
  std::vector<Directive::Handle> const& hook_directives(Hook hook) const;

protected:
  friend class When;
  friend class Context;

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

  /// Top level directives for each hook. Always invoked.
  std::array<std::vector<Directive::Handle>, std::tuple_size<Hook>::value> _roots;

  /// Maximum number of directives that can execute in a specific hook. These are updated during
  /// directive load, if needed. This includes the top level directives.
  std::array<size_t, std::tuple_size<Hook>::value> _directive_count { 0 };

  /// For localizing data at a configuration level, primarily strings.
  swoc::MemArena _arena;
};

inline bool Config::has_top_level_directive() const { return _has_top_level_directive_p; }

inline std::vector<Directive::Handle> const &Config::hook_directives(Hook hook) const {
  return _roots[static_cast<unsigned>(hook)];
}

inline Config::self_type &Config::require_capture_count(unsigned n) {
  _capture_groups = std::max(_capture_groups, n);
  return *this;
}
