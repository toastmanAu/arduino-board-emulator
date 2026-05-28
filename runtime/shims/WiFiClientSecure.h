#pragma once
#include "WiFiClient.h"

class WiFiClientSecure : public WiFiClient {
public:
    void setCACert(const char* /*cert*/)       {}
    void setCertificate(const char* /*cert*/)  {}
    void setPrivateKey(const char* /*key*/)    {}
    void setInsecure()                          {}
    void setHandshakeTimeout(uint32_t /*ms*/)  {}
};
