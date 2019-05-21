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

#include "ts/ts.h"

#include "swoc/TextView.h"
#include "swoc/swoc_file.h"

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"

#define PLUGIN_NAME "Transaction Tool Box"
#define PLUGIN_TAG "txn_box"

using swoc::TextView;

/* ------------------------------------------------------------------------------------ */

void load_func(swoc::file::path const& file_path) {
  std::error_code ec;
  std::string content = swoc::file::load(file_path, ec);

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
//    return ctx.notes.error("Loading failed: {}", ex.what());
  }
}

/* ------------------------------------------------------------------------------------ */

void
TSPluginInit(int argc, char const *argv[])
{
  TSPluginRegistrationInfo info{PLUGIN_NAME, "Verizon", "solidwallofcode@verizonmedia.com"};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PLUGIN_NAME ": plugin registration failed.");
  }
}
