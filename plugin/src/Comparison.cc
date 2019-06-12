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

#include <string>
#include <txn_box/Context.h>

#include "txn_box/Rxp.h"
#include "txn_box/Comparison.h"
#include "txn_box/Directive.h"
#include "txn_box/Config.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Comparison::Factory Comparison::_factory;

unsigned Comparison::rxp_group_count() const { return 0; }

Errata Comparison::define(swoc::TextView name, FeatureMask const& types, Comparison::Worker &&worker) {
  _factory[name] = std::make_tuple(std::move(worker), types);
  return {};
}

Rv<Comparison::Handle> Comparison::load(Config & cfg, FeatureType ftype, YAML::Node node) {
  for ( auto const& [ key_node, value_node ] : node ) {
    TextView key { key_node.Scalar() };
    if (key == Directive::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to comparison. First key that is in the factory determines the comparison type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto &&[worker, types] = spot->second;
      if (! types[IndexFor(ftype)]) {
        return {{}, Errata().error(
            R"(Comparison "{}" at {} is not valid for a feature of type "{}".)", key, node.Mark()
            , ftype)};
      }

      auto &&[handle, errata]{worker(cfg, node, value_node)};

      if (!errata.is_ok()) {
        return {{}, std::move(errata)};
      }

      return {std::move(handle), {}};
    }
  }
  return { {}, Errata().error(R"(No valid comparison key in object at {}.)", node.Mark()) };
}
/* ------------------------------------------------------------------------------------ */
/** Utility base class for comparisons that are based on literal string matching.
 * This is @b not intended to be used as a comparison itself.
 */
class StringComparison: public Comparison {
  using self_type = StringComparison; ///< Self reference type.
  using super_type = Comparison; ///< Parent type.
public:
  /// Mark for @c STRING support only.
  static const FeatureMask TYPES;

protected:
  Extractor::Format _exfmt; ///< Suffix value to compare.

  /// Load up the string, accounting for extraction and types.
  static Rv<Extractor::Format> load_exfmt(Config& cfg, YAML::Node cmp_node, YAML::Node key_node, std::string const& KEY);

  /// Internal constructor used by @c load.
  explicit StringComparison(Extractor::Format && exf);
};

const FeatureMask StringComparison::TYPES { MaskFor(STRING) };

StringComparison::StringComparison(Extractor::Format &&exf) : _exfmt(std::move(exf)) {}

Rv<Extractor::Format> StringComparison::load_exfmt(Config &cfg, YAML::Node cmp_node, YAML::Node key_node, std::string const& KEY) {
  auto &&[exfmt, errata]{cfg.parse_feature(key_node)};

  if (!errata.is_ok()) {
    errata.info(R"(While parsing comparison "{}" at {}.)", KEY, key_node.Mark());
    return {{}, std::move(errata)};
  }

  if (!TYPES[IndexFor(exfmt._feature_type)]) {
    Errata().error(R"(Value type "{}" for comparison "{}" at {} is not supported.)"
                   , exfmt._feature_type, KEY, key_node.Mark());
  }
  return {std::move(exfmt), {}};
}

/* ------------------------------------------------------------------------------------ */
/// Direct / exact string matching.
class Cmp_Match : public StringComparison {
  using self_type = Cmp_Match;
  using super_type = StringComparison;
public:
  /// Identifier for this comparison.
  static const std::string KEY;

  /** Instantiate an instance from YAML configuration.
   *
   * @param cfg Global configuration object.
   * @param cmp_node The node containing the comparison.
   * @param key_node The node in @a cmp_node that identified this comparison.
   * @return An instance or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

  /** Compare @a text for a match.
   *
   * @param ctx The transaction context.
   * @param text The feature to compare.
   * @return @c true if @a text matches, @c false otherwise.
   */
  bool operator() (Context& ctx, TextView& text) const override;

protected:
  using super_type::super_type;
};

const std::string Cmp_Match::KEY { "match" };

bool Cmp_Match::operator()(Context& ctx, TextView& text) const {
  FeatureData feature { ctx.extract(_exfmt) };
  return text == std::get<STRING>(feature);
}

Rv<Comparison::Handle> Cmp_Match::load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ exfmt, errata ] { super_type::load_exfmt(cfg, cmp_node, key_node, KEY) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  return { Handle{new self_type(std::move(exfmt))}, {} };
}

/* ------------------------------------------------------------------------------------ */
/// Direct / exact string matching, case insensitive.
class Cmp_MatchNocase : public StringComparison {
  using self_type = Cmp_MatchNocase;
  using super_type = StringComparison;
public:
  /// Identifier for this comparison.
  static const std::string KEY;

  /** Instantiate an instance from YAML configuration.
   *
   * @param cfg Global configuration object.
   * @param cmp_node The node containing the comparison.
   * @param key_node The node in @a cmp_node that identified this comparison.
   * @return An instance or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

  /** Compare @a text for a match.
   *
   * @param ctx The transaction context.
   * @param text The feature to compare.
   * @return @c true if @a text matches, @c false otherwise.
   */
  bool operator() (Context& ctx, TextView& text) const override;

protected:
  using super_type::super_type;
};

const std::string Cmp_MatchNocase::KEY { "match-nocase" };

bool Cmp_MatchNocase::operator()(Context& ctx, TextView& text) const {
  FeatureData feature { ctx.extract(_exfmt) };
  return 0 == strcasecmp(text, std::get<STRING>(feature));
}

Rv<Comparison::Handle> Cmp_MatchNocase::load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ exfmt, errata ] { super_type::load_exfmt(cfg, cmp_node, key_node, KEY) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  return { Handle{new self_type(std::move(exfmt))}, {} };
}

