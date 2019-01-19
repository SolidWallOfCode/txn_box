/** @file
   Traffic Server plugin API utilities.

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
#include <swoc/swoc_meta.h>
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
namespace ts {
namespace swoc = ::swoc; // Import to avoid global naming weirdness.
/* ------------------------------------------------------------------------------------ */

const swoc::Lexicon<TSRecordDataType> TSRecordDataTypeNames{
    {{TS_RECORDDATATYPE_NULL, "null"}
        , {TS_RECORDDATATYPE_INT, "integer"}
        , {TS_RECORDDATATYPE_FLOAT, "float"}
        , {TS_RECORDDATATYPE_STRING, "string"}
        , {TS_RECORDDATATYPE_COUNTER, "counter"}
        , {TS_RECORDDATATYPE_STAT_CONST, "stat"}
        , {TS_RECORDDATATYPE_STAT_FX, "stat function"}
    }, TS_RECORDDATATYPE_NULL, "null"
};

HttpTxn::TxnConfigVarTable ts::HttpTxn::_var_table;
std::mutex HttpTxn::_var_table_lock;
int HttpTxn::_arg_idx = -1;

/* ------------------------------------------------------------------------------------ */

TextView ts::URL::host() const {
  char const *text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return {text, static_cast<size_t>(size)};
  }
  return {};
}

TextView ts::URL::view() const {
  // Gonna live dangerously - since a reader is only allocated when a new IOBuffer is created
  // it doesn't need to be tracked - it will get cleaned up when the IOBuffer is destroyed.
  if (!_iobuff) {
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
  char const *text;
  if (this->is_valid() &&
      nullptr != (text = TSMimeHdrFieldValueStringGet(_buff, _hdr, _loc, -1, &size))) {
    return {text, static_cast<size_t>(size)};
  }
  return {};
}

bool ts::HttpField::assign(swoc::TextView value) {
  return this->is_valid() &&
         TS_SUCCESS ==
         TSMimeHdrFieldValueStringSet(_buff, _hdr, _loc, -1, value.data(), value.size());
}

bool ts::HttpField::assign_if_not_set(swoc::TextView value) {
  return this->is_valid() && (!this->value().empty() || this->assign(value));
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
  auto url{this->url()};
  if (!url.host().empty()) {
    url.host_set(host);
    if (auto field{this->field(HTTP_FIELD_HOST)}; field.is_valid()) {
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
    return {buff, loc};
  }
  return {};
}

ts::HttpRequest ts::HttpTxn::preq_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerReqGet(_txn, &buff, &loc)) {
    return {buff, loc};
  }
  return {};
}

ts::HttpResponse ts::HttpTxn::ursp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnServerRespGet(_txn, &buff, &loc)) {
    return {buff, loc};
  }
  return {};
}

ts::HttpResponse ts::HttpTxn::prsp_hdr() {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnClientRespGet(_txn, &buff, &loc)) {
    return {buff, loc};
  }
  return {};
}

ts::HttpField ts::HttpHeader::field(TextView name) const {
  TSMLoc field_loc;
  if (this->is_valid() &&
      nullptr != (field_loc = TSMimeHdrFieldFind(_buff, _loc, name.data(), name.size()))) {
    return {_buff, _loc, field_loc};
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
    if (HttpField field{this->field(name)}; field.is_valid()) {
      return field;
    }
    return this->field_create(name);
  }
  return {};
}

ts::HttpHeader &ts::HttpHeader::field_remove(swoc::TextView name) {
  if (this->is_valid()) {
    if (HttpField field{this->field(name)}; field.is_valid()) {
      field.destroy();
    }
  }
  return *this;
}

bool ts::HttpResponse::status_set(TSHttpStatus status) const {
  return TS_SUCCESS == TSHttpHdrStatusSet(_buff, _loc, status);
}

bool ts::HttpHeader::reason_set(swoc::TextView reason) {
  return this->is_valid() &&
         TS_SUCCESS == TSHttpHdrReasonSet(_buff, _loc, reason.data(), reason.size());
}

bool ts::HttpTxn::is_internal() const {
  return static_cast<bool>(TSHttpTxnIsInternal(_txn));
}

void ts::HttpTxn::error_body_set(swoc::TextView body, swoc::TextView content_type) {
  auto body_double{ts_dup(body)};
  TSHttpTxnErrorBodySet(_txn, body_double.data(), body_double.size(), ts_dup(content_type).data());
}

