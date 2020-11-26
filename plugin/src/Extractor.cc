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
        auto errata = std::visit(*this, expr._expr);
        if (! errata.is_ok()) {
          return errata;
        }
      }
      return {};
    }
  } v(*this, cfg, tracking);

  auto && [ expr, errata ] { cfg.parse_expr(node) };
  if (errata.is_ok()) {
    errata = std::visit(v, expr._expr); // update "this" extractor references.
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
  // @c load_key as that can modify @a tracking._count. Also must avoid calling this on keys that
  // are explicit but not required - need to fail on missing keys iff they're referenced, which is
  // checked by @c load_key. The presence of required keys has already been verified.
  for ( auto info = tracking_info, limit = info + tracking._count ; info < limit ; ++info ) {
    auto &&[dummy, errata]{this->load_key(cfg, tracking, info->_name)};
    if (!errata.is_ok()) {
      return errata;
    }
  }

  // Persist the tracking info, now that all the sizes are known.
  _expr_info = cfg.span<ExprInfo>(tracking._count);
  _expr_info.apply([](ExprInfo & info) { new (&info) ExprInfo; });

  // If there are dependency edges, copy those over.
  if (tracking._edge_count) {
    _edge = cfg.span<unsigned short>(tracking._edge_count);
    for (unsigned short idx = 0; idx < tracking._edge_count; ++idx) {
      _edge[idx] = tracking._info[idx]._edge;
    }
    _ctx_state_span = cfg.reserve_ctx_storage(sizeof(State));
  }

  // Persist the keys by copying persistent data from the tracking data to config allocated space.
  for ( unsigned short idx = 0 ; idx < tracking._count ; ++idx ) {
    Tracking::Info &src = tracking._info[idx];
    ExprInfo &dst = _expr_info[idx];
    dst._name = src._name;
    dst._expr = std::move(src._expr);
    dst._exf_idx = src._exf_idx;
    if (src._edge_count) {
      dst._edge.assign(&_edge[src._edge_idx], src._edge_count);
    }
  }

  return {};
}

Errata FeatureGroup::load_as_scalar(Config& cfg, const YAML::Node& value, swoc::TextView const& name) {
  auto && [ expr, errata ] { cfg.parse_expr(value) };
  if (! errata.is_ok()) {
    return std::move(errata);
  }
  _expr_info = cfg.span<ExprInfo>(1);
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
  _expr_info = cfg.span<ExprInfo>(idx);
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
  auto& info = _expr_info[idx];
  // Get the reserved storage for the @c State instance.
  State& state = ctx.storage_for(_ctx_state_span).rebind<State>()[0];
  Feature * cached = (info._exf_idx == INVALID_IDX ? nullptr : &state._features[info._exf_idx]);
  // If already extracted, return.
  // Ugly - need to improve this. Use GENERIC type with a nullport to indicate not a feature.
  if (cached && ( cached->index() != IndexFor(GENERIC) || std::get<IndexFor(GENERIC)>(*cached) != nullptr)) {
    return *cached;
  }
  // Extract precursors.
  for (index_type edge_idx : info._edge) {
    ExprInfo& precursor = _expr_info[edge_idx];
    if (state._features[precursor._exf_idx].index() == IndexFor(NIL)) {
      this->extract(ctx, edge_idx);
    }
  }
  auto f = ctx.extract(info._expr);
  if (cached) {
    *cached = f;
  }
  return f;
}

auto FeatureGroup::pre_extract(Context & ctx) -> void {
  if (_ref_count > 0) {
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
  bool operator()(feature_type_for<NIL>) { return false; }

  bool operator()(feature_type_for<STRING> const& s) { return BoolNames[s] == True; }

  bool operator()(feature_type_for<INTEGER> n) { return n != 0; }

  bool operator()(feature_type_for<FLOAT> f) { return f != 0; }

  bool operator()(feature_type_for<IP_ADDR> addr) { return addr.is_valid(); }

  bool operator()(feature_type_for<BOOLEAN> flag) { return flag; }

  bool operator()(feature_type_for<TUPLE> t) { return t.size() > 0; }

  template<typename T> auto operator()(T const&) -> EnableForFeatureTypes<T, bool> { return false; }
};

}

auto Feature::as_bool() const -> type_for<BOOLEAN> {
  return std::visit(bool_visitor{}, *this);
}

namespace {
struct integer_visitor {
  /// Target feature type.
  using ftype = feature_type_for<INTEGER>;
  /// Return type.
  using ret_type = Rv<ftype>;

  ftype _invalid;

  explicit integer_visitor(ftype invalid) : _invalid(invalid) {}

  ret_type operator()(feature_type_for<STRING> const& s) {
    TextView parsed;
    ftype zret = swoc::svtoi(s, &parsed);
    if (parsed.size() != s.size()) {
      return { _invalid
      , Error("Invalid format for integer at offset {}", parsed.size() + 1)};
    }
    return zret;
  }

  ret_type operator()(feature_type_for<INTEGER> n) { return n; }

  ret_type operator()(feature_type_for<FLOAT> f) { return ftype(f); }

  ret_type operator()(feature_type_for<BOOLEAN> flag) { return ftype(flag); }

  ret_type operator()(feature_type_for<TUPLE> t) { return t.size(); }

  template<typename F> auto operator()(F const&) -> EnableForFeatureTypes<F, ret_type> {
    return { _invalid
             , Error("Feature of type {} cannot be coerced to type {}."
             ,   value_type_of<F>, INTEGER)
           };
  }
};

}
auto Feature::as_integer(type_for<INTEGER> invalid) const -> Rv<type_for<INTEGER>> {
  return std::visit(integer_visitor{invalid}, *this);
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

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const& spec, FeatureTuple const& t) {
  if (t.count() > 0) {
    bwformat(w, spec, t[0]);
    for (auto && f : t.subspan(1, t.count() - 1)) {
      w.write(", ");
      bwformat(w, spec, f);
    }
  }
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
