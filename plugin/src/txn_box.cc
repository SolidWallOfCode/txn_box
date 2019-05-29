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

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>

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

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  HookName.set_default(Hook::INVALID);
  return true;
} ();
}; // namespace
/* ------------------------------------------------------------------------------------ */

Rv<Directive::Handle> Config::load_directive(YAML::Node drtv_node) {
  if (drtv_node.IsMap()) {
    return { Directive::load(*this, drtv_node) };
  } else if (drtv_node.IsSequence()) {
    Errata zret;
    Directive::Handle drtv_list{new DirectiveList};
    for (auto child : drtv_node) {
      auto && [handle, errata] {this->load_directive(child)};
      if (errata.is_ok()) {
        static_cast<DirectiveList *>(drtv_list.get())->push_back(std::move(handle));
      } else {
        return { {}, std::move(errata.error(R"(Failed to load directives at {})", drtv_node.Mark())) };
      }
    }
    return {std::move(drtv_list), {}};
  }
  return { {}, Errata().error(R"(Directive at {} is not an object or a sequence as required.)",
      drtv_node.Mark()) };
}

Errata Config::load_top_level_directive(YAML::Node drtv_node) {
  Errata zret;
  if (drtv_node.IsMap()) {
    YAML::Node key_node { drtv_node[When::KEY] };
    if (key_node) {
      try {
        auto hook_idx{HookName[key_node.Scalar()]};
        auto && [ handle, errata ]{ When::load(*this, drtv_node, key_node) };
        if (errata.is_ok()) {
          _roots[static_cast<unsigned>(hook_idx)].emplace_back(std::move(handle));
        } else {
          zret.note(errata);
        }
      } catch (std::exception& ex) {
        zret.error(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_node.Scalar(),
            When::KEY, key_node.Mark());
      }
    } else {
      zret.error(R"(Top level directive at {} is a "when" directive as required.)", drtv_node.Mark());
    }
  } else {
    zret.error(R"(Top level directive at {} is not an object as required.)", drtv_node.Mark());
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
    return zret.error(R"(Base key "{}" for plugin "{}" not found in "{}".)", ROOT_KEY,
        PLUGIN_NAME, file_path);
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
