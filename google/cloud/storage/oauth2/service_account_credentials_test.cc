// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/storage/oauth2/service_account_credentials.h"
#include "google/cloud/internal/random.h"
#include "google/cloud/internal/setenv.h"
#include "google/cloud/storage/internal/nljson.h"
#include "google/cloud/storage/oauth2/credential_constants.h"
#include "google/cloud/storage/oauth2/google_credentials.h"
#include "google/cloud/storage/testing/mock_fake_clock.h"
#include "google/cloud/storage/testing/mock_http_request.h"
#include "google/cloud/testing_util/assert_ok.h"
#include <gmock/gmock.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace oauth2 {
namespace {

using ::google::cloud::storage::internal::HttpResponse;
using ::google::cloud::storage::testing::FakeClock;
using ::google::cloud::storage::testing::MockHttpRequest;
using ::google::cloud::storage::testing::MockHttpRequestBuilder;
using ::testing::_;
using ::testing::An;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StartsWith;
using ::testing::StrEq;

constexpr char kAltScopeForTest[] =
    "https://www.googleapis.com/auth/devstorage.full_control";
// This "magic" assertion below was generated from helper script,
// "make_jwt_assertion_for_test_data.py". Note that when our JSON library dumps
// a string representation, the keys are always in alphabetical order; our
// helper script also takes special care to ensure Python dicts are dumped in
// this manner, as dumping the keys in a different order would result in a
// different Base64-encoded string, and thus a different assertion string.
constexpr char kExpectedAssertionParam[] =
    R"""(assertion=eyJhbGciOiJSUzI1NiIsImtpZCI6ImExYTExMWFhMTExMWExMWExMWExMWFhMTExYTExMWExYTExMTExMTEiLCJ0eXAiOiJKV1QifQ.eyJhdWQiOiJodHRwczovL29hdXRoMi5nb29nbGVhcGlzLmNvbS90b2tlbiIsImV4cCI6MTUzMDA2MzkyNCwiaWF0IjoxNTMwMDYwMzI0LCJpc3MiOiJmb28tZW1haWxAZm9vLXByb2plY3QuaWFtLmdzZXJ2aWNlYWNjb3VudC5jb20iLCJzY29wZSI6Imh0dHBzOi8vd3d3Lmdvb2dsZWFwaXMuY29tL2F1dGgvY2xvdWQtcGxhdGZvcm0ifQ.OtL40PSxdAB9rxRkXj-UeyuMhQCoT10WJY4ccOrPXriwm-DRl5AMgbBkQvVmWeYuPMTiFKWz_CMMBjVc3lFPW015eHvKT5r3ySGra1i8hJ9cDsWO7SdIGB-l00G-BdRxVEhN8U4C20eUhlvhtjXemOwlCFrKjF22rJB-ChiKy84rXs3O-Hz0dWmsSZPfVD9q-2S2vJdr9vz7NoP-fCmpxhQ3POVocYb-2OEM5c4Uo_e7lQTX3bRtVc19wz_wrTu9wMMMRYt52K8WPoWPURt7qpjHX88_EitXMzH-cJUQoDsgIoZ6vDlQMs7_nqNfgrlsGWHpPoSoGgvJMg1vJbzVLw)""";
// This "magic" assertion is generated in a similar manner, but specifies a
// non-default scope set and subject string (values used can be found in the
// kAltScopeForTest and kSubjectForGrant variables).
constexpr char kExpectedAssertionWithOptionalArgsParam[] =
    R"""(assertion=eyJhbGciOiJSUzI1NiIsImtpZCI6ImExYTExMWFhMTExMWExMWExMWExMWFhMTExYTExMWExYTExMTExMTEiLCJ0eXAiOiJKV1QifQ.eyJhdWQiOiJodHRwczovL29hdXRoMi5nb29nbGVhcGlzLmNvbS90b2tlbiIsImV4cCI6MTUzMDA2MzkyNCwiaWF0IjoxNTMwMDYwMzI0LCJpc3MiOiJmb28tZW1haWxAZm9vLXByb2plY3QuaWFtLmdzZXJ2aWNlYWNjb3VudC5jb20iLCJzY29wZSI6Imh0dHBzOi8vd3d3Lmdvb2dsZWFwaXMuY29tL2F1dGgvZGV2c3RvcmFnZS5mdWxsX2NvbnRyb2wiLCJzdWIiOiJ1c2VyQGZvby5iYXIifQ.D2sZntI1C0yF3LE3R0mssmidj8e9m5VU6UwzIUvDIG6yAxQLDRWK_gEdPW7etJ1xklIDwPEk0WgEsiu9pP89caPig0nK-bih7f1vbpRBTx4Vke07roW3DpFCLXFgaEXhKJYbzoYOJ62H_oBbQISC9qSF841sqEHmbjOqj5rSAR43wJm9H9juDT8apGpDNVCJM5pSo99NprLCvxUXuCBnacEsSQwbbZlLHfmBdyrllJsumx8RgFd22laEHsgPAMTxP-oM2iyf3fBEs2s1Dj7GxdWdpG6D9abJA6Hs8H1HqSwwyEWTXH6v_SPMYGsN1hIMTAWbO7J11bdHdjxo0hO5CA)""";
constexpr long int kFixedJwtTimestamp = 1530060324;
constexpr char kGrantParamUnescaped[] =
    "urn:ietf:params:oauth:grant-type:jwt-bearer";
constexpr char kGrantParamEscaped[] =
    "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer";
constexpr char kJsonKeyfileContents[] = R"""({
      "type": "service_account",
      "project_id": "foo-project",
      "private_key_id": "a1a111aa1111a11a11a11aa111a111a1a1111111",
      "private_key": "-----BEGIN PRIVATE KEY-----\nMIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCltiF2oP3KJJ+S\ntTc1McylY+TuAi3AdohX7mmqIjd8a3eBYDHs7FlnUrFC4CRijCr0rUqYfg2pmk4a\n6TaKbQRAhWDJ7XD931g7EBvCtd8+JQBNWVKnP9ByJUaO0hWVniM50KTsWtyX3up/\nfS0W2R8Cyx4yvasE8QHH8gnNGtr94iiORDC7De2BwHi/iU8FxMVJAIyDLNfyk0hN\neheYKfIDBgJV2v6VaCOGWaZyEuD0FJ6wFeLybFBwibrLIBE5Y/StCrZoVZ5LocFP\nT4o8kT7bU6yonudSCyNMedYmqHj/iF8B2UN1WrYx8zvoDqZk0nxIglmEYKn/6U7U\ngyETGcW9AgMBAAECggEAC231vmkpwA7JG9UYbviVmSW79UecsLzsOAZnbtbn1VLT\nPg7sup7tprD/LXHoyIxK7S/jqINvPU65iuUhgCg3Rhz8+UiBhd0pCH/arlIdiPuD\n2xHpX8RIxAq6pGCsoPJ0kwkHSw8UTnxPV8ZCPSRyHV71oQHQgSl/WjNhRi6PQroB\nSqc/pS1m09cTwyKQIopBBVayRzmI2BtBxyhQp9I8t5b7PYkEZDQlbdq0j5Xipoov\n9EW0+Zvkh1FGNig8IJ9Wp+SZi3rd7KLpkyKPY7BK/g0nXBkDxn019cET0SdJOHQG\nDiHiv4yTRsDCHZhtEbAMKZEpku4WxtQ+JjR31l8ueQKBgQDkO2oC8gi6vQDcx/CX\nZ23x2ZUyar6i0BQ8eJFAEN+IiUapEeCVazuxJSt4RjYfwSa/p117jdZGEWD0GxMC\n+iAXlc5LlrrWs4MWUc0AHTgXna28/vii3ltcsI0AjWMqaybhBTTNbMFa2/fV2OX2\nUimuFyBWbzVc3Zb9KAG4Y7OmJQKBgQC5324IjXPq5oH8UWZTdJPuO2cgRsvKmR/r\n9zl4loRjkS7FiOMfzAgUiXfH9XCnvwXMqJpuMw2PEUjUT+OyWjJONEK4qGFJkbN5\n3ykc7p5V7iPPc7Zxj4mFvJ1xjkcj+i5LY8Me+gL5mGIrJ2j8hbuv7f+PWIauyjnp\nNx/0GVFRuQKBgGNT4D1L7LSokPmFIpYh811wHliE0Fa3TDdNGZnSPhaD9/aYyy78\nLkxYKuT7WY7UVvLN+gdNoVV5NsLGDa4cAV+CWPfYr5PFKGXMT/Wewcy1WOmJ5des\nAgMC6zq0TdYmMBN6WpKUpEnQtbmh3eMnuvADLJWxbH3wCkg+4xDGg2bpAoGAYRNk\nMGtQQzqoYNNSkfus1xuHPMA8508Z8O9pwKU795R3zQs1NAInpjI1sOVrNPD7Ymwc\nW7mmNzZbxycCUL/yzg1VW4P1a6sBBYGbw1SMtWxun4ZbnuvMc2CTCh+43/1l+FHe\nMmt46kq/2rH2jwx5feTbOE6P6PINVNRJh/9BDWECgYEAsCWcH9D3cI/QDeLG1ao7\nrE2NcknP8N783edM07Z/zxWsIsXhBPY3gjHVz2LDl+QHgPWhGML62M0ja/6SsJW3\nYvLLIc82V7eqcVJTZtaFkuht68qu/Jn1ezbzJMJ4YXDYo1+KFi+2CAGR06QILb+I\nlUtj+/nH3HDQjM4ltYfTPUg=\n-----END PRIVATE KEY-----\n",
      "client_email": "foo-email@foo-project.iam.gserviceaccount.com",
      "client_id": "100000000000000000001",
      "auth_uri": "https://accounts.google.com/o/oauth2/auth",
      "token_uri": "https://oauth2.googleapis.com/token",
      "auth_provider_x509_cert_url": "https://www.googleapis.com/oauth2/v1/certs",
      "client_x509_cert_url": "https://www.googleapis.com/robot/v1/metadata/x509/foo-email%40foo-project.iam.gserviceaccount.com"
})""";
constexpr char kSubjectForGrant[] = "user@foo.bar";

class ServiceAccountCredentialsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockHttpRequestBuilder::mock =
        std::make_shared<MockHttpRequestBuilder::Impl>();
    FakeClock::reset_clock(kFixedJwtTimestamp);
  }
  void TearDown() override { MockHttpRequestBuilder::mock.reset(); }

  std::string CreateRandomFileName() {
    // When running on the internal Google CI systems we cannot write to the
    // local directory, GTest has a good temporary directory in that case.
    return ::testing::TempDir() +
           google::cloud::internal::Sample(
               generator_, 8, "abcdefghijklmnopqrstuvwxyz0123456789");
  }

  google::cloud::internal::DefaultPRNG generator_ =
      google::cloud::internal::MakeDefaultPRNG();
};

