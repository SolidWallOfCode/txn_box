/** @file
 * Base directive implementation.
 *
 * Copyright 2019 Oath, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <swoc/Errata.h>

#include "txn_box/Directive.h"
#include "txn_box/Config.h"

using swoc::Errata;
using swoc::Rv;
using swoc::TextView;

/* ------------------------------------------------------------------------------------ */
const std::string Directive::DO_KEY { "do" };
unsigned Directive::StaticInfo::_counter = 0;
/* ------------------------------------------------------------------------------------ */
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
/* ------------------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------------------ */
