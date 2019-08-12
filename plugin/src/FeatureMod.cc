/** @file
 * Comparison implementations.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "txn_box/FeatureMod.h"
#include "txn_box/Context.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using namespace swoc::literals;

FeatureMod::Factory FeatureMod::_factory;

Errata FeatureMod::define(swoc::TextView name, FeatureMod::Worker const &f) {
  _factory[name] = f;
  return {};
}

Rv<FeatureMod::Handle> FeatureMod::load(Config &cfg, YAML::Node const &node, FeatureType ftype) {
  if (! node.IsMap()) {
    return {{}, Errata().error(R"(Modifier at {} is not an object as required.)", node.Mark())};
  }

  for ( auto const& [ key_node, value_node ] : node ) {
    TextView key { key_node.Scalar() };
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to comparison. First key that is in the factory determines the comparison type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto &&[handle, errata]{spot->second(cfg, node, value_node)};

      if (!errata.is_ok()) {
        return {{}, std::move(errata)};
      }
      if (! handle->is_valid_for(ftype)) {
        return { {}, Errata().error(R"(Modifier "{}" at {} cannot accept a feature of type "{}".)", key, node.Mark(), ftype) };
      }

      return {std::move(handle), {}};
    }
  }
  return { {}, Errata().error(R"(No valid modifier key in object at {}.)", node.Mark()) };
}

class Mod_Hash : public FeatureMod {
  using self_type = Mod_Hash;
  using super_type = FeatureMod;
public:
  static const std::string KEY; ///< Identifier name.

  Errata operator()(Context& ctx, FeatureData & feature) override;

  bool is_valid_for(FeatureType ftype) const override;

  FeatureType output_type() const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node mod_node, YAML::Node key_node);

protected:
  unsigned _n = 0; ///< Number of hash buckets.

  /// Constructor for @c load.
  Mod_Hash(unsigned n);
};

const std::string Mod_Hash::KEY { "hash" };

Mod_Hash::Mod_Hash(unsigned n) : _n(n) {}

bool Mod_Hash::is_valid_for(FeatureType ftype) const {
  return STRING == ftype;
}

FeatureType Mod_Hash::output_type() const {
  return INTEGER;
}

Errata Mod_Hash::operator()(Context &ctx, FeatureData &feature) {
  return {};
}

Rv<FeatureMod::Handle> Mod_Hash::load(Config &cfg, YAML::Node mod_node, YAML::Node key_node) {
  if (! key_node.IsScalar()) {
    return {{}, Errata().error(R"(Value for "{}" at {} in modifier at {} is not a number as required.)", KEY, key_node.Mark(), mod_node.Mark())};
  }
  TextView src{key_node.Scalar()}, parsed;
  src.trim_if(&isspace);
  auto n = swoc::svtou(src, &parsed);
  if (src.size() != parsed.size()) {
    return {{}, Errata().error(R"(Value "{}" for "{}" at {} in modifier at {} is not a number as required.)", src, KEY, key_node.Mark(), mod_node.Mark())};
  }

  return { Handle{new self_type(n)}, {} };
}