void CheckInfoYieldsExpectedAssertion(ServiceAccountCredentialsInfo const& info,
                                      std::string const& assertion) {
  auto mock_request = std::make_shared<MockHttpRequest::Impl>();
  std::string response = R"""({
      "token_type": "Type",
      "access_token": "access-token-value",
      "expires_in": 1234
  })""";
  EXPECT_CALL(*mock_request, MakeRequest(_))
      .WillOnce(Invoke([response, assertion](std::string const& payload) {
        EXPECT_THAT(payload, HasSubstr(assertion));
        // Hard-coded in this order in ServiceAccountCredentials class.
        EXPECT_THAT(payload,
                    HasSubstr(std::string("grant_type=") + kGrantParamEscaped));
        return HttpResponse{200, response, {}};
      }));

  auto mock_builder = MockHttpRequestBuilder::mock;
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([mock_request] {
    MockHttpRequest result;
    result.mock = mock_request;
    return result;
  }));

  std::string expected_header =
      "Content-Type: application/x-www-form-urlencoded";
  EXPECT_CALL(*mock_builder, AddHeader(StrEq(expected_header)));
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));

  ServiceAccountCredentials<MockHttpRequestBuilder, FakeClock> credentials(
      info);
  // Calls Refresh to obtain the access token for our authorization header.
  EXPECT_EQ("Authorization: Type access-token-value",
            credentials.AuthorizationHeader().value());
}

