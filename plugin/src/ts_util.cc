/** @file
   Traffic Server plugin API utilities.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <alloca.h>

#include <openssl/ssl.h>

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <swoc/swoc_meta.h>

#include "txn_box/ts_util.h"

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

// Sigh - apparently it's not possible even in C++17 to detect the absence of an enum type.
// Therefore it must be declared when it's not around to make the rest of the compat logic work.
#if TS_VERSION_MAJOR < 9
enum TSUserArgType : uint8_t { TS_USER_ARGS_TXN };
#endif

template < typename T, typename U > constexpr auto eraser(U && u) -> U { return std::forward<U>(u); }

template<typename S = TSHttpStatus> auto
status_set(ts::HttpTxn &txn, TSHttpStatus status, swoc::meta::CaseTag<0>) -> bool {
  return txn.prsp_hdr().status_set(status);
}

// New for ATS 9, prefer this if available.
template<typename S = void > auto status_set(ts::HttpTxn &txn, TSHttpStatus status, swoc::meta::CaseTag<1>
                                                   ) -> decltype(TSHttpTxnStatusSet(txn, eraser<S>(status)), bool()) {
  TSHttpTxnStatusSet(txn, eraser<S>(status)); // no error return, sigh.
  return true;
}

// ---

// This API changed name in ATS 9.
template<typename V = void> auto
vconn_ssl_get(TSVConn vc, swoc::meta::CaseTag<0>) -> decltype(TSVConnSSLConnectionGet(eraser<V>(vc))) {
  return TSVConnSSLConnectionGet(eraser<V>(vc));
}

template<typename V = void> auto
vconn_ssl_get(TSVConn vc, swoc::meta::CaseTag<1>) -> decltype(TSVConnSslConnectionGet(eraser<V>(vc))) {
  return TSVConnSslConnectionGet(eraser<V>(vc));
}

// ---
// Txn / Ssn args were changed for ATS 10. Prefer the new API.
// Because SFINAE happens after trying to compile a template, and deprecated isn't a real compiler
// failure, need to do this for a clean compile in versions of ATS that only deprecate the old stuff.
//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// First a meta-template to provide a compile time constant on whether TSUserArg are available.
template < typename A, typename = void> struct has_TS_USER_ARGS : public std::false_type {};
template < typename A > struct has_TS_USER_ARGS<A, std::void_t<decltype(TSUserArgGet(nullptr, eraser<A>(0)))>> : public std::true_type {};

template < typename A = void >
auto user_arg_get(TSHttpTxn txnp, int arg_idx) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, void *> {
  return TSHttpTxnArgGet(txnp, eraser<A>(arg_idx));
}

template < typename A = void >
auto user_arg_get(TSHttpTxn txnp, int arg_idx) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, void *> {
  return TSUserArgGet(txnp, eraser<A>(arg_idx));
}

template < typename A = void >
auto user_arg_set(TSHttpTxn txnp, int arg_idx, void *arg) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, void> {
  TSHttpTxnArgSet(txnp, arg_idx, eraser<A>(arg));
}

template < typename A = void >
auto user_arg_set(TSHttpTxn txnp, int arg_idx, void *arg) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, void> {
  TSUserArgSet(txnp, arg_idx, eraser<A>(arg));
}

template < typename A = void >
auto user_arg_index_reserve(const char *name, const char *description, int *arg_idx) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSHttpTxnArgIndexReserve(name, description, eraser<A>(arg_idx));
}

template < typename A = void>
auto user_arg_index_reserve(const char *name, const char *description, int *arg_idx) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSUserArgIndexReserve(TS_USER_ARGS_TXN, name, description, eraser<A>(arg_idx));
}

template < typename A >
auto user_arg_index_name_lookup(char const *name, A *arg_idx, char const **description) -> std::enable_if_t<!has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSHttpTxnArgIndexNameLookup(name, arg_idx, description);
}

template < typename A >
auto user_arg_index_name_lookup(char const *name, A *arg_idx, char const **description) -> std::enable_if_t<has_TS_USER_ARGS<A>::value, TSReturnCode> {
  return TSUserArgIndexNameLookup(TS_USER_ARGS_TXN, name, arg_idx, description);
}

//#pragma GCC diagnostic pop

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

TextView ts::HttpRequest::host() const {
  auto url { const_cast<self_type*>(this)->url() };
  if (auto host = url.host() ; ! host.empty()) {
    return host;
  }
  if (auto field = this->field(HTTP_FIELD_HOST) ; field.is_valid()) {
    auto value = field.value();
    TextView host_token, port_token, rest_token;
    if (swoc::IPEndpoint::tokenize(value, &host_token, &port_token)) {
      return host_token;
    }
  }
  return {};
}

in_port_t ts::HttpRequest::port() const {
  auto url { const_cast<self_type*>(this)->url() };
  if (auto port = url.port_get() ; port != 0) {
    return port;
  }
  if (auto field = this->field(HTTP_FIELD_HOST) ; field.is_valid()) {
    auto value = field.value();
    TextView host_token, port_token, rest_token;
    if (swoc::IPEndpoint::tokenize(value, &host_token, &port_token)) {
      return swoc::svtoi(port_token);
    }
  }
  return 0;
}

bool ts::HttpRequest::host_set(swoc::TextView const &host) {
  auto url{this->url()};
  bool force_host_p = true;
  if (!url.host().empty()) {
    url.host_set(host);
    force_host_p = false;
  }
  auto field{this->field(HTTP_FIELD_HOST)};
  if (field.is_valid()) {
    auto text = field.value();
    TextView host_token, port_token, rest_token;
    if (swoc::IPEndpoint::tokenize(text, &host_token, &port_token)) {
      size_t n = host.size() + 1 + port_token.size();
      swoc::FixedBufferWriter w{static_cast<char*>(alloca(n)), n};
      w.print("{}:{}", host, port_token);
      field.assign(w.view());
    } else { // It's messed up, do the best we can by setting it to a valid value.
      field.assign(host);
    }
  } else if (force_host_p) {
    this->field_create(HTTP_FIELD_HOST).assign(host);
  }
  return true;
}

bool ts::HttpRequest::port_set(in_port_t port) {
  auto url{this->url()};
  if (!url.host().empty()) {
    url.port_set(port);
  }
  auto field{this->field(HTTP_FIELD_HOST)};
  if (field.is_valid()) {
    auto text = field.value();
    TextView host_token, port_token, rest_token;
    if (swoc::IPEndpoint::tokenize(text, &host_token, &port_token)) {
      size_t n = host_token.size() + 1 + std::numeric_limits<in_port_t>::max_digits10;
      swoc::FixedBufferWriter w{static_cast<char*>(alloca(n)), n};
      w.print("{}:{}", host_token, port);
      field.assign(w.view());
    }
  }
  return true;
}

bool ts::HttpRequest::scheme_set(swoc::TextView const &scheme) {
  this->url().scheme_set(scheme);
  return true;
}

swoc::TextView HttpRequest::method() const { int length;
  char const *text;
  text = TSHttpHdrMethodGet(_buff, _loc, &length);
  return {text, static_cast<size_t>(length) };
}

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
    // Work around for TS shutdown - if this is cleaned up during shutdown, it's done on TS_MAIN
    // which is not an EThread, which crashes TSMutextTryLock. If that's the case, though, there's
    // no point in worrying about locks because the ET_NET threads aren't running.
    if (TSThreadSelf() == nullptr || TSMutexLockTry(m)) {
      TSActionCancel(_action);
      TSMutexUnlock(m);
      delete data;
      TSContDestroy(_cont);
    } else {
      // Signal the task lambda (which has the lock) that it should clean up.
      bool canceled = false; // Need reference for first argument.
      data->_active.compare_exchange_strong(canceled, true);
    }
  }
}

TaskHandle PerformAsTask(std::function<void ()> &&task) {
  static auto lambda = [](TSCont contp, TSEvent, void *) -> int {
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
  // The lambda runs under lock for the continuation mutex, therefore it can cancel as needed.
  // External cancel tries the lock - if that happens it can cancel and prevent the lambda
  // entirely. Otherwise it atomically sets @a _active to @c false.
  static auto lambda = [](TSCont contp, TSEvent, void *event) -> int {
    auto data = static_cast<TaskHandle::Data*>(TSContDataGet(contp));
    if (data->_active) {
      data->_f();
    }

    // See if there as a request to cancel while busy.
    if (! data->_active) {
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
