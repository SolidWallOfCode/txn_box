/** @file
 * Regular expression support.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include "txn_box/common.h"
#include "txn_box/Rxp.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Rv<Rxp> Rxp::parse(TextView const& str, Options const& options) {
  int errc = 0;
  size_t err_off = 0;
  uint32_t rxp_opt = 0;
  if (options.f.nc) {
    rxp_opt = PCRE2_CASELESS;
  }
  auto result = pcre2_compile(reinterpret_cast<unsigned const char*>(str.data()), str.size(), rxp_opt, &errc, &err_off, nullptr);
  if (nullptr == result) {
    PCRE2_UCHAR err_buff[128];
    auto err_size = pcre2_get_error_message(errc, err_buff, sizeof(err_buff));
    return Error(R"(Failed to parse regular expression - error "{}" [{}] at offset {} in "{}".)", TextView(reinterpret_cast<char const*>(err_buff), err_size), errc, err_off, str);
  }
  return { result, {} };
};

int Rxp::operator()(swoc::TextView text, pcre2_match_data* match) const {
  return pcre2_match(_rxp.get(), reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), 0, 0, match, nullptr);
}

size_t Rxp::capture_count() const {
  uint32_t count = 0;
  auto result = pcre2_pattern_info(_rxp.get(), PCRE2_INFO_CAPTURECOUNT, &count);
  return result == 0 ? count + 1 : 0; // output doesn't reflect capture group 0, apparently.
}
