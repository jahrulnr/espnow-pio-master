#include "AsyncSimpleNTP.h"

AsyncSimpleNTP::AsyncSimpleNTP() : 
    _ntpServer("pool.ntp.org"),
    _port(123),
    _lastUpdate(0),
    _updateInterval(3600000), // 1 hour default
    _timeZone(0),
    _isTimeSet(false),
    _packetReceived(false),
    _waitingForPacket(false),
    _packetSendTime(0),
    _sharedUdp(nullptr),
    _usingSharedUdp(false) {
    memset(_packetBuffer, 0, sizeof(_packetBuffer));
}

AsyncSimpleNTP::~AsyncSimpleNTP() {
    if (!_usingSharedUdp) {
        _udp.close();
    }
}

bool AsyncSimpleNTP::begin(const char* server) {
    _ntpServer = server;
    
    // Check WiFi connectivity
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    // Get the STA IP address to bind UDP to the correct interface
    IPAddress staIP = WiFi.localIP();
    
    if (_usingSharedUdp) {
        // Using existing UDP instance, just send NTP packet
    } else {
        // First try to stop UDP if it was previously started
        _udp.close();
        
        // Set up packet handler with explicit binding to STA interface
        tcpip_adapter_if_t wifi_sta_if = TCPIP_ADAPTER_IF_STA;
        IPAddress staIP = WiFi.localIP();
        
        // First try to bind on the standard port with explicit STA interface
        if (!_udp.listen(staIP, _port)) {
            // Try alternate ports
            _port = 1123;  // Try a different port
            
            if (!_udp.listen(staIP, _port)) {
                
                // Last resort: try binding to ANY IP but on a high port
                _port = 32123;
                
                if (!_udp.listen(_port)) {
                    return false;
                }
            }
        }
        
        // Set up callback for packet receipt
        _udp.onPacket([this](AsyncUDPPacket packet) {
            this->handlePacket(packet);
        });
    }
    
    // Send an NTP packet immediately to trigger sync
    _waitingForPacket = true;
    _packetReceived = false;
    _packetSendTime = millis();
    sendNTPPacket();
    
    // Configure time but don't expect it to be set immediately
    configTime();
    
    // The packet will be handled asynchronously via callback
    return true;
}

void AsyncSimpleNTP::setTimeZone(int hours) {
    _timeZone = hours;
    
    // If time is already set, reconfigure with new timezone
    if (_isTimeSet) {
        configTime();
    }
}

void AsyncSimpleNTP::setUpdateInterval(unsigned long interval) {
    _updateInterval = interval;
}

bool AsyncSimpleNTP::forceUpdate() {
    // Force time update
    _lastUpdate = 0;
    return update();
}

bool AsyncSimpleNTP::update() {
    unsigned long currentMillis = millis();
    
    // First check if time is already set by calling ESP32's time functions
    if (!_isTimeSet) {
        time_t now = 0;
        struct tm timeinfo = {0};
        time(&now);
        localtime_r(&now, &timeinfo);
        
        // Check if time was set since our last check
        if (timeinfo.tm_year > (2016 - 1900)) {
            _isTimeSet = true;
            char timeStr[50];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        }
    }
    
    // Check for timeout on waiting for packet
    if (_waitingForPacket && !_packetReceived) {
        if (currentMillis - _packetSendTime > 5000) {  // 5-second timeout
            _waitingForPacket = false;
        }
    }
    
    // Check if update interval has passed or if first update or if time is not yet set
    if ((currentMillis - _lastUpdate >= _updateInterval) || (_lastUpdate == 0) || (!_isTimeSet && !_waitingForPacket)) {
        // Check WiFi status first
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
        _lastUpdate = currentMillis;
        
        // Send a fresh NTP packet
        _waitingForPacket = true;
        _packetReceived = false;
        _packetSendTime = currentMillis;
        sendNTPPacket();
        
        return true;
    }
    
    return false;
}

void AsyncSimpleNTP::handlePacket(AsyncUDPPacket packet) {
    // Validate packet size
    if (packet.length() < 48) {
        return;
    }
    
    // Read packet into buffer
    memcpy(_packetBuffer, packet.data(), 48);
    
    // NTP response contains the server time in the last 8 bytes
    // Extract the seconds since 1900 from bytes 40-43
    unsigned long highWord = word(_packetBuffer[40], _packetBuffer[41]);
    unsigned long lowWord = word(_packetBuffer[42], _packetBuffer[43]);
    
    // Combine to get seconds since Jan 1, 1900
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    
    // Unix time starts on Jan 1 1970, subtract 70 years in seconds
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epochTime = secsSince1900 - seventyYears;
    
    // Now call configTime to set the system time
    configTime();
    
    // Mark that we've received the packet
    _packetReceived = true;
    _waitingForPacket = false;
    
    // Check if time is now set
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    
    _isTimeSet = (timeinfo.tm_year > (2016 - 1900));
    
    if (_isTimeSet) {
        char timeStr[50];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    } else {
    }
}

bool AsyncSimpleNTP::isTimeSet() const {
    return _isTimeSet;
}

