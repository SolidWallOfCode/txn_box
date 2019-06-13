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

#include <memory>
#include <unordered_map>
#include <functional>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"

/** Feature modification / transformation.
 *
 */

class FeatureMod {
  using self_type = FeatureMod;

public:
  /// Handle for instances.
  using Handle = std::unique_ptr<self_type>;

  /** Function to create an instance from YAML configuraion.
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
   * @return Errors, if any
   *
   * The @a feature is modified in place.
   */
  virtual swoc::Errata operator()(Context& ctx, FeatureData & feature) = 0;

  /** Check if the comparison is valid for @a type.
   *
   * @param type Type of feature to compare.
   * @return @c true if this comparison can compare to that feature type, @c false otherwise.
   */
  virtual bool is_valid_for(FeatureType type) const = 0;

  /** Output type of the modifier.
   *
   * @return The type of the modified feature.
   */
  virtual FeatureType output_type() const = 0;

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
  static swoc::Rv<Handle> load(Config& cfg, YAML::Node const& node, FeatureType ftype);

protected:
  /// Set of defined modifiers.
  using Factory = std::unordered_map<swoc::TextView, Worker, std::hash<std::string_view>>;
  /// Storage for set of modifiers.
  static Factory _factory;
};
