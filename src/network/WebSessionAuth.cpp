#include "WebSessionAuth.h"

#include <esp_system.h>

namespace {

bool tokenMatches(const String& expected, const String& candidate) {
  if (expected.isEmpty() || candidate.isEmpty()) {
    return false;
  }
  return expected.equalsConstantTime(candidate);
}

String cookieValue(const String& cookieHeader, const char* name) {
  String key = String(name) + "=";
  int start = 0;
  while (start < cookieHeader.length()) {
    while (start < cookieHeader.length() && (cookieHeader[start] == ' ' || cookieHeader[start] == ';')) {
      start++;
    }
    int end = cookieHeader.indexOf(';', start);
    if (end < 0) {
      end = cookieHeader.length();
    }
    String item = cookieHeader.substring(start, end);
    item.trim();
    if (item.startsWith(key)) {
      return item.substring(key.length());
    }
    start = end + 1;
  }
  return "";
}

void setPairingCookie(WebServer& server, const String& token) {
  String cookie = String(WebSessionAuth::COOKIE_NAME) + "=" + token +
                  "; Path=/; Max-Age=7200; SameSite=Strict";
  server.sendHeader("Set-Cookie", cookie);
}

String randomHex(const size_t byteCount) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  String out;
  out.reserve(byteCount * 2);
  for (size_t i = 0; i < byteCount; i++) {
    const uint8_t value = static_cast<uint8_t>(esp_random() & 0xFF);
    out += HEX_DIGITS[value >> 4];
    out += HEX_DIGITS[value & 0x0F];
  }
  return out;
}

}  // namespace

namespace WebSessionAuth {

bool isAuthorized(WebServer& server, const String& sessionToken) {
  if (sessionToken.isEmpty()) {
    return true;
  }

  if (server.hasArg("token") && tokenMatches(sessionToken, server.arg("token"))) {
    setPairingCookie(server, sessionToken);
    return true;
  }

  if (server.hasHeader("Cookie")) {
    const String token = cookieValue(server.header("Cookie"), COOKIE_NAME);
    if (tokenMatches(sessionToken, token)) {
      return true;
    }
  }

  return server.authenticate([&sessionToken](HTTPAuthMethod mode, String authReq, String params[]) -> String* {
    if (mode == BASIC_AUTH) {
      (void)params;
      return authReq.equalsConstantTime(AUTH_USER) ? new String(sessionToken) : nullptr;
    }

    if (mode == OTHER_AUTH) {
      String lower = authReq;
      lower.toLowerCase();
      if (!lower.startsWith("bearer ")) {
        return nullptr;
      }

      String token = authReq.substring(7);
      token.trim();
      return tokenMatches(sessionToken, token) ? new String("ok") : nullptr;
    }

    return nullptr;
  });
}

void sendUnauthorized(WebServer& server) {
  server.sendHeader("WWW-Authenticate", "Basic realm=\"Biscuit\"");
  server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

String makeSessionToken() { return randomHex(16); }

String makeHotspotPassword() { return String("biscuit-") + randomHex(4); }

}  // namespace WebSessionAuth
