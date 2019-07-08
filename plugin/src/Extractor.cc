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
Errata FeatureGroup::load(Config & cfg, YAML::Node const& node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  static constexpr uint8_t DONE = 1;
  static constexpr uint8_t IN_PLAY = 2;
  static constexpr uint8_t MULTI_VALUED = 3;
  unsigned idx = 0;
  unsigned n_keys = node.size(); // Number of keys in @a node.

  // Initial tracking - must track data about each key before that key's order is finalized.
  // The first time a key is encountered at all it is put in this array.
  struct Tracking {
    TextView _name; ///< Name.
    unsigned _idx; ///< Index in final ordering.
    uint8_t _mark; ///< Ordering search march.
    uint8_t _required_p : 1; ///< Key must exist and have a valid format.
    uint8_t _multi_allowed_p : 1; ///< Format can be a list of formats.
    uint8_t _multi_found_p : 1; ///< Format was parsed and was a list of formats.
  };
  unsigned t_idx = 0; // # of valid entries in Tracking, index of next element to use.
  Tracking tracking[n_keys];
  Tracking * tracking_limit = tracking + n_keys;
  memset(tracking, 0, sizeof(tracking[0]) * n_keys);
  // Find the tracking entry by key name.
  // It is a basic assumption that the number of keys is small and it's faster to linear search
  // than to pay the costs (particularly allocation) of a more complex data structure.
  auto find_tracking_by_name = [&](TextView const& name) {
    Tracking * spot  = std::find_if(tracking, tracking_limit, [&](auto & t) { return 0 == strcasecmp(t._name, name); });
    return spot == tracking_limit ? nullptr : &*spot;
  };

  // This is the final ordered array. It will be copied in to configuration storage once the
  // size and order is fixed. It is allocated as the number of keys in the node, but most of the time
  // not every key will have a format to extact and the final array will be shorter.
  unsigned ex_idx = 0; ///< # of valid items in @a exdata
  ExData exdata[n_keys];
  for ( ExData & ex : exdata ) {
    new(&ex) ExData;
  }

  // Do the explicit keys.
  // This handles all keys because a key is implicit only if it is reached from an explicit key.
  for ( auto & d : ex_keys ) {
    // Put it in the tracking if it's not already there.
    Tracking * tinfo = find_tracking_by_name(d._name);
    if (nullptr == tinfo) {
      tinfo = tracking + t_idx++;
      tinfo->_name = d._name;
      for ( auto const& flag : d._flags ) {
        switch (flag) {
          case NONE: break;
          case REQUIRED: tinfo->_required_p = true;
            break;
          case MULTI: tinfo->_multi_allowed_p = true;
            break;
        }
      }
    } else if (tinfo->_mark == IN_PLAY) {
        // Circular dependency, FAIL.
    } else if (tinfo->_mark == DONE) {
      // ?? In the list twice ??
    }

    // Time to get the formats and walk the references.
    tinfo->_mark = IN_PLAY;
    if (auto n { node[d._name] } ; n) {
      if (n.IsScalar()) {
        auto && [ fmt, errata ] { cfg.parse_feature(n) };
        if (errata.is_ok()) {
          for ( auto const& item : fmt ) {
            if (item._extractor == &ex_this) {
              Tracking * rinfo = find_tracking_by_name(item._ext);
              if (nullptr == rinfo) {
                // Recurse here?
              }
              if (rinfo->_mark != DONE) {
                // Circular reference, FAIL.
              }
            }
          }
        }
      } else if (n.IsSequence()) {
        if (n.size() == 1) {
        } else if (n.size() > 1) {
          if (n[1].IsMap()) {
            auto &&[fmt, errata]{cfg.parse_feature(n)};
          } else { // list of formats.
            for ( auto const& child : n ) {
              auto && [ fmt, errata] { cfg.parse_feature(child) };
            }
          }
        } else {
        }
      } else {
        // FAIL
      }
    } else if (tinfo->_required_p) {
      // required but not found. FAIL.
    }
    // Get format.
    // Walk format for references.
    // Recurse on any reference.
    tinfo->_mark = DONE;
    // Order for this key finalized, put it in @a exdata.
  }
}
/* ---------------------------------------------------------------------------------------------- */