/// @test Verify that we can create service account credentials from a keyfile.
TEST_F(ServiceAccountCredentialsTest,
       RefreshingSendsCorrectRequestBodyAndParsesResponse) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  CheckInfoYieldsExpectedAssertion(*info, kExpectedAssertionParam);
}

/// @test Verify that we can create service account credentials from a keyfile.
TEST_F(ServiceAccountCredentialsTest,
       RefreshingSendsCorrectRequestBodyAndParsesResponseForNonDefaultVals) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  info->scopes = {kAltScopeForTest};
  info->subject = std::string(kSubjectForGrant);
  CheckInfoYieldsExpectedAssertion(*info,
                                   kExpectedAssertionWithOptionalArgsParam);
}

/// @test Verify that we refresh service account credentials appropriately.
TEST_F(ServiceAccountCredentialsTest,
       RefreshCalledOnlyWhenAccessTokenIsMissingOrInvalid) {
  // Prepare two responses, the first one is used but becomes immediately
  // expired, resulting in another refresh next time the caller tries to get
  // an authorization header.
  std::string r1 = R"""({
    "token_type": "Type",
    "access_token": "access-token-r1",
    "expires_in": 0
})""";
  std::string r2 = R"""({
    "token_type": "Type",
    "access_token": "access-token-r2",
    "expires_in": 1000
})""";
  auto mock_request = std::make_shared<MockHttpRequest::Impl>();
  EXPECT_CALL(*mock_request, MakeRequest(_))
      .WillOnce(Return(HttpResponse{200, r1, {}}))
      .WillOnce(Return(HttpResponse{200, r2, {}}));

  // Now setup the builder to return those responses.
  auto mock_builder = MockHttpRequestBuilder::mock;
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([mock_request] {
    MockHttpRequest request;
    request.mock = mock_request;
    return request;
  }));
  EXPECT_CALL(*mock_builder, AddHeader(An<std::string const&>())).Times(1);
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));

  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  ServiceAccountCredentials<MockHttpRequestBuilder> credentials(*info);
  // Calls Refresh to obtain the access token for our authorization header.
  EXPECT_EQ("Authorization: Type access-token-r1",
            credentials.AuthorizationHeader().value());
  // Token is expired, resulting in another call to Refresh.
  EXPECT_EQ("Authorization: Type access-token-r2",
            credentials.AuthorizationHeader().value());
  // Token still valid; should return cached token instead of calling Refresh.
  EXPECT_EQ("Authorization: Type access-token-r2",
            credentials.AuthorizationHeader().value());
}

/// @test Verify that nl::json::parse() failures are reported as is_discarded.
TEST_F(ServiceAccountCredentialsTest, JsonParsingFailure) {
  std::string config = R"""( not-a-valid-json-string )""";
  // The documentation for nl::json::parse() is a bit ambiguous, so wrote a
  // little test to verify it works as I expected.
  internal::nl::json parsed = internal::nl::json::parse(config, nullptr, false);
  EXPECT_TRUE(parsed.is_discarded());
  EXPECT_FALSE(parsed.is_null());
}

/// @test Verify that parsing a service account JSON string works.
TEST_F(ServiceAccountCredentialsTest, ParseSimple) {
  std::string contents = R"""({
      "type": "service_account",
      "private_key_id": "not-a-key-id-just-for-testing",
      "private_key": "not-a-valid-key-just-for-testing",
      "client_email": "test-only@test-group.example.com",
      "token_uri": "https://oauth2.googleapis.com/test_endpoint"
})""";

  auto actual =
      ParseServiceAccountCredentials(contents, "test-data", "unused-uri");
  ASSERT_STATUS_OK(actual);
  EXPECT_EQ("not-a-key-id-just-for-testing", actual->private_key_id);
  EXPECT_EQ("not-a-valid-key-just-for-testing", actual->private_key);
  EXPECT_EQ("test-only@test-group.example.com", actual->client_email);
  EXPECT_EQ("https://oauth2.googleapis.com/test_endpoint", actual->token_uri);
}

