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

using swoc::TextView;
using swoc::MemSpan;
using swoc::Errata;
using swoc::BufferWriter;
using swoc::FixedBufferWriter;

Context::Context(Config & cfg) {
  // This is arranged so @a _arena destructor will clean up properly, nothing more need be done.
  _arena.reset(swoc::MemArena::construct_self_contained(4000));

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

void Context::operator()(swoc::BufferWriter& w, Extractor::Spec const& spec) {
  spec._extractor->format(w, spec, *this);
}

FeatureData Context::extract(Extractor::Format const &fmt) {
  if (fmt._direct_p) {
    return dynamic_cast<DirectFeature *>(fmt[0]._extractor)->direct_view(*this);
  } else if (fmt._literal_p) {
    return FeatureView::Literal(fmt[0]._ext);
  } else {
    switch (fmt._feature_type) {
      case VIEW: {
        FixedBufferWriter w{_arena->remnant()};
        // double write - try in the remnant first. If that suffices, done.
        // Otherwise the size is now known and the needed space can be correctly allocated.
        w.print_nfv(*this, Extractor::FmtEx{fmt._specs}, ArgPack(*this));
        if (!w.error()) {
          return w.view();
        } else {
          FixedBufferWriter w2{_arena->require(w.extent()).remnant()};
          w2.print_nfv(*this, Extractor::FmtEx{fmt._specs}, ArgPack(*this));
          return w2.view();
        }
        break;
      }
      case IP_ADDR: break;
      case INTEGER: break;
      case BOOL: break;
    }
  }
  return {};
}

Context& Context::commit(FeatureData const &feature) {
  if (auto fv = std::get_if<VIEW>(&feature) ; fv && !(fv->_direct_p || fv->_literal_p)) {
    _arena->alloc(fv->size());
  }
  return *this;
}

ts::HttpHeader Context::creq_hdr() {
  if (!_creq.is_valid()) {
    _creq = _txn.creq_hdr();
  }
  return _creq;
}
