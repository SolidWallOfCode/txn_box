/** @file
 * YAML utilities.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string>

#include "txn_box/common.h"
#include "txn_box/yaml_util.h"
#include <swoc/bwf_std.h>

using swoc::TextView;
using namespace swoc::literals;
using swoc::Errata;
using swoc::Rv;

Rv<YAML::Node> yaml_load(swoc::file::path const& path) {
  std::error_code ec;
  std::string content = swoc::file::load(path, ec);

  if (ec) {
    return Error(R"(Unable to load file "{}" - {}.)", path, ec);
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (std::exception &ex) {
    return Error(R"(YAML parsing of "{}" failed - {}.)", path, ex.what());
  }

  yaml_merge(root);
  return root;
}
