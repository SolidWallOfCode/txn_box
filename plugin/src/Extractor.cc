/** @file
 * Base extractor logic.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/bwf_ip.h>

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

/* ------------------------------------------------------------------------------------ */
swoc::Lexicon<BoolTag> const BoolNames { {{ BoolTag::True, { "true", "1", "on", "enable", "Y", "yes" }}
                                             , { BoolTag::False, { "false", "0", "off", "disable", "N", "no" }}}
                                         , { BoolTag::INVALID }
};
/* ------------------------------------------------------------------------------------ */
Errata Extractor::define(TextView name, self_type * ex) {
  _ex_table[name] = ex;
  return {};
}

bool Extractor::has_ctx_ref() const { return false; }

swoc::Rv<ActiveType> Extractor::validate(Config&, Extractor::Spec&, TextView const&) {
  return ActiveType{ NIL, STRING };
}

Feature Extractor::extract(Config&, Extractor::Spec const&) { return NIL_FEATURE; }

BufferWriter& Extractor::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

Extractor::self_type *Extractor::find(TextView const& name) {
  auto spot { _ex_table.find(name)};
  return spot == _ex_table.end() ? nullptr : spot->second;
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

FeatureGroup::index_type FeatureGroup::index_of(swoc::TextView const &name) {
  auto spot = std::find_if(_expr_info.begin(), _expr_info.end(), [&name](ExprInfo const& info) { return 0 == strcasecmp(info._name, name); });
  return spot == _expr_info.end() ? INVALID_IDX : spot - _expr_info.begin();
}

Rv<Expr> FeatureGroup::load_expr(Config & cfg, Tracking& tracking, YAML::Node const &node) {
  /* A bit tricky, but not unduly so. The goal is to traverse all of the specifiers in the
   * expression and convert generic "this" extractors to the "this" extractor for @a this
   * feature group instance. This struct is required by the visitation rules of @c std::variant.
   * There's an overload for each variant type in @c Expr plus a common helper method.
   * It's not possible to use lambda gathering because the @c List case is recursive.
   */
  struct V {
    V(FeatureGroup & fg, Config & cfg, Tracking& tracking) : _fg(fg), _cfg(cfg), _tracking(tracking) {}
    FeatureGroup & _fg;
    Config & _cfg;
    Tracking & _tracking;

    /// Update @a spec as needed to have the correct "this" extractor.
    Errata load_spec(Extractor::Spec& spec) {
      if (spec._exf == &ex_this) {
        auto && [ tinfo, errata ] = _fg.load_key(_cfg, _tracking, spec._ext);
        if (errata.is_ok()) {
          spec._exf = &_fg._ex_this;
          // If not already marked as a reference target, do so.
          if (tinfo->_exf_idx == INVALID_IDX) {
            tinfo->_exf_idx = _fg._ref_count++;
          }
        }
        return errata;
      }
      return {};
    }

    Errata operator() (std::monostate) { return {}; }
    Errata operator() (Feature const&) { return {}; }
    Errata operator() (Expr::Direct & d) {
      return this->load_spec(d._spec);
    }
    Errata operator() (Expr::Composite & c) {
      for (auto& spec : c._specs) {
        auto errata = this->load_spec(spec);
        if (!errata.is_ok()) {
          return errata;
        }
      }
      return {};
    }
    // For list, it's a list of nested @c Expr instances, so visit those as this one was visited.
    Errata operator() (Expr::List & l){
      for ( auto & expr : l._exprs) {
        auto errata = std::visit(*this, expr._raw);
        if (! errata.is_ok()) {
          return errata;
        }
      }
      return {};
    }
  } v(*this, cfg, tracking);

  auto && [ expr, errata ] { cfg.parse_expr(node) };
  if (errata.is_ok()) {
    errata = std::visit(v, expr._raw); // update "this" extractor references.
    if (errata.is_ok()) {
      return std::move(expr);
    }
  }
  return { std::move(errata) };
}

auto FeatureGroup::load_key(Config &cfg, FeatureGroup::Tracking &tracking, swoc::TextView name)
     -> Rv<Tracking::Info*>
{
  YAML::Node n {tracking._node[name]};

  // Check if the key is present in the node. If not, it must be a referenced key because
  // the presence of explicit keys is checked before loading any keys.
  if (! n) {
    return Error(R"("{}" is referenced but no such key was found.)", name);
  }

  auto tinfo = tracking.obtain(name);

  if (tinfo->_mark == DONE) { // already loaded, presumably due to a reference.
    return tinfo;
  }

  if (tinfo->_mark == IN_PLAY) {
    return Error(R"(Circular dependency for key "{}" at {}.)", name, tracking._node.Mark());
  }

  tinfo->_mark = IN_PLAY;
  auto && [ expr, errata ] { this->load_expr(cfg, tracking, n) };
  if (! errata.is_ok()) {
    errata.info(R"(While loading extraction format for key "{}" at {}.)", name, tracking._node.Mark());
    return std::move(errata);
  }

  tinfo->_expr = std::move(expr);
  tinfo->_mark = DONE;
  return tinfo;
}

Errata FeatureGroup::load(Config & cfg, YAML::Node const& node, std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  unsigned n_keys = node.size(); // Number of keys in @a node.

  Tracking::Info tracking_info[n_keys];
  Tracking tracking(node, tracking_info, n_keys);

  // Find the roots of extraction - these are the named keys actually in the node.
  // Need to do this explicitly to transfer the flags, and to check for duplicates in @a ex_keys.
  // It is not an error for a named key to be missing unless it's marked @c REQUIRED.
  for ( auto & d : ex_keys ) {
    auto tinfo = tracking.find(d._name);
    if (nullptr != tinfo) {
      return Errata().error(R"("INTERNAL ERROR: "{}" is used more than once in the extractor key list of the feature group for the node {}.)", d._name, node.Mark());
    }
    if (node[d._name]) {
      tinfo = tracking.alloc();
      tinfo->_name = d._name;
    } else if (d._flags[REQUIRED]) {
      return Errata().error(R"(The required key "{}" was not found in the node {}.)", d._name, node.Mark());
    }
  }

  // Time to get the expressions and walk the references. Need to finalize the range before calling
  // @c load_key as that can modify @a tracking_count. Also must avoid calling this on keys that
  // are explicit but not required - need to fail on missing keys iff they're referenced, which is
  // checked by @c load_key. The presence of required keys has already been verified.
  for ( auto info = tracking_info, limit = info + tracking._count ; info < limit ; ++info ) {
    auto &&[dummy, errata]{this->load_key(cfg, tracking, info->_name)};
    if (!errata.is_ok()) {
      return errata;
    }
  }

  // Persist the tracking info, now that all the sizes are known.
  _expr_info = cfg.alloc_span<ExprInfo>(tracking._count);
  _expr_info.apply([](ExprInfo & info) { new (&info) ExprInfo; });

  // If there are dependencies, allocate state to hold cached values.
  if (_ref_count > 0) {
    _ctx_state_span = cfg.reserve_ctx_storage(sizeof(State));
  }

  // Persist the keys by copying persistent data from the tracking data to config allocated space.
  for ( unsigned short idx = 0 ; idx < tracking._count ; ++idx ) {
    Tracking::Info &src = tracking._info[idx];
    ExprInfo &dst = _expr_info[idx];
    dst._name = src._name;
    dst._expr = std::move(src._expr);
    dst._exf_idx = src._exf_idx;
  }

  return {};
}

Errata FeatureGroup::load_as_scalar(Config& cfg, const YAML::Node& value, swoc::TextView const& name) {
  auto && [ expr, errata ] { cfg.parse_expr(value) };
  if (! errata.is_ok()) {
    return std::move(errata);
  }
  _expr_info = cfg.alloc_span<ExprInfo>(1);
  auto info = _expr_info.data();
  new (info) ExprInfo;
  info->_expr = std::move(expr);
  info->_name = name;
  return {};
}

Errata FeatureGroup::load_as_tuple( Config &cfg, YAML::Node const &node
                                  , std::initializer_list<FeatureGroup::Descriptor> const &ex_keys) {
  unsigned idx = 0;
  unsigned n_keys = ex_keys.size();
  unsigned n_elts = node.size();
  ExprInfo info[n_keys];

  // No dependency in tuples, can just walk the keys and load them.
  for ( auto const& key : ex_keys ) {

    if (idx >= n_elts) {
      if (key._flags[REQUIRED]) {
        return Errata().error(R"(The list was {} elements long but {} are required.)", n_elts, n_keys);
      }
      continue; // it was optional, skip it and keep checking for REQUIRED keys.
    }

    auto && [ expr, errata ] = cfg.parse_expr(node[idx]);
    if (! errata.is_ok()) {
      return std::move(errata);
    }
    info[idx]._name = key._name;
    info[idx]._expr = std::move(expr); // safe because of the @c reserve
    ++idx;
  }
  // Localize feature info, now that the size is determined.
  _expr_info = cfg.alloc_span<ExprInfo>(idx);
  index_type i = 0;
  for ( auto & item : _expr_info ) {
    new (&item) ExprInfo{std::move(info[i++]) };
  }
  // No dependencies for tuple loads.
  // No feature data because no dependencies.

  return {};
}

Feature FeatureGroup::extract(Context& ctx, swoc::TextView const& name) {
  auto idx = this->index_of(name);
  if (idx == INVALID_IDX) {
    return {};
  }
  return this->extract(ctx, idx);
}

Feature FeatureGroup::extract(Context& ctx, index_type idx) {
  // Only cache if there are dependencies (i.e. there are dependency edges).
  // Note dependencies on literals are not tracked, as there's no benefit to caching them.
  auto * cache = _ref_count ? &ctx.storage_for(_ctx_state_span).rebind<State>()[0]._features : nullptr;
  Feature *cached_feature = nullptr;
  auto& info = _expr_info[idx];
  // Get the reserved storage for the @c State instance, if any.
  if (cache && info._exf_idx != INVALID_IDX) {
    cached_feature = &(*cache)[info._exf_idx];
    // If already extracted, return.
    // Ugly - need to improve this. Use GENERIC type with a nullport to indicate not a feature.
    if (cached_feature->index() != IndexFor(GENERIC) || std::get<IndexFor(GENERIC)>(*cached_feature) != nullptr) {
      return *cached_feature;
    }
  }

  auto f = ctx.extract(info._expr);
  if (cached_feature) {
    *cached_feature = f;
  }
  return f;
}

auto FeatureGroup::pre_extract(Context & ctx) -> void {
  if (_ref_count > 0) { // there are actual dependencies.
    static constexpr Generic *not_a_feature = nullptr;
    // Get the reserved storage for the @c State instance.
    State& state = ctx.initialized_storage_for<State>(_ctx_state_span)[0];
    // Allocate the precursor feature array.
    state._features = ctx.alloc_span<Feature>(_ref_count);
    // Initialize to a known invalid state to prevent multiple extractions.
    state._features.apply([](Feature& f) { new(&f) Feature(not_a_feature); });
  }
}

FeatureGroup::~FeatureGroup() {
  _expr_info.apply([](ExprInfo & info) { std::destroy_at(&info); });
}

/* ---------------------------------------------------------------------------------------------- */
Feature StringExtractor::extract(Context& ctx, Spec const& spec) {
  return ctx.render_transient([&](BufferWriter & w) { this->format(w, spec, ctx); });
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
/* ---------------------------------------------------------------------------------------------- */