/// @test Verify that parsing a service account JSON string works.
TEST_F(ServiceAccountCredentialsTest, ParseUsesExplicitDefaultTokenUri) {
  // No token_uri attribute here, so the default passed below should be used.
  std::string contents = R"""({
      "type": "service_account",
      "private_key_id": "not-a-key-id-just-for-testing",
      "private_key": "not-a-valid-key-just-for-testing",
      "client_email": "test-only@test-group.example.com"
})""";

  auto actual = ParseServiceAccountCredentials(
      contents, "test-data", "https://oauth2.googleapis.com/test_endpoint");
  ASSERT_STATUS_OK(actual);
  EXPECT_EQ("not-a-key-id-just-for-testing", actual->private_key_id);
  EXPECT_EQ("not-a-valid-key-just-for-testing", actual->private_key);
  EXPECT_EQ("test-only@test-group.example.com", actual->client_email);
  EXPECT_EQ("https://oauth2.googleapis.com/test_endpoint", actual->token_uri);
}

/// @test Verify that parsing a service account JSON string works.
TEST_F(ServiceAccountCredentialsTest, ParseUsesImplicitDefaultTokenUri) {
  // No token_uri attribute here.
  std::string contents = R"""({
      "type": "service_account",
      "private_key_id": "not-a-key-id-just-for-testing",
      "private_key": "not-a-valid-key-just-for-testing",
      "client_email": "test-only@test-group.example.com"
})""";

  // No token_uri passed in here, either.
  auto actual = ParseServiceAccountCredentials(contents, "test-data");
  ASSERT_STATUS_OK(actual);
  EXPECT_EQ("not-a-key-id-just-for-testing", actual->private_key_id);
  EXPECT_EQ("not-a-valid-key-just-for-testing", actual->private_key);
  EXPECT_EQ("test-only@test-group.example.com", actual->client_email);
  EXPECT_EQ(std::string(GoogleOAuthRefreshEndpoint()), actual->token_uri);
}

/// @test Verify that invalid contents result in a readable error.
TEST_F(ServiceAccountCredentialsTest, ParseInvalidContentsFails) {
  std::string config = R"""( not-a-valid-json-string )""";

  auto actual = ParseServiceAccountCredentials(config, "test-as-a-source");
  ASSERT_FALSE(actual) << "status=" << actual.status();
  EXPECT_THAT(actual.status().message(),
              HasSubstr("Invalid ServiceAccountCredentials"));
  EXPECT_THAT(actual.status().message(), HasSubstr("test-as-a-source"));
}

/// @test Parsing a service account JSON string should detect empty fields.
TEST_F(ServiceAccountCredentialsTest, ParseEmptyFieldFails) {
  std::string contents = R"""({
      "type": "service_account",
      "private_key_id": "not-a-key-id-just-for-testing",
      "private_key": "not-a-valid-key-just-for-testing",
      "client_email": "test-only@test-group.example.com",
      "token_uri": "https://oauth2.googleapis.com/token"
})""";

  for (auto const& field :
       {"private_key_id", "private_key", "client_email", "token_uri"}) {
    internal::nl::json json = internal::nl::json::parse(contents);
    json[field] = "";
    auto actual = ParseServiceAccountCredentials(json.dump(), "test-data", "");
    ASSERT_FALSE(actual) << "status=" << actual.status();
    EXPECT_THAT(actual.status().message(), HasSubstr(field));
    EXPECT_THAT(actual.status().message(), HasSubstr(" field is empty"));
    EXPECT_THAT(actual.status().message(), HasSubstr("test-data"));
  }
}

/// @test Parsing a service account JSON string should detect missing fields.
TEST_F(ServiceAccountCredentialsTest, ParseMissingFieldFails) {
  std::string contents = R"""({
      "type": "service_account",
      "private_key_id": "not-a-key-id-just-for-testing",
      "private_key": "not-a-valid-key-just-for-testing",
      "client_email": "test-only@test-group.example.com",
      "token_uri": "https://oauth2.googleapis.com/token"
})""";

  for (auto const& field : {"private_key_id", "private_key", "client_email"}) {
    internal::nl::json json = internal::nl::json::parse(contents);
    json.erase(field);
    auto actual = ParseServiceAccountCredentials(json.dump(), "test-data", "");
    ASSERT_FALSE(actual) << "status=" << actual.status();
    EXPECT_THAT(actual.status().message(), HasSubstr(field));
    EXPECT_THAT(actual.status().message(), HasSubstr(" field is missing"));
    EXPECT_THAT(actual.status().message(), HasSubstr("test-data"));
  }
}

