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

#include <tuple>
#include <variant>

#include <swoc/swoc_ip.h>
#include <swoc/Lexicon.h>

namespace swoc {
class BufferWriter;
namespace bwf {
class Spec;
} // namespace bwf
} // namespace swoc

/// Supported feature types.
enum FeatureType {
  VIEW, ///< View of a string.
  INTEGER, ///< An integer.
  IP_ADDR, ///< IP Address
  BOOL, ///< Boolean.
};

/** Data for a feature that is a view / string.
 * This is a @c TextView with a couple of extra flags to indicate the semantic location of the
 * string memory. If neither flag is set, the string data should be presumed to exist in transient
 * transaction memory and is therefore subject to overwriting.
 */
class FeatureView : public swoc::TextView {
  using self_type = FeatureView;
  using super_type = swoc::TextView;
public:
  bool _direct_p = false; ///< String is in externally controlled memory.
  bool _literal_p = false; ///< String is in transaction static memory.

  using super_type::super_type;
  using super_type::operator=;

  static self_type Literal(TextView view) { self_type zret { view }; zret._literal_p = true; return zret; }
};

/// Feature descriptor storage.
/// @note view types have only the view stored here, the string memory is elsewhere.
using FeatureData = std::variant<FeatureView, intmax_t, swoc::IPAddr, bool>;

/** Convert a feature @a type to a variant index.
 *
 * @param type Feature type.
 * @return Index in @c FeatureData for that feature type.
 */
inline unsigned IndexFor(FeatureType type) {
  static constexpr std::array<unsigned, 4> IDX { 0, 1, 2, 3, };
  return IDX[static_cast<unsigned>(type)];
};

extern swoc::Lexicon<FeatureType> FeatureTypeName;
extern swoc::BufferWriter& bwformat(swoc::BufferWriter& w, swoc::bwf::Spec const& spec, FeatureType type);

/// Supported hooks.
enum class Hook {
  INVALID, ///< Invalid hook (default initialization value).
  CREQ, ///< Read Request from user agent.
  PREQ, ///< Send request from proxy to upstream.
  URSP, ///< Read response from upstream.
  PRSP, ///< Send response to user agent from proxy.
  BEGIN = CREQ, ///< Iteration support.
  END = PRSP + 1 ///< Iteration support.
};

/// Make @c tuple_size work for the @c Hook enum.
namespace std {
template<> struct tuple_size<Hook> : public std::integral_constant<size_t,
    static_cast<size_t>(Hook::END) - 1> {
};
} // namespace std

/// Name lookup for hook values.
extern swoc::Lexicon<Hook> HookName;
