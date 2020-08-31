/** @file
 * Base extractor logic.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"
#include "txn_box/Expr.h"
#include "txn_box/Extractor.h"
#include "txn_box/Context.h"
#include "txn_box/Config.h"

using swoc::TextView;
using swoc::MemSpan;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

std::unique_ptr<Extractor::Table> Extractor::_ex_table;

/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<BoolTag> const BoolNames { {{ BoolTag::True, { "true", "1", "on", "enable", "Y", "yes" }}
                                             , { BoolTag::False, { "false", "0", "off", "disable", "N", "no" }}}
                                         , { BoolTag::INVALID }
};
/* ------------------------------------------------------------------------------------ */
auto Extractor::table() -> Table* {
  if (! _ex_table) {
    _ex_table.reset(new Table);
  }
  return _ex_table.get();
}

Errata Extractor::define(TextView name, self_type * ex) {
  (*table())[name] = ex;
  return {};
}

bool Extractor::has_ctx_ref() const { return false; }

swoc::Rv<ActiveType> Extractor::validate(Config&, Extractor::Spec&, TextView const&) {
  return ActiveType{ NIL, STRING };
}

Feature Extractor::extract(Config&, Extractor::Spec const&) { return NIL_FEATURE; }

Extractor::self_type *Extractor::find(TextView const& name) {
  auto spot { _ex_table->find(name)};
  return spot == _ex_table->end() ? nullptr : spot->second;
}

/* ---------------------------------------------------------------------------------------------- */
auto FeatureGroup::Tracking::find(swoc::TextView const &name) const -> Tracking::Info * {
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
  auto && [ expr, errata ] { cfg.parse_expr(node) };
  if (errata.is_ok()) {
    // Walk the items to see if any are cross references.
    #if 0
    for ( auto & item : expr ) {
      if (item._exf == &ex_this) {
        errata = this->load_key(cfg, info, item._ext);
        if (! errata.is_ok()) {
          break;
        }
        // replace the raw "this" extractor with the localized one.
        item._exf = &_ex_this;
      }
    }
    #endif
  }
  info._fmt_array.emplace_back(std::move(expr));
  return std::move(errata);
}

