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
using namespace swoc::literals;

Context::Context(Config & cfg) {
  // This is arranged so @a _arena destructor will clean up properly, nothing more need be done.
  _arena.reset(swoc::MemArena::construct_self_contained(4000 + cfg._ctx_storage_required));

  // Provide array storage for each potential conditional directive for each hook.
  for (unsigned idx = static_cast<unsigned>(Hook::BEGIN) ; idx < static_cast<unsigned>(Hook::END) ; ++idx) {
     MemSpan<Directive*> drtv_list = _arena->alloc(sizeof(Directive*) * cfg._directive_count[idx]).rebind<Directive*>();
     _directives[idx] = { 0, 0, drtv_list.data() };
  };

  // Provide local storage for regex capture groups.
  _rxp_ctx = pcre2_general_context_create([] (PCRE2_SIZE size, void* ctx) -> void* { return static_cast<self_type *>(ctx)->_arena->alloc(size).data(); }
  , [] (void*, void*) -> void {}, this);
  _rxp_capture = pcre2_match_data_create(cfg._capture_groups, _rxp_ctx);
  _rxp_working = pcre2_match_data_create(cfg._capture_groups, _rxp_ctx);

  // Directive shared storage
  _ctx_store = _arena->alloc(cfg._ctx_storage_required);
}

Errata Context::on_hook_do(Hook hook_idx, Directive *drtv) {
  auto & hd { _directives[static_cast<unsigned>(hook_idx)] };
  if (! hd._hook_set) { // no hook to invoke this directive, set one up.
    if (hook_idx > _cur_hook) {
      TSHttpTxnHookAdd(_txn, TS_Hook[static_cast<unsigned>(hook_idx)], _cont);
      hd._hook_set = true;
    } else if (hook_idx < _cur_hook) {
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
    return dynamic_cast<DirectFeature *>(fmt[0]._extractor)->direct_view(*this, fmt[0]);
  } else if (fmt._literal_p) {
    return FeatureView::Literal(fmt[0]._ext);
  } else {
    switch (fmt._feature_type) {
      case STRING: {
        FixedBufferWriter w{_arena->remnant()};
        // double write - try in the remnant first. If that suffices, done.
        // Otherwise the size is now known and the needed space can be correctly allocated.
        w.print_nfv(*this, Extractor::FmtEx{fmt._specs}, ArgPack(*this));
        if (fmt._force_c_string_p) {
          w.write('\0');
        }
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
      case INTEGER: return dynamic_cast<IntegerFeature*>(fmt[0]._extractor)->extract(*this);
      case BOOLEAN:
        return dynamic_cast<BooleanFeature*>(fmt[0]._extractor)->extract(*this);
    }
  }
  return {};
}

Context& Context::commit(FeatureData const &feature) {
  if (auto fv = std::get_if<STRING>(&feature) ; fv && !(fv->_direct_p || fv->_literal_p)) {
    _arena->alloc(fv->size());
  }
  return *this;
}

swoc::MemSpan<void> Context::storage_for(Directive *drtv) {
  auto zret { _ctx_store };
  zret.remove_prefix(drtv->_rtti->_ctx_storage_offset);
  zret.remove_suffix(zret.size() - drtv->_rtti->_ctx_storage_offset);
  return zret;
}

ts::HttpHeader Context::creq_hdr() {
  if (!_creq.is_valid()) {
    _creq = _txn.creq_hdr();
  }
  return _creq;
}

ts::HttpHeader Context::preq_hdr() {
  if (!_preq.is_valid()) {
    _preq = _txn.preq_hdr();
  }
  return _preq;
}

ts::HttpHeader Context::ursp_hdr() {
  if (!_ursp.is_valid()) {
    _ursp = _txn.ursp_hdr();
  }
  return _ursp;
}

ts::HttpHeader Context::prsp_hdr() {
  if (!_prsp.is_valid()) {
    _prsp = _txn.prsp_hdr();
  }
  return _prsp;
}

unsigned Context::ArgPack::count() const {
  return _ctx._rxp_capture ? pcre2_get_ovector_count(_ctx._rxp_capture) : 0;
}

BufferWriter& Context::ArgPack::print(unsigned idx, BufferWriter &w
                                      , swoc::bwf::Spec const &spec) const {
  auto ovector = pcre2_get_ovector_pointer(_ctx._rxp_capture);
  idx *= 2; // To account for offset pairs.
  return bwformat(w, spec, _ctx._rxp_src.substr(ovector[idx], ovector[idx+1] - ovector[idx]));
}

std::any Context::ArgPack::capture(unsigned idx) const { return "Bogus"_sv; }
