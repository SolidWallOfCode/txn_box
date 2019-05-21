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

#include "yaml-cpp/yaml.h"
#include "swoc/Errata.h"

#include "Context.h"

/** Base class for directives.
 *
 */
class Directive {
  using self_type = Directive; ///< Self reference type.

public:

  /// Function that creates a @c Directive.
  /// This is passed the @c YAML::Node in the configuration that specifies the directive.
  using Assembler = std::function<Directive* (YAML::Node)>;
  /// A factory that maps from directive names to generator functions (@c Assembler instances).
  using Factory = std::unordered_map<std::string_view, Assembler>;

  /** Invoke the directive.
   *
   * @param ctx The transaction context.
   * @return Errors, if any.
   *
0   * All information needed for the invocation of the directive is accessible from the @a ctx.
   */
  swoc::Errata invoke(Context &ctx);
};

/** An ordered list of directives.
 *
 * This has no action of its own, it contains a list of other directives which are performed.
 */
class DirectiveList : public Directive {
  using self_type = DirectiveList; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
};
