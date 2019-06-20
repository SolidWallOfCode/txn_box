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

#pragma once

#include <unordered_map>
#include <string_view>
#include <functional>
#include <memory>

#include "yaml-cpp/yaml.h"
#include "swoc/Errata.h"

#include "txn_box/common.h"

class Config;
class Context;

/** Base class for directives.
 *
 */
class Directive {
  using self_type = Directive; ///< Self reference type.
  friend Config;

public:
  /// Options for a directive instance.
  class Options {
  private:
    using self_type = Options; ///< Self reference type.
  public:
    Options() = default;
    /** Set the amount of shared configuration storage needed.
     *
     * Configuration storage is allocated per directive class, not instance, and shared
     * among all configuration instances and all transactions.
     *
     * @param n Number of bytes.
     * @return @a this
     */
    self_type & cfg_storage(size_t n) {
      _cfg_size = n;
      return *this;
    }

    /** Set the amount of shared configuration storage needed.
     *
     * This storage is allocated per context per directive and shared among all directive instances
     * in the configuration, but not shared across transactions.
     *
     * @param n Number of bytes.
     * @return @a this
     */
    self_type & ctx_storage(size_t n) {
      _ctx_size = n;
      return *this;
    }

    size_t _cfg_size = 0; ///< Amount of config storage.
    size_t _ctx_size = 0; ///< Amount of shared per context storage.
  };

  /// Standard name for nested directives.
  /// This key is never matched as a directive name.
  /// It is defined here, even though not all directives use it, in order to be consistent across
  /// those that do.
  static const std::string DO_KEY;

  /// Generic handle for all directives.
  using Handle = std::unique_ptr<self_type>;

  /** Functor to create an instance of a @c Directive from configuration.
   *
   * @param cfg Configuration object.
   * @param drtv_node Directive node.
   * @param key_node Child of @a drtv_node that contains the key used to match the functor.
   * @return A new instance of the appropriate directive, or errors on failure.
   */
  using Worker = std::function<swoc::Rv<Directive::Handle> (Config& cfg, YAML::Node drtv_node, YAML::Node key_node)>;

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  virtual swoc::Errata invoke(Context &ctx) = 0;

protected:
  /// Information about a specific type of Directive per @c Config instance.
  /// This data can vary between @c Config instances and is initialized during instance construction
  /// and configuration file loading. It is constant during runtime (transaction processing).
  /// @internal Equivalent to run time type information.
  struct CfgInfo {
    unsigned _idx = 0; ///< Identifier.
    unsigned _count = 0; ///< Number of instances.
    size_t _ctx_storage_size = 0; ///< Amount of shared context storage required.
    size_t _ctx_storage_offset = 0; ///< Offset into shared context storage block.
    swoc::MemSpan<void> _cfg_span; ///< Shared config storage.
  };

  /// Per directive type static information.
  /// This is the same for all @c Config instances and is initialized at process static initialization.
  /// @internal Equivalent to class static information.
  struct StaticInfo {
    unsigned _idx = 0; ///< Indentifier.
    size_t _cfg_storage_required = 0;
    size_t _ctx_storage_required = 0;

    /// Number of directive types, used to generate identifiers.
    static unsigned _counter;
  };

  CfgInfo const* _rtti; ///< Run time (per Config) information.
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
  static swoc::Rv<Handle> load(Config& config, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Hook _hook { Hook::INVALID };
  Handle _directive; /// Directive to invoke in the specified hook.

  When(Hook hook_idx, Directive::Handle && directive);
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

inline Hook When::get_hook() const { return _hook; }




