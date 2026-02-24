#include "SimpleNTP.h"

SimpleNTP::SimpleNTP() : 
    _ntpServer("pool.ntp.org"),
    _port(123),
    _lastUpdate(0),
    _updateInterval(3600000), // 1 hour default
    _retryInterval(30000),
    _timeZone(0),
    _isTimeSet(false),
    _failedAttempts(0) {
    memset(_packetBuffer, 0, sizeof(_packetBuffer));
}

SimpleNTP::~SimpleNTP() {
    _udp.stop();
}

bool SimpleNTP::begin(const char* server) {
    _ntpServer = server;
    
    // Check WiFi connectivity
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    _lastUpdate = 0;
    _failedAttempts = 0;
    _retryInterval = 30000;
    configTime();

    // Initialization success means updater can start polling in task loop.
    return true;
}

void SimpleNTP::setTimeZone(int hours) {
    _timeZone = hours;
    
    // If time is already set, reconfigure with new timezone
    if (_isTimeSet) {
        configTime();
    }
}

void SimpleNTP::setUpdateInterval(unsigned long interval) {
    _updateInterval = interval;
}

bool SimpleNTP::forceUpdate() {
    // Force time update
    _lastUpdate = 0;
    return update();
}

bool SimpleNTP::update() {
    unsigned long currentMillis = millis();
    unsigned long timeSinceLastUpdate = currentMillis - _lastUpdate;
    
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
    
    // Check if update interval has passed or if first update or if time is not yet set
    const unsigned long interval = _isTimeSet ? _updateInterval : _retryInterval;
    if ((timeSinceLastUpdate >= interval) || (_lastUpdate == 0)) {
        // Check WiFi status first
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
        
        _lastUpdate = currentMillis;

        bool result = configTime();

        if (result) {
            _failedAttempts = 0;
            _retryInterval = 30000;
        } else {
            if (_failedAttempts < 255) {
                _failedAttempts++;
            }

            if (_failedAttempts > 10) {
                _retryInterval = 300000;
            } else if (_failedAttempts > 5) {
                _retryInterval = 60000;
            } else {
                _retryInterval = 30000;
            }

            if ((_failedAttempts % 3) == 0) {
                const char* backupServers[] = {
                    "time.google.com",
                    "pool.ntp.org",
                    "time.cloudflare.com",
                    "time.windows.com",
                    "time.apple.com"
                };

                int serverIndex = (_failedAttempts / 3) % 5;
                _ntpServer = backupServers[serverIndex];
            }
        }
        
        return result;
    }
    
    return false;
}

bool SimpleNTP::isTimeSet() const {
    return _isTimeSet;
}

String SimpleNTP::getFormattedTime() const {
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

String SimpleNTP::getFormattedDate() const {
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

String SimpleNTP::getFormattedDateTime() const {
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

int SimpleNTP::getHours() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_hour;
}

int SimpleNTP::getMinutes() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_min;
}

int SimpleNTP::getSeconds() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_sec;
}

int SimpleNTP::getDay() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_mday;
}

int SimpleNTP::getMonth() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_mon + 1;  // tm_mon is 0-11, we return 1-12
}

int SimpleNTP::getYear() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_year + 1900;  // tm_year is years since 1900
}

int SimpleNTP::getDayOfWeek() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    return timeinfo.tm_wday;  // 0-6, 0 = Sunday
}

time_t SimpleNTP::getEpochTime() const {
    if (!_isTimeSet) return 0;
    
    time_t now;
    time(&now);
    return now;
}

JsonObject SimpleNTP::toJson(JsonDocument& doc) const {
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

void SimpleNTP::sendNTPPacket() {
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
    
    // Try multiple times with a short delay
    bool dnsSuccess = false;
    for (int attempt = 1; attempt <= 3 && !dnsSuccess; attempt++) {
        dnsSuccess = WiFi.hostByName(_ntpServer, ntpServerIP);
        
        if (dnsSuccess) {
            break;
        } else {
            if (attempt < 3) {
                delay(100);  // Short delay before retry
                yield();
            }
        }
    }
    
    if (!dnsSuccess) {
        return;
    }
    
    // Get the STA IP to ensure we're using the correct interface
    IPAddress staIP = WiFi.localIP();
    
    // Reinitialize UDP with explicit binding to STA IP
    _udp.stop();
    if (!_udp.begin(_port)) {
        return;
    }
    
    // Send the packet with retries
    bool packetSent = false;
    for (int attempt = 1; attempt <= 2 && !packetSent; attempt++) {
        
        // Use explicit beginPacket with NTP server IP and port 123 (NTP port)
        if (_udp.beginPacket(ntpServerIP, 123)) {
            
            // Write the packet to avoid buffer issues
            _udp.write(_packetBuffer, sizeof(_packetBuffer));
            yield(); // Allow other processing
            
            if (_udp.endPacket()) {
                packetSent = true;
                break;
            } else {
                if (attempt < 2) {
                    delay(200);  // Wait before retry
                    yield();
                }
            }
        } else {
            if (attempt < 2) {
                delay(200);
                yield();
            }
        }
    }
    
    if (!packetSent) {
    } else {
        // Short delay to allow packet to be processed
        delay(10);
        yield();
    }
}

bool SimpleNTP::configTime() {
    String tzString = getTzString(_timeZone);
    
    // Check WiFi connectivity first
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
                 
    // Use ESP32's built-in SNTP configuration (non-blocking).
    configTzTime(tzString.c_str(), _ntpServer);
    
    // Check if time was already set (might happen instantly if synced recently)
    time_t now = 0;
    struct tm timeinfo = {0};
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if we got a valid year (greater than 2016)
    _isTimeSet = (timeinfo.tm_year > (2016 - 1900));
    
    if (_isTimeSet) {
        return true;
    } else {
        return false;
    }
}

String SimpleNTP::getTzString(int timezone) const {
    char tz[20];
    if (timezone >= 0) {
        // POSIX TZ sign is inverted for GMT offset string.
        snprintf(tz, sizeof(tz), "GMT-%d", timezone);
    } else {
        snprintf(tz, sizeof(tz), "GMT+%d", -timezone);
    }
    return String(tz);
}
