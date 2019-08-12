/** @file
 * Base extractor logic.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ts/ts.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/Extractor.h"
#include "txn_box/Context.h"
#include "txn_box/Config.h"

using swoc::TextView;
using swoc::MemSpan;
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
  return spot == _info.end() ? nullptr : spot;
}
auto FeatureGroup::Tracking::obtain(swoc::TextView const &name) -> Tracking::Info * {
  Info * spot  = this->find(name);
  if (nullptr == spot) {
    spot = this->alloc();
    spot->_name = name;
  }
  return spot;
}

FeatureGroup::index_type FeatureGroup::exf_index(swoc::TextView const &name) {
  auto spot = std::find_if(_exf_info.begin(), _exf_info.end(), [&name](ExfInfo const& info) { return 0 == strcasecmp(info._name, name); });
  return spot == _exf_info.end() ? INVALID_IDX : spot - _exf_info.begin();
}
Errata FeatureGroup::load_fmt(Config & cfg, Tracking& info, YAML::Node const &node) {
  auto && [ fmt, errata ] { cfg.parse_feature(node) };
  if (errata.is_ok()) {
    // Walk the items to see if any are cross references.
    for ( auto & item : fmt ) {
      if (item._extractor == &ex_this) {
        errata = this->load_key(cfg, info, item._ext);
        if (! errata.is_ok()) {
          break;
        }
        // replace the raw "this" extractor with the localized one.
        item._extractor = &_ex_this;
      }
    }
  }
  info._fmt_array.emplace_back(std::move(fmt));
  return std::move(errata);
}

Errata FeatureGroup::load_key(Config &cfg, FeatureGroup::Tracking &info, swoc::TextView name)
{
  Errata errata;

  if (! info._node[name]) {
    errata.error(R"("{}" is referenced but no such key was found.)", name);
    return std::move(errata);
  }

  auto tinfo = info.obtain(name);

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
  tinfo->_fmt_idx = info._fmt_array.size();
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

  tinfo->_fmt_count = info._fmt_array.size() - tinfo->_fmt_idx;
  if (tinfo->_fmt_count > 1) {
    tinfo->_mark = MULTI_VALUED;
  } else {
    tinfo->_mark = DONE;
  }
  return std::move(errata);
}

Errata FeatureGroup::load(Config & cfg, YAML::Node const& node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  unsigned idx = 0;
  unsigned n_keys = node.size(); // Number of keys in @a node.

  Tracking::Info tracking_info[n_keys];
  Tracking tracking(node, tracking_info, n_keys);

  // Seed tracking info with the explicit keys.
  // Need to do this explicitly to transfer the flags, and to check for duplicates in @a ex_keys.
  for ( auto & d : ex_keys ) {
    auto tinfo = tracking.find(d._name);
    if (nullptr != tinfo) {
      return Errata().error(R"("INTERNAL ERROR: "{}" is used more than once in the extractor key list of the feature group for the node {}.)", d._name, node.Mark());
    }
    if (node[d._name]) {
      tinfo = tracking.alloc();
      tinfo->_name = d._name;
      tinfo->_required_p = d._flags[REQUIRED];
      tinfo->_multi_p = d._flags[MULTI];
    } else if (d._flags[REQUIRED]) {
      return Errata().error(R"(The required key "{}" was not found in the node {}.)", d._name, node.Mark());
    }
  }

  // Time to get the formats and walk the references.
  for ( auto & d : ex_keys ) {
    auto errata { this->load_key(cfg, tracking, d._name) };
    if (! errata.is_ok()) {
      return std::move(errata);
    }
  }

  // Persist the tracking info, now that all the sizes are known.
  _exf_info = cfg.span<ExfInfo>(tracking._count);
  _exf_info.apply([](ExfInfo & info) { new (&info) ExfInfo; });

  // If there are dependency edges, copy those over.
  if (tracking._edge_count) {
    _edge = cfg.span<unsigned short>(tracking._edge_count);
    for (unsigned short idx = 0; idx < tracking._edge_count; ++idx) {
      _edge[idx] = tracking._info[idx]._edge;
    }
  }

  // Persist the keys by copying persistent data from the tracking data to config allocated space.
  for ( unsigned short idx = 0 ; idx < tracking._count ; ++idx ) {
    auto &src = tracking._info[idx];
    auto &dst = _exf_info[idx];
    dst._name = src._name;
    if (src._fmt_count > 1) {
      dst._ex = ExfInfo::Multi{};
      ExfInfo::Multi & m = std::get<ExfInfo::MULTI>(dst._ex);
      m._fmt.reserve(src._fmt_count);
      for ( auto & fmt : MemSpan<Extractor::Format>{ &tracking._fmt_array[src._fmt_idx], src._fmt_count } ) {
        m._fmt.emplace_back(std::move(fmt));
      }
    } else {
      dst._ex = ExfInfo::Single{std::move(tracking._fmt_array[src._fmt_idx])};
    }
    if (src._edge_count) {
      dst._edge.assign(&_edge[src._edge_idx], src._edge_count);
    }
  };

  return {};
}

Errata FeatureGroup::load_as_tuple( Config &cfg, YAML::Node const &node
                                  , std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  unsigned idx = 0;
  unsigned n_keys = ex_keys.size();
  unsigned n_elts = node.size();
  ExfInfo info[n_keys];

  // No dependency in tuples, can just walk the keys and load them.
  for ( auto const& key : ex_keys ) {

    if (idx >= n_elts) {
      if (key._flags[REQUIRED]) {
        return Errata().error(R"(to be done)");
      }
      continue; // it was optional, skip it and keep checking for REQUIRED keys.
    }

    // Not handling MULTI correctly - need to check if element is a list that is not a format
    // with modifiers, and gather the multi-values.
    auto && [ fmt, errata ] = cfg.parse_feature(node[idx]);
    if (! errata.is_ok()) {
      return std::move(errata);
    }
    info[idx]._name = key._name;
    info[idx]._ex = ExfInfo::Single{std::move(fmt)}; // safe because of the @c reserve
    ++idx;
  }
  // Localize feature info, now that the size is determined.
  _exf_info = cfg.span<ExfInfo>(idx);
  index_type i = 0;
  for ( auto & item : _exf_info ) {
    new (&item) ExfInfo{ std::move(info[i++]) };
  }
  // No dependencies for tuple loads.
  // No feature data because no dependencies.

  return {};
}

FeatureData FeatureGroup::extract(Context &ctx, swoc::TextView const &name) {
  auto idx = this->exf_index(name);
  if (idx == INVALID_IDX) {
    return {};
  }
  return this->extract(ctx, idx);
}

FeatureData FeatureGroup::extract(Context &ctx, index_type idx) {
  auto& info = _exf_info[idx];
  if (info._ex.index() == ExfInfo::SINGLE) {
    ExfInfo::Single &data = std::get<ExfInfo::SINGLE>(info._ex);
    if (data._feature.index() != IndexFor(NIL)) {
      return data._feature;
    }

    for (index_type edge_idx : info._edge) {
      ExfInfo::Single& precursor = std::get<ExfInfo::SINGLE>(_exf_info[edge_idx]._ex);
      if (precursor._feature.index() == NIL) {
        precursor._feature = this->extract(ctx, edge_idx);
      }
    }
    data._feature = ctx.extract(data._fmt);
    return data._feature;
  }
  return {};
}

FeatureGroup::~FeatureGroup() {
  _exf_info.apply([](ExfInfo & info) { delete &info; });
}

/* ---------------------------------------------------------------------------------------------- */
