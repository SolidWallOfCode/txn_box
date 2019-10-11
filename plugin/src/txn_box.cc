/** @file
   Base plugin logic.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <getopt.h>

#include <openssl/ssl.h>

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

Global G;

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

swoc::Lexicon<TSRecordDataType> TSRecordDataTypeNames {
    {TS_RECORDDATATYPE_NULL, "null"}
  , {TS_RECORDDATATYPE_INT, "integer"}
  , {TS_RECORDDATATYPE_FLOAT, "float"}
  , {TS_RECORDDATATYPE_STRING, "string" }
  , {TS_RECORDDATATYPE_COUNTER, "counter" }
  , {TS_RECORDDATATYPE_STAT_CONST, "stat" }
  , {TS_RECORDDATATYPE_STAT_FX, "stat function" }
};

namespace {
 std::shared_ptr<Config> Plugin_Config;
}
/* ------------------------------------------------------------------------------------ */
YAML::Node yaml_merge(YAML::Node & root) {
  static constexpr auto flatten = [] (YAML::Node & dst, YAML::Node & src) -> void {
    if (src.IsMap()) {
      for ( auto const& [ key, value ] : src ) {
        // don't need to check for nested merge key, because this function is called only if
        // that's already set in @a dst therefore it won't be copied up from @a src.
        if (!dst[key]) {
          dst[key] = value;
        }
      }
    }
  };

  if (root.IsSequence()) {
    for ( auto && child : root ) {
      yaml_merge(child);
    }
  } else if (root.IsMap()) {
    // Do all nested merges first, so the result is iteration order independent.
    for ( auto && [ key, value ] : root ) {
      value = yaml_merge(value);
    }
    // If there's a merge key, merge it in.
    if ( auto merge_node { root[YAML_MERGE_KEY] } ; merge_node ) {
      if (merge_node.IsMap()) {
        flatten(root, merge_node);
      } else if (merge_node.IsSequence()) {
        for (auto &&src : merge_node) {
          flatten(root, src);
        }
      }
      root.remove(YAML_MERGE_KEY);
    }
  }
  return root;
}
/* ------------------------------------------------------------------------------------ */
ts::HttpTxn::TxnConfigVarTable ts::HttpTxn::_var_table;

TextView ts::URL::host() const {
  char const* text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return { text, static_cast<size_t>(size) };
  }
  return {};
}

