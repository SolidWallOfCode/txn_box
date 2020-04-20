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
#include "txn_box/Modifier.h"
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
// API changes.
namespace compat {

template<typename S = void> auto
status_set(ts::HttpTxn &txn, TSHttpStatus status, swoc::meta::CaseTag<0>) -> bool {
  return txn.prsp_hdr().status_set(status);
}

// New for ATS 9, prefer this if available.
template<typename S = void> auto status_set(ts::HttpTxn &txn, TSHttpStatus status
                                        , swoc::meta::CaseTag<1>) -> decltype(TSHttpTxnStatusSet(txn, status), bool()) {
  TSHttpTxnStatusSet(txn, status); // no error return, sigh.
  return true;
}

// ---

// This API changed name in ATS 9.
template<typename T > auto
vconn_ssl_get(T vc, swoc::meta::CaseTag<0>) -> decltype(TSVConnSSLConnectionGet(vc)) {
  return TSVConnSSLConnectionGet(vc);
}

template<typename T > auto
vconn_ssl_get(T vc, swoc::meta::CaseTag<1>) -> decltype(TSVConnSslConnectionGet(vc)) {
  return TSVConnSslConnectionGet(vc);
}

// ---
// Txn / Ssn args were changed for ATS 10. Prefer the new API.
// Because SFINAE happens after trying to compile a template, and deprecated isn't a real compiler
// failure, need to do this for a clean compile in versions of ATS that only deprecate the old stuff.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// First a meta-template to provide a compile time constant on whether TSUserArg are available.
template < typename A, typename = void> struct has_TS_USER_ARGS : public std::false_type {};
template < typename A > struct has_TS_USER_ARGS<A, std::void_t<decltype(&TSUserArgGet)>> : public std::true_type {};

template < typename A = void >
auto user_arg_get(TSHttpTxn txnp, int arg_idx) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, void *> {
  return TSHttpTxnArgGet(txnp, arg_idx);
}

template < typename A = void >
auto user_arg_get(TSHttpTxn txnp, int arg_idx) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, void *> {
  return TSUserArgGet(txnp, arg_idx);
}

template < typename A >
auto user_arg_set(TSHttpTxn txnp, A arg_idx, void *arg) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, void> {
  TSHttpTxnArgSet(txnp, arg_idx, arg);
}

template < typename A >
auto user_arg_set(TSHttpTxn txnp, A arg_idx, void *arg) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, void> {
  TSUserArgSet(txnp, arg_idx, arg);
}

template < typename A >
auto user_arg_index_reserve(const char *name, const char *description, A arg_idx) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSHttpTxnArgIndexReserve(name, description, arg_idx);
}

template < typename A >
auto user_arg_index_reserve(const char *name, const char *description, A *arg_idx) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSUserArgIndexReserve(TS_USER_ARGS_TXN, name, description, arg_idx);
}

template < typename A >
auto user_arg_index_name_lookup(char const *name, A *arg_idx, char const **description) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSHttpTxnArgIndexNameLookup(name, arg_idx, description);
}

template < typename A >
auto user_arg_index_name_lookup(char const *name, A *arg_idx, char const **description) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, name, arg_idx, description);
}

#pragma GCC diagnostic pop

} // namespace detail

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

TextView ts::HttpField::name() const {
  int size;
  char const *text;
  if (this->is_valid() &&
      nullptr != (text = TSMimeHdrFieldNameGet(_buff, _hdr, _loc, &size))) {
    return {text, static_cast<size_t>(size)};
  }
  return {};
}

TextView ts::HttpField::value() const {
  int size;
  char const *text;
  if (this->is_valid() &&
      nullptr != (text = TSMimeHdrFieldValueStringGet(_buff, _hdr, _loc, -1, &size))) {
    return {text, static_cast<size_t>(size)};
  }
  return {};
}

bool ts::HttpField::assign(swoc::TextView value) {
  value.rtrim_if(&isspace);
  return this->is_valid() &&
       TS_SUCCESS == TSMimeHdrFieldValueStringSet(_buff, _hdr, _loc, -1, value.data(), value.size())
       ;
}

bool ts::HttpField::destroy() {
  return TS_SUCCESS == TSMimeHdrFieldDestroy(_buff, _hdr, _loc);
}

