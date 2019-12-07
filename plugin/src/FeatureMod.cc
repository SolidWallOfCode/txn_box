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

Rv<FeatureMod::Handle> FeatureMod::load(Config &cfg, YAML::Node const &node, ValueType ftype) {
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
  bool is_valid_for(ValueType ftype) const override;

  /// Resulting type of feature after modifying.
  ValueType result_type() const override;

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

bool Mod_Hash::is_valid_for(ValueType ftype) const {
  return STRING == ftype;
}

ValueType Mod_Hash::result_type() const {
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
  bool is_valid_for(ValueType ftype) const override;

  /// Resulting type of feature after modifying.
  ValueType result_type() const override;

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

bool Mod_Else::is_valid_for(ValueType ftype) const {
  return STRING == ftype || NIL == ftype;
}

ValueType Mod_Else::result_type() const {
  return _value._result_type;
}

Errata Mod_Else::operator()(Context &ctx, Feature &feature) {
  if (is_empty(feature)) {
    feature = ctx.extract(_value);
  }
  return {};
}

// ---

/// Convert the feature to an Integer.
class Mod_As_Integer : public FeatureMod {
  using self_type = Mod_As_Integer;
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
  bool is_valid_for(ValueType ftype) const override;

  /// Resulting type of feature after modifying.
  ValueType result_type() const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node mod_node, YAML::Node key_node);

protected:
  /// Type for the internal @c convert method to return.
  using return_type = swoc::Rv<feature_type_for<INTEGER>>;
  /// Identity conversion.
  return_type convert(feature_type_for<INTEGER> n) { return n; }
  /// Convert from string
  return_type convert(feature_type_for<STRING> s) {
    TextView parsed;
    s.trim_if(&isspace);
    auto n = swoc::svtou(s, &parsed);
    if (parsed.size() == s.size()) {
      return n;
    }
    return Error(R"(Cannot convert "{}" to {}.)", s, INTEGER);
  }

  /// Generic failure case.
  template < typename T > auto convert(T & t) -> EnableForFeatureTypes<T, return_type> {
    return Error(R"(Modifier "{}" cannot convert to {}.)", KEY, INTEGER);
  }
};

const std::string Mod_As_Integer::KEY { "as-integer" };

bool Mod_As_Integer::is_valid_for(ValueType ftype) const {
  return STRING == ftype || INTEGER == ftype;
}

ValueType Mod_As_Integer::result_type() const {
  return INTEGER;
}

Errata Mod_As_Integer::operator()(Context &ctx, Feature &feature) {
  auto visitor = [&](auto & t) { return this->convert(t); };
  auto [ n, errata ] { std::visit(visitor, feature)};
  if (errata.is_ok()) {
    feature = n;
  }
  return errata;
}

Rv<FeatureMod::Handle> Mod_As_Integer::load(Config &cfg, YAML::Node mod_node, YAML::Node key_node) {
  return Handle(new self_type);
};

// ---

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  FeatureMod::define(Mod_Hash::KEY, &Mod_Hash::load);
  FeatureMod::define(Mod_Else::KEY, &Mod_Else::load);
  FeatureMod::define(Mod_As_Integer::KEY, &Mod_As_Integer::load);
  return true;
} ();
} // namespace
