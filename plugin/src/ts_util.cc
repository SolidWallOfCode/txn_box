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
#include <swoc/bwf_ex.h>
#include <swoc/ArenaWriter.h>
#include <swoc/swoc_meta.h>

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::MemArena;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
using swoc::MemSpan;
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

// TSHttpTxnServerSsnTransactionCount API only available in ATS 10.
template <typename T>
auto
get_server_ssn_txn_count([[maybe_unused]] T const &txn, swoc::meta::CaseTag<0>) -> int
{
  // if not ATS 10, then this should not be taken into consideration for connection reuse.
  return 0;
}

template <typename T>
auto
get_server_ssn_txn_count(T const &txn, swoc::meta::CaseTag<1>) -> decltype(TSHttpTxnServerSsnTransactionCount(txn), int())
{
  return TSHttpTxnServerSsnTransactionCount(txn);
}

//#pragma GCC diagnostic pop

} // namespace detail

/* ------------------------------------------------------------------------------------ */

BufferWriter& ts::URL::write_full(BufferWriter& w) const {
  // Gonna live dangerously - since a reader is only allocated when a new IOBuffer is created
  // it doesn't need to be tracked - it will get cleaned up when the IOBuffer is destroyed.
  // 32K should be large enough to hold the longest valid URL for ATS.
  IOBuffer iob{TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_32K)};
  auto reader = TSIOBufferReaderAlloc(iob.get());
  int64_t avail = 0;

  TSUrlPrint(_buff, _loc, iob.get());
  auto block = TSIOBufferReaderStart(reader);
  auto ptr = TSIOBufferBlockReadStart(block, reader, &avail);
  w.write(ptr, avail);
  return w;
}

TextView ts::URL::scheme() const {
  if (this->is_valid()) {
    char const* text;
    int size;
    if (nullptr != (text = TSUrlSchemeGet(_buff, _loc, &size))) {
      return {text, static_cast<size_t>(size)};
    }
  }
  return ""_tv;
}

BufferWriter& ts::URL::write_loc(BufferWriter& w) const {
  auto host_name = this->host();
  if (! host_name.empty()) {
    auto port = this->port();
    if (port == 0 || this->is_port_canonical()) {
      w.write(host_name);
    } else {
      w.print("{}:{}", host_name, port);
    }
  }
  return w;
}

TextView ts::URL::host() const {
  char const *text;
  int size;
  if (this->is_valid() && nullptr != (text = TSUrlHostGet(_buff, _loc, &size))) {
    return {text, static_cast<size_t>(size)};
  }
  return ""_tv;
}

in_port_t  ts::URL::port() const {
  return this->is_valid() ? TSUrlPortGet(_buff, _loc) : 0;
}

bool ts::URL::is_port_canonical(TextView const& scheme, in_port_t port) {
  return scheme.starts_with_nocase("http"_tv) &&
      ((80 == port  && scheme.size() == 4) ||
       (443 == port && scheme.size() == 5 && 's' == tolower(scheme[4]))
      );
}

std::tuple<TextView, in_port_t> ts::URL::loc() const {
  return { this->host(), this->port() };
}

bool ts::HttpRequest::url_set(TextView text) {
  TSMLoc url_loc;
  if (TS_SUCCESS != TSUrlCreate(_buff, &url_loc)) {
    return false;
  }
  auto src = text.data();
  auto limit = text.data_end();
  if (TS_PARSE_DONE != TSUrlParse(_buff, url_loc, &src, limit)) {
    TSHandleMLocRelease(_buff, TS_NULL_MLOC, url_loc);
    return false;
  }
  bool zret = TS_SUCCESS == TSHttpHdrUrlSet(_buff, _loc, url_loc);
  if (!zret) {
    TSHandleMLocRelease(_buff, TS_NULL_MLOC, url_loc);
  }
  return zret;
}

/* ------------------------------------------------------------------------------------ */
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

