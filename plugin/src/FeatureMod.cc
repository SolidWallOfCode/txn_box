/** @file Feature modifiers.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "txn_box/FeatureMod.h"
#include "txn_box/Context.h"
#include "txn_box/Config.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using namespace swoc::literals;

/// Static mapping from modifer to factory.
FeatureMod::Factory FeatureMod::_factory;

Errata FeatureMod::define(swoc::TextView name, FeatureMod::Worker const &f) {
  if (auto spot = _factory.find(name) ; spot != _factory.end()) {
    _factory.insert(spot, {name, f});
    return {};
  }
  return Error(R"(Modifier "{}" is already defined.)", name);
}

Rv<FeatureMod::Handle> FeatureMod::load(Config &cfg, YAML::Node const &node, FeatureType ftype) {
  if (! node.IsMap()) {
    return Error(R"(Modifier at {} is not an object as required.)", node.Mark());
  }

  for ( auto const& [ key_node, value_node ] : node ) {
    TextView key { key_node.Scalar() };
    // See if @a key is in the factory.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto &&[handle, errata]{spot->second(cfg, node, value_node)};

      if (!errata.is_ok()) {
        return std::move(errata);
      }
      if (! handle->is_valid_for(ftype)) {
        return Error(R"(Modifier "{}" at {} cannot accept a feature of type "{}".)", key, node.Mark(), ftype);
      }

      return std::move(handle);
    }
  }
  return Error(R"(No valid modifier key in object at {}.)", node.Mark());
}

class Mod_Hash : public FeatureMod {
  using self_type = Mod_Hash;
  using super_type = FeatureMod;
public:
  static const std::string KEY; ///< Identifier name.

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Errata operator()(Context& ctx, Feature & feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(FeatureType ftype) const override;

  /// Resulting type of feature after modifying.
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

Errata Mod_Hash::operator()(Context &ctx, Feature &feature) {
  feature_type_for<INTEGER> value = std::hash<std::string_view>{}(std::get<IndexFor(STRING)>(feature));
  feature = feature_type_for<INTEGER>{value % _n};
  return {};
}

Rv<FeatureMod::Handle> Mod_Hash::load(Config &cfg, YAML::Node mod_node, YAML::Node key_node) {
  if (! key_node.IsScalar()) {
    return Error(R"(Value for "{}" at {} in modifier at {} is not a number as required.)", KEY, key_node.Mark(), mod_node.Mark());
  }
  TextView src{key_node.Scalar()}, parsed;
  src.trim_if(&isspace);
  auto n = swoc::svtou(src, &parsed);
  if (src.size() != parsed.size()) {
    return Error(R"(Value "{}" for "{}" at {} in modifier at {} is not a number as required.)", src, KEY, key_node.Mark(), mod_node.Mark());
  }
  if (n < 2) {
    return Error(R"(Value "{}" for "{}" at {} in modifier at {} must be at least 2.)", src, KEY, key_node.Mark(), mod_node.Mark());
  }

  return { Handle{new self_type(n)}, {} };
}

// ---

/// Replace the feature with another feature if the input is nil or empty.
class Mod_Else : public FeatureMod {
  using self_type = Mod_Else;
  using super_type = FeatureMod;
public:
  static const std::string KEY; ///< Identifier name.

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Errata operator()(Context& ctx, Feature & feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ftype Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(FeatureType ftype) const override;

  /// Resulting type of feature after modifying.
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
  Extractor::Format _value;

  explicit Mod_Else(Extractor::Format && fmt) : _value(std::move(fmt)) {}
};

const std::string Mod_Else::KEY { "else" };

bool Mod_Else::is_valid_for(FeatureType ftype) const {
  return STRING == ftype || NIL == ftype;
}

FeatureType Mod_Else::output_type() const {
  return _value._feature_type;
}

Errata Mod_Else::operator()(Context &ctx, Feature &feature) {
  if (is_empty(feature)) {
    feature = ctx.extract(_value);
  }
  return {};
}

Rv<FeatureMod::Handle> Mod_Else::load(Config &cfg, YAML::Node mod_node, YAML::Node key_node) {
  auto && [ fmt, errata ] { cfg.parse_feature(key_node) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" modifier at {}.)", KEY, key_node.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(fmt)});
};

// ---

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  FeatureMod::define(Mod_Hash::KEY, &Mod_Hash::load);
  FeatureMod::define(Mod_Else::KEY, &Mod_Else::load);
  return true;
} ();
} // namespace
