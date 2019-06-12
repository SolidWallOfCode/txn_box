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

bool Comparison::has_regex() const { return false; }

Errata Comparison::define(swoc::TextView name, Comparison::Assembler &&cmp_asm) {
  _factory[name] = std::move(cmp_asm);
  return {};
}

Rv<Comparison::Handle> Comparison::load(Config & cfg, FeatureType type, YAML::Node node) {
  for ( auto const& [ key_node, value_node ] : node ) {
    TextView key { key_node.Scalar() };
    if (key == Directive::DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to comparison. First key that is in the factory determines the comparison type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto &&[handle, errata]{spot->second(cfg, node, value_node)};

      if (!errata.is_ok()) {
        return {{}, std::move(errata)};
      }

      if (!handle->is_valid_for(type)) {
        return {{}, Errata().error(
            R"(Comparison "{}" at {} is not valid for a feature of type "{}".)", key, node.Mark()
            , type)};
      }
      return {std::move(handle), {}};
    }
  }
  return { {}, Errata().error(R"(No valid comparison key in object at {}.)", node.Mark()) };
}
/* ------------------------------------------------------------------------------------ */
/// Direct / exact string matching.
class Cmp_Match : public Comparison {
  using self_type = Cmp_Match;
  using super_type = Comparison;
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

  /// Mark which feature types this can compare.
  bool is_valid_for(FeatureType type) const override;

  /** Compare @a text for a match.
   *
   * @param text The feature to compare.
   * @return @c true if @a text matches, @c false otherwise.
   */
  bool operator() (Context&, TextView& text) const override;

protected:
  std::string _value; ///< Value to match.

  /// Constructor used by @c load.
  explicit Cmp_Match(TextView text);
};

const std::string Cmp_Match::KEY { "match" };

Cmp_Match::Cmp_Match(TextView text) : _value(text) {}

bool Cmp_Match::is_valid_for(FeatureType type) const { return type == VIEW; }

bool Cmp_Match::operator()(Context&, TextView& text) const {
  return text == _value;
}

Rv<Comparison::Handle> Cmp_Match::load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node) {
  auto && [ ex_fmt, errata ] { cfg.parse_feature(key_node) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing comparison "{}" at {}.)", KEY, key_node.Mark());
    return { {}, std::move(errata) };
  }

  // Literal string comparison.
  auto self = new self_type(key_node.Scalar());
  return { Handle{self}, {} };
}

/* ------------------------------------------------------------------------------------ */
class Cmp_RegexMatch : public Comparison {
  using self_type = Cmp_RegexMatch;
  using super_type = Comparison;
public:
  static const std::string KEY;
  static const std::string KEY_NOCASE;

  bool operator() (Context& ctx, TextView& text) const override;
  bool is_valid_for(FeatureType ftype) const override;
  bool has_regex() const override;

  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

protected:
  Rxp _rxp; ///< regular expression to match.
  bool _caseless_p = false;

  explicit Cmp_RegexMatch(Rxp && rxp) : _rxp(std::move(rxp)) {}
};

const std::string Cmp_RegexMatch::KEY { "regex" };
const std::string Cmp_RegexMatch::KEY_NOCASE { "regex-nocase" };

bool Cmp_RegexMatch::is_valid_for(FeatureType ftype) const {
  return VIEW == ftype;
}

bool Cmp_RegexMatch::has_regex() const { return true; }

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

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Comparison::define(Cmp_Match::KEY, &Cmp_Match::load);
  Comparison::define(Cmp_RegexMatch::KEY, &Cmp_RegexMatch::load);
  Comparison::define(Cmp_RegexMatch::KEY_NOCASE, &Cmp_RegexMatch::load);

  return true;
} ();

}; // namespace