Errata FeatureGroup::load_key(Config &cfg, FeatureGroup::Tracking &info, swoc::TextView name)
{
  Errata errata;

  if (! info._node[name]) {
    errata.error(R"("{}" is referenced but no such key was found.)", name);
    return errata;
  }

  auto tinfo = info.obtain(name);

  if (tinfo->_mark == DONE) {
    return errata;
  }

  if (tinfo->_mark == IN_PLAY) {
    errata.error(R"(Circular dependency for key "{}" at {}.)", name, info._node.Mark());
    return errata;
  }

  if (tinfo->_mark == MULTI_VALUED) {
    errata.error(R"(A multi-valued key cannot be referenced - "{}" at {}.)", name, info._node.Mark());
    return errata;
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
        errata = this->load_fmt(cfg, info, n[0]);
      } else { // > 1
        if (n[1].IsMap()) {
          errata = this->load_fmt(cfg, info, n[1]);
        } else { // list of formats.
          for (auto const &child : n) {
            errata = this->load_fmt(cfg, info, child);
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
  return errata;
}

Errata FeatureGroup::load(Config & cfg, YAML::Node const& node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
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
      return errata;
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
      ExfInfo::Multi & m = std::get<ExfInfo::IDX_MULTI>(dst._ex);
      m._fmt.reserve(src._fmt_count);
      for ( auto & fmt : MemSpan<Expr>{&tracking._fmt_array[src._fmt_idx], src._fmt_count } ) {
        m._fmt.emplace_back(std::move(fmt));
      }
    } else {
      dst._ex = ExfInfo::Single{std::move(tracking._fmt_array[src._fmt_idx]), {}};
    }
    if (src._edge_count) {
      dst._edge.assign(&_edge[src._edge_idx], src._edge_count);
    }
  }

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
    auto && [ fmt, errata ] = cfg.parse_expr(node[idx]);
    if (! errata.is_ok()) {
      return std::move(errata);
    }
    info[idx]._name = key._name;
    info[idx]._ex = ExfInfo::Single{std::move(fmt), {}}; // safe because of the @c reserve
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

Feature FeatureGroup::extract(Context &ctx, swoc::TextView const &name) {
  auto idx = this->exf_index(name);
  if (idx == INVALID_IDX) {
    return {};
  }
  return this->extract(ctx, idx);
}

Feature FeatureGroup::extract(Context &ctx, index_type idx) {
  auto& info = _exf_info[idx];
  if (info._ex.index() == ExfInfo::IDX_SINGLE) {
    ExfInfo::Single &data = std::get<ExfInfo::IDX_SINGLE>(info._ex);
    if (data._feature.index() != IndexFor(NIL)) {
      return data._feature;
    }

    for (index_type edge_idx : info._edge) {
      ExfInfo::Single& precursor = std::get<ExfInfo::IDX_SINGLE>(_exf_info[edge_idx]._ex);
      if (precursor._feature.index() == NIL) {
        precursor._feature = this->extract(ctx, edge_idx);
      }
    }
    data._feature = ctx.extract(data._expr);
    return data._feature;
  }
  return {};
}

FeatureGroup::~FeatureGroup() {
  _exf_info.apply([](ExfInfo & info) { delete &info; });
}

/* ---------------------------------------------------------------------------------------------- */
Feature StringExtractor::extract(Context& ctx, Spec const& spec) {
  swoc::FixedBufferWriter w{ctx._arena->remnant()};
  // double write - try in the remnant first. If that suffices, done.
  // Otherwise the size is now known and the needed space can be correctly allocated.
  this->format(w, spec, ctx);
  if (!w.error()) {
    return w.view();
  }
  w.assign(ctx._arena->require(w.extent()).remnant().rebind<char>());
  this->format(w, spec, ctx);
  return w.view();
}
/* ------------------------------------------------------------------------------------ */
// Utilities.
bool Feature::is_list() const {
  auto idx = this->index();
  return IndexFor(TUPLE) == idx || IndexFor(CONS) == idx;
}
// ----
ActiveType Feature::active_type() const {
  auto vt = this->value_type();
  ActiveType at = vt;
  if (TUPLE == vt) {
    auto & tp = std::get<IndexFor(TUPLE)>(*this);
    if (tp.size() == 0) { // empty tuple can be a tuple of any type.
      at = ActiveType::TupleOf(ActiveType::any_type().base_types());
    } else if (auto tt = tp[0].value_type() ; std::all_of(tp.begin()+1, tp.end(), [=](Feature const& f){return f.value_type() == tt;})) {
      at = ActiveType::TupleOf(tt);
    } // else leave it as just a tuple with no specific type.
  }
  return at;
}
// ----
namespace {
struct bool_visitor {
  bool operator()(feature_type_for<NIL>) { return false;}
  bool operator()(feature_type_for<STRING> const& s) { return BoolNames[s] == True; }
  bool operator()(feature_type_for<INTEGER> n) { return n != 0; }
  bool operator()(feature_type_for<FLOAT> f) { return f != 0; }
//  bool operator()(feature_type_for<IP_ADDR> addr) { return addr.is_addr_any(); }
  bool operator()(feature_type_for<BOOLEAN> flag) { return flag; }
  bool operator()(feature_type_for<TUPLE> t) { return t.size() > 0; }

  template < typename T > auto operator()(T const&) -> EnableForFeatureTypes<T, bool> { return false; }
};

}
Feature::operator bool() const {
  return std::visit(bool_visitor{}, *this);
}
// ----
namespace {
struct join_visitor {
  swoc::BufferWriter & _w;
  TextView _glue;
  unsigned _recurse = 0;

  swoc::BufferWriter&  glue() {
    if (_w.size()) {
      _w.write(_glue);
    }
    return _w;
  }

  void operator()(feature_type_for<NIL>) {}
  void operator()(feature_type_for<STRING> const& s) { this->glue().write(s); }
  void operator()(feature_type_for<INTEGER> n) { this->glue().print("{}", n); }
  void operator()(feature_type_for<BOOLEAN> flag) { this->glue().print("{}", flag);}
  void operator()(feature_type_for<TUPLE> t) {
    this->glue();
    if (_recurse) {
      _w.write("( "_tv);
    }
    auto lw = swoc::FixedBufferWriter{_w.aux_span()};
    for ( auto const& item : t) {
      std::visit(join_visitor{lw, _glue, _recurse + 1}, item);
    }
    _w.commit(lw.size());
    if (_recurse) {
      _w.write(" )"_tv);
    }
  }

  template < typename T > auto operator()(T const&) -> EnableForFeatureTypes<T, void> {}
};

}

Feature Feature::join(Context &ctx, const swoc::TextView &glue) const {
  swoc::FixedBufferWriter w{ ctx._arena->remnant()};
  std::visit(join_visitor{w, glue}, *this);
  return w.view();
}
// ----
Feature car(Feature const& feature) {
  switch (feature.index()) {
    case IndexFor(CONS):
      return std::get<IndexFor(CONS)>(feature)->_car;
    case IndexFor(TUPLE):
      return std::get<IndexFor(TUPLE)>(feature)[0];
    case IndexFor(GENERIC):{
      auto gf = std::get<IndexFor(GENERIC)>(feature);
      if (gf) {
        return gf->extract();
      }
    }
  }
  return feature;
}
// ----
Feature & cdr(Feature & feature) {
  switch (feature.index()) {
    case IndexFor(CONS):
      feature = std::get<feature_type_for<CONS>>(feature)->_cdr;
      break;
    case IndexFor(TUPLE): {
      Feature cdr { feature };
      auto &span = std::get<feature_type_for<TUPLE>>(cdr);
      span.remove_prefix(1);
      feature = span.empty() ? NIL_FEATURE : cdr;
    }
      break;
  }
  return feature;
}
/* ------------------------------------------------------------------------------------ */
namespace swoc {
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &, std::monostate) {
  return w.write("NULL");
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ValueType type) {
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<unsigned>(type));
  }
  return bwformat(w, spec, ValueTypeNames[type]);
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ValueMask const &mask) {
  auto span{w.aux_span()};
  if (span.size() > spec._max) {
    span = span.prefix(spec._max);
  }
  swoc::FixedBufferWriter lw{span};
  if (mask.any()) {
    for (auto const&[e, v] : ValueTypeNames) {
      if (!mask[e]) {
        continue;
      }
      if (lw.extent()) {
        lw.write(", ");
      }
      bwformat(lw, spec, v);
    }
  } else {
    bwformat(lw, spec, "*no value"_tv);
  }
  w.commit(lw.extent());
  return w;
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Feature const &feature) {
  if (is_nil(feature)) {
    return bwformat(w, spec, "NULL"_tv);
  } else {
    auto visitor = [&](auto &&arg) -> BufferWriter & { return bwformat(w, spec, arg); };
    return std::visit(visitor, feature);
  }
}
} // namespace swoc

BufferWriter &
bwformat(BufferWriter& w, bwf::Spec const& spec, ActiveType const& type) {
  bwformat(w, spec, type._base_type);
  if (type._tuple_type.any()) {
    w.write(", Tuples of [");
    bwformat(w, spec, type._tuple_type);
    w.write(']');
  }
  return w;
}
/* ---------------------------------------------------------------------------------------------- */
