#!/bin/bash

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

usage="`basename $0` <ats_src_dir> --ats-bin <ats_build_bin_dir> --proxy-verifier-bin <proxy_verifier_bin_dir> [other_autest_arguments]"

fail()
{
  echo -e $1
  exit 1
}

[ $# -lt 5 ] && fail "Usage:\n${usage}"
ats_src_dir=$1; shift
[ -d "${ats_src_dir}" ] || fail "First argument is not a directory. Usage:\n${usage}"

ats_test_dir="${ats_src_dir}/tests"
[ -d "${ats_test_dir}" ] || fail "${ats_test_dir} does not exist. Usage:\n${usage}"

ats_pipenv="${ats_test_dir}/Pipfile"
[ -d "${ats_test_dir}" ] || fail "${ats_pipenv} does not exist. Usage:\n${usage}"

# check for pipenv
pipenv --version &> /dev/null || fail "pipenv is not install/enabled."

if [ -f Pipfile ]
then
  if ! diff -q ${ats_pipenv} Pipfile 2>&1 > /dev/null
  then
    pipenv --rm
    rm Pipfile
  fi
fi
cp ${ats_test_dir}/Pipfile .
cp ${ats_test_dir}/test-env-check.sh .

pushd $(dirname $0) > /dev/null
export PYTHONPATH=$(pwd):$PYTHONPATH
./test-env-check.sh;
# this is for rhel or centos systems
echo "Environment config finished. Running AuTest..."
pipenv run autest -D gold_tests --autest-site ${ats_test_dir}/gold_tests/autest-site/ gold_tests/autest-site $@
ret=$?
popd > /dev/null
exit $ret