unsigned ts::HttpField::dup_count() const {
  unsigned zret = 0;
  if (this->is_valid()) {
    for ( auto f = ts::HttpHeader{_buff, _hdr }.field(this->name()) ; f.is_valid() ; f = f.next_dup() ) {
      ++zret;
    }
  }
  return zret;
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

bool ts::HttpTxn::set_upstream_addr(const swoc::IPAddr &addr) const {
  return TS_SUCCESS == TSHttpTxnServerAddrSet(_txn, (swoc::IPEndpoint(addr)));
}

swoc::MemSpan<char> ts::HttpTxn::ts_dup(swoc::TextView const &text) {
  auto dup = static_cast<char *>(TSmalloc(text.size() + 1));
  memcpy(dup, text.data(), text.size());
  dup[text.size()] = '\0';
  return {dup, text.size()};
}

void ts::HttpTxn::status_set(int status) {
  compat::status_set(*this, static_cast<TSHttpStatus>(status), swoc::meta::CaseArg);
}

ts::String ts::HttpTxn::effective_url_get() const {
  int size;
  auto s = TSHttpTxnEffectiveUrlStringGet(_txn, &size);
  return {s, size};
}

TextView ts::HttpSsn::inbound_sni() const {
  if (_ssn) {
    TSVConn ssl_vc = TSHttpSsnClientVConnGet(_ssn);
    TSSslConnection ts_ssl_ctx = compat::vconn_ssl_get(ssl_vc, swoc::meta::CaseArg);
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

TextView ts::HttpSsn::proto_contains(const swoc::TextView &tag) const {
  TextView probe { tag };
  if (tag.empty() || tag.back() != '\0') {
    char* span = static_cast<char*>(alloca(tag.size() + 1));
    memcpy(span, tag.data(), tag.size());
    span[tag.size()] = '\0';
    probe.assign(span, tag.size() + 1);
  }
  auto result = TSHttpSsnClientProtocolStackContains(_ssn, probe.data());
  return { result, result ? strlen(result) : 0 };
}

swoc::IPEndpoint ts::HttpSsn::remote_addr() const {
  return swoc::IPEndpoint{ TSHttpSsnClientAddrGet(_ssn) };
}

Errata ts::HttpTxn::cache_key_assign(TextView const &key) {
  TSCacheUrlSet(_txn, key.data(), key.size());
  return {};
}

void * HttpTxn::arg(int idx) {
  return compat::user_arg_get(_txn, idx);
}

void HttpTxn::arg_assign(int idx, void * value) {
  compat::user_arg_set(_txn, idx, value);
}

swoc::Rv<int> HttpTxn::reserve_arg(swoc::TextView const &name, swoc::TextView const &description) {
  int idx = -1;

  char const * buff = nullptr;
  if (TS_SUCCESS == ts::compat::user_arg_index_name_lookup(name.data(), &idx, &buff)) {
    return idx;
  }

  if (TS_ERROR == ts::compat::user_arg_index_reserve(name.data(), description.data(), &idx)) {
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

Errata HttpTxn::override_assign(TxnConfigVar const &var, int n) {
  if (!var.is_valid(n)) {
    return Error(R"(Integer value {} is not valid for transaction overridable configuration "{}".)", var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigIntSet(_txn, var.key(), n)) {
    return Error(R"(Integer value {} assignment to transaction overridable configuration "{}" failed.)", var.name());
  }
  return {};
}

Errata HttpTxn::override_assign(TxnConfigVar const &var, TextView const& text){
  if (!var.is_valid(text)) {
    return Error(R"(String value "{}" is not valid for transaction overridable configuration "{}".)", var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigStringSet(_txn, var.key(), text.data(), text.size())) {
    return Error(R"(String value "{}" assignment to transaction overridable configuration "{}" failed.)", var.name());
  }
  return {};
}

Errata &HttpTxn::init(swoc::Errata &errata) {
  return errata;
}

void TaskHandle::cancel() {
  if (_action != nullptr) {
    TSMutex m = TSContMutexGet(_cont);
    auto data = static_cast<Data *>(TSContDataGet(_cont));
    if (TSMutexLockTry(m)) {
      TSActionCancel(_action);
      TSMutexUnlock(m);
      delete data;
      TSContDestroy(_cont);
    } else {
      bool canceled = false; // Need reference for first argument.
      data->_active.compare_exchange_strong(canceled, true);
    }
  }
}

TaskHandle PerformAsTask(std::function<void ()> &&task) {
  static auto lambda = [](TSCont contp, TSEvent ev_code, void *) -> int {
    auto data = static_cast<TaskHandle::Data*>(TSContDataGet(contp));
    if (data->_active) {
      data->_f();
    }
    delete data;
    TSContDestroy(contp);
    return 0;
  };

  auto contp = TSContCreate(lambda, TSMutexCreate());
  auto data = new TaskHandle::Data(std::move(task));
  TSContDataSet(contp, data);
  return { TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_TASK), contp };
}

TaskHandle PerformAsTaskEvery(std::function<void ()> &&task, std::chrono::milliseconds period) {
  static auto lambda = [](TSCont contp, TSEvent ev_code, void *event) -> int {
    auto data = static_cast<TaskHandle::Data*>(TSContDataGet(contp));
    if (data->_active) {
      data->_f();
    } else {
      TSActionCancel(static_cast<TSAction>(event));
      delete data;
      TSContDestroy(contp);
    }
    return 0;
  };
  auto contp = TSContCreate(lambda, TSMutexCreate());
  auto data = new TaskHandle::Data(std::move(task));
  TSContDataSet(contp, data);
  return { TSContScheduleEveryOnPool(contp, period.count(), TS_THREAD_POOL_TASK), contp };
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
