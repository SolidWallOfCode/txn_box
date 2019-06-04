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

#include <swoc/MemSpan.h>

#include "txn_box/Context.h"
#include "txn_box/Config.h"
#include "txn_box/ts_util.h"

using swoc::MemSpan;
using swoc::Errata;

Context::Context(Config & cfg) {
  swoc::MemArena arena { 4000 }; // close enough to a page to get bumped up.
  // This is arranged so @a _arena destructor will clean up properly, nothing more need be done.
  _arena.reset(arena.make<swoc::MemArena>(std::move(arena)));

  // Provide array storage for each potential conditional directive for each hook.
  for ( unsigned idx = static_cast<unsigned>(Hook::BEGIN) ; idx < static_cast<unsigned>(Hook::END) ; ++idx) {
     MemSpan<Directive*> drtv_list = _arena->alloc(sizeof(Directive*) * cfg._directive_count[idx]).rebind<Directive*>();
     _directives[idx] = { static_cast<unsigned>(drtv_list.count()), 0, drtv_list.data() };
  };
}

Errata Context::when_do(Hook hook, Directive* drtv) {
  auto & hd { _directives[static_cast<unsigned>(hook)] };
  if (! hd._hook_set) { // no hook to invoke this directive, set one up.
    if (hook > _cur_hook) {
      TSHttpTxnHookAdd(_txn, TS_Hook[static_cast<unsigned>(hook)], _cont);
      hd._hook_set = true;
    } else if (hook < _cur_hook) {
      // error condition - should report. Also, should detect this on config load.
    }
  }
  hd._drtv[hd._count++] = drtv;
  return {};
};

Errata Context::invoke_for_hook(Hook hook) {
  auto & hd { _directives[static_cast<unsigned>(hook)] };
  while (hd._idx < hd._count) {
    Directive* drtv = hd._drtv[hd._idx++];
    drtv->invoke(*this);
  }
  return {};
}

void Context::names(swoc::BufferWriter& w, Extractor::Spec const& spec) {
  spec._extractor->format(w, spec, *this);
}

Errata Context::extract(Extractor::Format const &fmt) {
  if (fmt._direct_p) {
  } else {
    swoc::FixedBufferWriter w{ _arena->remnant().rebind<char>() };
    w.print_nfv(this, Extractor::FmtEx{fmt._specs}, ArgPack(*this));
  };
}

ts::HttpHeader Context::creq_hdr() {
  if (!_creq.is_valid()) {
    _creq = _txn.creq_hdr();
  }
  return _creq;
}
