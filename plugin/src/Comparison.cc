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

using swoc::TextView;
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

/// String match.
class Cmp_Match : public Comparison {
  using self_type = Cmp_Match;
  using super_type = Comparison;
public:
  static constexpr TextView NAME { "match" };
  static Rv<Handle> load(Config& cfg, YAML::Node cmp_node, YAML::Node key_node);

  bool is_valid_for(FeatureType type) const override;
  bool operator() (TextView& text) const override;

protected:
  std::string _value; ///< Value to match.

  explicit Cmp_Match(TextView text);
};

Cmp_Match::Cmp_Match(TextView text) : _value(text) {}

bool Cmp_Match::is_valid_for(FeatureType type) const { return type == VIEW; }

bool Cmp_Match::operator()(TextView& text) const {
  return text == _value;
}

Rv<Comparison::Handle> Cmp_Match::load(Config& cfg, YAML::Node, YAML::Node key_node) {
  if (!key_node.IsScalar()) {
    return { {}, Errata().error(R"(Value for "{}" at {} is not a string.)", NAME, key_node.Mark()) };
  }
  return { Handle{new self_type{TextView{key_node.Scalar()}}}, {} };
}

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Comparison::define(Cmp_Match::NAME, &Cmp_Match::load);

  return true;
} ();
}; // namespace

