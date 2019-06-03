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

#include <ts/ts.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/Extractor.h"
#include "txn_box/Context.h"

using swoc::TextView;
using swoc::Errata;

Extractor::Table Extractor::_ex_table;

/* ------------------------------------------------------------------------------------ */

swoc::Rv<Extractor::Format> Extractor::parse(swoc::TextView format_string) {
  Spec literal_spec; // used to handle literals as spec instances.
  auto parser { swoc::bwf::Format::bind(format_string) };
  Format fmt;
  Errata zret;

  literal_spec._type = swoc::bwf::Spec::LITERAL_TYPE;

  while (parser) {
    Spec spec;
    std::string_view literal;
    bool spec_p = parser(literal, spec);

    if (!literal.empty()) {
      literal_spec._ext = literal;
      fmt.push_back(literal_spec);
    }

    if (spec_p) {
      if (spec._name.empty()) {
        if (spec._idx < 0) {
          zret.error(R"(Extractor missing name at offset {}.)", format_string.size() - parser._fmt
          .size());
        } else {
          fmt.push_back(spec);
        }
      } else {
        if ( auto ex { _ex_table.find(spec._name) } ; ex != _ex_table.end() ) {
          spec._extractor = ex->second;
          fmt.push_back(spec);
        } else {
          zret.error(R"(Extractor "{}" not found at offset {}.)", spec._name, format_string.size
          () - parser._fmt.size());
        }
      }
    }
  }
  return { std::move(fmt), std::move(zret) };
}

Errata Extractor::define(TextView name, self_type * ex) {
  _ex_table[name] = ex;
  return {};
}

Extractor::Type Extractor::feature_type() const { return VIEW; }

bool Extractor::has_ctx_ref() const { return false; }

Extractor::Type ViewFeature::feature_type() const { return Extractor::VIEW; }

Extractor::Type DirectFeature::feature_type() const { return Extractor::DIRECT; }

Extractor::Format::self_type Extractor::Format::push_back(Extractor::Spec const &spec) {
  _items.push_back(spec);
  if (spec._extractor && spec._extractor->has_ctx_ref()) {
    _has_ctx_ref = true;
  }
}

