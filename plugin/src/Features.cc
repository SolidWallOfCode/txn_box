/** @file
   Non-core feature extractor implementations.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <random>
#include <chrono>

#include <swoc/TextView.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"
#include "txn_box/Extractor.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using swoc::BufferWriter;
using swoc::Errata;
using swoc::Rv;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

swoc::Lexicon<ValueType> ValueTypeNames {{
    { ValueType::NIL, "nil" }
  , { ValueType::STRING, "string"}
  , { ValueType::INTEGER, "integer"}
  , { ValueType::BOOLEAN, "boolean"}
  , { ValueType::IP_ADDR, "IP address"}
  , { ValueType::CONS, "cons" }
  , { ValueType::TUPLE, "tuple" }
  , { ValueType::GENERIC, "generic"}
  , { ValueType::VARIABLE, "var" }
  , { ValueType::ACTIVE, "active" }
  , { ValueType::NO_VALUE, "no value" }
}};

/* ------------------------------------------------------------------------------------ */
bool Feature::is_list() const {
  auto idx = this->index();
  return IndexFor(TUPLE) == idx || IndexFor(CONS) == idx;
}

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
/* ------------------------------------------------------------------------------------ */
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
    case IndexFor(GENERIC): {
      auto & generic = std::get<feature_type_for<GENERIC>>(feature);
      if (TupleIterator::TAG == generic->_tag) {
        static_cast<TupleIterator*>(generic)->advance();
      } else {
        feature = NIL_FEATURE;
      }
    }
    break;
  }
  return feature;
}
/* ------------------------------------------------------------------------------------ */
namespace swoc {
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
  for (auto const& [ e, v] : ValueTypeNames) {
    if (!mask[e]) {
      continue;
    }
    if (lw.extent()) {
      w.write(", ");
    }
    bwformat(w, spec, v);
  }
  w.commit(lw.extent());
  return w;
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Feature const &feature) {
  if (is_nil(feature)) {
    return bwformat(w, spec, "NULL"_tv);
  } else {
    auto visitor = [&](auto &&arg) -> BufferWriter & { return bwformat(w, spec, arg); };
    //  return std::visit(visitor, static_cast<FeatureData::variant_type const&>(feature));
    return std::visit(visitor, feature);
  }
}
} // namespace swoc
/* ------------------------------------------------------------------------------------ */
class Ex_var : public Extractor {
public:
  static constexpr TextView NAME { "var" };

  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ValueType> Ex_var::validate(class Config & cfg, struct Extractor::Spec & spec, const class swoc::TextView & arg) {
  auto name = cfg.span<feature_type_for<STRING>>(1);
  spec._data = name.rebind<void>();
  name[0] = cfg.localize(arg);
  return VARIABLE;
}

Feature Ex_var::extract(Context &ctx, Spec const& spec) {
  return ctx.load_txn_var(spec._data.rebind<feature_type_for<STRING>>()[0]);
}

BufferWriter& Ex_var::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/** The entire URL.
 * Because of the nature of the C API, this can only be a transient external string and
 * therefore must be copied in to context storage.
 */
class Ex_creq_url : public StringExtractor {
public:
  static constexpr TextView NAME { "creq-url" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

BufferWriter& Ex_creq_url::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  FeatureView zret;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      bwformat(w, spec, url.view());
    }
  }
  return w;
}

/* ------------------------------------------------------------------------------------ */
class Ex_creq_url_host : public Extractor {
public:
  static constexpr TextView NAME { "creq-url-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_creq_url_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

BufferWriter& Ex_creq_url_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
class Ex_creq_method : public Extractor {
public:
  static constexpr TextView NAME { "creq-method" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec)  override;
};

Feature Ex_creq_method::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    zret = hdr.method();
  }
  return zret;
}

BufferWriter& Ex_creq_method::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_scheme : public Extractor {
public:
  static constexpr TextView NAME { "creq-scheme" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_creq_scheme::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.scheme();
    }
  }
  return zret;
}

