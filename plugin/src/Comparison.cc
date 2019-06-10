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

#include "txn_box/Comparison.h"
#include "txn_box/Directive.h"
#include "txn_box/Config.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Comparison::Factory Comparison::_factory;

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
  bool operator() (TextView& text) const override;

protected:
  std::string _value; ///< Value to match.

  /// Constructor used by @c load.
  explicit Cmp_Match(TextView text);
};

const std::string Cmp_Match::KEY { "match" };

Cmp_Match::Cmp_Match(TextView text) : _value(text) {}

bool Cmp_Match::is_valid_for(FeatureType type) const { return type == VIEW; }

bool Cmp_Match::operator()(TextView& text) const {
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

class Cmp_RegexMatch : public Comparison {
  using self_type = Cmp_RegexMatch;
  using super_type = Comparison;
  friend class Cmp_Match;
public:
  static const std::string KEY;

  bool operator() (TextView& text) const override;
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

protected:
  Rxp _rxp; ///< regular expression to match.
  bool _caseless_p = false;

  explicit Cmp_RegexMatch(Rxp && rxp) : _rxp(std::move(rxp)) {}
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);
};

const std::string Cmp_RegexMatch::KEY { "regex" };
Rv<Comparison::Handle> Cmp_RegexMatch::load(Config &cfg, YAML::Node cmp_node, YAML::Node key_node) {
  
  return { {}, {} };
}

Rv<Rxp> Rxp::parse(TextView str) {
  int errc = 0;
  size_t err_off = 0;
  auto result = pcre2_compile(reinterpret_cast<unsigned const char*>(str.data()), str.size(), 0, &errc, &err_off, nullptr);
  if (nullptr == result) {
    PCRE2_UCHAR err_buff[128];
    auto err_size = pcre2_get_error_message(errc, err_buff, sizeof(err_buff));
    return { {}, Errata().error(R"(Failed to parse regular expression - error "{}" [{}] at offset {} in "{}".)", TextView(
        reinterpret_cast<char const*>(err_buff), err_size), errc, err_off, str) };
  }
  return { result, {} };
};


namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Comparison::define(Cmp_Match::KEY, &Cmp_Match::load);

  return true;
} ();

}; // namespace

