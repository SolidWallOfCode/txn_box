/** @file
 * Comparison classes and utilities.
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
#include "txn_box/yaml_util.h"

/** Base class for comparisons.
 *
 */
class Comparison {
  using self_type = Comparison;
public:
  /// Handle type for local instances.
  using Handle = std::unique_ptr<self_type>;

  /** Factory functor that creates an instance from a configuration node.
   *
   * @param cfg Configuration.
   * @param cmp_node Comparison node.
   * @param key Key identifying the comparison.
   * @param arg Argument, if any.
   * @param value_node The value node for the @a key.
   */
  using Loader = std::function<swoc::Rv<Handle> (Config& cfg, YAML::Node const& cmp_node, swoc::TextView const& key, swoc::TextView const& arg, YAML::Node const& value_node)>;

  // Factory that maps from names to assemblers.
  using Factory = std::unordered_map<swoc::TextView, std::tuple<Loader, ValueMask>, std::hash<std::string_view>>;

  /** Number of regular expression capture groups provided by a match.
   *
   * @return The number of capture groups, or 0 if it is not a regular expression.
   *
   * The default implementation returns @c 0, regular expression based comparisons must
   * override to return the appropriate number for the regular expression.
   */
  virtual unsigned rxp_group_count() const;

  /// @defgroup Comparison overloads.
  /// These must match the set of types in @c FeatureTypes.
  /// Subclasses (specific comparisons) should override these as appropriate for its supported types.
  /// The feature is passed by reference because comparisons are allowed to perform updates.
  /// @{
  virtual bool operator()(Context&, std::monostate& nil) const { return false; }
  virtual bool operator()(Context&, FeatureView& view) const { return false; }
  virtual bool operator()(Context&, intmax_t& n) const { return false; }
  virtual bool operator()(Context&, bool& f) const { return false; }
  virtual bool operator()(Context&, swoc::IPAddr& addr) const { return false; }
  virtual bool operator()(Context&, Cons* cons) const { return false; }
  virtual bool operator()(Context&, FeatureTuple& tuple) const { return false; }
  virtual bool operator()(Context&, Generic* g) const;
  /// @}

  bool operator()(Context& ctx, Feature& feature) const {
    auto visitor = [&](auto && value) { return (*this)(ctx, value); };
// This should work, but it doesn't. Need to find out why not.
//    return std::visit(visitor, data);
    return std::visit(visitor, static_cast<Feature::variant_type&>(feature));
  }

  /** Define a comparison.
   *
   * @param name Name for key node to indicate this comparison.
   * @param types Mask of types that are supported by this comparison.
   * @param worker Assembler to construct instance from configuration node.
   * @return A handle to a constructed instance on success, errors on failure.
   */
  static swoc::Errata define(swoc::TextView name, ValueMask const& types, Loader && worker);

  /** Load a comparison from a YAML @a node.
   *
   * @param cfg Configuration object.
   * @param ftype Type of feature for comparison.
   * @param node Node with comparison config.
   * @return A constructed instance or errors on failure.
   */
  static swoc::Rv<Handle> load(Config & cfg, ValueType ftype, YAML::Node node);

protected:
  /// The assemblers.
  static Factory _factory;
};