ts::URL ts::HttpRequest::url() const {
  TSMLoc url_loc;
  if (this->is_valid() && TS_SUCCESS == TSHttpHdrUrlGet(_buff, _loc, &url_loc)) {
    return {_buff, url_loc};
  }
  return {};
}

BufferWriter& ts::HttpRequest::effective_url(BufferWriter& w) {
  if (this->is_valid()) {
    auto url { this->url() };
    auto scheme { url.scheme() };
    auto path { url.path() };
    auto query { url.query() };
    auto [ host, port ] { this->loc() };
    if (url.is_port_canonical(scheme, port)) {
      port = 0;
    }

    w.print("{}{}{}{}{}"
            , bwf::Optional("{}:", scheme)
            , bwf::Optional("//{}", host)
            , bwf::Optional(":{}", port)
            , bwf::Optional("/{}", path)
            , bwf::Optional("?{}", query)
            );
  }
  return w;
}

BufferWriter& ts::HttpRequest::write_loc(BufferWriter& w) const {
  // Try the URL first.
  auto n = w.extent();
  if (auto url{this->url()}; url.is_valid()) {
    url.write_loc(w);
  }
  if (n == w.extent()) { // URL wrote nothing
    if (auto field = this->field(HTTP_FIELD_HOST) ; field.is_valid()) {
      w.write(field.value());
    }
  }
  return w;
}

TextView ts::HttpRequest::host() const {
  auto url { this->url() };
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
  if (auto port = url.port() ; port != 0) {
    return port;
  }
  if (auto field = this->field(HTTP_FIELD_HOST) ; field.is_valid()) {
    auto value = field.value();
    TextView host_token, port_token;
    if (swoc::IPEndpoint::tokenize(value, &host_token, &port_token)) {
      return swoc::svtoi(port_token);
    }
  }
  return 0;
}