/// @test Verify that refreshing a credential updates the timestamps.
TEST_F(ServiceAccountCredentialsTest, RefreshingUpdatesTimestamps) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);

  auto make_request_assertion = [&info](long timestamp) {
    return [timestamp, &info](std::string const& p) {
      std::string const prefix =
          std::string("grant_type=") + kGrantParamEscaped + "&assertion=";
      EXPECT_THAT(p, StartsWith(prefix));

      std::string assertion = p.substr(prefix.size());

      std::istringstream is(assertion);
      std::string encoded_header;
      std::getline(is, encoded_header, '.');
      std::string encoded_payload;
      std::getline(is, encoded_payload, '.');

      auto header_bytes = internal::UrlsafeBase64Decode(encoded_header);
      std::string header_str{header_bytes.begin(), header_bytes.end()};
      auto payload_bytes = internal::UrlsafeBase64Decode(encoded_payload);
      std::string payload_str{payload_bytes.begin(), payload_bytes.end()};

      auto header = internal::nl::json::parse(header_str);
      EXPECT_EQ("RS256", header.value("alg", ""));
      EXPECT_EQ("JWT", header.value("typ", ""));
      EXPECT_EQ(info->private_key_id, header.value("kid", ""));

      auto payload = internal::nl::json::parse(payload_str);
      EXPECT_EQ(timestamp, payload.value("iat", 0));
      EXPECT_EQ(timestamp + 3600, payload.value("exp", 0));
      EXPECT_EQ(info->client_email, payload.value("iss", ""));
      EXPECT_EQ(info->token_uri, payload.value("aud", ""));

      // Hard-coded in this order in ServiceAccountCredentials class.
      std::string token = "mock-token-value-" + std::to_string(timestamp);
      internal::nl::json response{{"token_type", "Mock-Type"},
                                  {"access_token", token},
                                  {"expires_in", 3600}};
      return HttpResponse{200, response.dump(), {}};
    };
  };

  // Setup the mock request / response for the first Refresh().
  auto mock_request = std::make_shared<MockHttpRequest::Impl>();
  auto const clock_value_1 = 10000;
  auto const clock_value_2 = 20000;
  EXPECT_CALL(*mock_request, MakeRequest(_))
      .WillOnce(Invoke(make_request_assertion(clock_value_1)))
      .WillOnce(Invoke(make_request_assertion(clock_value_2)));

  auto mock_builder = MockHttpRequestBuilder::mock;
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([mock_request] {
    MockHttpRequest result;
    result.mock = mock_request;
    return result;
  }));

  std::string expected_header =
      "Content-Type: application/x-www-form-urlencoded";
  EXPECT_CALL(*mock_builder, AddHeader(StrEq(expected_header))).Times(1);
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));

  FakeClock::now_value = clock_value_1;
  ServiceAccountCredentials<MockHttpRequestBuilder, FakeClock> credentials(
      *info);
  // Call Refresh to obtain the access token for our authorization header.
  auto authorization_header = credentials.AuthorizationHeader();
  ASSERT_STATUS_OK(authorization_header);
  EXPECT_EQ("Authorization: Mock-Type mock-token-value-10000",
            *authorization_header);

  // Advance the clock past the expiration time of the token and then get a
  // new header.
  FakeClock::now_value = clock_value_2;
  EXPECT_GT(clock_value_2 - clock_value_1, 2 * 3600);
  authorization_header = credentials.AuthorizationHeader();
  ASSERT_STATUS_OK(authorization_header);
  EXPECT_EQ("Authorization: Mock-Type mock-token-value-20000",
            *authorization_header);
}

/// @test Verify that we can create sign blobs using a service account.
TEST_F(ServiceAccountCredentialsTest, SignBlob) {
  auto mock_builder = MockHttpRequestBuilder::mock;
  std::string expected_header =
      "Content-Type: application/x-www-form-urlencoded";
  EXPECT_CALL(*mock_builder, AddHeader(StrEq(expected_header)));
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([] {
    return MockHttpRequest();
  }));

  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  ServiceAccountCredentials<MockHttpRequestBuilder, FakeClock> credentials(
      *info);

  std::string blob = R"""(GET
rmYdCNHKFXam78uCt7xQLw==
text/plain
1388534400
x-goog-encryption-algorithm:AES256
x-goog-meta-foo:bar,baz
/bucket/objectname)""";

  auto actual = credentials.SignBlob(SigningAccount(), blob);
  ASSERT_STATUS_OK(actual);

  // To generate the expected output I used:
  //   openssl dgst -sha256 -sign private.pem blob.txt | openssl base64 -A
  // where `blob.txt` contains the `blob` string, and `private.pem` contains
  // the private key embedded in `kJsonKeyfileContents`.
  std::string expected_signed =
      "Zsy8o5ci07DQTvO/"
      "SVr47PKsCXvN+"
      "FzXga0iYrReAnngdZYewHdcAnMQ8bZvFlTM8HY3msrRw64Jc6hoXVL979An5ugXoZ1ol/"
      "DT1KlKp3l9E0JSIbqL88ogpElTxFvgPHOtHOUsy2mzhqOVrNSXSj4EM50gKHhvHKSbFq8Pcj"
      "lAkROtq5gqp5t0OFd7EMIaRH+tekVUZjQPfFT/"
      "hRW9bSCCV8w1Ex+"
      "QxmB5z7P7zZn2pl7JAcL850emTo8f2tfv1xXWQGhACvIJeMdPmyjbc04Ye4M8Ljpkg3YhE6l"
      "4GwC2MnI8TkuoHe4Bj2MvA8mM8TVwIvpBs6Etsj6Jdaz4rg==";
  EXPECT_EQ(expected_signed, internal::Base64Encode(*actual));
}

/// @test Verify that signing blobs fails with invalid e-mail.
TEST_F(ServiceAccountCredentialsTest, SignBlobFailure) {
  auto mock_builder = MockHttpRequestBuilder::mock;
  std::string expected_header =
      "Content-Type: application/x-www-form-urlencoded";
  EXPECT_CALL(*mock_builder, AddHeader(StrEq(expected_header)));
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([] {
    return MockHttpRequest();
  }));

  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  ServiceAccountCredentials<MockHttpRequestBuilder, FakeClock> credentials(
      *info);

  auto actual =
      credentials.SignBlob(SigningAccount("fake@fake.com"), "test-blob");
  EXPECT_FALSE(actual);
  EXPECT_EQ(actual.status().code(), StatusCode::kInvalidArgument);
  EXPECT_THAT(
      actual.status().message(),
      ::testing::HasSubstr("The current_credentials cannot sign blobs for "));
}

