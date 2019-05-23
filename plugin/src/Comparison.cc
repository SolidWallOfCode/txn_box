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

#include "txn_box/Comparison.h"

#include <swoc/Errata.h>
#include <yaml-cpp/yaml.h>

#include <string>

/// Exact string match.
class Cmp_Match : public StringComparison {
  using self_type = Cmp_Match;
  using super_type = StringComparison;
public:
  static swoc::Rv<Handle> load(YAML::Node node);
protected:
  std::string _value; ///< Valuie to match.
};