std::tuple<TextView, in_port_t> ts::HttpRequest::loc() const {
  auto loc { url().loc() };
  if (std::get<0>(loc).empty()) {
    if (auto field = this->field(HTTP_FIELD_HOST) ; field.is_valid()) {
      auto value = field.value();
      TextView host_token, port_token;
      if (swoc::IPEndpoint::tokenize(value, &host_token, &port_token)) {
        return { host_token, swoc::svtoi(port_token) };
      }
    }
  }
  return { TextView{}, 0 };
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
      if (port_token.size()) {
        w.print("{}:{}", host, port_token);
      } else {
        w.print("{}", host);
      }
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

swoc::TextView HttpRequest::method() const { int length;
  char const *text;
  text = TSHttpHdrMethodGet(_buff, _loc, &length);
  return {text, static_cast<size_t>(length) };
}

ts::URL ts::HttpTxn::pristine_url_get() const {
  TSMBuffer buff;
  TSMLoc loc;
  if (_txn != nullptr && TS_SUCCESS == TSHttpTxnPristineUrlGet(_txn, &buff, &loc)) {
    return { buff, loc };
  }
  return {};
}

ts::HttpRequest ts::HttpTxn::ua_req_hdr() {
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

TextView ts::HttpResponse::reason() const {
  int length = 0;
  char const * text = TSHttpHdrReasonGet(_buff, _loc, &length);
  return length > 0 ? TextView{text, size_t(length) } : TextView{};
}

bool ts::HttpResponse::reason_set(swoc::TextView reason) {
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

int
HttpTxn::server_ssn_txn_count() const
{
  return compat::get_server_ssn_txn_count(_txn, swoc::meta::CaseArg);
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
  auto var = new TxnConfigVar{name, key, type};
  auto result = _var_table.emplace(var->name(), var);
  return std::get<0>(result)->second.get();
}

Errata HttpTxn::override_assign(TxnConfigVar const &var, intmax_t n) {
  if (!var.is_valid(n)) {
    return Error(R"(Integer value {} is not valid for transaction overridable configuration variable "{}".)", n, var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigIntSet(_txn, var.key(), n)) {
    return Error(R"(Integer value {} assignment to transaction overridable configuration variable "{}" failed.)", n, var.name());
  }
  return {};
}

Errata HttpTxn::override_assign(TxnConfigVar const &var, TextView const& text){
  if (!var.is_valid(text)) {
    return Error(R"(String value "{}" is not valid for transaction overridable configuration variable "{}".)", text, var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigStringSet(_txn, var.key(), text.data(), text.size())) {
    return Error(R"(String value "{}" assignment to transaction overridable configuration variable "{}" failed.)", text, var.name());
  }
  return {};
}

Errata HttpTxn::override_assign(TxnConfigVar const &var, double f) {
  if (!var.is_valid(f)) {
    return Error(R"(Floating value {} is not valid for transaction overridable configuration variable "{}".)", var.name());
  }
  if (TS_ERROR == TSHttpTxnConfigFloatSet(_txn, var.key(), f)) {
    return Error(R"(Floating value {} assignment to transaction overridable configuration variable "{}" failed.)", var.name());
  }
  return {};
}

Rv<ConfVarData> HttpTxn::override_fetch(const TxnConfigVar &var) {
  switch (var.type()) {
    case TS_RECORDDATATYPE_FLOAT: {
      TSMgmtFloat v;
      if (TS_SUCCESS == TSHttpTxnConfigFloatGet(_txn, var.key(), &v)) {
        return ConfVarData{v};
      }
      break;
    }
    case TS_RECORDDATATYPE_STRING: {
      char const* text;
      int len;
      if (TS_SUCCESS == TSHttpTxnConfigStringGet(_txn, var.key(), &text, &len)) {
        return ConfVarData{TextView{text, size_t(len)}};
      }
      break;
    }
    case TS_RECORDDATATYPE_INT: {
      TSMgmtInt v;
      if (TS_SUCCESS == TSHttpTxnConfigIntGet(_txn, var.key(), &v)) {
        return ConfVarData{v};
      }
      break;
    }
    default:
      return Error("Var '{}' does not have a valid data type [{}]", var.name(), var.type());
      break;
  }
  return Error(R"(Failed to retrieve config variable "{})", var.name());
}

int HttpSsn::protocol_stack(MemSpan<const char *> tags) const {
  int n = 0;
  if (TS_SUCCESS != TSHttpSsnClientProtocolStackGet(_ssn, tags.count(), tags.data(), &n)) {
    return -1;
  }
  return n;
}

Errata &HttpTxn::init(swoc::Errata &errata) {
  return errata;
}

// ----

int plugin_stat_value(int idx) {
  return TSStatIntGet(idx);
}

int plugin_stat_index(TextView const & name) {
  int idx;
  if (TS_SUCCESS == TSStatFindName(name.data(), &idx)) {
    return idx;
  }
  return -1;
}

Rv<int> plugin_stat_define(TextView const& name, int value, bool persistent_p) {
  int idx = plugin_stat_index(name);
  if (idx >= 0) { // Already there, just return the index.
    return idx;
  }
  // Create the stat.
  idx = TSStatCreate(name.data(), TS_RECORDDATATYPE_INT, (persistent_p ? TS_STAT_PERSISTENT : TS_STAT_NON_PERSISTENT), TS_STAT_SYNC_SUM);
  if (idx == TS_ERROR) {
    return Error("Failed to create stat '{}'", name);
  }
  TSStatIntSet(idx, value);
  return idx;
}

void plugin_stat_update(int idx, intmax_t value) {
  TSStatIntIncrement(idx, value);
}

// ----
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
    _action = nullptr; // Don't cancel again.
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
  // External cancel tries the lock - if that succeeds it can cancel and prevent the lambda
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

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const& spec, ts::ConfVarData const& data) {
  switch (data.index()) {
    default: w.write("NIL"); break;
    case 1: bwformat(w, spec, std::get<1>(data)); break;
    case 2: bwformat(w, spec, std::get<2>(data)); break;
    case 3: bwformat(w, spec, std::get<2>(data)); break;
  }
  return w;
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