swoc::MemSpan<char> ts::HttpTxn::ts_dup(swoc::TextView const &text) {
  auto dup = static_cast<char *>(TSmalloc(text.size() + 1));
  memcpy(dup, text.data(), text.size());
  dup[text.size()] = '\0';
  return {dup, text.size()};
}

// API changed.
namespace detail {
template<typename S> auto
ts_status_set(ts::HttpTxn &txn, S status, swoc::meta::CaseTag<0>) -> bool {
  return txn.prsp_hdr().status_set(status);
}

// New for ATS 9, prefer this if available.
template<typename S> auto ts_status_set(ts::HttpTxn &txn, S status
                                        , swoc::meta::CaseTag<1>) -> decltype(TSHttpTxnStatusSet(txn, status), bool()) {
  TSHttpTxnStatusSet(txn, status); // no error return, sigh.
  return true;
}

// This API changed name in the ATS 9-10 transition.
template<typename T> auto
ts_vconn_ssl_get(T vc, swoc::meta::CaseTag<0>) -> decltype(TSVConnSSLConnectionGet(vc)) {
  return TSVConnSSLConnectionGet(vc);
}

template<typename T> auto
ts_vconn_ssl_get(T vc, swoc::meta::CaseTag<1>) -> decltype(TSVConnSslConnectionGet(vc)) {
  return TSVConnSslConnectionGet(vc);
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

TextView ts::HttpSsn::inbound_sni() const {
  if (_ssn) {
    TSVConn ssl_vc = TSHttpSsnClientVConnGet(_ssn);
    TSSslConnection ts_ssl_ctx = detail::ts_vconn_ssl_get(ssl_vc, swoc::meta::CaseArg);
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

Errata ts::HttpTxn::cache_key_assign(TextView const &key) {
  TSCacheUrlSet(_txn, key.data(), key.size());
  return {};
}

swoc::Rv<int> HttpTxn::reserve_arg(swoc::TextView const &name, swoc::TextView const &description) {
  int idx = -1;
  if (TS_ERROR == TSHttpTxnArgIndexReserve(name.data(), description.data(), &idx)) {
    return { idx, Error(R"(Failed to reserve transaction argument index.)") };
  }
  return idx;
}

TxnConfigVar * HttpTxn::find_override(swoc::TextView const& name) {
  TSOverridableConfigKey key;
  TSRecordDataType type;

  std::lock_guard lock{_var_table_lock};

  if (auto spot{_var_table.find(name)}; spot != _var_table.end()) {
    return spot->second.get();
  }

  // Does it exist?
  if (TS_SUCCESS != TSHttpTxnConfigFind(name.data(), name.size(), &key, &type)) {
    return nullptr;
  }

  // It exists, put it in the table and return it.
  auto result = _var_table.emplace(name, new TxnConfigVar{name, key, type});
  return std::get<0>(result)->second.get();
}

Errata ts::HttpTxn::override_assign(TxnConfigVar const &var, int n) {
  if (!var.is_valid(n)) {
    return Error(R"(Integer value {} is not valid for transaction overridable configuration "{}".)", var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigIntSet(_txn, var.key(), n)) {
    return Error(R"(Integer value {} assignment to transaction overridable configuration "{}" failed.)", var.name());
  }
  return {};
}

Errata ts::HttpTxn::override_assign(ts::TxnConfigVar const &var, TextView const& text){
  if (!var.is_valid(text)) {
    return Error(R"(String value "{}" is not valid for transaction overridable configuration "{}".)", var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigStringSet(_txn, var.key(), text.data(), text.size())) {
    return Error(R"(String value "{}" assignment to transaction overridable configuration "{}" failed.)", var.name());
  }
  return {};
}

Errata &ts::HttpTxn::init(swoc::Errata &errata) {
  return errata;
}
/* ------------------------------------------------------------------------ */
} // namespace ts
/* ------------------------------------------------------------------------------------ */
namespace swoc {
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, TSHttpStatus status) {
  return bwformat(w, spec, static_cast<unsigned>(status));
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, TSRecordDataType type) {
  return bwformat(w, spec, ts::TSRecordDataTypeNames[type]);
}
} // namespace swoc


/* ------------------------------------------------------------------------------------ */
namespace {
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  ts::HttpTxn::init(G._preload_errata);
  return true;
}();
} // namespace

/* ------------------------------------------------------------------------------------ */
