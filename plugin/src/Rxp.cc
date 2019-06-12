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

#include "txn_box/Rxp.h"

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Rv<Rxp> Rxp::parse(TextView const& str, std::initializer_list<Option> const& options) {
  OptionGroup rxp_opt;
  for ( auto opt : options ) {
    rxp_opt[opt] = true;
  }
  return self_type::parse(str, rxp_opt);
}

Rv<Rxp> Rxp::parse(TextView const& str, OptionGroup const& options) {
  int errc = 0;
  size_t err_off = 0;
  uint32_t rxp_opt = 0;
  if (options[OPT_NOCASE]) {
    rxp_opt = PCRE2_CASELESS;
  }
  auto result = pcre2_compile(reinterpret_cast<unsigned const char*>(str.data()), str.size(), rxp_opt, &errc, &err_off, nullptr);
  if (nullptr == result) {
    PCRE2_UCHAR err_buff[128];
    auto err_size = pcre2_get_error_message(errc, err_buff, sizeof(err_buff));
    return { {}, Errata().error(R"(Failed to parse regular expression - error "{}" [{}] at offset {} in "{}".)", TextView(
        reinterpret_cast<char const*>(err_buff), err_size), errc, err_off, str) };
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
