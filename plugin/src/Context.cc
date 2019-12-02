/** @file
   Per transaction context implementation.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
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

Context::Context(std::shared_ptr<Config> const& cfg) : _cfg(cfg) {
  // This is arranged so @a _arena destructor will clean up properly, nothing more need be done.
  _arena.reset(swoc::MemArena::construct_self_contained(4000 + (cfg ? cfg->_ctx_storage_required : 0)));

  if (cfg) {
    // Provide local storage for regex capture groups.
    _rxp_ctx = pcre2_general_context_create([](PCRE2_SIZE size
                                               , void *ctx) -> void * { return static_cast<self_type *>(ctx)->_arena->alloc(size).data(); }
                                            , [](void *, void *) -> void {}, this);
    _rxp_capture = pcre2_match_data_create(cfg->_capture_groups, _rxp_ctx);
    _rxp_working = pcre2_match_data_create(cfg->_capture_groups, _rxp_ctx);

    // Directive shared storage
    _ctx_store = _arena->alloc(cfg->_ctx_storage_required);
  }
}

Errata Context::on_hook_do(Hook hook_idx, Directive *drtv) {
  auto & info { _hooks[IndexFor(hook_idx)] };
  if (! info.hook_set_p) { // no hook to invoke this directive, set one up.
    if (hook_idx >= _cur_hook) {
      TSHttpTxnHookAdd(_txn, TS_Hook[IndexFor(hook_idx)], _cont);
      info.hook_set_p = true;
    } else if (hook_idx < _cur_hook) {
      // error condition - should report. Also, should detect this on config load.
    }
  }
  info.cb_list.append(_arena->make<Callback>(drtv));
  return {};
}

Errata Context::invoke_callbacks() {
  // Bit of subtlety here - directives / callbacks can be added to the list due to the action
  // of the invoked directive. However, because this is an intrusive list and items are only
  // added to the end, the @c next pointer for the current item will be updated before the
  // loop iteration occurs.
  auto & info { _hooks[IndexFor(_cur_hook)] };
  for ( auto & cb : info.cb_list ) {
    cb.invoke(*this);
  }
  return {};
}

Errata Context::invoke_for_hook(Hook hook) {
  _cur_hook = hook;
  this->clear_cache();

  // Run the top level directives in the config first.
  if (_cfg) {
    for (auto const &handle : _cfg->hook_directives(hook)) {
      handle->invoke(*this); // need to log errors here.
    }
  }
  this->invoke_callbacks();

  _cur_hook = Hook::INVALID;

  return {};
}

Errata Context::invoke_for_remap(Config &rule_cfg, TSRemapRequestInfo *rri) {
  _cur_hook = Hook::REMAP;
  _remap_info = rri;
  this->clear_cache();

  // Ugly, but need to make sure the regular expression storage is sufficient.
  if (!_cfg || rule_cfg._capture_groups > _cfg->_capture_groups) {
    _rxp_ctx = pcre2_general_context_create([](PCRE2_SIZE size
                                               , void *ctx) -> void * { return static_cast<self_type *>(ctx)->_arena->alloc(size).data(); }
                                            , [](void *, void *) -> void {}, this);
    _rxp_capture = pcre2_match_data_create(rule_cfg._capture_groups, _rxp_ctx);
    _rxp_working = pcre2_match_data_create(rule_cfg._capture_groups, _rxp_ctx);
  }
  // What about directive storage?

  // Remap rule directives.
  for (auto const &handle : rule_cfg.hook_directives(_cur_hook)) {
    handle->invoke(*this); // need to log errors here.
  }
  // Now the global config directives for REMAP
  if (_cfg) {
    for (auto const &handle : _cfg->hook_directives(_cur_hook)) {
      handle->invoke(*this); // need to log errors here.
    }
  }
  this->invoke_callbacks(); // Any accumulated callbacks.

  _cur_hook = Hook::INVALID;
  _remap_info = nullptr;

  return {};
}

void Context::operator()(swoc::BufferWriter& w, Extractor::Spec const& spec) {
  spec._exf->format(w, spec, *this);
}

Feature Context::extract(Extractor::Format const &fmt) {
  if (fmt._direct_p) {
    return dynamic_cast<DirectFeature *>(fmt[0]._exf)->direct_view(*this, fmt[0]);
  } else if (fmt._literal_p) {
    return fmt._literal;
  } else {
    switch (fmt._result_type) {
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
      case INTEGER:
      case BOOLEAN:
      case VARIABLE:
        return fmt[0]._exf->extract(*this, fmt[0]);
    }
  }
  return {};
}

Context& Context::commit(Feature &feature) {
  if (auto fv = std::get_if<STRING>(&feature) ; fv != nullptr) {
    if (fv->_literal_p) {
      // nothing
    } else if (fv->_direct_p) {
      auto span { _arena->alloc(fv->size())};
      memcpy(span, *fv);
      fv->_direct_p = false;
      fv->_literal_p = true;
      *fv = span.view(); // update view to be the localized copy.
    } else if (fv == _arena->remnant().data()) { // it's in transient memory, finalize it.
      _arena->alloc(fv->size());
      fv->_literal_p = true;
    }
  }
  return *this;
}

swoc::MemSpan<void> Context::storage_for(Directive *drtv) {
  auto zret { _ctx_store };
  zret.remove_prefix(drtv->_rtti->_ctx_storage_offset);
  zret.remove_suffix(zret.size() - drtv->_rtti->_ctx_storage_offset);
  return zret;
}

ts::HttpRequest Context::creq_hdr() {
  if (!_creq.is_valid()) {
    _creq = _txn.creq_hdr();
  }
  return _creq;
}

ts::HttpRequest Context::preq_hdr() {
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

Context::self_type &Context::enable_hooks(TSHttpTxn txn) {
  // Create a continuation to hold the data.
  _cont = TSContCreate(ts_callback, TSContMutexGet(reinterpret_cast<TSCont>(txn)));
  TSContDataSet(_cont, this);
  _txn = txn;

  // set hooks for top level directives.
  if (_cfg) {
    for (unsigned idx = 0; idx < std::tuple_size<Hook>::value; ++idx) {
      auto const &drtv_list{_cfg->hook_directives(static_cast<Hook>(idx))};
      if (!drtv_list.empty()) {
        TSHttpTxnHookAdd(txn, TS_Hook[idx], _cont);
        _hooks[idx].hook_set_p = true;
      }
    }
  }

  // Always set a cleanup hook.
  TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, _cont);
  TSHttpTxnArgSet(_txn, G.TxnArgIdx, this);
  return *this;
}

int Context::ts_callback(TSCont cont, TSEvent evt, void *payload) {
  self_type * self = static_cast<self_type*>(TSContDataGet(cont));
  auto txn = self->_txn; // cache in case it's a close.

  // Run the directives.
  Hook hook { Convert_TS_Event_To_TxB_Hook(evt) };
  if (Hook::INVALID != hook) {
    self->invoke_for_hook(hook);
  }

  /// TXN Close is special
  if (TS_EVENT_HTTP_TXN_CLOSE == evt) {
    TSContDataSet(cont, nullptr);
    TSContDestroy(cont);
    delete self;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
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
