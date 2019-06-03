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

#include "swoc/TextView.h"

#include "txn_box/Extractor.h"
#include "txn_box/Context.h"

#include "txn_box/yaml-util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;

const TextView HTTP_FIELD_HOST { TS_MIME_FIELD_HOST, static_cast<size_t>(TS_MIME_LEN_HOST) };

/* ------------------------------------------------------------------------------------ */
class Ex_creq_url_host : public Extractor, public DirectFeature {
  using self_type = Ex_creq_url_host;
  using super_type = Extractor;
public:
  swoc::TextView direct_view(Context & ctx) const override;

  static constexpr TextView NAME { "creq-url-host" };
};

TextView Ex_creq_url_host::direct_view(Context &ctx) const {
  TextView zret;
  if ( ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

class Ex_creq_host : public Extractor, public DirectFeature {
  using self_type = Ex_creq_host;
  using super_type = Extractor;
public:
  static constexpr TextView NAME { "creq-host" };

  TextView direct_view(Context & ctx) const override;
};

TextView Ex_creq_host::direct_view(Context &ctx) const {
  TextView zret;
  if ( ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
      if (zret.data() == nullptr) { // not in the URL, look in the HOST field.
        if ( auto field { hdr.field(HTTP_FIELD_HOST) } ; field.is_valid()) {
          zret = field.value();
        }
      }
    }
  }
  return zret;
}

/* ------------------------------------------------------------------------------------ */

namespace {
Ex_creq_host creq_host;
Ex_creq_url_host creq_url_host;
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Extractor::define(Ex_creq_host::NAME, &creq_host);
  Extractor::define(Ex_creq_url_host::NAME, &creq_url_host);

  return true;
} ();
}; // namespace
