/** @file
   Base plugin logic.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include <string>
#include <map>
#include <numeric>
#include <shared_mutex>

#include <swoc/TextView.h>
#include <swoc/bwf_std.h>

#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;
/* ------------------------------------------------------------------------------------ */

Global G;
extern std::string glob_to_rxp(TextView glob);

const std::string Config::GLOBAL_ROOT_KEY {"txn_box" };
const std::string Config::REMAP_ROOT_KEY { "." };

Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev) {
  static const std::map<TSEvent, Hook> table{
      { TS_EVENT_HTTP_TXN_START, Hook::TXN_START}
      , {TS_EVENT_HTTP_READ_REQUEST_HDR,  Hook::CREQ}
      , {TS_EVENT_HTTP_SEND_REQUEST_HDR,  Hook::PREQ}
      , {TS_EVENT_HTTP_READ_RESPONSE_HDR, Hook::URSP}
      , {TS_EVENT_HTTP_SEND_RESPONSE_HDR, Hook::PRSP}
      , {TS_EVENT_HTTP_PRE_REMAP, Hook::PRE_REMAP}
      , {TS_EVENT_HTTP_POST_REMAP, Hook::POST_REMAP}
      , { TS_EVENT_HTTP_TXN_CLOSE, Hook::TXN_CLOSE}
  };
  if (auto spot{table.find(ev)}; spot != table.end()) {
    return spot->second;
  }
  return Hook::INVALID;
}

namespace {

 std::shared_ptr<Config> Plugin_Config;
 std::shared_mutex Plugin_Config_Mutex; // safe updating of the shared ptr.
 std::atomic<bool> Plugin_Reloading = false;

 // Get a shared pointer to the configuration safely against updates.
 std::shared_ptr<Config> scoped_plugin_config() {
   std::shared_lock lock(Plugin_Config_Mutex);
   return Plugin_Config;
 }

 } // namespace
/* ------------------------------------------------------------------------------------ */
void Global::reserve_txn_arg() {
  if (G.TxnArgIdx < 0) {
    auto && [ idx, errata ] { ts::HttpTxn::reserve_arg(Config::GLOBAL_ROOT_KEY, "Transaction Box") };
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

void Task_ConfigReload() {
  std::shared_ptr cfg = std::make_shared<Config>();
  auto t0 = std::chrono::system_clock::now();
  auto errata = cfg->load_cli_args(G._args, 1);
  if (!errata.is_ok()) {
    std::string err_str;
    swoc::bwprint(err_str, "{}: Failed to reload configuration.\n{}", Config::PLUGIN_NAME, errata);
    TSError("%s", err_str.c_str());
  } else {
    std::unique_lock lock(Plugin_Config_Mutex);
    Plugin_Config = cfg;
  }
  Plugin_Reloading = false;
  auto delta = std::chrono::system_clock::now() - t0;
  std::string text;
  TSDebug(Config::PLUGIN_TAG.data(), "%s", swoc::bwprint(text, "{} files loaded in {} ms.", Plugin_Config->file_count(), std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()).c_str());
}

int CB_TxnBoxMsg(TSCont, TSEvent, void * data) {
  static constexpr TextView TAG {"txn_box."};
  static constexpr TextView RELOAD("reload");
  auto msg = static_cast<TSPluginMsg *>(data);
  if (TextView tag{msg->tag, strlen(msg->tag)} ; tag.starts_with_nocase(TAG)) {
    tag.remove_prefix(TAG.size());
    if (0 == strcasecmp(tag, RELOAD)) {
      bool expected = false;
      if (Plugin_Reloading.compare_exchange_strong(expected, true)) {
        ts::PerformAsTask(&Task_ConfigReload);
      } else {
        std::string err_str;
        swoc::bwprint(err_str, "{}: Reload requested while previous reload still active", Config::PLUGIN_NAME);
        TSError("%s", err_str.c_str());
      }
    }
  }
  return TS_SUCCESS;
}

Errata
TxnBoxInit() {
  TSPluginRegistrationInfo info{Config::PLUGIN_TAG.data(), "Verizon Media"
                                , "solidwallofcode@verizonmedia.com"};

  Plugin_Config = std::make_shared<Config>();
  auto t0 = std::chrono::system_clock::now();
  auto errata = Plugin_Config->load_cli_args(G._args, 1);
  if (!errata.is_ok()) {
    return errata;
  }
  auto delta = std::chrono::system_clock::now() - t0;
  std::string text;
  TSDebug(Config::PLUGIN_TAG.data(), "%s", swoc::bwprint(text, "{} files loaded in {} ms.", Plugin_Config->file_count(), std::chrono::duration_cast<std::chrono::milliseconds>(delta).count()).c_str());

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
  TSPluginDSOReloadEnable(false);
};
/* ------------------------------------------------------------------------ */