BufferWriter& Ex_creq_scheme::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_path : public Extractor {
public:
  static constexpr TextView NAME { "creq-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_creq_path::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.path();
    }
  }
  return zret;
}

BufferWriter& Ex_creq_path::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_host : public Extractor {
public:
  static constexpr TextView NAME { "creq-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const&) override;
};

Feature Ex_creq_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
      if (zret.data() == nullptr) { // not in the URL, look in the HOST field.
        if ( auto field { hdr.field(ts::HTTP_FIELD_HOST) } ; field.is_valid()) {
          zret = field.value();
        }
      }
    }
  }
  return zret;
}

BufferWriter& Ex_creq_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
class Ex_creq_field : public Extractor {
public:
  static constexpr TextView NAME { "creq-field" };

  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override {
    spec._data.assign(const_cast<char*>(arg.data()), arg.size());
    return STRING;
  }

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_creq_field::extract(Context &ctx, Spec const& spec) {
  FeatureView zret;
  zret._direct_p = true;
  zret = TextView{};
  if ( ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( auto field { hdr.field(spec._data.view()) } ; field.is_valid()) {
      zret = field.value();
    }
  }
  return zret;
};

BufferWriter& Ex_creq_field::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_remap_path : public Extractor {
public:
  static constexpr TextView NAME { "remap-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_remap_path::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if (nullptr != ctx._remap_info) {
    zret = ts::URL(ctx._remap_info->requestBufp, ctx._remap_info->requestUrl).path();
  }
  return zret;
}

BufferWriter& Ex_remap_path::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class ExHttpField : public Extractor {
public:
  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override {
    auto span = cfg.span<Data>(1);
    spec._data = span;
    auto & data = span[0];
    data.opt.all = 0;
    data._arg = cfg.localize(arg);
    if (0 == strcasecmp(spec._ext, "by-field"_tv)) {
      data.opt.f.by_field = true;
    } else if (0 == strcasecmp(spec._ext, "by-value"_tv)) {
      data.opt.f.by_value = true;
    }
    return data.opt.f.by_field ? GENERIC : STRING;
  }

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;

protected:
  struct Data {
    TextView _arg;
    union {
      uint32_t all = 0;
      struct {
        unsigned by_value : 1;
        unsigned by_field : 1;
      } f;
    } opt;
  };

  /// @return The key (name) for the extractor.
  virtual TextView const& key() const = 0;

  /** Get the appropriate @c HttpHeader.
   *
   * @param ctx Runtime context.
   * @return The HTTP header to manipulate.
   *
   * This is used by subclasses to specify which HTTP header in the transaction is in play.
   */
  virtual ts::HttpHeader hdr(Context & ctx) const = 0;
};

Feature ExHttpField::extract(Context &ctx, const Spec &spec) {
  Data & data = spec._data.rebind<Data>()[0];
  if (data.opt.f.by_field) {
    auto iter = ctx._arena->make<HttpFieldTuple>(this->key(), this->hdr(ctx), data._arg);
    return iter;
  } else if (data.opt.f.by_value) {
    return NIL_FEATURE;
  }

  if ( ts::HttpHeader hdr { this->hdr(ctx) } ; hdr.is_valid()) {
    if ( auto field { hdr.field(data._arg) } ; field.is_valid()) {
      if (field.next_dup().is_valid()) {
        auto n = field.dup_count();
        auto span = ctx.span<Feature>(n);
        for ( auto & item : span) {
          item = field.value();
          field = field.next_dup();
        }
        return span;
      } else {
        return field.value();
      }
    }
  }
  return {};
};

BufferWriter& ExHttpField::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

// -----
class Ex_prsp_field : public ExHttpField {
public:
  static constexpr TextView NAME { "prsp-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_prsp_field::key() const { return NAME; }
ts::HttpHeader Ex_prsp_field::hdr(Context & ctx) const {
  return ctx.prsp_hdr();
}
// -----
class Ex_ursp_field : public ExHttpField {
public:
  static constexpr TextView NAME { "ursp-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_ursp_field::key() const { return NAME; }
ts::HttpHeader Ex_ursp_field::hdr(Context & ctx) const {
  return ctx.ursp_hdr();
}

void HttpFieldTuple::update() {
  if (_current.is_valid()) {
    _next = _current.next_dup();
  } else {
    _next = ts::HttpField{};
  }
}

HttpFieldTuple& HttpFieldTuple::rewind() {
  _current = _hdr.field(_name);
  this->update();
  return *this;
}

void HttpFieldTuple::advance() {
  std::swap(_next, _current);
  this->update();
}

Feature HttpFieldTuple::extract() const { return _current.value(); }

/* ------------------------------------------------------------------------------------ */
/** The entire URL.
 * Because of the nature of the C API, this can only be a transient external string and
 * therefore must be copied in to context storage.
 */
class Ex_preq_url : public StringExtractor {
public:
  static constexpr TextView NAME { "preq-url" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

BufferWriter& Ex_preq_url::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  FeatureView zret;
  if ( auto hdr { ctx.preq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      bwformat(w, spec, url.view());
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_preq_host : public Extractor {
public:
  static constexpr TextView NAME { "preq-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const&) override;
};

Feature Ex_preq_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.preq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
      if (zret.data() == nullptr) { // not in the URL, look in the HOST field.
        if ( auto field { hdr.field(ts::HTTP_FIELD_HOST) } ; field.is_valid()) {
          zret = field.value();
        }
      }
    }
  }
  return zret;
}

BufferWriter& Ex_preq_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
class Ex_ursp_status : public IntegerExtractor {
public:
  static constexpr TextView NAME { "ursp-status" };

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_ursp_status::extract(Context &ctx, Extractor::Spec const&) {
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.ursp_hdr().status());
}

BufferWriter& Ex_ursp_status::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._txn.ursp_hdr().status());
}
/* ------------------------------------------------------------------------------------ */
class Ex_is_internal : public BooleanExtractor {
public:
  static constexpr TextView NAME { "is-internal" }; ///< Extractor name.

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Spec const& spec) override;

  /// Required text formatting access.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context & ctx) override;
};

auto Ex_is_internal::extract(Context &ctx, Spec const&) -> Feature {
  return ctx._txn.is_internal();
}

BufferWriter& Ex_is_internal::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Extract the SNI name from the inbound session.
class Ex_cssn_sni : public Extractor {
public:
  static constexpr TextView NAME { "cssn-sni" };
  /// Extract the SNI  name from the inbound session.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_cssn_sni::extract(Context & ctx, Spec const& spec) {
  return ctx._txn.ssn().inbound_sni();
}

BufferWriter& Ex_cssn_sni::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Extract the client session remote address.
class Ex_cssn_remote_addr : public Extractor {
public:
  static constexpr TextView NAME { "cssn-remote-addr" };
  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Rv<ValueType> Ex_cssn_remote_addr::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
  return IP_ADDR;
}

Feature Ex_cssn_remote_addr::extract(Context & ctx, Spec const& spec) {
  return ctx._txn.ssn().remote_addr();
}

BufferWriter& Ex_cssn_remote_addr::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Client Session protocol information.
class Ex_cssn_proto : public StringExtractor {
  using self_type = Ex_cssn_proto; ///< Self reference type.
  using super_type = StringExtractor; ///< Parent type.
public:
  static constexpr TextView NAME { "cssn-proto" };

  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ValueType> Ex_cssn_proto::validate(Config &cfg, Spec &spec, const TextView &arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  auto text = cfg.span<char>(arg.size() + 1);
  auto view = cfg.span<TextView>(1);
  memcpy(text, arg);
  text[arg.size()] = 0; // API call, need C string.
  view[0].assign(text.data(), arg.size() + 1);
  spec._data = view.rebind<void>();
  return STRING;
}

BufferWriter& Ex_cssn_proto::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  auto view = spec._data.rebind<TextView>()[0];
  auto tag = ctx._txn.ssn().proto_contains(view);
  return bwformat(w, spec, tag);
}
/* ------------------------------------------------------------------------------------ */
class Ex_random : public Extractor {
  using self_type = Ex_random; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME { "random" };

  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const& spec) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
protected:
  /// Random generator.
  /// Not thread safe, so have one for each thread.
  static thread_local std::mt19937 _engine;
};

thread_local std::mt19937 Ex_random::_engine(std::chrono::high_resolution_clock::now().time_since_epoch().count());

Feature Ex_random::extract(Context &ctx, Extractor::Spec const& spec) {
  auto values = spec._data.rebind<feature_type_for<INTEGER>>();
  return std::uniform_int_distribution{values[0], values[1]}(_engine);
};

BufferWriter& Ex_random::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

Rv<ValueType> Ex_random::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
  auto values = cfg.span<feature_type_for<INTEGER>>(2);
  spec._data = values.rebind<void>(); // remember where the storage is.
  feature_type_for<INTEGER> min = 0, max = 99; // temporaries for parsing output.
  // Config storage for parsed output.
  values[0] = min;
  values[1] = max;
  // Parse the parameter.
  if (arg) {
    auto max_arg { arg };
    auto min_arg = max_arg.split_prefix_at(",-");
    TextView parsed;
    if (min_arg) {
      min = swoc::svtoi(min_arg, &parsed);
      if (parsed.size() != min_arg.size()) {
        return Error(R"(Parameter "{}" for "{}" is not an integer as required)", min_arg, NAME);
      }
    }
    if (max_arg) {
      max = swoc::svtoi(max_arg, &parsed);
      if (parsed.size() != max_arg.size()) {
        return Error(R"(Parameter "{}" for "{}" is not an integer as required)", max_arg, NAME);
      }
    }
  }

  if (min >= max) {
    return Error(R"(Parameter "{}" for "{}" has an invalid range {}-{})", min, max);
  }

  // Update the stored values now that *both* input values are validated.
  values[0] = min;
  values[1] = max;
  return INTEGER;
}
/* ------------------------------------------------------------------------------------ */
template < typename T, const TextView* KEY> class Ex_duration : public Extractor {
  using self_type = Ex_duration; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
  using ftype = feature_type_for<DURATION>;
public:
  static constexpr TextView NAME { *KEY };

  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const& spec) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

template < typename T, const TextView* KEY > Feature Ex_duration<T,KEY>::extract(Context &ctx, Extractor::Spec const& spec) {
  return spec._data.rebind<ftype>()[0];
};

template < typename T, const TextView* KEY > BufferWriter& Ex_duration<T,KEY>::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

template < typename T, const TextView* KEY> Rv<ValueType> Ex_duration<T,KEY>::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
  auto span = cfg.span<ftype>(1);
  spec._data = span.rebind<void>(); // remember where the storage is.

