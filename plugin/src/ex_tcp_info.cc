/** @file
   TCP info support.

 * Copyright 2021, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <type_traits>

#include "txn_box/common.h"

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>

#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

#if __has_include("linux/tcp.h")
#include <linux/tcp.h>
#endif
#if __has_include("netinet/in.h")
#include <netinet/in.h>
#endif

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::MemSpan;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
// Unfortunate, but there are limits to what can be done when using C based interfaces.
// The value is picked out of the header files, but ultimately it doesn't matter as it is not used
// if the other tcp_info support is not available.
# if ! defined TCP_INFO
# define TCP_INFO 11
# endif

namespace {

// All of this is for OS compatibility - it enables the code to compile and run regardless of
// whether tcp_info is available. If it is not, the NULL value is returned by the extractors.

// Determine if a specific type has been declared.
template < typename A, typename = void> struct has_type : public std::false_type {};
template < typename A > struct has_type<A, std::void_t<decltype(sizeof(A))>> : public std::true_type {};

// Need specialized support for retrans because the name is not consistent. This prefers
// @a tcpi_retrans if available, and falls back to @a __tcpi_retrans
template<typename T>
auto
field_retrans(T const& info, swoc::meta::CaseTag<0>) -> int32_t {
  return 0;
}

template<typename T>
auto
field_retrans(T const& info, swoc::meta::CaseTag<1>) -> decltype(info.__tcpi_retrans) {
  return info.__tcpi_retrans;
}

template<typename T>
auto
field_retrans(T const& info, swoc::meta::CaseTag<2>) -> decltype(info.tcpi_retrans) {
  return info.tcpi_retrans;
}

} // namespace
/* ------------------------------------------------------------------------------------ */
/** Extract tcp_info.
 *
 */
class Ex_tcp_info : public Extractor {
  using self_type = Ex_tcp_info; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME{"tcp-info"};

  /// Usage validation.
  Rv<ActiveType> validate(Config& cfg, Spec& spec, TextView const& arg) override;

  /// Extract the feature.
  Feature extract(Context & ctx, Spec const& spec) override;
protected:
  /// Support fields for extraction.
  enum Field {
    NONE,
    RTT,
    RTO,
    SND_CWND,
    RETRANS
  };

  // Compat - necessary because constexpr if still requires the excluded code to compile and
  // using undeclared types will break.
  template < typename> static auto value(ts::HttpTxn const&, Field, swoc::meta::CaseTag<0>) -> intmax_t { return 0; };
  template < typename tcp_info> static auto value(ts::HttpTxn const& txn, Field field, swoc::meta::CaseTag<1>) -> std::enable_if_t<has_type<tcp_info>::value, intmax_t>;

  // Conversion between names and enumerations.
  inline static const swoc::Lexicon<Field> _field_lexicon {{
                                                               {NONE, "none"}
                                                               , {RTT, "rtt"}
                                                               , { RTO, "rto"}
                                                               , { SND_CWND, "snd-cwnd"}
                                                               , { RETRANS, "retrans"}
                                                           }, NONE};
};

Rv<ActiveType> Ex_tcp_info::validate(Config &, Spec &spec, const TextView &arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to specify the field.)", NAME);
  }
  auto field = _field_lexicon[arg];
  if (NONE == field) {
    return Error(R"(Field "{}" for "{}" extractor is not supported.)", arg, NAME);
  }
  // Ugly - need to store the enum, and it's not worth allocating some chunk of config to do that
  // instead of just stashing it in the span size.
  spec._data = MemSpan<void>{ nullptr, size_t(field) };
  return { { NIL, INTEGER} }; // Result can be an integer or NULL (NIL).
}

Feature Ex_tcp_info::extract(Context &ctx, const Spec &spec) {
  if constexpr(! has_type<struct tcp_info>::value) { // No tcp_info is available.
    return NIL_FEATURE;
  }
  if (ctx._txn.is_internal()) {  // Internal requests do not have TCP Info
    return NIL_FEATURE;
  }

  return self_type::template value<struct tcp_info>(ctx._txn, Field(spec._data.size()), swoc::meta::CaseArg);
}

template<typename tcp_info>
auto Ex_tcp_info::value(ts::HttpTxn const& txn, Ex_tcp_info::Field field
                        , swoc::meta::CaseTag<1>) -> std::enable_if_t<has_type<tcp_info>::value, intmax_t> {
  auto fd = txn.inbound_fd();
  if (fd >= 0) {
    tcp_info info;
    socklen_t info_len = sizeof(info);
    if (0 == ::getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) && info_len > 0) {
      switch (field) {
        case NONE: return 0;
        case RTT: return info.tcpi_rtt;
        case RTO: return info.tcpi_rto;
        case SND_CWND: return info.tcpi_snd_cwnd;
        case RETRANS: return field_retrans(info, swoc::meta::CaseArg);
      }
    }
  }
  return 0;

}

/* ------------------------------------------------------------------------------------ */

namespace {
Ex_tcp_info tcp_info;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(tcp_info.NAME, &tcp_info);
  return true;
} ();
} // namespace