void AsyncSimpleNTP::sendNTPPacket() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    memset(_packetBuffer, 0, sizeof(_packetBuffer));
    
    // Initialize values needed to form NTP request
    _packetBuffer[0] = 0b11100011;   // LI, Version, Mode (NTP v4, Client)
    _packetBuffer[1] = 0;            // Stratum, or type of clock
    _packetBuffer[2] = 6;            // Polling Interval
    _packetBuffer[3] = 0xEC;         // Peer Clock Precision
    
    // 8 bytes of zero for Root Delay & Root Dispersion
    _packetBuffer[12] = 49;
    _packetBuffer[13] = 0x4E;
    _packetBuffer[14] = 49;
    _packetBuffer[15] = 52;
    
    // Try to resolve host with timeout
    IPAddress ntpServerIP;
    
    if (!WiFi.hostByName(_ntpServer, ntpServerIP)) {
        return;
    }
    
    // Create UDP packet
    if (_usingSharedUdp && _sharedUdp != nullptr) {
        // Using shared UDP instance - convert IP to ip_addr_t
        ip_addr_t dest_addr;
        dest_addr.type = IPADDR_TYPE_V4;
#if CONFIG_LWIP_IPV6
        dest_addr.u_addr.ip4.addr = ntpServerIP;
#else
        dest_addr.addr = ntpServerIP;
#endif
        // Send directly to the NTP server IP using the shared UDP instance
        _sharedUdp->writeTo(_packetBuffer, sizeof(_packetBuffer), &dest_addr, 123);
    } else {
        // Using our own UDP instance - explicit STA interface
        tcpip_adapter_if_t wifi_sta_if = TCPIP_ADAPTER_IF_STA;
        _udp.writeTo(_packetBuffer, sizeof(_packetBuffer), ntpServerIP, 123, wifi_sta_if);
    }
}

bool AsyncSimpleNTP::configTime() {
    String tzString = getTzString(_timeZone);
    
    // Check WiFi connectivity first
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
                 
    // Use ESP32's built-in time configuration
    configTzTime(tzString.c_str(), _ntpServer);
    
    // Check if time was already set
    time_t now = 0;
    struct tm timeinfo = {0};
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if we got a valid year (greater than 2016)
    _isTimeSet = (timeinfo.tm_year > (2016 - 1900));
    
    // Log the status and time details
    if (_isTimeSet) {
        char timeStr[50];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return true;
    } else {
        return false;
    }
}

String AsyncSimpleNTP::getTzString(int timezone) const {
    char tz[20];
    if (timezone >= 0) {
        snprintf(tz, sizeof(tz), "GMT-%d", timezone);
    } else {
        snprintf(tz, sizeof(tz), "GMT+%d", -timezone);
    }
    return String(tz);
}

String AsyncSimpleNTP::getFormattedTime() const {
    if (!_isTimeSet) {
        return "00:00:00";
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buffer[9];  // HH:MM:SS\0
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    return String(buffer);
}

String AsyncSimpleNTP::getFormattedDate() const {
    if (!_isTimeSet) {
        return "0000-00-00";
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buffer[11];  // YYYY-MM-DD\0
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    
    return String(buffer);
}

String AsyncSimpleNTP::getFormattedDateTime() const {
    if (!_isTimeSet) {
        return "0000-00-00 00:00:00";
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char buffer[20];  // YYYY-MM-DD HH:MM:SS\0
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    return String(buffer);
}

int AsyncSimpleNTP::getHours() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_hour;
}

int AsyncSimpleNTP::getMinutes() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_min;
}

int AsyncSimpleNTP::getSeconds() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_sec;
}

int AsyncSimpleNTP::getDay() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_mday;
}

int AsyncSimpleNTP::getMonth() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_mon + 1;  // tm_mon is 0-11, we return 1-12
}

int AsyncSimpleNTP::getYear() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_year + 1900;  // tm_year is years since 1900
}

int AsyncSimpleNTP::getDayOfWeek() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_wday;  // 0-6, 0 = Sunday
}

time_t AsyncSimpleNTP::getEpochTime() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    time(&now);
    return now;
}

JsonObject AsyncSimpleNTP::toJson(JsonDocument& doc) const {
    JsonObject timeObj = doc.to<JsonObject>();
    
    if (!_isTimeSet) {
        timeObj["status"] = "not_set";
        return timeObj;
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    timeObj["status"] = "set";
    timeObj["epoch"] = now;
    timeObj["timezone"] = _timeZone;
    timeObj["time"] = getFormattedTime();
    timeObj["date"] = getFormattedDate();
    timeObj["datetime"] = getFormattedDateTime();
    
    JsonObject details = timeObj["details"].to<JsonObject>();
    details["year"] = timeinfo.tm_year + 1900;
    details["month"] = timeinfo.tm_mon + 1;
    details["day"] = timeinfo.tm_mday;
    details["hour"] = timeinfo.tm_hour;
    details["minute"] = timeinfo.tm_min;
    details["second"] = timeinfo.tm_sec;
    details["dayofweek"] = timeinfo.tm_wday;
    
    return timeObj;
}

void AsyncSimpleNTP::setSharedUDP(AsyncUDP* udp) {
    if (udp != nullptr) {
        // Check if the UDP instance is initialized and connected
        if (udp->connected()) {
            _sharedUdp = udp;
            _usingSharedUdp = true;
            
            // Close our own UDP if it's open
            if (_udp.connected()) {
                _udp.close();
            }
        } else {
            _usingSharedUdp = false;
            _sharedUdp = nullptr;
        }
    } else {
        _usingSharedUdp = false;
        _sharedUdp = nullptr;
    }
}