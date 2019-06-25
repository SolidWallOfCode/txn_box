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

#include <string>
#include <map>
#include <numeric>
#include <getopt.h>

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <yaml-cpp/yaml.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/FeatureMod.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */

const std::string Config::ROOT_KEY { "txn_box" };

Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev) {
  static const std::map<TSEvent, Hook> table{
    {TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ}
  , {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ}
  , {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP}
  , {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP}
  , {TS_EVENT_HTTP_PRE_REMAP, Hook::PRE_REMAP}
  , {TS_EVENT_HTTP_POST_REMAP, Hook::POST_REMAP}
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

namespace {
 Config Plugin_Config;
}
/* ------------------------------------------------------------------------------------ */

TextView ts::URL::host() {
  char const* text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

TextView ts::URL::view() {
  // Gonna live dangerously - since a reader is only allocated when a new IOBuffer is created
  // it doesn't need to be tracked - it will get cleaned up when the IOBuffer is destroyed.
  if (! _iobuff) {
    _iobuff.reset(TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_32K));
    auto reader = TSIOBufferReaderAlloc(_iobuff.get());
    TSUrlPrint(_buff, _loc, _iobuff.get());
    int64_t avail = 0;
    auto block = TSIOBufferReaderStart(reader);
    auto ptr = TSIOBufferBlockReadStart(block, reader, &avail);
    _view.assign(ptr, avail);
  }
  return _view;
}

ts::HttpField::~HttpField() {
  TSHandleMLocRelease(_buff, _hdr, _loc);
}

TextView ts::HttpField::value() {
  int size;
  char const* text;
  if (this->is_valid() && nullptr != (text = TSMimeHdrFieldValueStringGet(_buff, _hdr, _loc, -1, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

bool ts::HttpField::assign(swoc::TextView value) {
  return this->is_valid() &&
    TS_SUCCESS == TSMimeHdrFieldValueStringSet(_buff, _hdr, _loc, -1, value.data(), value.size());
}

bool ts::HttpField::assign_if_not_set(swoc::TextView value) {
  return this->is_valid() && ( ! this->value().empty() || this->assign(value) );
}

bool ts::HttpField::destroy() {
  return TS_SUCCESS == TSMimeHdrFieldDestroy(_buff, _hdr, _loc);
}

ts::URL ts::HttpHeader::url() {
  TSMLoc url_loc;
  if (this->is_valid() && TS_SUCCESS == TSHttpHdrUrlGet(_buff, _loc, &url_loc)) {
    return {_buff, url_loc};
  }
  return {};
}

ts::HttpField ts::HttpHeader::field(TextView name) {
  TSMLoc field_loc;
  if (this->is_valid() && nullptr != (field_loc = TSMimeHdrFieldFind(_buff, _loc, name.data(), name.size()))) {
    return { _buff, _loc, field_loc};
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::creq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::preq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::ursp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerRespGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpHeader ts::HttpTxn::prsp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientRespGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpField ts::HttpHeader::field_create(TextView name) {
  if (this->is_valid()) {
    TSMLoc field_loc;
    if (TS_SUCCESS ==
        TSMimeHdrFieldCreateNamed(_buff, _loc, name.data(), name.size(), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldAppend(_buff, _loc, field_loc)) {
        return HttpField{_buff, _loc, field_loc};
      }
      TSMimeHdrFieldDestroy(_buff, _loc, field_loc);
    }
  }
  return {};
}

ts::HttpField ts::HttpHeader::field_obtain(TextView name) {
  if (this->is_valid()) {
    if (HttpField field { this->field(name) } ; field.is_valid()) {
      return field;
    }
    return this->field_create(name);
  }
  return {};
}

ts::HttpHeader& ts::HttpHeader::field_remove(swoc::TextView name) {
  if (this->is_valid()) {
    if (HttpField field { this->field(name) } ; field.is_valid()) {
      field.destroy();
    }
  }
  return *this;
}

bool ts::HttpHeader::status_set(TSHttpStatus status) {
  return TS_SUCCESS == TSHttpHdrStatusSet(_buff, _loc, status);
}

bool ts::HttpHeader::reason_set(swoc::TextView reason) {
  return this->is_valid() && TS_SUCCESS == TSHttpHdrReasonSet(_buff, _loc, reason.data(), reason.size());
}

bool ts::HttpTxn::is_internal() const {
  return static_cast<bool>(TSHttpTxnIsInternal(_txn));
}

void ts::HttpTxn::error_body_set(swoc::TextView body, swoc::TextView content_type) {
  auto body_double { ts_dup(body) };
  TSHttpTxnErrorBodySet(_txn, body_double.data(), body_double.size(), ts_dup(content_type).data());
}

swoc::MemSpan<char> ts::HttpTxn::ts_dup(swoc::TextView const &text) {
  auto dup = static_cast<char*>(TSmalloc(text.size() + 1));
  memcpy(dup, text.data(), text.size());
  dup[text.size()] = '\0';
  return {dup, text.size()};
}

void ts::HttpTxn::status_set(int status) {
  TSHttpTxnStatusSet(_txn, static_cast<TSHttpStatus>(status));
}

ts::String ts::HttpTxn::effective_url_get() const {
  int size;
  auto s = TSHttpTxnEffectiveUrlStringGet(_txn, &size);
  return {s, size};
}

namespace swoc {
BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, TSHttpStatus status) {
  return bwformat(w, spec, static_cast<unsigned>(status));
}
} // namespace swoc

/* ------------------------------------------------------------------------------------ */

int CB_Directive(TSCont cont, TSEvent ev, void * payload) {
  Context* ctx = static_cast<Context*>(TSContDataGet(cont));
  /// TXN Close is special
  if (ev == TS_EVENT_HTTP_TXN_CLOSE) {
      TSContDataSet(cont, nullptr);
      TSContDestroy(cont);
      delete ctx;
  } else {
    Hook hook { Convert_TS_Event_To_TxB_Hook(ev) };
    if (Hook::INVALID != hook) {
      ctx->_cur_hook = hook;
      // Run the top level directives first.
      for (auto const &handle : Plugin_Config.hook_directives(hook)) {
        handle->invoke(*ctx); // need to log errors here.
      }
      // Run any accumulated directives for the hook.
      ctx->invoke_for_hook(hook);
    }
  }
  TSHttpTxnReenable(ctx->_txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// Global callback, not thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int CB_Txn_Start(TSCont cont, TSEvent ev, void * payload) {
  auto txn {reinterpret_cast<TSHttpTxn>(payload) };
  Context* ctx = new Context(Plugin_Config);
  TSCont txn_cont { TSContCreate(CB_Directive, TSMutexCreate()) };
  TSContDataSet(txn_cont, ctx);
  ctx->_cont = txn_cont;
  ctx->_txn = txn;

  // set hooks for top level directives.
  for ( unsigned idx = static_cast<unsigned>(Hook::BEGIN) ; idx < static_cast<unsigned>(Hook::END) ; ++idx ) {
    auto const& drtv_list { Plugin_Config.hook_directives(static_cast<Hook>(idx)) };
    if (! drtv_list.empty()) {
      TSHttpTxnHookAdd(txn, TS_Hook[idx], txn_cont);
      ctx->_directives[idx]._hook_set = true;
    }
  }
  // Always set a cleanup hook.
  TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, cont);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
};

namespace {
std::array<option, 2> Options = {
    {{"config", 1, nullptr, 'c'}, {nullptr, 0, nullptr, 0}}};
}

void
TSPluginInit(int argc, char const *argv[]) {
  Errata errata;

  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  int opt;
  int idx;
  optind = 0; // Reset options in case of other plugins.
  while (-1 != (opt = getopt_long(argc, const_cast<char **>(argv), ":", Options.data(), &idx))) {
    switch (opt) {
      case ':':errata.error("'{}' requires a value", argv[optind - 1]);
        break;
      case 'c':errata.note(Plugin_Config.load_file(swoc::file::path{argv[optind-1]}));
        break;
      default:errata.warn("Unknown option '{}' - ignored", char(opt), argv[optind - 1]);
        break;
    }
  }
  if (errata.is_ok()) {
    if (TSPluginRegister(&info) == TS_SUCCESS) {
      if (Plugin_Config.has_top_level_directive()) {
        TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
        TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
      }
    } else {
      TSError("%s: plugin registration failed.", Config::PLUGIN_TAG.data());
    }
  } else {
    std::string err_str;
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
}