  if (! arg) {
    return Error(R"("{}" extractor requires an integer argument.)", NAME);
  }

  TextView parsed;
  auto n = swoc::svtoi(arg, &parsed);
  if (parsed.size() != arg.size()) {
    return Error(R"(Parameter "{}" for "{}" is not an integer as required)", arg, NAME);
  }

  span[0] = T{n};
  return DURATION;
}
/* ------------------------------------------------------------------------------------ */
/// The active feature.
class Ex_active_feature : public Extractor {
  using self_type = Ex_active_feature; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME = ACTIVE_FEATURE_KEY;
  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override { return ACTIVE; }
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_active_feature::extract(class Context & ctx, const struct Extractor::Spec & spec) {
  return ctx._active;
}

BufferWriter& Ex_active_feature::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._active);
}

/* ------------------------------------------------------------------------------------ */
/// Extract the most recent selection feature.
class Ex_remainder_feature : public Extractor {
  using self_type = Ex_remainder_feature; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME = REMAINDER_FEATURE_KEY;
  Rv<ValueType> validate(Config & cfg, Spec & spec, TextView const& arg) override { return STRING; }
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_remainder_feature::extract(class Context & ctx, const struct Extractor::Spec & spec) {
  return ctx._remainder;
}

BufferWriter& Ex_remainder_feature::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._remainder);
}
/* ------------------------------------------------------------------------------------ */
BufferWriter& Ex_this::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  Feature feature {_fg->extract(ctx, spec._ext)};
  return bwformat(w, spec, feature);
}