/// @test Verify that we can get the client id from a service account.
TEST_F(ServiceAccountCredentialsTest, ClientId) {
  auto mock_builder = MockHttpRequestBuilder::mock;
  std::string expected_header =
      "Content-Type: application/x-www-form-urlencoded";
  EXPECT_CALL(*mock_builder, AddHeader(StrEq(expected_header)));
  EXPECT_CALL(*mock_builder, Constructor(GoogleOAuthRefreshEndpoint()))
      .Times(1);
  EXPECT_CALL(*mock_builder, MakeEscapedString(An<std::string const&>()))
      .WillRepeatedly(
          Invoke([](std::string const& s) -> std::unique_ptr<char[]> {
            EXPECT_EQ(kGrantParamUnescaped, s);
            auto t =
                std::unique_ptr<char[]>(new char[sizeof(kGrantParamEscaped)]);
            std::copy(kGrantParamEscaped,
                      kGrantParamEscaped + sizeof(kGrantParamEscaped), t.get());
            return t;
          }));
  EXPECT_CALL(*mock_builder, BuildRequest()).WillOnce(Invoke([] {
    return MockHttpRequest();
  }));

  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  ServiceAccountCredentials<MockHttpRequestBuilder, FakeClock> credentials(
      *info);

  EXPECT_EQ("foo-email@foo-project.iam.gserviceaccount.com",
            credentials.AccountEmail());
  EXPECT_EQ("a1a111aa1111a11a11a11aa111a111a1a1111111", credentials.KeyId());
}

// This is a base64-encoded p12 key-file. The service account was deleted
// after creating the key-file, so the key was effectively invalidated, but
// the format is correct, so we can use it to verify that p12 key-files can be
// loaded.
//
// If you want to change the file (for whatever reason), these commands are
// helpful:
//    Generate a new key:
//      gcloud iam service-accounts keys create /dev/shm/key.p12
//          --key-file-type=p12 --iam-account=${SERVICE_ACCOUNT}
//    Base64 encode the key (then cut&paste) here:
//      openssl base64 -e < /dev/shm/key.p12
//    Find the service account ID:
//      openssl pkcs12 -in /dev/shm/key.p12
//          -passin pass:notasecret  -nodes -info  | grep =CN
//    Delete the service account ID:
//      gcloud iam service-accounts delete --quiet ${SERVICE_ACCOUNT}
char const kP12ServiceAccountId[] = "104849618361176160538";
char const kP12KeyFileContents[] =
    "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";

char const kP12KeyFileMissingCerts[] =
    "MIIDzAIBAzCCA5IGCSqGSIb3DQEHAaCCA4MEggN/MIIDezCCA3cGCSqGSIb3DQEH"
    "BqCCA2gwggNkAgEAMIIDXQYJKoZIhvcNAQcBMBwGCiqGSIb3DQEMAQYwDgQILaGB"
    "fWhJ2V0CAggAgIIDMM5EI/ck4VQD4JyGchVPbgd5HQjFbn+HThIoxBYpMPEK+iT7"
    "t32idiirDi0qH+6nZancp69nnKhjpAOnMLSjCvba7HDFzi/op7fgf9hnwupEOahv"
    "4b8Wv0S9ePTqsLfJy8tJzOAPYKOJO7HGSeZanWh2HpyCd2g1K1dBXsqsabTtJBsF"
    "TSGsfUg08/SMT5o12BlMk/wjzUrcSNQxntyPXLfjO1uZ0gFjFO6xsFyclVWr8Zax"
    "7fTA6SLdgeE1Iu2+mS1ohwNNzeBrCU6kXVzgw1GSn0UV0ZGbANRWDZZThWzQs9UW"
    "sn8l1fr70OZ4JhUwPZe9g0Tu7EeGNPkM5dW1Lr3izKNtYdInBD/1J7wGxsmomsU3"
    "khIH2FMqqYX7NFkI0TZiHpLYk2bQmMnfFbBDlXluzO2iLvBY5FPUCn5W4ZPAJlFs"
    "Ryo/OytciwJUIRoz76CIg3TmzM1b+RLBMEr6lAsD1za3fcTMwbsBeYY0FEFfb/I6"
    "ddmJTxjbCLPLekgkV7MIFSWPiL4t2eXR3rlu1Vnoys0aTWmFtJhEOI16Q1bkJ9L1"
    "c/KXHm/Srccm8hTazNYQewHRXWiAvigg6slRnx1I36Z0TMbnikDVCRH8cjFsMKO5"
    "/qNMKSsZ6EAePHYAu4N5CpqaTl0hjHI8sW+CDzzmGOn8Acb00gJ+DOu+wiTZtJYS"
    "GIZogs7PluMJ7cU1Ju38OixWbQDvfDdloQ/7kZrM6DoEKhvC2bwMwlfxin9jUwjJ"
    "98dtdAwQVgckvnYYVpqKnn/dlkiStaiZFKx27kw6o2oobcDrkg0wtOZFeX8k0SXZ"
    "ekcmMc5Xfl+5HyJxH5ni8UmHyOHAM8dNjpnzCD9J2K0U7z8kdzslZ95X5MAxYIUa"
    "r50tIaWHxeLLYYZUi+nyjNbMZ+yvAqOjQqI1mIcYZurHRPRIHVi2x4nfcKKQIkxn"
    "UTF9d3VWbkWoJ1qfe0OSpWg4RrdgDCSB1BlF0gQHEsDTT5/xoZIEoUV8t6TYTVCe"
    "axreBYxLhvROONz94v6GD6Eb4kakbSObn8NuBiWnaPevFyEF5YluKR87MbZRQY0Z"
    "yJ/4PuEhDIioRdY7ujAxMCEwCQYFKw4DAhoFAAQU4/UMFJQGUvgPuTXRKp0gVU4B"
    "GbkECPTYJIica3DWAgIIAA==";

