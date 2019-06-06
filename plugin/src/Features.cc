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

#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;

/* ------------------------------------------------------------------------------------ */
class Ex_creq_url_host : public Extractor, public DirectFeature {
public:
  static constexpr TextView NAME { "creq-url-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx) const override;
};

FeatureView Ex_creq_url_host::direct_view(Context &ctx) const {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
    }
  }
  return zret;
}

BufferWriter& Ex_creq_url_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->direct_view(ctx));
}

class Ex_creq_host : public Extractor, public DirectFeature {
public:
  static constexpr TextView NAME { "creq-host" };

  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
  FeatureView direct_view(Context & ctx) const override;
};

FeatureView Ex_creq_host::direct_view(Context &ctx) const {
  FeatureView zret;
  zret._direct_p = true;
  if ( ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    if ( ts::URL url { hdr.url() } ; url.is_valid()) {
      zret = url.host();
      if (zret.data() == nullptr) { // not in the URL, look in the HOST field.
        if ( auto field { hdr.field(ts::HTTP_FIELD_HOST) } ; field.is_valid()) {
          zret = field.value();
        }
      }
    }
  }
  return zret;
}

BufferWriter& Ex_creq_host::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->direct_view(ctx));
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
} // namespace
