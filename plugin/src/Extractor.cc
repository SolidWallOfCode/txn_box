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
using swoc::Rv;
using namespace swoc::literals;

Extractor::Table Extractor::_ex_table;

/* ------------------------------------------------------------------------------------ */

Extractor::Format Extractor::literal(TextView format_string) {
  Spec lit;
  Format fmt;
  lit._type = swoc::bwf::Spec::LITERAL_TYPE;
  lit._ext = format_string;
  fmt.push_back(lit);
  return std::move(fmt);
}

Rv<Extractor::Format> Extractor::parse(TextView format_string) {
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
        zret.error(R"(Extractor missing name at offset {}.)", format_string.size() - parser._fmt.size());
      } else {
        if (spec._idx >= 0) {
          fmt.push_back(spec);
          fmt._max_arg_idx = std::max(fmt._max_arg_idx, spec._idx);
        } else if ( auto ex { _ex_table.find(spec._name) } ; ex != _ex_table.end() ) {
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

//Extractor::Type Extractor::feature_type() const { return STRING; }

bool Extractor::has_ctx_ref() const { return false; }

/* ------------------------------------------------------------------------------------ */

Extractor::Format::self_type & Extractor::Format::push_back(Extractor::Spec const &spec) {
  _specs.push_back(spec);
  // update properties.
  if (spec._type == swoc::bwf::Spec::LITERAL_TYPE) {
    _direct_p = false; // literals aren't direct.
  } else {
    _literal_p = false;
    if (_specs.size() == 1) {
      if (spec._extractor) {
        _feature_type = spec._extractor->feature_type();
        if (nullptr == dynamic_cast<DirectFeature*>(spec._extractor)) {
          _direct_p = false;
        }
      }
    } else { // multiple items
      _direct_p = false;
    }
  }
  return *this;
}

bool Extractor::FmtEx::operator()(std::string_view &literal, Extractor::Spec &spec) {
  bool zret = false;
  if (_iter->_type == swoc::bwf::Spec::LITERAL_TYPE) {
    literal = _iter->_ext;
    if (++_iter == _specs.end()) { // all done!
      return zret;
    }
  }
  if (_iter->_type != swoc::bwf::Spec::LITERAL_TYPE) {
    spec = *_iter;
    ++_iter;
    zret = true;
  }
  return zret;
}

/* ------------------------------------------------------------------------------------ */
