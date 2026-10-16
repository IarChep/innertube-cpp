#!/bin/bash
# The comparison libraries, at the commits the spec's numbers were measured with.
set -e
cd "$(dirname "$0")"
mkdir -p ext
get() {   # get <dir> <github repo> <commit>
    [ -d ext/$1 ] && return
    git init -q ext/$1 && git -C ext/$1 fetch -q --depth 1 https://github.com/$2.git $3 && git -C ext/$1 checkout -q FETCH_HEAD
}
get yyjson ibireme/yyjson a84070fa6dc58f9b379ed75b8c8fab2c1da1b604
get simdjson simdjson/simdjson 9c003534b1be3bc4f1322bfa7a770a7ffbf247d9
get sonic-cpp bytedance/sonic-cpp c8db640739f08f400265ce3b76b2caeaed8da455
get rapidjson Tencent/rapidjson 24b5e7a8b27f42fa16b96fc70aade9106cf7102f
get JSONTestSuite nst/JSONTestSuite 1ef36fa01286573e846ac449e8683f8833c5b26a
