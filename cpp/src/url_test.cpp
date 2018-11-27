/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <catch.hpp>
#include <proton/url.hpp>

namespace {
using proton::url;

#define CHECK_URL(U, SCHEME, USER, PWD, HOST, PORT, PATH)                      \
    do {                                                                       \
        CHECK((SCHEME) == (U).scheme());                                       \
        CHECK((USER) == (U).user());                                           \
        CHECK((PWD) == (U).password());                                        \
        CHECK((HOST) == (U).host());                                           \
        CHECK((PORT) == (U).port());                                           \
        CHECK((PATH) == (U).path());                                           \
    } while (0)

TEST_CASE("parse URL", "[url]") {
    SECTION("full and defaulted") {
        CHECK_URL(url("amqp://foo:xyz/path"), "amqp", "", "", "foo", "xyz",
                  "path");
        CHECK_URL(url("amqp://username:password@host:1234/path"), "amqp",
                  "username", "password", "host", "1234", "path");
        CHECK_URL(url("host:1234"), "amqp", "", "", "host", "1234", "");
        CHECK_URL(url("host"), "amqp", "", "", "host", "amqp", "");
        CHECK_URL(url("host/path"), "amqp", "", "", "host", "amqp", "path");
        CHECK_URL(url("amqps://host"), "amqps", "", "", "host", "amqps", "");
        CHECK_URL(url("/path"), "amqp", "", "", "localhost", "amqp", "path");
        CHECK_URL(url(""), "amqp", "", "", "localhost", "amqp", "");
        CHECK_URL(url(":1234"), "amqp", "", "", "localhost", "1234", "");
    }
    SECTION("starting with //") {
        CHECK_URL(url("//username:password@host:1234/path"), "amqp", "username",
                  "password", "host", "1234", "path");
        CHECK_URL(url("//host:port/path"), "amqp", "", "", "host", "port",
                  "path");
        CHECK_URL(url("//host"), "amqp", "", "", "host", "amqp", "");
        CHECK_URL(url("//:port"), "amqp", "", "", "localhost", "port", "");
        CHECK_URL(url("//:0"), "amqp", "", "", "localhost", "0", "");
    }
    SECTION("no defaults") {
        CHECK_URL(url("", false), "", "", "", "", "", "");
        CHECK_URL(url("//:", false), "", "", "", "", "", "");
        CHECK_URL(url("//:0", false), "", "", "", "", "0", "");
        CHECK_URL(url("//h:", false), "", "", "", "h", "", "");
    }
}

} // namespace
