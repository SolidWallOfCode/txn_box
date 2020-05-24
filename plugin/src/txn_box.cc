/** @file
   Base plugin logic.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <shared_mutex>

#include <glob.h>
#include <getopt.h>

#include <openssl/ssl.h>

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <swoc/bwf_std.h>
#include <yaml-cpp/yaml.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"
#include "txn_box/yaml_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */

Global G;
extern std::string glob_to_rxp(TextView glob);

const std::string Config::ROOT_KEY { "txn_box" };

Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev) {
  static const std::map<TSEvent, Hook> table{
      {TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ}
      , {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ}
      , {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP}
      , {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP}
      , {TS_EVENT_HTTP_PRE_REMAP, Hook::PRE_REMAP}
      , {TS_EVENT_HTTP_POST_REMAP, Hook::POST_REMAP}
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

namespace {

 std::shared_ptr<Config> Plugin_Config;
 std::shared_mutex Plugin_Config_Mutex; // safe updating of the shared ptr.

 // Get a shared pointer to the configuration safely against updates.
 std::shared_ptr<Config> scoped_plugin_config() {
   std::shared_lock lock(Plugin_Config_Mutex);
   return Plugin_Config;
 }

 } // namespace
/* ------------------------------------------------------------------------------------ */
Errata Config::load_file(swoc::file::path const& cfg_path, TextView cfg_key) {
  if (auto spot = _cfg_files.find(cfg_path.view()) ; spot != _cfg_files.end())
    if (spot->has_cfg_key(cfg_key)) {
      ts::DebugMsg(R"(Skipping "{}":{} - already loaded)", cfg_path, cfg_key);
      return {};
    } else {
      spot->add_key_cfg(_arena, cfg_key);
  } else {
    auto fi = _arena.make<FileInfo>(this->localize(cfg_path.view()));
    fi->add_key_cfg(_arena, cfg_key);
    _cfg_files.insert(fi);
  }
  // Try loading and parsing the file.
  auto &&[root, yaml_errata ]{yaml_load(cfg_path)};
  if (!yaml_errata.is_ok()) {
    yaml_errata.info(R"(While loading file "{}".)", cfg_path);
    return std::move(yaml_errata);
  }

  // Process the YAML data.
  auto errata = Plugin_Config->parse_yaml(root, cfg_key);
  if (!errata.is_ok()) {
    errata.info(R"(While parsing key "{}" in configuration file "{}".)", cfg_key, cfg_path);
    return errata;
  }

  return {};
}
/* ------------------------------------------------------------------------------------ */
Errata Config::load_file_glob(TextView pattern, swoc::TextView cfg_key) {
  int flags = 0;
  glob_t files;
  auto err_f = [](char const*, int) -> int { return 0; };
  swoc::file::path abs_pattern = ts::make_absolute(pattern);
  int result = glob(abs_pattern.c_str(), flags, err_f, &files);
  if (result == GLOB_NOMATCH) {
    return Warning(R"(The pattern "{}" did not match any files.)", abs_pattern);
  }
  for ( size_t idx = 0 ; idx < files.gl_pathc ; ++idx) {
    auto errata = this->load_file(swoc::file::path(files.gl_pathv[idx]), cfg_key);
    if (! errata.is_ok()) {
      errata.info(R"(While processing pattern "{}".)", pattern);
      return errata;
    }
  }
  globfree(&files);
  return {};
}
/* ------------------------------------------------------------------------------------ */
void Global::reserve_txn_arg() {
  if (G.TxnArgIdx < 0) {
    auto && [ idx, errata ] { ts::HttpTxn::reserve_arg(Config::ROOT_KEY, "Transaction Box") };
    if (! errata.is_ok()) {
      _preload_errata.note(errata);
    } else {
      TxnArgIdx = idx;
    }
  }
}
/* ------------------------------------------------------------------------------------ */
// Global callback, thread safe.
// This sets up local context for a transaction and spins up a per TXN Continuation which is
// protected by a mutex. This hook isn't set if there are no top level directives.
int CB_Txn_Start(TSCont, TSEvent, void * payload) {
  auto txn {reinterpret_cast<TSHttpTxn>(payload) };
  Context *ctx = new Context(scoped_plugin_config());
  ctx->enable_hooks(txn);
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

Errata Config::load_args(const std::vector<std::string> &args) {
  static constexpr TextView KEY_OPT = "key";
  static constexpr TextView CONFIG_OPT = "config"; // An archaism for BC - take out someday.

  TextView cfg_key { Config::ROOT_KEY };
  for ( unsigned idx = 1 ; idx < args.size() ; ++idx ) {
    TextView arg(args[idx]);
    if (arg.empty()) {
      continue;
    }
    if (arg.front() == '-') {
      arg.ltrim('-');
      if (arg.empty()) {
        return Error("Arg {} has an option prefix but no name.", idx);
      }
      if (++idx >= args.size()) {
        return Error("Arg {} is an option '{}' that requires a value but none was found.", arg);
      }
      if (arg.starts_with_nocase(KEY_OPT)) {
        cfg_key.assign(args[idx]);
      } else if (arg.starts_with_nocase(CONFIG_OPT)) {
        auto errata = Plugin_Config->load_file_glob(args[idx], cfg_key);
        if (!errata.is_ok()) {
          return errata;
        }
      } else {
        return Error("Unrecongnized option '{}'.", arg);
      }
      continue;
    }
    auto errata = Plugin_Config->load_file_glob(arg, cfg_key);
    if (!errata.is_ok()) {
      return errata;
    }
  }

  // Config loaded, run the post load directives and enable them to break the load by reporting
  // errors.
  auto& post_load_directives = this->hook_directives(Hook::POST_LOAD);
  if (post_load_directives.size() > 0) {
    // It's not possible to release an object from a shared ptr, so instead the shared_ptr is
    // constructed with a deleter that does nothing. I think this is cleaner than re-arranging the
    // code to pass in the shared_ptr from the enclosing scope.
    std::shared_ptr<self_type> tmp(this, [](self_type*)->void{});
    std::unique_ptr<Context> ctx{new Context(tmp)};
    for (auto&& drtv : post_load_directives) {
      auto errata = drtv->invoke(*ctx);
      if (! errata.is_ok()) {
        errata.info("While processing post-load directives.");
        return errata;
      }
    }
  }
  return {};
}

void Task_ConfigReload() {
  std::shared_ptr cfg = std::make_shared<Config>();
  auto errata = cfg->load_args(G._args);
  if (!errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: Failed to reload configuration.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  } else {
    std::unique_lock lock(Plugin_Config_Mutex);
    Plugin_Config = cfg;
  }
}

int CB_TxnBoxMsg(TSCont, TSEvent, void * data) {
  static constexpr TextView TAG {"txn_box."};
  static constexpr TextView RELOAD("reload");
  auto msg = static_cast<TSPluginMsg *>(data);
  if (TextView tag{msg->tag, strlen(msg->tag)} ; tag.starts_with_nocase(TAG)) {
    tag.remove_prefix(TAG.size());
    if (0 == strcasecmp(tag, RELOAD)) {
      ts::PerformAsTask(&Task_ConfigReload);
    }
  }
  return TS_SUCCESS;
}

Errata
TxnBoxInit() {
  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  Plugin_Config = std::make_shared<Config>();
  auto errata = Plugin_Config->load_args(G._args);
  if (!errata.is_ok()) {
    return errata;
  }

  if (TSPluginRegister(&info) == TS_SUCCESS) {
    TSCont cont{TSContCreate(CB_Txn_Start, nullptr)};
    TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
    G.reserve_txn_arg();
  } else {
    errata.error(R"({}: plugin registration failed.)", Config::PLUGIN_TAG);
    return errata;
  }
  return {};
}

void
TSPluginInit(int argc, char const *argv[]) {
  for ( int idx = 0 ; idx < argc ; ++idx ) {
    G._args.emplace_back(argv[idx]);
  }
  std::string err_str;
  if (! G._preload_errata.is_ok()) {
    swoc::bwprint(err_str, "{}: startup issues.\n{}", Config::PLUGIN_NAME, G._preload_errata);
    G._preload_errata.clear();
    TSError("%s", err_str.c_str());
  }
  auto errata { TxnBoxInit() };
  if (! errata.is_ok()) {
    swoc::bwprint(err_str, "{}: initialization failure.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  }
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(&CB_TxnBoxMsg, nullptr));
};
/* ------------------------------------------------------------------------ */