TextView ts::URL::view() const {
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

ts::URL ts::HttpRequest::url() {
  TSMLoc url_loc;
  if (this->is_valid() && TS_SUCCESS == TSHttpHdrUrlGet(_buff, _loc, &url_loc)) {
    return {_buff, url_loc};
  }
  return {};
}

bool ts::HttpRequest::host_set(swoc::TextView const &host) {
  auto url { this->url() };
  if (!url.host().empty()) {
    url.host_set(host);
    if (auto field { this->field(HTTP_FIELD_HOST) } ; field.is_valid()) {
      field.assign(host);
    }
  } else {
    this->field_obtain(HTTP_FIELD_HOST).assign(host);
  }
  return true;
};

ts::HttpRequest ts::HttpTxn::creq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpRequest ts::HttpTxn::preq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerReqGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpResponse ts::HttpTxn::ursp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerRespGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpResponse ts::HttpTxn::prsp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientRespGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpField ts::HttpHeader::field(TextView name) const {
  TSMLoc field_loc;
  if (this->is_valid() && nullptr != (field_loc = TSMimeHdrFieldFind(_buff, _loc, name.data(), name.size()))) {
    return { _buff, _loc, field_loc};
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

bool ts::HttpResponse::status_set(TSHttpStatus status) const {
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

// API changed.
namespace detail {
template < typename S > auto
ts_status_set(ts::HttpTxn &txn, S status, swoc::meta::CaseTag<0>) -> bool {
  return txn.prsp_hdr().status_set(status);
}

// New for ATS 9, prefer this if available.
template < typename S > auto ts_status_set(ts::HttpTxn &txn, S status, swoc::meta::CaseTag<1>) -> decltype(TSHttpTxnStatusSet(txn._txn, status), bool()) {
  return TSHttpTxnStatusSet(txn._txn, status) == TS_SUCCESS;
}
} // namespace detail

void ts::HttpTxn::status_set(int status) {
  detail::ts_status_set(*this, static_cast<TSHttpStatus>(status), swoc::meta::CaseArg);
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

BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, TSRecordDataType type) {
  return bwformat(w, spec, TSRecordDataTypeNames[type]);
}
} // namespace swoc


TextView ts::HttpSsn::inbound_sni() const {
  if (_ssn) {
    TSVConn ssl_vc = TSHttpSsnClientVConnGet(_ssn);
    TSSslConnection ts_ssl_ctx = TSVConnSSLConnectionGet(ssl_vc);
    if (ts_ssl_ctx) {
      SSL *ssl = reinterpret_cast<SSL *>(ts_ssl_ctx);
      const char *sni = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
      if (sni) {
        return {sni, strlen(sni)};
      }
    }
  }
  return {};
}

Errata ts::HttpTxn::cache_key_set(swoc::TextView const& key) {
  TSCacheUrlSet(_txn, key.data(), key.size());
  return {};
}

int Global::reserve_TxnArgIdx() {
  if (G.TxnArgIdx < 0) {
    TSHttpTxnArgIndexReserve(Config::ROOT_KEY.data(), "Transaction Box Plugin", &G.TxnArgIdx);
  }
  return G.TxnArgIdx;
}

Errata ts::TxnConfigVar::is_valid(int) const {
  return Error(R"(Config variable "{}" does not support integer values.)",
      _name);
}

Errata ts::TxnConfigVar::is_valid(TextView const&) const {
  return Error(R"(Config variable "{}" does not support string values.)",
      _name);
}

Errata ts::TxnConfigInteger::is_valid(int n) const {
  if (n < _min || n > _max) {
    return Error(R"(Value {} for overridable configuration "{}" is not in the range {}..{})", n, _name, _min, _max);
  }
  return {};
}

Errata ts::TxnConfigString::is_valid(TextView const& text) const { return {}; }

ts::TxnConfigVar * ts::HttpTxn::find_override(swoc::TextView name) {
  if ( auto spot { _var_table.find(name) } ; spot != _var_table.end()) {
    return spot->second.get();
  }
  return nullptr;
}

Errata ts::HttpTxn::set_override(TxnConfigVar const& var, int n) {
  if (auto errata { var.is_valid(n) } ; ! errata.is_ok()) {
    return std::move(errata);
  }
  TSHttpTxnConfigIntSet(_txn, var._key, n);
  return {};
}

Errata ts::HttpTxn::set_override(TxnConfigVar const& var, TextView const& text) {
  if (auto errata { var.is_valid(text) } ; ! errata.is_ok()) {
    return std::move(errata);
  }
  TSHttpTxnConfigStringSet(_txn, var._key, text.data(), text.size());
  return {};
}

void ts::HttpTxn::config_bool_record(Errata & errata, TextView name) {
  TSOverridableConfigKey key;
  TSRecordDataType type;
  if (TS_SUCCESS != TSHttpTxnConfigFind(name.data(), name.size(), &key, &type)) {
    errata.error(R"(Overridable configuration "{}" was not found.)", name);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else if (type != TS_RECORDDATATYPE_INT) {
    errata.error(R"(Overridable configuration "{}" is of type {} and not {} as required.)", name, type, TS_RECORDDATATYPE_INT);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else {
    _var_table.emplace(name, new TxnConfigInteger{name, key, 0, 1});
  }
};

void ts::HttpTxn::config_string_record(Errata & errata, TextView name) {
  TSOverridableConfigKey key;
  TSRecordDataType type;
  if (TS_SUCCESS != TSHttpTxnConfigFind(name.data(), name.size(), &key, &type)) {
    errata.error(R"(Overridable configuration "{}" was not found.)", name);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else if (type != TS_RECORDDATATYPE_STRING) {
    errata.error(R"(Overridable configuration "{}" is of type {} and not {} as required.)", name, type, TS_RECORDDATATYPE_INT);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else {
    _var_table.emplace(name, new TxnConfigString{name, key});
  }
};

void ts::HttpTxn::config_integer_record(Errata & errata, TextView name, int min, int max) {
  TSOverridableConfigKey key;
  TSRecordDataType type;
  if (TS_SUCCESS != TSHttpTxnConfigFind(name.data(), name.size(), &key, &type)) {
    errata.error(R"(Overridable configuration "{}" was not found.)", name);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else if (type != TS_RECORDDATATYPE_INT) {
    errata.error(R"(Overridable configuration "{}" is of type {} and not {} as required.)", name, type, TS_RECORDDATATYPE_INT);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else {
    _var_table.emplace(name, new TxnConfigInteger{name, key, min, max});
  }
};

void ts::HttpTxn::config_integer_record(Errata & errata, TextView name) {
  TSOverridableConfigKey key;
  TSRecordDataType type;
  if (TS_SUCCESS != TSHttpTxnConfigFind(name.data(), name.size(), &key, &type)) {
    errata.error(R"(Overridable configuration "{}" was not found.)", name);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else if (type != TS_RECORDDATATYPE_INT) {
    errata.error(R"(Overridable configuration "{}" is of type {} and not {} as required.)", name, type, TS_RECORDDATATYPE_INT);
    _var_table.emplace(name, new TxnConfigVar{name, key});
  } else {
    _var_table.emplace(name, new TxnConfigInteger{name, key});
  }
};

Errata & ts::HttpTxn::init(swoc::Errata &errata) {
  config_bool_record(errata, "proxy.config.url_remap.pristine_host_hdr");
  config_integer_record(errata, "proxy.config.http.cache.required_headers", 0, 2);
  config_bool_record(errata, "proxy.config.http.negative_caching_enabled");
  config_string_record(errata, "proxy.config.http.global_user_agent_header");
  return errata;
}
/* ------------------------------------------------------------------------------------ */

// Global callback, thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int CB_Txn_Start(TSCont cont, TSEvent ev, void * payload) {
  auto txn {reinterpret_cast<TSHttpTxn>(payload) };
  Context* ctx = new Context(Plugin_Config);
  ctx->enable_hooks(txn);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

Errata
TxnBoxInit(int argc, char const *argv[]) {
  static constexpr std::array<option, 3> Options = {
      {{"file", 1, nullptr, 'c'}
          , { "key", 1, nullptr, 'k' }
          , {nullptr, 0, nullptr, 0}}};

  Errata errata;

  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  Plugin_Config.reset(new Config);
  TextView cfg_path { "txn_box.yaml" };
  TextView cfg_key { Config::ROOT_KEY };
  int opt;
  int idx;
  optind = 0; // Reset options in case of other plugins.
  while (-1 != (opt = getopt_long(argc, const_cast<char **>(argv), ":", Options.data(), &idx))) {
    switch (opt) {
      case ':':errata.error("'{}' requires a value", argv[optind - 1]);
        break;
      case 'c': cfg_path.assign(argv[optind-1], strlen(argv[optind-1]));
        break;
      case 'k': cfg_key.assign(argv[optind-1], strlen(argv[optind-1]));
        break;
      default:errata.warn("Unknown option '{}' - ignored", char(opt), argv[optind - 1]);
        break;
    }
  }

  if (!errata.is_ok()) {
    return std::move(errata);
  }

  // Try loading and parsing the file.
  auto &&[root, yaml_errata ]{yaml_load(cfg_path)};
  if (!errata.is_ok()) {
    yaml_errata.info(R"(While loading file "{}".)", cfg_path);
    return std::move(yaml_errata);
  }

  // Process the YAML data.
  errata = Plugin_Config->parse_yaml(root, cfg_key);
  if (!errata.is_ok()) {
    errata.info(R"(While parsing key "{}" in configuration file "{}".)", cfg_key, cfg_path);
    return std::move(errata);
  }

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    if (Plugin_Config->has_top_level_directive()) {
      TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
      TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
      G.reserve_TxnArgIdx();
    }
  } else {
    errata.error(R"({}: plugin registration failed.)", Config::PLUGIN_TAG);
    return std::move(errata);
  }
  return {};
}

void
TSPluginInit(int argc, char const *argv[]) {
  auto errata { TxnBoxInit(argc, argv) };
  std::string err_str;
  if (! G._preload_errata.is_ok()) {
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
  }
  if (! errata.is_ok()) {
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
};

/* ------------------------------------------------------------------------ */namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  TSRecordDataTypeNames.set_default("null");
  TSRecordDataTypeNames.set_default(TS_RECORDDATATYPE_NULL);
  ts::HttpTxn::init(G._preload_errata);
  return true;
} ();
} // namespace
