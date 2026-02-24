#ifndef ASYNC_SIMPLE_NTP_H
#define ASYNC_SIMPLE_NTP_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncUDP.h>
#include <time.h>
#include <ArduinoJson.h>
#include <functional>

// Include ESP-IDF headers for network interface handling
#include "lwip/opt.h"
#include "lwip/inet.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include <esp_wifi.h>
#include <esp_netif.h>

class AsyncSimpleNTP {
public:
    AsyncSimpleNTP();
    ~AsyncSimpleNTP();

    /**
     * @brief Initialize the NTP client
     * @param server NTP server address
     * @return true if initialization was successful
     */
    bool begin(const char* server = "pool.ntp.org");

    /**
     * @brief Set the timezone offset in hours from UTC
     * @param hours The timezone offset in hours (-12 to +14)
     */
    void setTimeZone(int hours);
    
    /**
     * @brief Set the update interval for NTP synchronization
     * @param interval The interval in milliseconds
     */
    void setUpdateInterval(unsigned long interval);
    
    /**
     * @brief Force an update from the NTP server
     * @return true if update was successful
     */
    bool forceUpdate();
    
    /**
     * @brief Update time if update interval has passed
     * @return true if an update was performed and successful
     */
    bool update();
    
    /**
     * @brief Check if time is set
     * @return true if time is set
     */
    bool isTimeSet() const;
    
    /**
     * @brief Get current time as formatted string (HH:MM:SS)
     * @return String with formatted time
     */
    String getFormattedTime() const;
    
    /**
     * @brief Get current date as formatted string (YYYY-MM-DD)
     * @return String with formatted date
     */
    String getFormattedDate() const;
    
    /**
     * @brief Get current date and time as formatted string
     * @return String with formatted date and time
     */
    String getFormattedDateTime() const;
    
    /**
     * @brief Get current hour (0-23)
     * @return Current hour
     */
    int getHours() const;
    
    /**
     * @brief Get current minute (0-59)
     * @return Current minute
     */
    int getMinutes() const;
    
    /**
     * @brief Get current second (0-59)
     * @return Current second
     */
    int getSeconds() const;
    
    /**
     * @brief Get current day of month (1-31)
     * @return Current day of month
     */
    int getDay() const;
    
    /**
     * @brief Get current month (1-12)
     * @return Current month
     */
    int getMonth() const;
    
    /**
     * @brief Get current year (e.g., 2023)
     * @return Current year
     */
    int getYear() const;
    
    /**
     * @brief Get current day of week (0-6, where 0 = Sunday)
     * @return Current day of week
     */
    int getDayOfWeek() const;
    
    /**
     * @brief Get current Unix epoch time
     * @return Current Unix epoch time
     */
    time_t getEpochTime() const;
    
    /**
     * @brief Convert this object to a JSON object
     * @return JsonObject with time information
     */
    JsonObject toJson(JsonDocument& doc) const;
    
    /**
     * @brief Share an existing AsyncUDP instance for NTP operations
     * @param udp Pointer to existing AsyncUDP instance
     * Note: The existing instance must be on a different port than 123
     */
    void setSharedUDP(AsyncUDP* udp);

private:
    AsyncUDP _udp;
    AsyncUDP* _sharedUdp;
    bool _usingSharedUdp;
    const char* _ntpServer;
    unsigned int _port;
    byte _packetBuffer[48];
    unsigned long _lastUpdate;
    unsigned long _updateInterval;
    int _timeZone;
    bool _isTimeSet;
    bool _packetReceived;
    bool _waitingForPacket;
    unsigned long _packetSendTime;
    
    /**
     * @brief Send an NTP packet to the NTP server
     */
    void sendNTPPacket();
    
    /**
     * @brief Configure system time with NTP server
     * @return true if successful
     */
    bool configTime();
    
    /**
     * @brief Get timezone string for ESP32 time functions
     * @param timezone The timezone offset in hours
     * @return String with timezone format for ESP32
     */
    String getTzString(int timezone) const;
    
    /**
     * @brief Handle received UDP packet
     */
    void handlePacket(AsyncUDPPacket packet);
};

#endif // ASYNC_SIMPLE_NTP_H
