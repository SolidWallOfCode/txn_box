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

using swoc::Errata;

Directive::Factory Directive::_factory;

Errata Directive::define(swoc::TextView name, Directive::Assembler const &assm) {
  _factory[name] = assm;
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
