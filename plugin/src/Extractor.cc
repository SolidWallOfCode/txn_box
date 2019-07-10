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
#include "txn_box/Config.h"

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
/* ---------------------------------------------------------------------------------------------- */
auto FeatureGroup::Tracking::find(swoc::TextView const &name) -> Tracking::Info * {
  Info * spot  = std::find_if(_info.begin(), _info.end(), [&](auto & t) { return 0 == strcasecmp(t._name, name); });
  return spot == _info.end() ? nullptr : &*spot;
}

Errata FeatureGroup::load_fmt(Config & cfg, Tracking& info, YAML::Node const &node) {
  auto && [ fmt, errata ] { cfg.parse_feature(node) };
  if (errata.is_ok()) {
    // Walk the items to see if any are cross references.
    for ( auto const& item : fmt ) {
      if (item._extractor == &ex_this) {
        errata = this->load_key(cfg, info, item._ext);
        if (! errata.is_ok()) {
          break;
        }
      }
    }
  }
  return std::move(errata);
}

Errata FeatureGroup::load_key(Config &cfg, FeatureGroup::Tracking &info, swoc::TextView name)
{
  Errata errata;

  auto tinfo = info.find(name);
  if (tinfo->_mark == DONE) {
    return std::move(errata);
  }

  if (tinfo->_mark == IN_PLAY) {
    errata.error(R"(Circular dependency for key "{}" at {}.)", name, info._node.Mark());
    return std::move(errata);
  }

  if (tinfo->_mark == MULTI_VALUED) {
    errata.error(R"(A multi-valued key cannot be referenced - "{}" at {}.)", name, info._node.Mark());
    return std::move(errata);
  }

  tinfo->_mark = IN_PLAY;
  if (auto n { info._node[name] } ; n) {
    if (n.IsScalar()) {
      errata = this->load_fmt(cfg, info, n);
    } else if (n.IsSequence()) {
      // many possibilities - empty, singleton, modifier, list of formats.
      if (n.size() == 0) {
        if (tinfo->_required_p) {
          errata.error(R"(Required key "{}" at {} has an empty list with no extraction formats.)", name, info._node.Mark());
        }
      } else if (n.size() == 1) {
        errata = this->load_fmt(cfg, info, n);
      } else { // > 1
        if (n[1].IsMap()) {
          errata = this->load_fmt(cfg, info, n);
        } else { // list of formats.
          tinfo->_multi_found_p = true;
          for (auto const &child : n) {
            errata = this->load_fmt(cfg, info, n);
            if (!errata.is_ok()) {
              break;
            }
          }
        }
      }
    } else {
      errata.error(R"(extraction format expected but is not a scalar nor a list.)", name, info._node.Mark());
    }

    if (! errata.is_ok()) {
      errata.info(R"(While loading extraction format for key "{}" at {}.)", name, info._node.Mark());
    }
  } else if (tinfo->_required_p) {
    errata.error(R"(Required key "{}" not found in value at {}.)", name, info._node.Mark());
  }

  if (tinfo->_multi_found_p) {
    tinfo->_mark = MULTI_VALUED;
  } else {
    tinfo->_mark = DONE;
    tinfo->_idx = info._idx++;
  }
  return std::move(errata);
}

Errata FeatureGroup::load(Config & cfg, YAML::Node const& node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  unsigned idx = 0;
  unsigned n_keys = node.size(); // Number of keys in @a node.

  unsigned t_idx = 0; // # of valid entries in Tracking, index of next element to use.
  Tracking::Info tracking_info[n_keys];
  Tracking tracking(node, tracking_info, n_keys);

  // Seed tracking info with the explicit keys.
  for ( auto & d : ex_keys ) {
    auto tinfo = tracking.alloc();
    tinfo->_name = d._name;
    tinfo->_required_p = d._flags[REQUIRED];
    tinfo->_multi_allowed_p = d._flags[MULTI];
  }

  // Time to get the formats and walk the references.
  for ( auto & d : ex_keys ) {
    auto errata { this->load_key(cfg, tracking, d._name) };
    if (! errata.is_ok()) {
      return std::move(errata);
    }
  }

  // Final ordering is set, create extraction array.
  _exf_data = cfg.span<ExfData>(tracking._idx);

  return {};
}
/* ---------------------------------------------------------------------------------------------- */