char const kP12KeyFileMissingKey[] =
    "MIIDzAIBAzCCA5IGCSqGSIb3DQEHAaCCA4MEggN/MIIDezCCA3cGCSqGSIb3DQEH"
    "BqCCA2gwggNkAgEAMIIDXQYJKoZIhvcNAQcBMBwGCiqGSIb3DQEMAQYwDgQILaGB"
    "fWhJ2V0CAggAgIIDMM5EI/ck4VQD4JyGchVPbgd5HQjFbn+HThIoxBYpMPEK+iT7"
    "t32idiirDi0qH+6nZancp69nnKhjpAOnMLSjCvba7HDFzi/op7fgf9hnwupEOahv"
    "4b8Wv0S9ePTqsLfJy8tJzOAPYKOJO7HGSeZanWh2HpyCd2g1K1dBXsqsabTtJBsF"
    "TSGsfUg08/SMT5o12BlMk/wjzUrcSNQxntyPXLfjO1uZ0gFjFO6xsFyclVWr8Zax"
    "7fTA6SLdgeE1Iu2+mS1ohwNNzeBrCU6kXVzgw1GSn0UV0ZGbANRWDZZThWzQs9UW"
    "sn8l1fr70OZ4JhUwPZe9g0Tu7EeGNPkM5dW1Lr3izKNtYdInBD/1J7wGxsmomsU3"
    "khIH2FMqqYX7NFkI0TZiHpLYk2bQmMnfFbBDlXluzO2iLvBY5FPUCn5W4ZPAJlFs"
    "Ryo/OytciwJUIRoz76CIg3TmzM1b+RLBMEr6lAsD1za3fcTMwbsBeYY0FEFfb/I6"
    "ddmJTxjbCLPLekgkV7MIFSWPiL4t2eXR3rlu1Vnoys0aTWmFtJhEOI16Q1bkJ9L1"
    "c/KXHm/Srccm8hTazNYQewHRXWiAvigg6slRnx1I36Z0TMbnikDVCRH8cjFsMKO5"
    "/qNMKSsZ6EAePHYAu4N5CpqaTl0hjHI8sW+CDzzmGOn8Acb00gJ+DOu+wiTZtJYS"
    "GIZogs7PluMJ7cU1Ju38OixWbQDvfDdloQ/7kZrM6DoEKhvC2bwMwlfxin9jUwjJ"
    "98dtdAwQVgckvnYYVpqKnn/dlkiStaiZFKx27kw6o2oobcDrkg0wtOZFeX8k0SXZ"
    "ekcmMc5Xfl+5HyJxH5ni8UmHyOHAM8dNjpnzCD9J2K0U7z8kdzslZ95X5MAxYIUa"
    "r50tIaWHxeLLYYZUi+nyjNbMZ+yvAqOjQqI1mIcYZurHRPRIHVi2x4nfcKKQIkxn"
    "UTF9d3VWbkWoJ1qfe0OSpWg4RrdgDCSB1BlF0gQHEsDTT5/xoZIEoUV8t6TYTVCe"
    "axreBYxLhvROONz94v6GD6Eb4kakbSObn8NuBiWnaPevFyEF5YluKR87MbZRQY0Z"
    "yJ/4PuEhDIioRdY7ujAxMCEwCQYFKw4DAhoFAAQU4/UMFJQGUvgPuTXRKp0gVU4B"
    "GbkECPTYJIica3DWAgIIAA==";

void WriteBase64AsBinary(std::string const& filename, char const* data) {
  std::ofstream os(filename, std::ios::binary);
  os.exceptions(std::ios::badbit);
  auto bytes = internal::Base64Decode(data);
  for (unsigned char c : bytes) {
    os << c;
  }
  os.close();
}

/// @test Verify that parsing a service account JSON string works.
TEST_F(ServiceAccountCredentialsTest, ParseSimpleP12) {
  auto filename = CreateRandomFileName() + ".p12";
  WriteBase64AsBinary(filename, kP12KeyFileContents);

  auto info = ParseServiceAccountP12File(filename);
  ASSERT_STATUS_OK(info);

  EXPECT_EQ(kP12ServiceAccountId, info->client_email);
  EXPECT_FALSE(info->private_key.empty());
  EXPECT_EQ(0, std::remove(filename.c_str()));

  ServiceAccountCredentials<> credentials(*info);

  auto signed_blob = credentials.SignBlob(SigningAccount(), "test-blob");
  EXPECT_STATUS_OK(signed_blob);
}

TEST_F(ServiceAccountCredentialsTest, ParseP12MissingKey) {
  std::string filename = CreateRandomFileName() + ".p12";
  WriteBase64AsBinary(filename, kP12KeyFileMissingKey);
  auto info = ParseServiceAccountP12File(filename);
  EXPECT_FALSE(info.ok());
}

TEST_F(ServiceAccountCredentialsTest, ParseP12MissingCerts) {
  std::string filename = ::testing::TempDir() + CreateRandomFileName() + ".p12";
  WriteBase64AsBinary(filename, kP12KeyFileMissingCerts);
  auto info = ParseServiceAccountP12File(filename);
  EXPECT_FALSE(info.ok());
}

TEST_F(ServiceAccountCredentialsTest, CreateFromP12MissingFile) {
  std::string filename = CreateRandomFileName();
  // Loading a non-existent file should fail.
  auto actual = CreateServiceAccountCredentialsFromP12FilePath(filename);
  EXPECT_FALSE(actual.ok());
}

TEST_F(ServiceAccountCredentialsTest, CreateFromP12EmptyFile) {
  std::string filename = CreateRandomFileName();
  std::ofstream(filename).close();

  // Loading an empty file should fail.
  auto actual = CreateServiceAccountCredentialsFromP12FilePath(filename);
  EXPECT_FALSE(actual.ok());

  EXPECT_EQ(0, std::remove(filename.c_str()));
}

