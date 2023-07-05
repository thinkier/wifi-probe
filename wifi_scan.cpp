#include <iostream>
#include <sstream>
#include <iomanip>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "pico/cyw43_arch.h"

#define scan_passes 5
#define scan_result_ttl_default 3

using namespace std;

struct scan_result_t {
    string bssid;
    string ssid;
    int16_t rssi;
    uint16_t channels_bitflag;
    uint8_t ttl;
};

#define scan_result_coll_t map<string, scan_result_t>

auto_init_mutex(wifi_scan_guard);
/// Comprehensive scan result deduplicated by BSSID (AP MAC Address)
///
/// Direct access is discouraged. For most purposes `basic_wifi_scan()` does a better job.
scan_result_coll_t wifi_scan_results;

/// Format a MAC address to hex delimited by colons
string format_mac_address(const uint8_t *mac_address) {
    stringstream ss;

    ss << setfill('0') << hex << setw(2) << static_cast<unsigned int>( mac_address[0]);
    for (int i = 1; i < 6; i++) {
        ss << ":";
        ss << setfill('0') << hex << setw(2) << static_cast<unsigned int>(mac_address[i]);
    }

    return ss.str();
}

/// cyw43 callback for networks received by scan
static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    auto buf = (scan_result_coll_t *) env;

    if (result) {
        auto bssid = format_mac_address(result->bssid);
        string ssid;
        if (result->ssid[0] != 0x00) {
            ssid = string((char *) result->ssid, result->ssid_len);
        }
        uint16_t channels_bitflag = 1 << result->channel;
        int16_t rssi = result->rssi;

        if (auto prev = buf->find(bssid); prev != buf->end()) {
            if (ssid.length() > 0) {
                prev->second.ssid = ssid;
            }

            if (prev->second.ttl == scan_result_ttl_default) {
                prev->second.channels_bitflag |= channels_bitflag;
                if (prev->second.rssi > rssi) {
                    prev->second.rssi = rssi;
                }
            } else {
                prev->second.channels_bitflag = channels_bitflag;
                prev->second.rssi = rssi;
                prev->second.ttl = scan_result_ttl_default;
            }
        } else {
            uint8_t ttl = scan_result_ttl_default;

            buf->emplace(make_pair(
                                 bssid,
                                 scan_result_t{bssid, ssid, rssi, channels_bitflag, ttl}
                         )
            );
        }
    }
    return 0;
}

/// Launch this on a dedicated thread to continuously scan for wifi networks
void wifi_scan_thread() {
    if (cyw43_arch_init()) {
        cout << "Failed to init wifi chip." << endl;
        return;
    }

    cyw43_arch_enable_sta_mode();

    while (true) {
        scan_result_coll_t buffered_scan_results = wifi_scan_results;
        vector<string> deletes;
        for (auto &item: buffered_scan_results) {
            if (item.second.ttl-- == 0) {
                deletes.push_back(item.first);
            }
        }

        cyw43_wifi_scan_options_t scan_options = {0};
        for (int i = 0; i < scan_passes; i++) {
            cyw43_wifi_scan(&cyw43_state, &scan_options, &buffered_scan_results, scan_result);
            while (cyw43_wifi_scan_active(&cyw43_state)) {
                sleep_ms(50);
            }
            sleep_ms(50);
        }

        for (const auto &item: deletes) {
            buffered_scan_results.erase(item);
        }

        cout << "Scan found " << buffered_scan_results.size() << " networks." << endl;

        mutex_enter_blocking(&wifi_scan_guard);
        wifi_scan_results.swap(buffered_scan_results);
        mutex_exit(&wifi_scan_guard);

        sleep_ms(100);
    }

    cyw43_arch_deinit();
}

bool compareAPs(const scan_result_t &a, const scan_result_t &b) {
    // Compare by RSSI if it's significantly different
    if (abs(a.rssi - b.rssi) > 3) {
        return a.rssi > b.rssi;
    }
    // Otherwise sort alphabetically by SSID
    return strcmp(a.ssid.c_str(), b.ssid.c_str()) < 0;
}

/// Linux iwconfig style quality calculation, adjusted to percent instead of /70
uint8_t rssiToPercent(int rssi) {
    return clamp((rssi + 110) * 10 / 7, 0, 100);
}

/// Returns a basic scan result, all SSIDs are deduplicated, channels bitflags are combined, and the result is sorted by RSSI.
vector<scan_result_t> basic_wifi_scan() {
    map<string, scan_result_t> dedup_by_ssid;
    mutex_enter_blocking(&wifi_scan_guard);
    for (const auto &item: wifi_scan_results) {
        if (item.second.ssid.length() > 0) {
            if (auto prev = dedup_by_ssid.find(item.second.ssid); prev != dedup_by_ssid.end()) {
                prev->second.channels_bitflag |= item.second.channels_bitflag;
                if (prev->second.rssi > item.second.rssi) {
                    prev->second.rssi = item.second.rssi;
                }
            } else {
                dedup_by_ssid.emplace(item.second.ssid, item.second);
            }
        }
    }
    mutex_exit(&wifi_scan_guard);

    vector<scan_result_t> results;
    results.reserve(wifi_scan_results.size());
    for (const auto &item: dedup_by_ssid) {
        results.push_back(item.second);
    }

    sort(results.begin(), results.end(), compareAPs);
    return results;
}
