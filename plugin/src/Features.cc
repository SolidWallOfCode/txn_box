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
  , { ValueType::VARIABLE, "var" }
  , { ValueType::ACTIVE, "active" }
}};

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

  ValueType result_type() const override { return VARIABLE; }

  Errata validate(Config & cfg, Spec & spec, TextView const& arg) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Extractor::Spec const&) override;

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Errata Ex_var::validate(class Config & cfg, struct Extractor::Spec & spec, const class swoc::TextView & arg) {
  auto name = cfg.span<feature_type_for<STRING>>(1);
  spec._data = name.rebind<void>();
  name[0] = cfg.localize(arg);
  return {};
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
class Ex_creq_url_host : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-url-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_creq_url_host::direct_view(Context &ctx, Spec const&) const {
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
  return bwformat(w, spec, this->direct_view(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
class Ex_creq_method : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-method" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_creq_method::direct_view(Context &ctx, Spec const&) const {
  FeatureView zret;
  zret._direct_p = true;
  if ( auto hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    zret = hdr.method();
  }
  return zret;
}

BufferWriter& Ex_creq_method::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->direct_view(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_scheme : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-scheme" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_creq_scheme::direct_view(Context &ctx, Spec const&) const {
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
  return bwformat(w, spec, this->direct_view(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_path : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-path" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_creq_path::direct_view(Context &ctx, Spec const&) const {
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
  return bwformat(w, spec, this->direct_view(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_creq_host : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const&) const override;
};

FeatureView Ex_creq_host::direct_view(Context &ctx, Spec const&) const {
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
  return bwformat(w, spec, this->direct_view(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */
class Ex_creq_field : public DirectFeature {
public:
  static constexpr TextView NAME { "creq-field" };

  swoc::Errata validate(Config & cfg, Spec & spec, TextView const& arg) override {
    spec._data.assign(const_cast<char*>(arg.data()), arg.size());
    return {};
  }

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_creq_field::direct_view(Context &ctx, Spec const& spec) const {
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
  return bwformat(w, spec, this->direct_view(ctx, spec));
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
class Ex_cssn_sni : public DirectFeature {
  using self_type = Ex_cssn_sni; ///< Self reference type.
  using super_type = DirectFeature; ///< Parent type.
public:
  static constexpr TextView NAME { "cssn-sni" };
  /// Extract the SNI  name from the inbound session.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx, Spec const& spec) const override;
};

FeatureView Ex_cssn_sni::direct_view(Context & ctx, Spec const& spec) const {
  return ctx._txn.ssn().inbound_sni();
}

BufferWriter& Ex_cssn_sni::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->direct_view(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_random : public IntegerExtractor {
  using self_type = Ex_random; ///< Self reference type.
  using super_type = IntegerExtractor; ///< Parent type.
public:
  static constexpr TextView NAME { "random" };

  Errata validate(Config & cfg, Spec & spec, TextView const& arg) override;

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

Errata Ex_random::validate(Config &cfg, Extractor::Spec &spec, TextView const &arg) {
  auto values = cfg.span<feature_type_for<INTEGER>>(2);
  spec._data = values.rebind<void>();
  feature_type_for<INTEGER> min = 0, max = 99;
  // Use these values if anything goes wrong.
  values[0] = min;
  values[1] = max;
  // Parse the paramenter.
  if (arg) {
    auto max_arg { arg };
    auto min_arg = max_arg.split_prefix_at(',');
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

  values[0] = min;
  values[1] = max;
  return {};
}

/* ------------------------------------------------------------------------------------ */
/// Extract the most recent selection feature.
class Ex_with_feature : public Extractor {
  using self_type = Ex_with_feature; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME { "..." };
  ValueType result_type() const override { return ACTIVE; }
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_with_feature::extract(class Context & ctx, const struct Extractor::Spec & spec) {
  return ctx._feature;
}

BufferWriter& Ex_with_feature::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._feature);
}
/* ------------------------------------------------------------------------------------ */
BufferWriter& Ex_this::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  Feature feature {_fg->extract(ctx, spec._ext)};
  return bwformat(w, spec, feature);
}

auto Ex_this::result_type() const -> ValueType { return VARIABLE; }

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
Ex_ursp_status ursp_status;
Ex_is_internal is_internal;

Ex_cssn_sni cssn_sni;

Ex_random random;

Ex_with_feature ex_with_feature;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_this::NAME, &ex_this);
  Extractor::define(Ex_with_feature::NAME, &ex_with_feature);

  Extractor::define(Ex_creq_url::NAME, &creq_url);
  Extractor::define(Ex_creq_host::NAME, &creq_host);
  Extractor::define(Ex_creq_scheme::NAME, &creq_method);
  Extractor::define(Ex_creq_method::NAME, &creq_scheme);
  Extractor::define(Ex_creq_path::NAME, &creq_path);
  Extractor::define(Ex_creq_url_host::NAME, &creq_url_host);
  Extractor::define(Ex_creq_field::NAME, &creq_field);
  Extractor::define(Ex_ursp_status::NAME, &ursp_status);
  Extractor::define(Ex_is_internal::NAME, &is_internal);
  Extractor::define(Ex_cssn_sni::NAME, &cssn_sni);
  Extractor::define(Ex_random::NAME, &random);
  Extractor::define(Ex_var::NAME, &var);

  return true;
} ();
} // namespace
