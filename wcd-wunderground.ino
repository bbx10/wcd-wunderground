/****************************
The MIT License (MIT)

Copyright (c) 2016 by bbx10node@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
******************************/

/*
 * Get weather current conditions from Weather Underground using TLS.
 * Works on Adafruit Feather WICED board.
 */

#include <adafruit_feather.h>
#include <adafruit_http.h>
#include <ArduinoJson.h>

#define WLAN_SSID  "yourSSID"
#define WLAN_PASS  "yourPASSWORD"

// Use your own API key by signing up for a free developer account.
// http://www.wunderground.com/weather/api/
#define WU_API_KEY "xxxxxxxxxxxxxxxx"

// Specify your favorite location one of these ways.
#define WU_LOCATION "CA/HOLLYWOOD"

// US ZIP code
//#define WU_LOCATION ""
//#define WU_LOCATION "90210"

// Country and city
//#define WU_LOCATION "Australia/Sydney"

#define WUNDERGROUND "api.wunderground.com"

// RootCAs require a lot of SRAM to manage (~900 bytes for each certificate
// in the chain). The default RootCA has 5 certificates, so ~4.5 KB of
// FeatherLib's SRAM is used to manage them.
// A lack of memory could cause FeatherLib to malfunction in some cases.
// It is advised to disable the default RootCA list if you only need to
// connect to one specific website (or sites where the RootCA is not
// included in the default root certificate chain).
#define INCLUDE_DEFAULT_ROOTCA  0

#define SERVER  WUNDERGROUND           // The TCP server to connect to
// The HTTP resource to request
#define PAGE    "/api/" WU_API_KEY "/conditions/q/" WU_LOCATION ".json"
#define PORT    80                     // The TCP port to use
#define HTTPS_PORT            443

#define USE_TLS 1
#if USE_TLS
// The root CA file was generated using the following command.
//   python pycert.py download api.wunderground.com
//
// WARNING: wunderground.com uses a different root CA so be sure to
// specify api.wunderground.com.
//
#include "certificates.h"
#endif

// Works for Weather Underground
#define USER_AGENT_HEADER    "FeatherWICED/0.1"

// Use the HTTP class
AdafruitHTTP http;

// HTTP response buffer
static char respBuf[4096];
static int respBufLen = 0;

/**************************************************************************/
/*!
    @brief  TCP/HTTP received callback
*/
/**************************************************************************/
void receive_callback(void)
{
  static bool skip_headers = true;
  static int content_length = -1;

  Serial.print('.');
  if (skip_headers) {
    // If there are incoming bytes available
    // from the server, skip HTTP headers until blank line
    while ( http.available() ) {
      String aLine = http.readStringUntil('\n');
      Serial.println(aLine);
      aLine.toLowerCase();
      if (aLine.startsWith("content-length:")) {
        content_length = aLine.substring(16).toInt();
        Serial.printf("content_length=%d\n", content_length);
      }
      // Blank line means end of headers
      if (aLine.length() <= 1) {
        skip_headers = false;
        Serial.println("End of http headers");
        break;
      }
    }
  }

  // The rest of the response should be JSON. Since this can be 2-3 K long,
  // read in big gulps. Reading 1 byte at time is slow.
  while ( http.available() )
  {
    int bytesIn = http.read(respBuf+respBufLen, (sizeof(respBuf)-1)-respBufLen);
    if (bytesIn > 0) {
      respBufLen += bytesIn;
      Serial.printf("read bytesIn %d\n", bytesIn);
      if ((content_length > 0) && (respBufLen >= content_length)) {
        respBuf[respBufLen++] = '\0';   // NUL terminate the string.
        showWeather(respBuf);
        break;
      }
    }
  }
}

/**************************************************************************/
/*!
    @brief  TCP/HTTP disconnect callback
*/
/**************************************************************************/
void disconnect_callback(void)
{
  Serial.println();
  Serial.println("---------------------");
  Serial.println("DISCONNECTED CALLBACK");
  Serial.println("---------------------");
  Serial.println();

  http.stop();

  respBuf[respBufLen++] = '\0';   // NUL terminate the string.
  showWeather(respBuf);
}

