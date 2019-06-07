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
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
/* ------------------------------------------------------------------------------------ */

const std::string Config::ROOT_KEY { "txn_box" };

swoc::Lexicon<FeatureType> FeatureTypeName {{ {FeatureType::VIEW, "string"}
                                            , {FeatureType::INTEGER, "integer"}
                                            , {FeatureType::BOOL, "boolean"}
                                            , {FeatureType::IP_ADDR, "IP address"}
                                           }};

BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, FeatureType type) {
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<unsigned>(type));
  }
  return bwformat(w, spec, FeatureTypeName[type]);
}

swoc::Lexicon<Hook> HookName {{ {Hook::CREQ, {"read-request", "creq"}}
                              , {Hook::PREQ, {"send-request", "preq"}}
                              , {Hook::URSP, {"read-response", "ursp"}}
                              , {Hook::PRSP, {"send-response", "prsp"}}
                              }};

std::array<TSHttpHookID, std::tuple_size<Hook>::value> TS_Hook;

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  HookName.set_default(Hook::INVALID);

  TS_Hook[static_cast<unsigned>(Hook::CREQ)] = TS_HTTP_READ_REQUEST_HDR_HOOK;
  TS_Hook[static_cast<unsigned>(Hook::PREQ)] = TS_HTTP_SEND_REQUEST_HDR_HOOK;
  TS_Hook[static_cast<unsigned>(Hook::URSP)] = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  TS_Hook[static_cast<unsigned>(Hook::PRSP)] = TS_HTTP_SEND_RESPONSE_HDR_HOOK;

  return true;
} ();
}; // namespace

Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev) {
  static const std::map<TSEvent, Hook> table{{TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ},
                                                  {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ},
                                                  {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP},
                                                  {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP},
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

Config Plugin_Config;

template < typename F > struct scope_exit {
  F _f;
  explicit scope_exit(F &&f) : _f(std::move(f)) {}
  ~scope_exit() { _f(); }
};
/* ------------------------------------------------------------------------------------ */
TextView ts::URL::host() {
  char const* text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

TextView ts::HttpField::value() {
  int size;
  char const* text;
  if (this->is_valid() && nullptr != (text = TSMimeHdrFieldValueStringGet(_buff, _loc, _hdr, -1, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

bool ts::HttpField::assign(swoc::TextView value) {
  return this->is_valid() &&
    TS_SUCCESS == TSMimeHdrFieldValueStringSet(_buff, _hdr, _loc, -1, value.data(), value.size());
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
/* ------------------------------------------------------------------------------------ */
Rv<Extractor::Format> Config::parse_feature(swoc::TextView fmt_string) {
  auto && [ fmt, errata ] { Extractor::parse(fmt_string) };
  if (errata.is_ok()) {

    if (fmt._ctx_ref_p && _feature_ref_p) {
      *_feature_ref_p = true;
    }

    // Copy literal data from the YAML data into Config arena memory to stabilize it. Additionally if it's
    // multiple literals (which can happen) then consolidate it into a single literal string.
    if (fmt._literal_p) {
      size_t n = std::accumulate(fmt._specs.begin(), fmt._specs.end(), size_t{0}, [] (size_t sum, Extractor::Spec const& spec) -> size_t { return sum += spec._ext.size(); });
      auto span { _arena.alloc(n).rebind<char>() };
      Extractor::Spec literal_spec;
      literal_spec._type = swoc::bwf::Spec::LITERAL_TYPE;
      literal_spec._ext = { span.data(), span.size() };
      for ( auto const& spec : fmt._specs ) {
        memcpy(span.data(), spec._ext.data(), spec._ext.size());
        span.remove_prefix(spec._ext.size());
      }
      fmt._specs.resize(1);
      fmt._specs[0] = literal_spec;
    }
  }
  return { std::move(fmt), std::move(errata) };
}

// Basically a wrapper for @c load_directive to handle stacking feature provisioning. During
// load, all paths must be explored and so the active feature needs to be stacked up. During
// runtime, only one path is followed and therefore this isn't required.
Rv<Directive::Handle>
Config::load_directive(YAML::Node drtv_node, Extractor::Type feature_type, bool &referenced_p) {
  bool * saved_flag = _feature_ref_p;
  Extractor::Type saved_type = _feature_type;
  _feature_ref_p = &referenced_p;
  _feature_type = feature_type;
  // put the old values back on return, this is cleaner than needing a temporary and @c std::move
  // to perform the restore.
  scope_exit cleanup([&] () {
    _feature_ref_p = saved_flag;
    _feature_type = saved_type;
  });

  return this->load_directive(drtv_node);
}

Rv<Directive::Handle> Config::load_directive(YAML::Node drtv_node) {
  if (drtv_node.IsMap()) {
    return { Directive::load(*this, drtv_node) };
  } else if (drtv_node.IsSequence()) {
    Errata zret;
    Directive::Handle drtv_list{new DirectiveList};
    for (auto child : drtv_node) {
      auto && [handle, errata] {this->load_directive(child)};
      if (errata.is_ok()) {
        static_cast<DirectiveList *>(drtv_list.get())->push_back(std::move(handle));
      } else {
        return { {}, std::move(errata.error(R"(Failed to load directives at {})", drtv_node.Mark())) };
      }
    }
    return {std::move(drtv_list), {}};
  } else if (drtv_node.IsNull()) {
    return {Directive::Handle(new NilDirective)};
  }
  return { {}, Errata().error(R"(Directive at {} is not an object or a sequence as required.)",
      drtv_node.Mark()) };
}

Errata Config::load_top_level_directive(YAML::Node drtv_node) {
  Errata zret;
  if (drtv_node.IsMap()) {
    YAML::Node key_node { drtv_node[When::KEY] };
    if (key_node) {
      try {
        auto hook_idx{HookName[key_node.Scalar()]};
        auto && [ handle, errata ]{ When::load(*this, drtv_node, key_node) };
        if (errata.is_ok()) {
          _roots[static_cast<unsigned>(hook_idx)].emplace_back(std::move(handle));
          _active_p = true;
        } else {
          zret.note(errata);
        }
      } catch (std::exception& ex) {
        zret.error(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_node.Scalar(),
            When::KEY, key_node.Mark());
      }
    } else {
      zret.error(R"(Top level directive at {} is not a "when" directive as required.)", drtv_node.Mark());
    }
  } else {
    zret.error(R"(Top level directive at {} is not an object as required.)", drtv_node.Mark());
  }
  return std::move(zret);
}

Errata Config::load_file(swoc::file::path const& file_path) {
  Errata zret;
  std::error_code ec;
  std::string content = swoc::file::load(file_path, ec);

  if (ec) {
    return zret.error(R"(Unable to load file "{}" - {}.)", file_path, ec);
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
    return zret.error(R"(YAML parsing of "{}" failed - {}.)", file_path, ex.what());
  }

  YAML::Node base_node { root[ROOT_KEY] };
  if (! base_node) {
    return zret.error(R"(Base key "{}" for plugin "{}" not found in "{}".)", ROOT_KEY,
        PLUGIN_NAME, file_path);
  }

  if (base_node.IsSequence()) {
    for ( auto child : base_node ) {
      zret.note(this->load_top_level_directive(child));
    }
    if (! zret.is_ok()) {
      zret.error(R"(Failure while loading list of top level directives for "{}" at {}.)",
      ROOT_KEY, base_node.Mark());
    }
  } else if (base_node.IsMap()) {
    zret = this->load_top_level_directive(base_node);
  } else {
  }
  return std::move(zret);
};

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

std::array<option, 2> Options = {
    {{"config", 1, nullptr, 'c'}, {nullptr, 0, nullptr, 0}}};

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
      if (Plugin_Config.is_active()) {
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

