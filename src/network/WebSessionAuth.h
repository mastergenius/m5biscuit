#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace WebSessionAuth {

constexpr const char* AUTH_USER = "biscuit";
constexpr const char* COOKIE_NAME = "biscuit_token";

bool isAuthorized(WebServer& server, const String& sessionToken);
void sendUnauthorized(WebServer& server);
String makeSessionToken();
String makeHotspotPassword();

}  // namespace WebSessionAuth