TEST_F(ServiceAccountCredentialsTest, CreateFromP12ValidFile) {
  std::string filename = CreateRandomFileName() + ".p12";
  WriteBase64AsBinary(filename, kP12KeyFileContents);

  // Loading an empty file should fail.
  auto actual = CreateServiceAccountCredentialsFromP12FilePath(filename);
  EXPECT_STATUS_OK(actual);

  EXPECT_EQ(0, std::remove(filename.c_str()));
}

/// @test Verify we can obtain JWT assertion components given the info parsed
/// from a keyfile.
TEST_F(ServiceAccountCredentialsTest, AssertionComponentsFromInfo) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  auto const clock_value_1 = 10000;
  FakeClock::now_value = clock_value_1;
  auto components = AssertionComponentsFromInfo(*info, FakeClock::now());

  auto header = internal::nl::json::parse(components.first);
  EXPECT_EQ("RS256", header.value("alg", ""));
  EXPECT_EQ("JWT", header.value("typ", ""));
  EXPECT_EQ(info->private_key_id, header.value("kid", ""));

  auto payload = internal::nl::json::parse(components.second);
  EXPECT_EQ(clock_value_1, payload.value("iat", 0));
  EXPECT_EQ(clock_value_1 + 3600, payload.value("exp", 0));
  EXPECT_EQ(info->client_email, payload.value("iss", ""));
  EXPECT_EQ(info->token_uri, payload.value("aud", ""));
}

/// @test Verify we can construct a JWT assertion given the info parsed from a
/// keyfile.
TEST_F(ServiceAccountCredentialsTest, MakeJWTAssertion) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  FakeClock::reset_clock(kFixedJwtTimestamp);
  auto components = AssertionComponentsFromInfo(*info, FakeClock::now());
  auto assertion =
      MakeJWTAssertion(components.first, components.second, info->private_key);

  std::istringstream is(kExpectedAssertionParam);
  std::string expected_encoded_header;
  std::getline(is, expected_encoded_header, '.');
  std::string expected_encoded_payload;
  std::getline(is, expected_encoded_payload, '.');
  std::string expected_encoded_signature;
  std::getline(is, expected_encoded_signature);

  is.str(assertion);
  is.clear();
  std::string actual_encoded_header;
  std::getline(is, actual_encoded_header, '.');
  std::string actual_encoded_payload;
  std::getline(is, actual_encoded_payload, '.');
  std::string actual_encoded_signature;
  std::getline(is, actual_encoded_signature);

  EXPECT_EQ(expected_encoded_header, "assertion=" + actual_encoded_header);
  EXPECT_EQ(expected_encoded_payload, actual_encoded_payload);
  EXPECT_EQ(expected_encoded_signature, actual_encoded_signature);
}

/// @test Verify we can construct a service account refresh payload given the
/// info parsed from a keyfile.
TEST_F(ServiceAccountCredentialsTest, CreateServiceAccountRefreshPayload) {
  auto info = ParseServiceAccountCredentials(kJsonKeyfileContents, "test");
  ASSERT_STATUS_OK(info);
  FakeClock::reset_clock(kFixedJwtTimestamp);
  auto components = AssertionComponentsFromInfo(*info, FakeClock::now());
  auto assertion =
      MakeJWTAssertion(components.first, components.second, info->private_key);
  auto actual_payload = CreateServiceAccountRefreshPayload(
      *info, kGrantParamEscaped, FakeClock::now());

  EXPECT_THAT(actual_payload, HasSubstr(std::string("assertion=") + assertion));
  EXPECT_THAT(actual_payload, HasSubstr(kGrantParamEscaped));
}

/// @test Parsing a refresh response with missing fields results in failure.
TEST_F(ServiceAccountCredentialsTest,
       ParseServiceAccountRefreshResponseMissingFields) {
  std::string r1 = R"""({})""";
  // Does not have access_token.
  std::string r2 = R"""({
    "token_type": "Type",
    "id_token": "id-token-value",
    "expires_in": 1000
})""";

  FakeClock::reset_clock(1000);
  auto status = ParseServiceAccountRefreshResponse(HttpResponse{400, r1, {}},
                                                   FakeClock::now());
  EXPECT_FALSE(status);
  EXPECT_EQ(status.status().code(), StatusCode::kInvalidArgument);
  EXPECT_THAT(status.status().message(),
              ::testing::HasSubstr("Could not find all required fields"));

  status = ParseServiceAccountRefreshResponse(HttpResponse{400, r2, {}},
                                              FakeClock::now());
  EXPECT_FALSE(status);
  EXPECT_EQ(status.status().code(), StatusCode::kInvalidArgument);
  EXPECT_THAT(status.status().message(),
              ::testing::HasSubstr("Could not find all required fields"));
}

/// @test Parsing a refresh response yields a TemporaryToken.
TEST_F(ServiceAccountCredentialsTest, ParseServiceAccountRefreshResponse) {
  std::string r1 = R"""({
    "token_type": "Type",
    "access_token": "access-token-r1",
    "expires_in": 1000
})""";

  auto expires_in = 1000;
  FakeClock::reset_clock(2000);
  auto status = ParseServiceAccountRefreshResponse(HttpResponse{200, r1, {}},
                                                   FakeClock::now());
  EXPECT_STATUS_OK(status);
  EXPECT_EQ(status.status().code(), StatusCode::kOk);
  auto token = *status;
  EXPECT_EQ(
      std::chrono::time_point_cast<std::chrono::seconds>(token.expiration_time)
          .time_since_epoch()
          .count(),
      FakeClock::now_value + expires_in);
  EXPECT_EQ(token.token, "Authorization: Type access-token-r1");
}

}  // namespace
}  // namespace oauth2
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