void setup()
{
  Serial.begin(115200);

  // wait for serial port to connect. Needed for native USB port only
  while (!Serial) delay(1);

  while ( !connectAP() )
  {
    delay(500); // delay between each attempt
  }

  // Connected: Print network info
  Feather.printNetwork();

#if USE_TLS
  // Include default RootCA if necessary
  Feather.useDefaultRootCA(INCLUDE_DEFAULT_ROOTCA);

  // Add custom RootCA since target server is not covered by default list
  Feather.addRootCA(rootca_certs, ROOTCA_CERTS_LEN);
#endif

  // Tell the HTTP client to auto print error codes and halt on errors
  http.err_actions(true, true);

  // Set the HTTP client timeout (in ms)
  http.setTimeout(1000);

  // Set the callback handlers
  http.setReceivedCallback(receive_callback);
  http.setDisconnectCallback(disconnect_callback);

  // Connect to the HTTP server
#if USE_TLS
  Serial.printf("Connecting to %s port %d ... ", SERVER, HTTPS_PORT);
  http.connectSSL(SERVER, HTTPS_PORT); // Will halt if an error occurs
#else
  Serial.printf("Connecting to %s port %d ... ", SERVER, PORT);
  http.connect(SERVER, PORT); // Will halt if an error occurs
#endif
  Serial.println("OK");

  // Setup the HTTP request with any required header entries
  http.addHeader("User-Agent", USER_AGENT_HEADER);
  http.addHeader("Accept", "text/html");
  http.addHeader("Connection", "keep-alive");

  // Send the HTTP request
  Serial.printf("Requesting '%s' ... ", PAGE);
  http.get(PAGE); // Will halt if an error occurs
  Serial.println("OK");
}

bool showWeather(char *json)
{
  StaticJsonBuffer<3*1024> jsonBuffer;

  // Skip characters until first '{' found
  // Ignore chunked length, if present
  char *jsonstart = strchr(json, '{');
  //Serial.print(F("jsonstart ")); Serial.println(jsonstart);
  if (jsonstart == NULL) {
    Serial.println(F("JSON data missing"));
    return false;
  }
  json = jsonstart;

  // Parse JSON
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println(F("jsonBuffer.parseObject() failed"));
    return false;
  }

  // Extract weather info from parsed JSON
  JsonObject& current = root["current_observation"];
  const float temp_f = current["temp_f"];
  Serial.print(temp_f, 1); Serial.print(F(" F, "));
  const float temp_c = current["temp_c"];
  Serial.print(temp_c, 1); Serial.print(F(" C, "));
  const char *humi = current[F("relative_humidity")];
  Serial.print(humi);   Serial.println(F(" RH"));
  const char *weather = current["weather"];
  Serial.println(weather);
  const char *pressure_mb = current["pressure_mb"];
  Serial.println(pressure_mb);
  const char *observation_time = current["observation_time_rfc822"];
  Serial.println(observation_time);

  // Extract local timezone fields
  const char *local_tz_short = current["local_tz_short"];
  Serial.println(local_tz_short);
  const char *local_tz_long = current["local_tz_long"];
  Serial.println(local_tz_long);
  const char *local_tz_offset = current["local_tz_offset"];
  Serial.println(local_tz_offset);
  return true;
}

/**************************************************************************/
/*!
    @brief  Connect to the defined access point (AP)
*/
/**************************************************************************/
bool connectAP(void)
{
  // Attempt to connect to an AP
  Serial.print("Please wait while connecting to: '" WLAN_SSID "' ... ");

  if ( Feather.connect(WLAN_SSID, WLAN_PASS) )
  {
    Serial.println("Connected!");
  }
  else
  {
    Serial.printf("Failed! %s (%d)", Feather.errstr(), Feather.errno());
    Serial.println();
  }
  Serial.println();

  return Feather.connected();
}

void loop()
{
}
