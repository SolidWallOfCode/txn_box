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

#include <swoc/Errata.h>

#include "txn_box/Directive.h"
#include "txn_box/Config.h"

using swoc::Errata;
using swoc::Rv;
using swoc::TextView;

Directive::Factory Directive::_factory;
const std::string Directive::DO_KEY { "do" };

/* ------------------------------------------------------------------------------------ */
Rv<Directive::Handle> Directive::load(Config & cfg, YAML::Node drtv_node) {
  YAML::Node key_node;
  for ( auto const&  [ key_name, key_value ] : drtv_node ) {
    TextView key { key_name.Scalar() };
    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (key == DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      auto const& [ hooks, worker ] { spot->second };
      if (! hooks[IndexFor(cfg.current_hook())]) {
        return { {}, Errata().error(R"(Directive "{}" at {} is not allowed on hook "{}".)", key, drtv_node.Mark(), cfg.current_hook()) };
      }
      return worker(cfg, drtv_node, key_value);
    }
  }
  return { {}, Errata().error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark()) };
}


Errata Directive::define(swoc::TextView name, HookMask const& hooks, Directive::Worker const &worker) {
  _factory[name] = std::make_tuple(hooks, worker);
  return {};
}

DirectiveList& DirectiveList::push_back(Directive::Handle &&d) {
  _directives.emplace_back(std::move(d));
  return *this;
}

Errata DirectiveList::invoke(Context &ctx) {
  Errata zret;
  for ( auto const& drtv : _directives ) {
    zret.note(drtv->invoke(ctx));
  }
  return std::move(zret);
}

// Do nothing.
swoc::Errata NilDirective::invoke(Context &ctx) { return {}; }
