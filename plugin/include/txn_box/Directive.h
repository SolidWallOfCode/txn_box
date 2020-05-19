/** @file
   Base directive types.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <unordered_map>
#include <string_view>
#include <functional>
#include <memory>
#include <initializer_list>

#include "yaml-cpp/yaml.h"
#include "swoc/Errata.h"

#include "txn_box/common.h"
#include "txn_box/Extractor.h"

class Context;

/// Base class for directives.
class Directive {
  using self_type = Directive; ///< Self reference type.
  friend Config;
  friend Context;

public:
  /// Import global value for convenience.
  static constexpr swoc::TextView DO_KEY = Global::DO_KEY;

  /// Generic handle for all directives.
  using Handle = std::unique_ptr<self_type>;

  /** Functor to create an instance of a @c Directive from configuration.
   *
   * @param cfg Configuration object.
   * @param drtv_node Directive node.
   * @param key_node Child of @a drtv_node that contains the key used to match the functor.
   * @return A new instance of the appropriate directive, or errors on failure.
   */
  using InstanceLoader = std::function<swoc::Rv<Directive::Handle> (Config& cfg, YAML::Node drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node key_value)>;

  /** Functor to do config level initialization.
   *
   * @param cfg Configuration object.
   *
   * This is called once per directive class when the @c Config instance is initialized. This should
   * perform any initialization needed for the directive as a type, rather than as an instance used
   * in the configuration. The most common use is if the directive needs space in a @c Context -
   * that space must be reserved during the invocation of this functor.
   */
  using CfgInitializer = std::function<swoc::Errata (Config& cfg)>;

  /** Information about a directive type.
   * This is stored in the directive factory.
   */
  struct FactoryInfo {
    unsigned _idx; ///< Index for doing config time type info lookup.
    HookMask _hook_mask; ///< Valid hooks for this directive.
    Directive::InstanceLoader _load_cb; ///< Functor to load the directive from YAML data.
    Directive::CfgInitializer _cfg_init_cb; ///< Configuration init callback.
  };

  /** Config level information.
   * Each instance of a directive of a specific type has a pointer to this record, which is used to
   * provide the equivalent of run time type information. Instances are stored in the @c Config.
   */
  struct CfgInfo {
    FactoryInfo const * _static; ///< Related static information.
    unsigned _count = 0; ///< Number of instances.
    swoc::MemSpan<void> _cfg_store; ///< Shared config storage.
    // @c Context storage can't be stored as a span, because it's different in every @c context instance.
    size_t _ctx_storage_size = 0; ///< Amount of shared context storage required.
    size_t _ctx_storage_offset = 0; ///< Offset into shared context storage block.
  };

  virtual ~Directive() = default;

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  virtual swoc::Errata invoke(Context &ctx) = 0;

  /** Configuration initializer.
   *
   * @param Config& Configuration object.
   * @return Errors, if any.
   *
   * Default implementation that does nothing. Override as needed.
   */
  static swoc::Errata cfg_init(Config&) { return {}; }

protected:
  CfgInfo const* _rtti = nullptr; ///< Run time (per Config) information.
};

/** An ordered list of directives.
 *
 * This has no action of its own, it contains a list of other directives which are performed.
 */
class DirectiveList : public Directive {
  using self_type = DirectiveList; ///< Self reference type.
  using super_type = Directive; ///< Parent type.

public:
  self_type & push_back(Handle && d);

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  swoc::Errata invoke(Context &ctx) override;

protected:
  std::vector<Directive::Handle> _directives;
};

/// @c when directive - control which hook on which the configuration is handled.
// @c when is special and needs to be globally visible.
class When : public Directive {
  using super_type = Directive;
  using self_type = When;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  swoc::Errata invoke(Context &ctx) override;

  Hook get_hook() const;

  /** Load / create an instance from configuration data.
   *
   * @param config Configuration object.
   * @param drtv_node Directive node.
   * @param key_node Child of @a dctv_node which matched the directive key.
   * @return A new directive instance on success, error notes on failure.
   */
  static swoc::Rv<Handle> load(Config& cfg, YAML::Node drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node key_value);

protected:
  Hook _hook { Hook::INVALID };
  Handle _directive; /// Directive to invoke in the specified hook.

  /** Construct from hook and a directive.
   *
   * @param hook_idx The hook on which @a directive is invoked.
   * @param directive Directive to invoke.
   */
  When(Hook hook_idx, Directive::Handle && directive);

  // Because @c When is handle in a special manner for configurations, it must be able to reach in.
  friend Config;
};

/** Directive that explicitly does nothing.
 *
 * Used for a place holder to avoid null checks. This isn't explicitly available from configuration
 * it is used when the directive is omitted (e.g. an empty @c do key).
 */

class NilDirective : public Directive {
  using self_type = NilDirective; ///< Self reference type.
  using super_type = Directive; ///< Parent type.

public:
  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  swoc::Errata invoke(Context &ctx) override;;
protected:
};

class LambdaDirective : public Directive {
  using self_type = LambdaDirective; ///< Self reference type.
  using super_type = Directive; ///< Parent type.

public:
  using Lambda = std::function<swoc::Errata (Context&)>;
  /// Construct with function @a f.
  /// When the directive is invoked, it in turn invokes @a f.
  LambdaDirective(Lambda && f);

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  swoc::Errata invoke(Context &ctx) override;
protected:
  /// Function to invoke.
  Lambda _f;
};

inline Hook When::get_hook() const { return _hook; }

inline swoc::Errata LambdaDirective::invoke(Context &ctx) { return _f(ctx); }

inline LambdaDirective::LambdaDirective(std::function<swoc::Errata(Context &)> &&f) : _f(std::move(f)) {}

