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

public:
  /// Standard name for nested directives.
  /// This key is never matched as a directive name.
  /// It is defined here, even though not all directives use it, in order to be consistent across
  /// those that do.
  static const std::string DO_KEY;

  using Handle = std::unique_ptr<self_type>;

  /** Functor to create an instance of a @c Directive from configuration.
   *
   * @param cfg Configuration object.
   * @param drtv_node Directive node.
   * @param key_node Child of @a drtv_node that contains the key used to match the functor.
   * @return A new instance of the appropriate directive, or errors on failure.
   */
  using Assembler = std::function<swoc::Rv<Directive::Handle> (Config& cfg, YAML::Node drtv_node, YAML::Node key_node)>;

  /// A factory that maps from directive names to generator functions (@c Assembler instances).
  using Factory = std::unordered_map<std::string_view, Assembler>;

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  virtual swoc::Errata invoke(Context &ctx) = 0;

  /** Find the assembler for the directive @a name.
   *
   * @param cfg Configuration object.
   * @param node The directive node, which must be an object / map.
   * @return A new directive instance on successful load, errata otherwise.
   */
  static swoc::Rv<Handle> load(Config& cfg, YAML::Node drtv_node);
protected:

  /// The directive assemblers.
  static Factory _factory;
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
};

/// @c when directive - control which hook on which the configuration is handled.
// @c when is special and needs to be globally visible.
class When : public Directive {
  using super_type = Directive;
  using self_type = When;
public:
  static const std::string KEY;

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

