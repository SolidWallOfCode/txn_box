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

/// Supported feature types.
enum FeatureType {
  VIEW, ///< View of a string.
  INTEGER, ///< An integer.
  IP_ADDR, ///< IP Address
  BOOL ///< Boolean.
};

/** Data storage for a feature.
 * This is carefully arranged to have types in the same order as @c Type.
 */
using FeatureData = std::variant<swoc::TextView, intmax_t, swoc::IPAddr, bool>;

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
