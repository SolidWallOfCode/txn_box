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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/yaml_util.h"
#include "txn_box/common.h"

class Config;

/** Regular expression support.
 *
 * This is split out from @c Comparison because regular expressions will be used in additional
 * situations. It is non-copyable because it is basically a wrapper on a non-shared PCRE code
 * block and it seems silly to have a handle to what is effectively a handle. Aggregrating classes
 * can deal with it the same way as a @c std::unique_ptr.
 */
class Rxp {
  using self_type = Rxp;
  struct PCRE_Deleter {
    void operator()(pcre2_code* ptr) { pcre2_code_free(ptr); }
  };
  using RxpHandle = std::unique_ptr<pcre2_code, PCRE_Deleter>;

public:
  Rxp() = default;
  Rxp(self_type const&) = delete;
  Rxp(self_type && that) : _rxp(std::move(that._rxp)) {}
  self_type & operator = (self_type const&) = delete;

  size_t capture_count() const {
    uint32_t count = 0;
    auto result = pcre2_pattern_info(_rxp.get(), PCRE2_INFO_CAPTURECOUNT, &count);
    return result == 0 ? count : 0;
  }

  static swoc::Rv<self_type> parse(swoc::TextView str);
protected:
  RxpHandle _rxp;

  Rxp(pcre2_code* rxp) : _rxp(rxp) {}
};

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

  bool operator()(FeatureData & data) {
    return std::visit(*this, data);
  }

  /// @defgroup Comparison overloads.
  /// These must match the set of types in @c FeatureData.
  /// Subclasses (specific comparisons) should override these as appropriate for its supported types.
  /// The feature is passed by reference because comparisons are allowed to perform updates.
  /// @{
  virtual bool operator()(swoc::TextView& view) const { return false; }
  virtual bool operator()(intmax_t& n) const { return false; }
  virtual bool operator()(bool& f) const { return false; }
  virtual bool operator()(swoc::IPAddr & addr) const { return false; }
  /// @}

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
