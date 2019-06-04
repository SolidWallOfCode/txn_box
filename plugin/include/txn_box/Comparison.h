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

#include "txn_box/yaml_util.h"
#include "txn_box/common.h"

class Config;

class Comparison {
  using self_type = Comparison;
public:
  /// Handle type for local instances.
  using Handle = std::unique_ptr<self_type>;

  /// Factory functor that creates an instance from a configuration node.
  /// Arguments are the comparison node and the value for the comparison identity key.
  using Assembler = std::function<swoc::Rv<Handle> (Config&, YAML::Node, YAML::Node)>;

  // Factory that maps from names to assemblers.
  using Factory = std::unordered_map<swoc::TextView, Assembler, std::hash<std::string_view>>;

  /** Check if the comparison is valid for @a type.
   *
   * @param type Type of feature to compare.
   * @return @c true if this comparison can compare to that feature type, @c false otherwise.
   */
  virtual bool is_valid_for(FeatureType type) const = 0;

  /** Define an assembler that constructs @c Comparison instances.
   *
   * @param name Name for key node to indicate this comparison.
   * @param cmp_asm Assembler to construct instance from configuration node.
   * @return A handle to a constructed instance on success, errors on failure.
   */
  static swoc::Errata define(swoc::TextView name, Assembler && cmp_asm);

  /** Load a comparison from a YAML @a node.
   *
   * @param cfg Configuration object.
   * @param type Type of feature for comparison.
   * @param node Node with comparison config.
   * @return A constructed instance or errors on failure.
   */
  static swoc::Rv<Handle> load(Config & cfg, FeatureType type, YAML::Node node);

protected:
  /// The assemblers.
  static Factory _factory;
};

/// Subclass of comparison for string comparisons.
class StringComparison : public Comparison {
  using self_type = StringComparison;
  using super_type = Comparison;
public:

  bool is_valid_for(FeatureType type) const override;

  virtual bool operator () (swoc::TextView text) = 0;

protected:
};
