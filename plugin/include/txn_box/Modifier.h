/** @file
 * Feature modifier bases classes.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <functional>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"

/** Feature modification / transformation.
 *
 */
class Modifier {
  using self_type = Modifier; ///< Self reference type.

public:
  /// Handle for instances.
  using Handle = std::unique_ptr<self_type>;

  /** Function to create an instance from YAML configuration.
   * @param cfg The configuration state object.
   * @param mod_node The YAML node for the feature modifier.
   * @param key_node The YAML node in @a mod_node that identified the modifier.
   * @return A handle for the new instance, or errors if any.
   */
  using Worker = std::function<swoc::Rv<Handle> (Config& cfg, YAML::Node const& mod_node, YAML::Node const& key_node)>;

  /** Modification operator.
   *
   * @param ctx Runtime transaction context.
   * @param feature Feature to modify.
   * @return Modified feature, or errors.
   */
  virtual swoc::Rv<Feature> operator()(Context& ctx, Feature const& feature) = 0;

  /** Check if the comparison is valid for @a type.
   *
   * @param type Type of feature to compare.
   * @return @c true if this comparison can compare to that feature type, @c false otherwise.
   */
  virtual bool is_valid_for(ValueType type) const = 0;

  /** Output type of the modifier.
   *
   * @param in The input type for the modifier.
   * @return The type of the modified feature.
   */
  virtual ValueType result_type(ValueType in) const = 0;

  /** Define a mod for @a name.
   *
   * @param name Name of the mode.
   * @param f Instance constructor.
   * @return Errors, if any.
   *
   */
  static swoc::Errata define(swoc::TextView name, Worker const& f);

  /** Load an instance from YAML.
   *
   * @param cfg Config state object.
   * @param node Node containing the modifier.
   * @param ftype Feature type to modify.
   * @return
   */
  static swoc::Rv<Handle> load(Config& cfg, YAML::Node const& node, ValueType ftype);

protected:
  /// Set of defined modifiers.
  using Factory = std::unordered_map<swoc::TextView, Worker, std::hash<std::string_view>>;
  /// Storage for set of modifiers.
  static Factory _factory;
};