/* ------------------------------------------------------------------------------------ */

/** Compare a suffix.
 * This matches if the suffix of the feature is the same as the static value.
 */
class Cmp_Suffix : public StringComparison {
  using self_type = Cmp_Suffix; ///< Self reference type.
  using super_type = StringComparison; ///< Parent type.
public:
  /// Name of comparison.
  static const std::string KEY;

  /// Test for suffix being @a text.
  bool operator() (Context& ctx, TextView& text) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

protected:
  using super_type::super_type;
};

const std::string Cmp_Suffix::KEY { "suffix" };

bool Cmp_Suffix::operator()(Context &ctx, TextView &text) const {
  FeatureData feature { ctx.extract(_exfmt) };
  return text.ends_with(std::get<IndexFor(STRING)>(feature));
}

Rv<Comparison::Handle> Cmp_Suffix::load(Config &cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ exfmt, errata ] { super_type::load_exfmt(cfg, cmp_node, key_node, KEY) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  return { Handle{new self_type(std::move(exfmt))}, {} };
}

/* ------------------------------------------------------------------------------------ */
/** Compare a suffix.
 * This matches if the suffix of the feature is the same as the static value.
 */
class Cmp_SuffixNocase : public StringComparison {
  using self_type = Cmp_SuffixNocase; ///< Self reference type.
  using super_type = StringComparison; ///< Parent type.
public:
  /// Name of comparison.
  static const std::string KEY;

  /// Test for suffix being @a text.
  bool operator() (Context& ctx, TextView& text) const override;

  /// Construct an instance from YAML configuration.
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

protected:
  using super_type::super_type;
};

const std::string Cmp_SuffixNocase::KEY { "suffix-nocase" };

bool Cmp_SuffixNocase::operator()(Context &ctx, TextView &text) const {
  FeatureData feature { ctx.extract(_exfmt) };
  return text.ends_with_nocase(std::get<IndexFor(STRING)>(feature));
}

Rv<Comparison::Handle> Cmp_SuffixNocase::load(Config &cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ exfmt, errata ] { super_type::load_exfmt(cfg, cmp_node, key_node, KEY) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  return { Handle{new self_type(std::move(exfmt))}, {} };
}

/* ------------------------------------------------------------------------------------ */
class Cmp_RegexMatch : public Comparison {
  using self_type = Cmp_RegexMatch;
  using super_type = Comparison;
public:
  /// Standard key.
  static const std::string KEY;
  /// Case insensitive comparison key.
  static const std::string KEY_NOCASE;
  /// Valid types for this comparison.
  static const FeatureMask TYPES;

  bool operator() (Context& ctx, TextView& text) const override;
  unsigned rxp_group_count() const override;

  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

protected:
  Rxp _rxp; ///< regular expression to match.
  bool _caseless_p = false;

  explicit Cmp_RegexMatch(Rxp && rxp) : _rxp(std::move(rxp)) {}
};

const std::string Cmp_RegexMatch::KEY { "regex" };
const std::string Cmp_RegexMatch::KEY_NOCASE { "regex-nocase" };
const FeatureMask Cmp_RegexMatch::TYPES { MaskFor(STRING) };

unsigned Cmp_RegexMatch::rxp_group_count() const { return _rxp.capture_count(); }

Rv<Comparison::Handle> Cmp_RegexMatch::load(Config &cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ fmt, errata ] { cfg.parse_feature(key_node) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" comparison value at {}.)", KEY, cmp_node.Mark());
    return { {}, std::move(errata) };
  }
  if (! fmt._literal_p) {
    return { {}, Errata().error(R"(Dynamic regular expression support is not yet implemented at {}.)", key_node.Mark()) };
  }
  // Handle empty format / string?
  Rxp::OptionGroup rxp_opt;
  if (cmp_node[KEY_NOCASE] == key_node) {
    rxp_opt[Rxp::OPT_NOCASE] = true;
  }
  auto && [ rxp, rxp_errata ] { Rxp::parse(fmt[0]._ext, rxp_opt) }; // Config coalesced the literals.
  if (! rxp_errata.is_ok()) {
    rxp_errata.info(R"(While parsing "{}" value at {}.)", KEY, key_node.Mark());
    return { {}, std::move(rxp_errata) };
  }

  cfg.require_capture_count(rxp.capture_count());
  return { Handle(new self_type(std::move(rxp))), {} };
}

bool Cmp_RegexMatch::operator()(Context& ctx, TextView &text) const {
  auto result = _rxp(text, ctx._rxp_working);
  if (result > 0) {
    // Update context to have this match as the active capture groups.
    ctx.promote_capture_data();
    ctx._rxp_src = text;
    return true;
  }
  return false;
}

/* ------------------------------------------------------------------------------------ */

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Comparison::define(Cmp_Match::KEY, Cmp_Match::TYPES, &Cmp_Match::load);
  Comparison::define(Cmp_MatchNocase::KEY, Cmp_MatchNocase::TYPES, &Cmp_MatchNocase::load);
  Comparison::define(Cmp_Suffix::KEY, Cmp_Suffix::TYPES, &Cmp_Suffix::load);
  Comparison::define(Cmp_SuffixNocase::KEY, Cmp_SuffixNocase::TYPES, &Cmp_SuffixNocase::load);
  Comparison::define(Cmp_RegexMatch::KEY, Cmp_RegexMatch::TYPES, &Cmp_RegexMatch::load);
  Comparison::define(Cmp_RegexMatch::KEY_NOCASE, Cmp_RegexMatch::TYPES, &Cmp_RegexMatch::load);

  return true;
} ();

}; // namespace

