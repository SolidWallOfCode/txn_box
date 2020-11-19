/** @file
   Plugin statistics support.

 * Copyright 2020, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include "txn_box/common.h"

#include <string>

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

#include "txn_box/Config.h"
#include "txn_box/Directive.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/// Statistic information.
/// The name is used when it can't be resolved during configuration loading.
struct Stat {
  TextView _name; ///< Statistic name.
  int _idx = -1; ///< Statistic index.

  Stat(Config& cfg, TextView const& name) {
    this->assign(cfg, name);
  }

  Stat& assign(Config& cfg, TextView const& name) {
    _idx = ts::plugin_stat_index(name);
    if (_idx < 0) {
      _name = cfg.localize(name, Config::LOCAL_CSTR);
    }
    return *this;
  }

  int index () {
    if (_idx < 0 && !_name.empty()) {
      _idx = ts::plugin_stat_index(_name);
      if (_idx < 0) { // On a lookup failure, give up and prevent future lookups.
        _name.clear();
      }
    }
    return _idx;
  }

  Feature value() {
    auto n { this->index() };
    return n < 0 ? NIL_FEATURE : feature_type_for<INTEGER>{ts::plugin_stat_value(_idx)};
  }

  Stat& update(feature_type_for<INTEGER> value) {
    auto n { this->index() };
    if (n >= 0) {
      ts::plugin_stat_update(n, value);
    }
    return *this;
  }
};
/* ------------------------------------------------------------------------------------ */
/// Define a plugin statistic.
class Do_stat_define : public Directive {
  using self_type = Do_stat_define; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, CfgStaticData const* rtti, YAML::Node drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node key_value);

protected:
  TextView _name; ///< Stat name
  int _value = 0; ///< Initial value.
  bool _persistent_p = false; ///< Make persistent.

  static const std::string NAME_TAG;
  static const std::string VALUE_TAG;
  static const std::string PERSISTENT_TAG;
};

const std::string Do_stat_define::KEY{"stat-define" };
const std::string Do_stat_define::NAME_TAG{"name" };
const std::string Do_stat_define::VALUE_TAG{"value" };
const std::string Do_stat_define::PERSISTENT_TAG{"persistent" };
const HookMask Do_stat_define::HOOKS{MaskFor(Hook::POST_LOAD)};

Errata Do_stat_define::invoke(Context &) {
  auto && [ idx, errata ] { ts::plugin_stat_define(_name, _value, _persistent_p)};
  return errata;
}

Rv<Directive::Handle> Do_stat_define::load(Config& cfg, CfgStaticData const*, YAML::Node drtv_node, swoc::TextView const&, swoc::TextView const&, YAML::Node key_value) {
  auto self = new self_type();
  Handle handle(self);

  // Must have a NAME

  auto name_node = key_value[NAME_TAG];
  if (!name_node) {
    return Error("{} directive at {} must have a {} key.", KEY, drtv_node.Mark(), NAME_TAG);
  }

  auto &&[name_expr, name_errata] { cfg.parse_expr(name_node) };
  if (! name_errata.is_ok()) {
    name_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
    return std::move(name_errata);
  }
  if (! name_expr.is_literal() || ! name_expr.result_type().can_satisfy(STRING)) {
    return Error("{} value at {} for {} directive at {} must be a literal string.", NAME_TAG, name_node.Mark(), KEY, drtv_node.Mark());
  }

  drtv_node.remove(name_node);
  self->_name = cfg.localize(TextView{std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._expr))}, Config::LOCAL_CSTR);

  auto value_node = key_value[VALUE_TAG];
  if (value_node) {
    auto &&[value_expr, value_errata]{cfg.parse_expr(value_node)};
    if (!value_errata.is_ok()) {
      value_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(value_errata);
    }
    if (!value_expr.is_literal() || ! value_expr.result_type().can_satisfy(INTEGER)) {
      return Error("{} value at {} for {} directive at {} must be a literal integer.", VALUE_TAG, value_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(value_node);
    self->_value = std::get<IndexFor(INTEGER)>(std::get<Expr::LITERAL>(value_expr._expr));
  }

  auto persistent_node = key_value[PERSISTENT_TAG];
  if (persistent_node) {
    auto &&[persistent_expr, persistent_errata]{cfg.parse_expr(persistent_node)};
    if (!persistent_errata.is_ok()) {
      persistent_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(persistent_errata);
    }
    if (! persistent_expr.is_literal() || ! persistent_expr.result_type().can_satisfy(BOOLEAN)) {
      return Error("{} value at {} for {} directive at {} must be a literal string.", PERSISTENT_TAG, persistent_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(persistent_node); // ugly, need to fix the overall API.
    self->_persistent_p = std::get<IndexFor(BOOLEAN)>(std::get<Expr::LITERAL>(persistent_expr._expr));
  }
  return handle;
}
/* ------------------------------------------------------------------------------------ */
class Do_stat_update : public Directive {
  using self_type = Do_stat_update; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, CfgStaticData const* rtti, YAML::Node drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node key_value);

protected:
  Stat _stat; ///< Stat to update.
  Expr _expr; ///< Value of update.

  Do_stat_update(Config& cfg, TextView const& name, Expr && expr);
};

const std::string Do_stat_update::KEY { "stat-update" };
const HookMask Do_stat_update::HOOKS{MaskFor({Hook::CREQ, Hook::PREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PRSP, Hook::URSP, Hook::TXN_START, Hook::TXN_CLOSE})};

Do_stat_update::Do_stat_update(Config& cfg, TextView const& name, Expr && expr) : _stat{cfg, name}, _expr(std::move(expr)) {}

Errata Do_stat_update::invoke(Context& ctx) {
  auto f { ctx.extract(_expr) };
  if (f.index() == IndexFor(INTEGER)) {
    _stat.update(std::get<IndexFor(INTEGER)>(f));
  }
  return {};
}

Rv<Directive::Handle> Do_stat_update::load(Config& cfg, CfgStaticData const*, YAML::Node drtv_node, swoc::TextView const&, swoc::TextView const& arg, YAML::Node key_value) {
  if (key_value.IsNull()) {
    return Handle(new self_type(cfg, arg, Expr{feature_type_for<INTEGER>(1)}));
  }

  auto && [expr, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  if (! expr.result_type().can_satisfy(INTEGER)) {
    return Error("Value for {} directive at {} must be an integer.", KEY, drtv_node.Mark());
  }

  return Handle(new self_type{cfg, arg, std::move(expr)});
}
/* ------------------------------------------------------------------------------------ */
class Ex_stat : public Extractor {
  using self_type = Ex_stat; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME{"stat"};

  Rv<ActiveType> validate(Config& cfg, Spec& spec, TextView const& arg) override;

  Feature extract(Context & ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_stat::validate(Config &cfg, Spec &spec, const TextView &arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to specify the statistic.)", NAME);
  }
  spec._data = cfg.span<Stat>(1); // allocate and stash.
  spec._data.rebind<Stat>()[0].assign(cfg, arg);
  return { INTEGER };
}

Feature Ex_stat::extract(Context &, const Spec &spec) {
  return spec._data.rebind<Stat>()[0].value();
}

BufferWriter& Ex_stat::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */

namespace {
Ex_stat stat;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Config::define<Do_stat_define>();
  Config::define<Do_stat_update>();
  Extractor::define(Ex_stat::NAME, &stat);
  return true;
} ();
} // namespace
