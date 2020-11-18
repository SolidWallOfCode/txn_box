/** @file
   Basic extractors implementations.

 * Copyright 2020, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <random>
#include <chrono>

#include <swoc/TextView.h>
#include <swoc/ArenaWriter.h>
#include <swoc/bwf_ip.h>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"
#include "txn_box/Extractor.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

using swoc::TextView;
using swoc::MemSpan;
using swoc::BufferWriter;
using swoc::Errata;
using swoc::Rv;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
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
class Ex_is_internal : public Extractor {
public:
  static constexpr TextView NAME { "is-internal" }; ///< Extractor name.

  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const&) override { return ActiveType{BOOLEAN}; }
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
class Ex_inbound_remote_addr : public Extractor {
public:
  static constexpr TextView NAME { "inbound-addr-remote" };
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  Feature extract(Context & ctx, Spec const& spec) override;
};

Rv<ActiveType> Ex_inbound_remote_addr::validate(Config &, Extractor::Spec &, TextView const &) {
  return ActiveType{ NIL, IP_ADDR };
}

Feature Ex_inbound_remote_addr::extract(Context & ctx, Spec const& ) {
  return swoc::IPAddr(ctx._txn.ssn().remote_addr());
}

BufferWriter& Ex_inbound_remote_addr::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_has_inbound_protocol_prefix : public Extractor {
public:
  static constexpr TextView NAME {"has-inbound-protocol-prefix"};

  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const&) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Spec const& spec) override;

  /// Required text formatting access.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context & ctx) override;
};

Rv<ActiveType> Ex_has_inbound_protocol_prefix::validate(Config& cfg, Extractor::Spec& spec, TextView const& arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to use as a protocol prefix.)", NAME);
  }
  auto local = cfg.localize(arg, Config::LOCAL_CSTR);
  spec._data = MemSpan<char>(const_cast<char*>(local.data()), local.size());
  return {BOOLEAN};
}

auto Ex_has_inbound_protocol_prefix::extract(Context &ctx, Spec const& spec) -> Feature {
  return { ! ctx._txn.ssn().proto_contains(spec._data.view()).empty() };
}

BufferWriter& Ex_has_inbound_protocol_prefix::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}
/* ------------------------------------------------------------------------------------ */
class Ex_inbound_protocol_stack : public Extractor {
public:
  static constexpr TextView NAME {"inbound-protocol-stack"};

  /// Check argument and indicate possible feature types.
  Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const&) override;

  /// Extract the feature from the @a ctx.
  Feature extract(Context& ctx, Spec const& spec) override;

  /// Required text formatting access.
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context & ctx) override;
};

Rv<ActiveType> Ex_inbound_protocol_stack::validate(Config&, Extractor::Spec&, TextView const&) {
  return {ActiveType::TupleOf(STRING)};
}

auto Ex_inbound_protocol_stack::extract(Context &ctx, Spec const&) -> Feature {
  std::array<char const*, 10> tags;
  auto n = ctx._txn.ssn().protocol_stack(MemSpan{tags.data(), tags.size()});
  if (n > 0) {
    auto span = ctx.alloc_span<Feature>(n);
    for ( decltype(n) idx = 0 ; idx < n ; ++idx ) {
      // Plugin API guarantees returned tags are process lifetime so can be marked literal.
      span[idx] = FeatureView::Literal(TextView{tags[idx], TextView::npos});
    }
    return span;
  }
  return NIL_FEATURE;
}

BufferWriter& Ex_inbound_protocol_stack::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
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
  auto local = cfg.localize(arg, Config::LOCAL_CSTR);
  spec._data = swoc::MemSpan<char>(const_cast<char*>(local.data()), local.size());
  return { STRING };
}

BufferWriter& Ex_inbound_protocol::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  auto tag = ctx._txn.ssn().proto_contains(spec._data.view());
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
/// Remnant capture group.
class Ex_unmatched_group : public Extractor {
  using self_type = Ex_unmatched_group; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME = UNMATCHED_FEATURE_KEY;
  Rv<ActiveType> validate(Config & cfg, Spec & spec, TextView const& arg) override;
  Feature extract(Context& ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Feature Ex_unmatched_group::extract(class Context & ctx, const struct Extractor::Spec &) {
  return ctx._remainder;
}

BufferWriter& Ex_unmatched_group::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, ctx._remainder);
}

Rv<ActiveType>
Ex_unmatched_group::validate(Config&, Extractor::Spec&, TextView const&) { return {STRING }; }

/* ------------------------------------------------------------------------------------ */
BufferWriter& Ex_this::format(BufferWriter &w, Extractor::Spec const &spec, Context &ctx) {
  return bwformat(w, spec, _fg->extract(ctx, spec._ext));
}

Feature Ex_this::extract(class Context & ctx, Extractor::Spec const & spec) {
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

Ex_is_internal is_internal;

Ex_txn_conf txn_conf;

Ex_inbound_sni inbound_sni;
Ex_inbound_protocol inbound_protocol;
Ex_inbound_remote_addr inbound_remote_addr;
Ex_has_inbound_protocol_prefix has_inbound_protocol_prefix;
Ex_inbound_protocol_stack inbound_protocol_stack;

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
Ex_unmatched_group unmatched_group;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_this::NAME, &ex_this);
  Extractor::define(Ex_active_feature::NAME, &ex_with_feature);
  Extractor::define(Ex_unmatched_group::NAME, &unmatched_group);
  Extractor::define("unmatched", &unmatched_group);

  Extractor::define(Ex_txn_conf::NAME, &txn_conf);

  Extractor::define(Ex_is_internal::NAME, &is_internal);
  Extractor::define(Ex_random::NAME, &random);
  Extractor::define(Ex_var::NAME, &var);

  Extractor::define(Ex_inbound_sni::NAME, &inbound_sni);
  Extractor::define(Ex_inbound_protocol::NAME, &inbound_protocol);
  Extractor::define(Ex_has_inbound_protocol_prefix::NAME, &has_inbound_protocol_prefix);
  Extractor::define(Ex_inbound_protocol_stack::NAME, &inbound_protocol_stack);
  Extractor::define(Ex_inbound_remote_addr::NAME, &inbound_remote_addr);

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
