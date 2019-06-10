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
   * @param feature_type Provided feature type.
   * @param referenced_p Set if feature was referenced.
   *
   * @return A new directive instance, or errors if loading failed.
   *
   * This is used by directives that provide a feature and contain other directives. The type of
   * the feature must be specified and also a flag, which is set if any of the directives loaded
   * by this call reference the feature. This can be used to perform optimizations if desired.
   */
  swoc::Rv<Directive::Handle> load_directive(YAML::Node drtv_node, Extractor::Type feature_type, bool& referenced_p);

  /** Parse a string as a feature extractor.
   *
   * @param fmt The node with the extractor.
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

  /// Check for active directives.
  /// @return @a true if there are any top level directives, @c false if not.
  bool is_active() const;

  /** Get the top level directives for a @a hook.
   *
   * @param hook The hook identifier.
   * @return A reference to the vector of top level directives.
   */
  std::vector<Directive::Handle> const& hook_directives(Hook hook) const;

  #if 0
  /** Indicate an extractor string is used by the @c Context.
   *
   * @param fmt Condensed extractor format string.
   * @return @a this
   */
  self_type &use_extractors(Extractor::Format & fmt);

  /** Indicate a directive provides a context based feature.
   *
   * @param fmt Condensed extractor format string that defines the feature.
   * @return @a this
   */
  self_type &provides(Extractor::Format & fmt);
  #endif

protected:
  friend class When;
  friend class Context;

  /// Mark whether there are any top level directives.
  bool _active_p { false };

  /// Flag and reference. If set, there is an active feature and this is the reference flag.
  /// If not, then no active feature.
  bool * _feature_ref_p { nullptr };

  /// If a feature is active, this is the type.
  Extractor::Type _feature_type { VIEW };

  /// Top level directives for each hook. Always invoked.
  std::array<std::vector<Directive::Handle>, std::tuple_size<Hook>::value> _roots;

  /// Maximum number of directives that can execute in a specific hook. These are updated during
  /// directive load, if needed. This includes the top level directives.
  std::array<size_t, std::tuple_size<Hook>::value> _directive_count { 0 };

  /// For localizing data at a configuration level, primarily strings.
  swoc::MemArena _arena;
};

inline bool Config::is_active() const { return _active_p; }

inline std::vector<Directive::Handle> const &Config::hook_directives(Hook hook) const {
  return _roots[static_cast<unsigned>(hook)];
}