Feature Ex_this::extract(class Context & ctx, const struct Extractor::Spec & spec) {
  return _fg->extract(ctx, spec._ext);
}
/* ------------------------------------------------------------------------------------ */
// Needs to be external visible.
Ex_this ex_this;

namespace {
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.
Ex_var var;

Ex_creq_url creq_url;
Ex_creq_host creq_host;
Ex_creq_scheme creq_scheme;
Ex_creq_method creq_method;
Ex_creq_path creq_path;
Ex_creq_url_host creq_url_host;
Ex_creq_field creq_field;

Ex_preq_host preq_host;

Ex_preq_url preq_url;
Ex_prsp_field prsp_field;
Ex_ursp_field ursp_field;

Ex_remap_path remap_path;

Ex_ursp_status ursp_status;
Ex_is_internal is_internal;

Ex_cssn_sni cssn_sni;
Ex_cssn_proto cssn_proto;
Ex_cssn_remote_addr cssn_remote_addr;

Ex_random random;

static constexpr TextView NANOSECONDS = "nanoseconds";
Ex_duration<std::chrono::nanoseconds, &NANOSECONDS> nanoseconds;
static constexpr TextView SECONDS = "seconds";
Ex_duration<std::chrono::seconds, &SECONDS> seconds;

Ex_active_feature ex_with_feature;
Ex_remainder_feature ex_remainder_feature;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_this::NAME, &ex_this);
  Extractor::define(Ex_active_feature::NAME, &ex_with_feature);
  Extractor::define(Ex_remainder_feature::NAME, &ex_remainder_feature);

  Extractor::define(Ex_creq_url::NAME, &creq_url);
  Extractor::define(Ex_creq_host::NAME, &creq_host);
  Extractor::define(Ex_creq_scheme::NAME, &creq_method);
  Extractor::define(Ex_creq_method::NAME, &creq_scheme);
  Extractor::define(Ex_creq_path::NAME, &creq_path);
  Extractor::define(Ex_creq_url_host::NAME, &creq_url_host);
  Extractor::define(Ex_creq_field::NAME, &creq_field);

  Extractor::define(Ex_preq_host::NAME, &preq_host);

  Extractor::define(Ex_preq_url::NAME, &preq_url);
  Extractor::define(Ex_prsp_field::NAME, &prsp_field);
  Extractor::define(Ex_ursp_field::NAME, &ursp_field);

  Extractor::define(Ex_ursp_status::NAME, &ursp_status);

  Extractor::define(Ex_remap_path::NAME, &remap_path);

  Extractor::define(Ex_is_internal::NAME, &is_internal);
  Extractor::define(Ex_random::NAME, &random);
  Extractor::define(Ex_var::NAME, &var);

  Extractor::define(Ex_cssn_sni::NAME, &cssn_sni);
  Extractor::define(Ex_cssn_proto::NAME, &cssn_proto);
  Extractor::define(Ex_cssn_remote_addr::NAME, &cssn_remote_addr);

  Extractor::define(NANOSECONDS, &nanoseconds);
  Extractor::define(SECONDS, &seconds);

  return true;
} ();
} // namespace
