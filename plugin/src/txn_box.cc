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
#include "txn_box/Config.h"
#include "txn_box/yaml-util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;

const std::string Config::ROOT_KEY { "txn_box" };

swoc::Lexicon<Hook> HookName {{Hook::READ_REQ, "read-request" },
                              {Hook::SEND_RSP, "send-response"}
};
/* ------------------------------------------------------------------------------------ */

Rv<Directive::Handle> process_directive_node(YAML::Node node) {
  Rv<Directive::Handle> zret;
  auto & [ handle, errata ] = zret;
  if (node.IsMap()) {
    if (node.size() == 1) {
      auto const& [ key, value ] { *node.begin() };
      auto && [ dir_handle, dir_errata ] { Directive::assemble(key.as<TextView>(), value) };
      if (dir_errata.is_ok()) {
        handle = std::move(dir_handle);
      } else {
        errata = std::move(dir_errata);
        errata.error(R"(Directive at {} is invalid.)", node.Mark());
      }
    } else {
      errata.error(R"(Directive at {} has more than one key)", node.Mark());
    }
  } else if (node.IsSequence()) {
    handle.reset(new DirectiveList);
    for (auto child : node) {
      auto && [child_handle, child_errata] {process_directive_node(child)};
      if (child_errata.is_ok()) {
        static_cast<DirectiveList *>(handle.get())->push_back(std::move(child_handle));
      } else {
        errata.note(child_errata);
        errata.error(R"(Failed to load directives at {})", child.Mark());
      }
    }
  } else {
  }
  return std::move(zret);
}

Errata Config::load_top_level_directive(YAML::Node node) {
  Errata zret;
  if (node.IsMap()) {
    if (node.size() == 1) {
      auto const& [ key, value ] { *node.begin() };
      auto && [ dir_handle, dir_errata ] { Directive::assemble(key.as<TextView>(), value) };
      if (dir_errata.is_ok()) {
        handle = std::move(dir_handle);
      } else {
        errata = std::move(dir_errata);
        errata.error(R"(Directive at {} is invalid.)", node.Mark());
      }
    } else {
      errata.error(R"(Directive at {} has more than one key)", node.Mark());
    }
  } else if (node.IsSequence()) {
    handle.reset(new DirectiveList);
    for (auto child : node) {
      auto && [child_handle, child_errata] {process_directive_node(child)};
      if (child_errata.is_ok()) {
        static_cast<DirectiveList *>(handle.get())->push_back(std::move(child_handle));
      } else {
        errata.note(child_errata);
        errata.error(R"(Failed to load directives at {})", child.Mark());
      }
    }
  } else {
  }
  return std::move(zret);
}

Errata Config::load_file(swoc::file::path const& file_path) {
  Errata zret;
  std::error_code ec;
  std::string content = swoc::file::load(file_path, ec);

  if (ec) {
    return zret.error(R"(Unable to load file "{}" - {}.)", file_path, ec);
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
    return zret.error(R"(YAML parsing of "{}" failed - {}.)", file_path, ex.what());
  }

  YAML::Node base_node { root[ROOT_KEY] };
  if (! base_node) {
    return zret.error(R"(Base key "{}" not found in "{}".)", ROOT_KEY, file_path);
  }

  if (base_node.IsSequence()) {
    for ( auto child : base_node ) {
      zret.note(this->load_top_level_directive(child));
    }
    if (! zret.is_ok()) {
      zret.error(R"(Failure while loading list of top level directives for "{}" at {}.)",
      ROOT_KEY, base_node.Mark());
    }
  } else if (base_node.IsMap()) {
    zret = this->load_top_level_directive(base_node);
  } else {
  }
  return std::move(zret);
};

/* ------------------------------------------------------------------------------------ */

void
TSPluginInit(int argc, char const *argv[])
{
  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media",
                                "solidwallofcode@verizonmedia.com"};

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("%s: plugin registration failed.", Config::PLUGIN_TAG.data());
  }
}
