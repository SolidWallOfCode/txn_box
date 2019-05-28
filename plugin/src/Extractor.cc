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

#include <ts/ts.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/Extractor.h"

using swoc::TextView;
using swoc::Errata;

/* ------------------------------------------------------------------------------------ */

swoc::Rv<Extractor::Format> Extractor::parse(swoc::TextView format_string, Table const& table) {
  Spec literal_spec; // used to handle literals as spec instances.
  auto ex { swoc::bwf::Format::bind(format_string) };
  Format fmt;
  Errata errata;

  literal_spec._type = swoc::bwf::Spec::LITERAL_TYPE;

  while (ex) {
    Spec spec;
    std::string_view literal;
    bool spec_p = ex(literal, spec);

    if (!literal.empty()) {
      literal_spec._ext = literal;
      fmt.push_back(literal_spec);
    }

    if (spec_p) {
      if (spec._name.empty()) {
        if (spec._idx < 0) {
          errata.error(R"(Extractor missing name at offset {})", format_string.size() - ex._fmt.size());
        };
      } else {
      }
    }
  }
}
