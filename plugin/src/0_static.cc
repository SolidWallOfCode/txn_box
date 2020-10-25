/** @file Static data.
 *
 * Copyright 20120 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
 */

#include "txn_box/Modifier.h"
#include "txn_box/Config.h"

/// Directive definition.
Config::Factory Config::_factory;

/// Defined extractors.
Extractor::Table Extractor::_ex_table;

/// Static mapping from modifier to factory.
Modifier::Factory Modifier::_factory;
