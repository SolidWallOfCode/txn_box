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

#include <unordered_map>
#include <string_view>

#include <swoc/TextView.h>
#include <swoc/bwf_base.h>
#include <swoc/Errata.h>

#include "txn_box/Context.h"

class Extractor {
  using self_type = Extractor; ///< Self reference type.
public:
  /// Preferred type, if any, for the extracted feature.
  enum Type {
    STRING, ///< A string.
    INTEGER, ///< An integer.
    BOOL, ///< Boolean.
    IP_ADDR ///< IP Address
  };

  using Table = std::unordered_map<std::string_view, self_type *>;

  struct Spec : public swoc::bwf::Spec {
    /// Extractor used in the spec, if any.
    self_type * _extractor = nullptr;
  };

  /// Compiled format string containing extractors.
  using Format = std::vector<Spec>;

  virtual Type preferred_type() const = 0;
  virtual swoc::TextView direct_view(Context const& ctx) const = 0;

  /** Parse a format string.
   *
   * @param format_string Format string.
   * @param table
   * @return
   */
  static swoc::Rv<Format> parse(swoc::TextView format_string, Table const& table);
};

class StringFeature {
public:
  Extractor::Type preferred_type const { return Extractor::STRING; }
};

class IPAddrFeature {
};

class IntegerFeature {
};
