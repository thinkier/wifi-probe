#include <pico_display_2.hpp>
#include <drivers/st7789/st7789.hpp>
#include <libraries/pico_graphics/pico_graphics.hpp>
#include <rgbled.hpp>
#include <button.hpp>

#include <pico/multicore.h>
#include <pico/stdlib.h>
#include "wifi_scan.cpp"

#define AP_DISPLAYED_AT_ONCE 8

#define BLACK 0, 0, 0
#define DARKDARKGREY 32, 32, 32
#define DARKGREY 64, 64, 64
#define WHITE 255, 255, 255
#define RED 255, 0, 0
#define ORANGE 255, 128, 0
#define YELLOW 255, 255, 0
#define GREEN 0, 255, 0

using namespace pimoroni;

void display_thread() {
    ST7789 st7789(PicoDisplay2::WIDTH, PicoDisplay2::HEIGHT, ROTATE_0, false, get_spi_pins(BG_SPI_FRONT));

    PicoGraphics_PenRGB565 graphics(st7789.width, st7789.height, nullptr);

    // On board LED between X and Y buttons
    RGBLED led(PicoDisplay2::LED_R, PicoDisplay2::LED_G, PicoDisplay2::LED_B);
    led.set_brightness(0);

    Button button_a(PicoDisplay2::A);
    Button button_b(PicoDisplay2::B);
    Button button_x(PicoDisplay2::X);
    Button button_y(PicoDisplay2::Y);

    st7789.set_backlight(255);

    // State variables
    string pinned_ssid;
    int cursor = 0;
    int mode = 1;

    while (true) {
        graphics.set_pen(BLACK);
        graphics.clear();
        vector<scan_result_t> scanned_aps = basic_wifi_scan();
        int count = scanned_aps.size();

        // Scanning screen
        if (count == 0) {
            graphics.set_pen(WHITE);
            graphics.circle(Point(160, 120), 4);
            for (int i = 1; i <= 3; i++) {
                int dist = i * 10;
                int x = 160 + dist;
                int y = 120 - dist;

                graphics.arc(Point(160, 125), Point(x, y), 90, 4);
            }

            auto scanning = "Scanning for 2.4GHz WiFi";
            graphics.text(scanning, Point((320 - graphics.measure_text(scanning, 2)) / 2, 160), 240, 2, true);

            st7789.update(&graphics);
            continue;
        }

        // A/B (Up/down) button logic; AP cursor override for A/B
        if (pinned_ssid.length() > 0) {
            cursor = 0;
            for (const auto &item: scanned_aps) {
                if (item.ssid == pinned_ssid) {
                    break;
                }
                cursor += 1;
            }
        } else if (button_a.read()) {
            cursor -= 1;
        } else if (button_b.read()) {
            cursor += 1;
        }
        cursor = clamp(cursor, 0, count - 1);
        int skip = 0;
        if (cursor > AP_DISPLAYED_AT_ONCE / 2) {
            skip = min(cursor - (AP_DISPLAYED_AT_ONCE / 2), count - AP_DISPLAYED_AT_ONCE);
        }

        // Y button (select mode) logic
        if (button_y.read()) {
            mode += 1;
        }
        mode %= 2;

        // X button (pin/unpin AP) logic
        if (button_x.read()) {
            if (pinned_ssid.length() > 0) {
                pinned_ssid = "";
            } else {
                pinned_ssid = scanned_aps[cursor].ssid;
            }
        }

        // Show cursor
        graphics.set_pen(WHITE);
        graphics.text(">", Point(0, 30 * (cursor - skip)), 30);
        graphics.rectangle(Rect(315, 240 * skip / count,
                                5, 240 * AP_DISPLAYED_AT_ONCE / count));

        // Show current page of APs (continuous scrolling)
        auto i = -1;
        for (const auto &ap: scanned_aps) {
            if (skip-- > 0) {
                continue;
            }
            i++;

            // Pinned AP highlight
            if (pinned_ssid == ap.ssid) {
                graphics.set_pen(DARKDARKGREY);
                graphics.rectangle(Rect(0, i * 30, 310, 30));
            }

            // AP name
            graphics.set_pen(WHITE);
            graphics.text(ap.ssid, Point(15, i * 30), 200);

            if (mode == 0) {
                // Display RSSI-based signal strength percentage
                stringstream ss;
                ss << static_cast<int>(rssiToPercent(ap.rssi)) << "%";
                auto strength = ss.str();
                graphics.text(strength, Point(310 - graphics.measure_text(strength, 3), i * 30), 60, 3);
            } else if (mode == 1) {
                // Display interference scoring, I'd argue this is way more important than
                // RSSI in the real world because interference is most of the problem.
                auto lower_bound_strength = ap.rssi - 10;
                auto channel = ap.channels_bitflag;
                auto score = 0;
                for (auto &other_ap: scanned_aps) {
                    // If you're 10 times stronger than the "interfering signal" you're gucci
                    if (other_ap.rssi <= lower_bound_strength) {
                        break;
                    }

                    // 802.11b/g/n checks for 4 adjacent channels on each side, we're only going to do 2 for simplicity
                    if (channel & other_ap.channels_bitflag) {
                        // Direct clash
                        // Guest network check (similar first 4 bytes of BSSID)
                        if (ap.bssid.substr(0, 11) != other_ap.bssid.substr(0, 11)) {
                            score = max(score, 3);
                        }
                    } else if ((channel << 1) & other_ap.channels_bitflag ||
                               (channel >> 1) & other_ap.channels_bitflag) {
                        // Moderate interference
                        score = max(score, 2);
                    } else if ((channel << 2) & other_ap.channels_bitflag ||
                               (channel >> 2) & other_ap.channels_bitflag) {
                        // Mild interference
                        score = max(score, 1);
                    }
                }

                // Discrete nominal interference score
                if (score == 3) {
                    graphics.set_pen(RED);
                    graphics.text("CLASH", Point(310 - graphics.measure_text("CLASH", 3), i * 30), 60, 3);
                } else if (score == 2) {
                    graphics.set_pen(ORANGE);
                    graphics.text("BAD", Point(310 - graphics.measure_text("BAD", 3), i * 30), 60, 3);
                } else if (score == 1) {
                    graphics.set_pen(YELLOW);
                    graphics.text("NOISY", Point(310 - graphics.measure_text("NOISY", 3), i * 30), 60, 3);
                } else {
                    graphics.set_pen(GREEN);
                    graphics.text("GOOD", Point(310 - graphics.measure_text("GOOD", 3), i * 30), 60, 3);
                }
            }
        }

        st7789.update(&graphics);
    }
}

int main() {
    stdio_init_all();

//    while (!stdio_usb_connected()) {
//        cout << "Waiting for USB Serial..." << endl;
//        sleep_ms(100);
//    }
//    cout << "Hello!" << endl;

    multicore_launch_core1(display_thread);
    wifi_scan_thread();
}
