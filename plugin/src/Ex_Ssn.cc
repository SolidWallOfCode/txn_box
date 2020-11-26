/** @file
   Session Extractors.

 * Copyright 2020, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <random>
#include <chrono>

#include <swoc/TextView.h>
#include <swoc/ArenaWriter.h>

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
using swoc::MemSpan;
//namespace bwf = swoc::bwf;
//using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/** Extract the number of transactions for the inbound session.
 */
class Ex_inbound_txn_count : public Extractor {
public:
  /// Extractor name.
  static constexpr TextView NAME { "inbound-txn-count" };

  /** Validate argument and indicate extracted type.
   *
   * @return The active type and any errors.
   */
  Rv<ActiveType> validate(Config&, Extractor::Spec&, TextView const&) override;

  /** Extract the transaction count.
   *
   * @param ctx Current transaction context.
   * @param spec Format specifier.
   * @return The extracted feature.
   *
   * This is called when the extractor is a @c Direct feature and therefore typed.
   */
  Feature extract(Context & ctx, Spec const& spec)  override;
};

Rv<ActiveType> Ex_inbound_txn_count::validate(Config&, Extractor::Spec&, TextView const&) {
  return ActiveType{ INTEGER }; // never a problem, just return the type.
}
Feature Ex_inbound_txn_count::extract(Context &ctx, Spec const&) {
  return feature_type_for<INTEGER>(ctx.inbound_ssn().txn_count());
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
namespace {
// Extractors aren't constructed, they are always named references to singletons.
// These are the singletons.

Ex_inbound_txn_count inbound_txn_count;
Ex_inbound_sni inbound_sni;
Ex_inbound_protocol inbound_protocol;
Ex_inbound_remote_addr inbound_remote_addr;
Ex_has_inbound_protocol_prefix has_inbound_protocol_prefix;
Ex_inbound_protocol_stack inbound_protocol_stack;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_inbound_txn_count::NAME, &inbound_txn_count);
  Extractor::define(Ex_inbound_sni::NAME, &inbound_sni);
  Extractor::define(Ex_inbound_protocol::NAME, &inbound_protocol);
  Extractor::define(Ex_has_inbound_protocol_prefix::NAME, &has_inbound_protocol_prefix);
  Extractor::define(Ex_inbound_protocol_stack::NAME, &inbound_protocol_stack);
  Extractor::define(Ex_inbound_remote_addr::NAME, &inbound_remote_addr);

  return true;
} ();
} // namespace
