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

swoc::Lexicon<ValueType> const ValueTypeNames {{
    { ValueType::NIL, "nil" }
  , { ValueType::STRING, "string"}
  , { ValueType::INTEGER, "integer"}
  , { ValueType::BOOLEAN, "boolean"}
  , { ValueType::FLOAT, "float"}
  , { ValueType::IP_ADDR, "IP address"}
  , { ValueType::DURATION, "duration"}
  , { ValueType::TIMEPOINT, "time point"}
  , { ValueType::CONS, "cons" }
  , { ValueType::TUPLE, "tuple" }
  , { ValueType::GENERIC, "generic"}
}};

/* ------------------------------------------------------------------------------------ */
bool Feature::is_list() const {
  auto idx = this->index();
  return IndexFor(TUPLE) == idx || IndexFor(CONS) == idx;
}

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

/* ------------------------------------------------------------------------------------ */
class Ex_var : public Extractor {
public:
  static constexpr TextView NAME { "var" };

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_var::validate(class Config & cfg, struct Extractor::Spec & spec, const class swoc::TextView & arg) {
  auto name = cfg.span<feature_type_for<STRING>>(1);
  spec._data = name.rebind<void>();
  name[0] = cfg.localize(arg);
  return ActiveType::any_type();
}

Feature Ex_var::extract(Context &ctx, Spec const& spec) {
  return ctx.load_txn_var(spec._data.rebind<feature_type_for<STRING>>()[0]);
}

BufferWriter& Ex_var::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/** The entire URL.
 * The C API doesn't provide direct access to a string for the URL, therefore the string
 * obtained here is transient. To avoid problems, the base class copies it into context
 * storage.
 */
class Ex_ua_req_url : public StringExtractor {
public:
  static constexpr TextView NAME { "ua-req-url" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

BufferWriter& Ex_ua_req_url::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  FeatureView zret;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      bwformat(w, spec, url.view());
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_url_host : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-url-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_req_url_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

BufferWriter& Ex_ua_req_url_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_method : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-method" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec)  override;
};

Feature Ex_ua_req_method::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    zret = hdr.method();
  }
  return zret;
}

BufferWriter& Ex_ua_req_method::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_scheme : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-scheme" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_req_scheme::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.scheme();
    }
  }
  return zret;
}

BufferWriter& Ex_ua_req_scheme::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_path : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_req_path::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.path();
    }
  }
  return zret;
}

BufferWriter& Ex_ua_req_path::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_query : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-query" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_req_query::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.query();
    }
  }
  return zret;
}

BufferWriter& Ex_ua_req_query::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_req_host : public Extractor {
public:
  static constexpr TextView NAME { "ua-req-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const&) override;
};

Feature Ex_ua_req_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    zret = hdr.host();
  }
  return zret;
}

BufferWriter& Ex_ua_req_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/** The entire pristine URL.
 * The C API doesn't provide direct access to a string for the URL, therefore the string
 * obtained here is transient. To avoid problems, the base class copies it into context
 * storage.
 */
class Ex_ua_pre_remap_url : public StringExtractor {
public:
  static constexpr TextView NAME { "ua-pre-remap-url" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

BufferWriter& Ex_ua_pre_remap_url::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  FeatureView zret;
  if ( ts::URL url { ctx._txn.pristine_url_get() } ; url.is_valid()) {
    bwformat(w, spec, url.view());
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_pre_remap_host : public Extractor {
public:
  static constexpr TextView NAME { "ua-pre-remap-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_pre_remap_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::URL url { ctx._txn.pristine_url_get() } ; url.is_valid()) {
    zret = url.host();
  }
  return zret;
}

BufferWriter& Ex_ua_pre_remap_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_pre_remap_scheme : public Extractor {
public:
  static constexpr TextView NAME { "ua-pre-remap-scheme" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_pre_remap_scheme::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::URL url { ctx._txn.pristine_url_get() } ; url.is_valid()) {
    zret = url.scheme();
  }
  return zret;
}

BufferWriter& Ex_ua_pre_remap_scheme::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_pre_remap_path : public Extractor {
public:
  static constexpr TextView NAME { "ua-pre-remap-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_pre_remap_path::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::URL url { ctx._txn.pristine_url_get() } ; url.is_valid()) {
    zret = url.path();
  }
  return zret;
}

BufferWriter& Ex_ua_pre_remap_path::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_ua_pre_remap_query : public Extractor {
public:
  static constexpr TextView NAME { "ua-pre-remap-query" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_ua_pre_remap_query::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::URL url { ctx._txn.pristine_url_get() } ; url.is_valid()) {
    zret = url.query();
  }
  return zret;
}

BufferWriter& Ex_ua_pre_remap_query::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_req_path : public Extractor {
public:
  static constexpr TextView NAME { "proxy-req-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_proxy_req_path::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.path();
    }
  }
  return zret;
}

BufferWriter& Ex_proxy_req_path::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_req_query : public Extractor {
public:
  static constexpr TextView NAME { "proxy-req-query" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_proxy_req_query::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.proxy_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.query();
    }
  }
  return zret;
}

BufferWriter& Ex_proxy_req_query::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class ExHttpField : public Extractor {
public:
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override {
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
    return ActiveType{ NIL, STRING, ActiveType::TupleOf(STRING) };
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
    return NIL_FEATURE;
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
class Ex_ua_req_field : public ExHttpField {
public:
  static constexpr TextView NAME { "ua-req-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_ua_req_field::key() const { return NAME; }
ts::HttpHeader Ex_ua_req_field::hdr(Context & ctx) const {
  return ctx.ua_req_hdr();
}
// -----
class Ex_proxy_req_field : public ExHttpField {
public:
  static constexpr TextView NAME { "proxy-req-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_proxy_req_field::key() const { return NAME; }
ts::HttpHeader Ex_proxy_req_field::hdr(Context & ctx) const {
  return ctx.proxy_req_hdr();
}
// -----
class Ex_proxy_rsp_field : public ExHttpField {
public:
  static constexpr TextView NAME { "proxy-rsp-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_proxy_rsp_field::key() const { return NAME; }
ts::HttpHeader Ex_proxy_rsp_field::hdr(Context & ctx) const {
  return ctx.proxy_rsp_hdr();
}
// -----
class Ex_upstream_rsp_field : public ExHttpField {
public:
  static constexpr TextView NAME { "upstream-rsp-field" };

protected:
  TextView const& key() const override;
  ts::HttpHeader hdr(Context & ctx) const override;
};

TextView const& Ex_upstream_rsp_field::key() const { return NAME; }
ts::HttpHeader Ex_upstream_rsp_field::hdr(Context & ctx) const {
  return ctx.upstream_rsp_hdr();
}

/* ------------------------------------------------------------------------------------ */
/** The entire URL.
 * Because of the nature of the C API, this can only be a transient external string and
 * therefore must be copied in to context storage.
 */
class Ex_proxy_req_url : public StringExtractor {
public:
  static constexpr TextView NAME { "proxy-req-url" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

BufferWriter& Ex_proxy_req_url::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  FeatureView zret;
  if ( auto hdr {ctx.proxy_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      bwformat(w, spec, url.view());
    }
  }
  return w;
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_req_host : public Extractor {
public:
  static constexpr TextView NAME { "proxy-req-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const&) override;
};

Feature Ex_proxy_req_host::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.proxy_req_hdr() } ; hdr.is_valid()) {
    zret = hdr.host();
  }
  return zret;
}

BufferWriter& Ex_proxy_req_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_req_scheme : public Extractor {
public:
  static constexpr TextView NAME { "proxy-req-scheme" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_proxy_req_scheme::extract(Context &ctx, Spec const&) {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr {ctx.ua_req_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.scheme();
    }
  }
  return zret;
}

BufferWriter& Ex_proxy_req_scheme::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
#if 0
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
#endif
/* ------------------------------------------------------------------------------------ */
class Ex_upstream_rsp_status : public IntegerExtractor {
public:
  static constexpr TextView NAME { "upstream-rsp-status" };

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_upstream_rsp_status::validate(Config &, Spec &, TextView const&) {
  return { INTEGER };
}

Feature Ex_upstream_rsp_status::extract(Context &ctx, Extractor::Spec const&) {
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.ursp_hdr().status());
}

BufferWriter& Ex_upstream_rsp_status::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._txn.ursp_hdr().status());
}
/* ------------------------------------------------------------------------------------ */
class Ex_proxy_rsp_status : public IntegerExtractor {
public:
  static constexpr TextView NAME { "proxy-rsp-status" };

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_proxy_rsp_status::validate(Config &, Spec &, TextView const&) {
  return { INTEGER };
}

Feature Ex_proxy_rsp_status::extract(Context &ctx, Extractor::Spec const&) {
  return static_cast<feature_type_for<INTEGER>>(ctx._txn.prsp_hdr().status());
}

BufferWriter& Ex_proxy_rsp_status::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._txn.prsp_hdr().status());
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
class Ex_inbound_sni : public Extractor {
public:
  static constexpr TextView NAME { "inbound-sni" };
  /// Extract the SNI  name from the inbound session.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Feature Ex_inbound_sni::extract(Context & ctx, Spec const&) {
  return ctx._txn.ssn().inbound_sni();
}

BufferWriter& Ex_inbound_sni::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Extract the client session remote address.
class Ex_inbound_remote_remote : public Extractor {
public:
  static constexpr TextView NAME { "inbound-addr-remote" };
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Rv<ActiveType> Ex_inbound_remote_remote::validate(Config &, Extractor::Spec &, TextView const &) {
  return { IP_ADDR };
}

Feature Ex_inbound_remote_remote::extract(Context & ctx, Spec const& ) {
  return ctx._txn.ssn().remote_addr();
}

BufferWriter& Ex_inbound_remote_remote::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// Client Session protocol information.
class Ex_inbound_protocol : public StringExtractor {
  using self_type = Ex_inbound_protocol; ///< Self reference type.
  using super_type = StringExtractor; ///< Parent type.
public:
  static constexpr TextView NAME { "inbound-protocol" };

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_inbound_protocol::validate(Config &cfg, Spec &spec, const TextView &arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  auto text = cfg.span<char>(arg.size() + 1);
  auto view = cfg.span<TextView>(1);
  memcpy(text, arg);
  text[arg.size()] = 0; // API call, need C string.
  view[0].assign(text.data(), arg.size() + 1);
  spec._data = view.rebind<void>();
  return { STRING };
}

BufferWriter& Ex_inbound_protocol::format(BufferWriter &w, Spec const &spec, Context &ctx) {
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

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const& spec) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
protected:
  /// Random generator.
  /// Not thread safe, so have one for each thread.
  static thread_local std::mt19937 _engine;
};

thread_local std::mt19937 Ex_random::_engine(std::chrono::high_resolution_clock::now().time_since_epoch().count());

Feature Ex_random::extract(Context &, Extractor::Spec const& spec) {
  auto values = spec._data.rebind<feature_type_for<INTEGER>>();
  return std::uniform_int_distribution{values[0], values[1]}(_engine);
};

BufferWriter& Ex_random::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

Rv<ActiveType> Ex_random::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
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
  return { INTEGER };
}
/* ------------------------------------------------------------------------------------ */
template < typename T, const TextView* KEY> class Ex_duration : public Extractor {
  using self_type = Ex_duration; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
  using ftype = feature_type_for<DURATION>;
public:
  static constexpr TextView NAME { *KEY };

  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const& spec) override;
  /// Extract the feature from the config.
  Feature extract(Config& cfg, Extractor::Spec const& spec) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

template < typename T, const TextView* KEY > Feature Ex_duration<T,KEY>::extract(Context &, Extractor::Spec const& spec) {
  return spec._data.rebind<ftype>()[0];
}

template < typename T, const TextView* KEY > Feature Ex_duration<T,KEY>::extract(Config &, Extractor::Spec const& spec) {
  return spec._data.rebind<ftype>()[0];
}

template < typename T, const TextView* KEY > BufferWriter& Ex_duration<T,KEY>::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

template < typename T, const TextView* KEY> Rv<ActiveType> Ex_duration<T,KEY>::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
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

  ActiveType zret { DURATION };
  zret.mark_cfg_const();
  return zret;
}
/* ------------------------------------------------------------------------------------ */
class Ex_txn_conf : public Extractor {
  using self_type = Ex_txn_conf; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
  using store_type = ts::TxnConfigVar*; ///< Storage type for config var record.
public:
  static constexpr TextView NAME { "txn-conf" };

  /** Validate the use of the extractor in a feature string.
   *
   * @param cfg Configuration.
   * @param spec Specifier used in the feature string for the extractor.
   * @param arg Argument for the extractor.
   * @return The value type for @a spec and @a arg.
   *
   */
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const& spec) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_txn_conf::validate(Config &cfg, Spec &spec, const TextView &arg) {
  auto var = ts::HttpTxn::find_override(arg);
  if (nullptr == var) {
    return Error(R"("{}" is not a recognized transaction overridable configuration variable name.)", arg);
  }
  auto ptr = cfg.span<store_type>(1);
  ptr[0] = var;
  spec._data = ptr.rebind<void>(); // remember where the pointer is.
  ValueType vt = NIL;
  switch(var->type()) {
    case TS_RECORDDATATYPE_INT : vt = INTEGER;
      break;
    case TS_RECORDDATATYPE_FLOAT : vt = FLOAT;
      break;
    case TS_RECORDDATATYPE_STRING : vt = STRING;
      break;
    default: break;
  }
  return ActiveType{vt};
}

Feature Ex_txn_conf::extract(Context &ctx, const Extractor::Spec &spec) {
  Feature zret;
  auto var = spec._data.rebind<store_type>()[0];
  auto && [ value , errata ] = ctx._txn.override_fetch(*var);
  if (errata.is_ok()) {
    switch (value.index()) {
      case 0: break;
      case 1: zret = std::get<1>(value); break;
      case 2: zret = std::get<2>(value); break;
      case 3:
        FeatureView v = std::get<3>(value);
        v._direct_p = true;
        zret = v;
        break;
    }
  }
  return zret;
}

BufferWriter & Ex_txn_conf::format(BufferWriter &w, const Spec &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
/// The active feature.
class Ex_active_feature : public Extractor {
  using self_type = Ex_active_feature; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME = ACTIVE_FEATURE_KEY;
  Rv<ActiveType> validate(Config & cfg, Spec &, TextView const&) override { return cfg.active_type(); }
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_active_feature::extract(class Context & ctx, const struct Extractor::Spec &) {
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
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_remainder_feature::extract(class Context & ctx, const struct Extractor::Spec &) {
  return ctx._remainder;
}

BufferWriter& Ex_remainder_feature::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._remainder);
}

Rv<ActiveType>
Ex_remainder_feature::validate(Config&, Extractor::Spec&, TextView const&) { return { STRING }; }

/* ------------------------------------------------------------------------------------ */
BufferWriter& Ex_this::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  Feature feature {_fg->extract(ctx, spec._ext)};
  return bwformat(w, spec, feature);
}

Feature Ex_this::extract(class Context & ctx, const struct Extractor::Spec & spec) {
  return _fg->extract(ctx, spec._ext);
}

swoc::Rv<ActiveType> Ex_this::validate(Config& cfg, Extractor::Spec&, TextView const&) { return cfg.active_type(); }
/* ------------------------------------------------------------------------------------ */
// Needs to be external visible.
Ex_this ex_this;

namespace {
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.
Ex_var var;

Ex_ua_req_url ua_req_url;
Ex_ua_req_host ureq_host;
Ex_ua_req_scheme ua_req_scheme;
Ex_ua_req_method ua_req_method;
Ex_ua_req_path ua_req_path;
Ex_ua_req_query ua_req_query;
Ex_ua_req_url_host ua_req_url_host;
Ex_ua_req_field ua_req_field;

Ex_ua_pre_remap_url ua_pre_remap_url;
Ex_ua_pre_remap_scheme ua_pre_remap_scheme;
Ex_ua_pre_remap_host ua_pre_remap_host;
Ex_ua_pre_remap_path ua_pre_remap_path;
Ex_ua_pre_remap_query ua_pre_remap_query;

Ex_proxy_req_host proxy_req_host;
Ex_proxy_req_path proxy_req_path;
Ex_proxy_req_query proxy_req_query;
Ex_proxy_req_url proxy_req_url;
Ex_proxy_req_scheme proxy_req_scheme;
Ex_proxy_req_field proxy_req_field;

Ex_proxy_rsp_status proxy_rsp_status;
Ex_proxy_rsp_field proxy_rsp_field;

Ex_upstream_rsp_field upstream_rsp_field;

Ex_upstream_rsp_status upstream_rsp_status;
Ex_is_internal is_internal;

Ex_txn_conf txn_conf;

Ex_inbound_sni inbound_sni;
Ex_inbound_protocol inbound_protocol;
Ex_inbound_remote_remote inbound_remote_addr;

Ex_random random;

static constexpr TextView NANOSECONDS = "nanoseconds";
Ex_duration<std::chrono::nanoseconds, &NANOSECONDS> nanoseconds;
static constexpr TextView MILLISECONDS = "milliseconds";
Ex_duration<std::chrono::milliseconds , &MILLISECONDS> milliseconds;
static constexpr TextView SECONDS = "seconds";
Ex_duration<std::chrono::seconds, &SECONDS> seconds;
static constexpr TextView MINUTES = "minutes";
Ex_duration<std::chrono::minutes, &MINUTES> minutes;
static constexpr TextView HOURS = "hours";
Ex_duration<std::chrono::hours, &HOURS> hours;
static constexpr TextView DAYS = "days";
Ex_duration<std::chrono::days, &DAYS> days;
static constexpr TextView WEEKS = "weeks";
Ex_duration<std::chrono::weeks, &WEEKS> weeks;

Ex_active_feature ex_with_feature;
Ex_remainder_feature ex_remainder_feature;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_this::NAME, &ex_this);
  Extractor::define(Ex_active_feature::NAME, &ex_with_feature);
  Extractor::define(Ex_remainder_feature::NAME, &ex_remainder_feature);

  Extractor::define(Ex_ua_req_url::NAME, &ua_req_url);
  Extractor::define(Ex_ua_req_host::NAME, &ureq_host);
  Extractor::define(Ex_ua_req_scheme::NAME, &ua_req_method);
  Extractor::define(Ex_ua_req_method::NAME, &ua_req_scheme);
  Extractor::define(Ex_ua_req_path::NAME, &ua_req_path);
  Extractor::define(Ex_ua_req_query::NAME, &ua_req_query);
  Extractor::define(Ex_ua_req_url_host::NAME, &ua_req_url_host);
  Extractor::define(Ex_ua_req_field::NAME, &ua_req_field);

  Extractor::define(Ex_ua_pre_remap_url::NAME, &ua_pre_remap_url);
  Extractor::define(Ex_ua_pre_remap_scheme::NAME, &ua_pre_remap_scheme);
  Extractor::define(Ex_ua_pre_remap_host::NAME, &ua_pre_remap_host);
  Extractor::define(Ex_ua_pre_remap_path::NAME, &ua_pre_remap_path);
  Extractor::define(Ex_ua_pre_remap_query::NAME, &ua_pre_remap_query);

  Extractor::define("ua-pristine-url", &ua_pre_remap_url);
  Extractor::define("ua-pristine-scheme", &ua_pre_remap_scheme);
  Extractor::define("ua-pristine-host", &ua_pre_remap_host);
  Extractor::define("ua-pristine-path", &ua_pre_remap_path);
  Extractor::define("ua-pristine-query", &ua_pre_remap_query);

  Extractor::define(Ex_proxy_req_host::NAME, &proxy_req_host);
  Extractor::define(Ex_proxy_req_url::NAME, &proxy_req_url);
  Extractor::define(Ex_proxy_req_path::NAME, &proxy_req_path);
  Extractor::define(Ex_proxy_req_scheme::NAME, &proxy_req_scheme);
  Extractor::define(Ex_proxy_req_query::NAME, &proxy_req_query);
  Extractor::define(Ex_proxy_req_field::NAME, &proxy_req_field);

  Extractor::define(Ex_proxy_rsp_status::NAME, &proxy_rsp_status);
  Extractor::define(Ex_proxy_rsp_field::NAME, &proxy_rsp_field);
  Extractor::define(Ex_upstream_rsp_field::NAME, &upstream_rsp_field);

  Extractor::define(Ex_upstream_rsp_status::NAME, &upstream_rsp_status);

  Extractor::define(Ex_txn_conf::NAME, &txn_conf);

  Extractor::define(Ex_is_internal::NAME, &is_internal);
  Extractor::define(Ex_random::NAME, &random);
  Extractor::define(Ex_var::NAME, &var);

  Extractor::define(Ex_inbound_sni::NAME, &inbound_sni);
  Extractor::define(Ex_inbound_protocol::NAME, &inbound_protocol);
  Extractor::define(Ex_inbound_remote_remote::NAME, &inbound_remote_addr);

  Extractor::define(NANOSECONDS, &nanoseconds);
  Extractor::define(MILLISECONDS, &milliseconds);
  Extractor::define(SECONDS, &seconds);
  Extractor::define(MINUTES, &minutes);
  Extractor::define(HOURS, &hours);
  Extractor::define(DAYS, &days);
  Extractor::define(WEEKS, &weeks);

  return true;
} ();
} // namespace
