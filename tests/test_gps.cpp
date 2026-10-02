#include "gps.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;

// ================================================================================

static void expect(bool cond, const char *name) {
    if (cond) {
        std::cout << "PASS " << name << std::endl;
        return;
    }
    std::cout << "FAIL " << name << std::endl;
    failures += 1;
}

// ================================================================================

static bool near(double value, double expected, double tolerance) {
    return std::fabs(value - expected) < tolerance;
}

// ================================================================================

static void feed_text(GpsDecoder *decoder, const std::string &text, int64_t now) {
    decoder->feed(
            reinterpret_cast<const uint8_t *>(text.data()),
            text.size(),
            now);
}

// ================================================================================

static void feed_bytes(GpsDecoder *decoder, const char *csv, int64_t now) {
    std::vector<uint8_t> bytes;
    const std::string text(csv);
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string token = text.substr(
                start,
                comma == std::string::npos ? std::string::npos : comma - start);
        bytes.push_back(static_cast<uint8_t>(std::stoi(token)));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    decoder->feed(bytes.data(), bytes.size(), now);
}

// ================================================================================

static const char kNavPvt[] =
        "181,98,1,7,92,0,0,0,0,0,234,7,10,1,17,30,"
        "0,3,0,0,0,0,128,178,230,14,3,129,0,17,200,173,"
        "224,3,55,75,206,31,0,0,0,0,10,40,0,0,94,1,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "0,0,165,209";

static const char kNavStatus[] =
        "181,98,1,3,16,0,0,0,0,0,0,0,0,16,0,0,"
        "0,0,0,0,0,0,36,253";

static const char kMonRf[] =
        "181,98,10,56,28,0,0,1,0,0,0,2,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "0,0,97,153";

static const char kSbfPvt[] =
        "36,64,21,123,167,15,96,0,0,0,0,0,0,0,5,0,"
        "37,141,15,181,118,205,237,63,210,100,101,106,64,17,189,63,"
        "0,0,0,0,0,160,76,64,0,0,60,66,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,14,0,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,35,0,0,0,0,0";

static const char kBestPos[] =
        "170,68,18,28,42,0,0,0,72,0,0,0,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"
        "50,0,0,0,27,172,238,71,64,174,74,64,104,208,208,63,"
        "193,5,26,64,0,0,0,0,0,128,36,64,0,0,0,0,"
        "0,0,0,0,10,215,163,60,143,194,245,60,0,0,0,0,"
        "0,0,0,0,0,0,0,0,0,0,0,0,0,18,0,0,"
        "0,0,0,0,96,190,164,16";

// ================================================================================

int main() {
    expect(gps_device_path_allowed(""), "empty GPS device is the scan path");
    expect(gps_device_path_allowed("/dev/ttyUSB0"), "ttyUSB path is allowed");
    expect(gps_device_path_allowed("/dev/ttyACM0"), "ttyACM path is allowed");
    expect(gps_device_path_allowed("/dev/ttyS0"), "ttyS path is allowed");
    expect(gps_device_path_allowed(
                   "/dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00"),
           "by-id GPS path is allowed");
    expect(gps_device_path_allowed("/tmp/x") == false, "non-serial path is rejected");
    expect(gps_device_path_allowed("/dev/ttyUSB0;reboot") == false,
           "shell metacharacters are rejected");
    expect(gps_device_path_allowed("/dev/ttyUSB0$(reboot)") == false,
           "command substitution is rejected");
    expect(gps_device_path_allowed("/dev/serial/by-id/../ttyUSB0") == false,
           "parent segments in a GPS path are rejected");
    expect(gps_device_path_allowed("/dev/ttyUSB0/extra") == false,
           "a GPS path cannot name a subdirectory");

    const GpsPortChoice ublox = gps_choose_port({
            "usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00"});
    expect(ublox.ok && ublox.kind == GpsKind::ublox
                    && ublox.device.find("-if00") != std::string::npos,
           "one u-blox by-id node selects that receiver");

    const GpsPortChoice septentrio = gps_choose_port({
            "usb-Septentrio_Septentrio_USB_Device_3820135-if00",
            "usb-Septentrio_Septentrio_USB_Device_3820135-if02"});
    expect(septentrio.ok && septentrio.kind == GpsKind::septentrio
                    && septentrio.device.find("-if02") != std::string::npos,
           "Septentrio interfaces group and prefer if02");

    const GpsPortChoice both = gps_choose_port({
            "usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00",
            "usb-Septentrio_Septentrio_USB_Device_3820135-if02"});
    expect(both.ok == false && both.error == "More than one GPS receiver is attached",
           "two receiver families are not guessed");

    const GpsPortChoice none = gps_choose_port({"usb-other-device-if00"});
    expect(none.ok == false && none.error == "No GPS receiver found",
           "unrecognized by-id names are not a receiver");

    const std::vector<std::string> ublox_log = gps_discovery_messages(
            "auto",
            {"usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00"});
    expect(ublox_log.size() >= 2
                    && ublox_log.back().find("kind ublox") != std::string::npos
                    && ublox_log.back().find("-if00") != std::string::npos
                    && ublox_log[1].find("u-blox") != std::string::npos,
           "discovery log names the u-blox node and selects it");

    const std::vector<std::string> septentrio_log = gps_discovery_messages(
            "auto",
            {"usb-Septentrio_Septentrio_USB_Device_3820135-if00",
             "usb-Septentrio_Septentrio_USB_Device_3820135-if02"});
    expect(septentrio_log.back().find("-if02") != std::string::npos
                    && septentrio_log.back().find("kind septentrio") != std::string::npos,
           "discovery log prefers the Septentrio data interface");

    const std::vector<std::string> both_log = gps_discovery_messages(
            "auto",
            {"usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00",
             "usb-Septentrio_Septentrio_USB_Device_3820135-if02"});
    expect(both_log.back() == "GPS discovery: More than one GPS receiver is attached",
           "discovery log reports two receivers");

    const std::vector<std::string> empty_log = gps_discovery_messages("auto", {});
    expect(empty_log.back() == "GPS discovery: No GPS receiver found",
           "discovery log reports an empty scan");

    GpsDecoder empty;
    const doggy::v1::Gps never = empty.publish(1000);
    expect(never.ok() == false && never.fix_type() == doggy::v1::GPS_FIX_NO_GPS
                    && never.has_latitude_deg() == false,
           "a decoder with no sentence reports no GPS");

    GpsDecoder nmea;
    const int64_t nmea_now = 1000000;
    feed_text(
            &nmea,
            "$GNRMC,092750.000,A,5321.6802,N,00630.3372,E,0.0,0.0,011026,,,A*74\n",
            nmea_now);
    feed_text(
            &nmea,
            "$GNGGA,092750.000,5321.6802,N,00630.3372,E,4,12,0.9,10.5,M,47.0,M,,*7A\n",
            nmea_now);
    feed_text(
            &nmea,
            "$GNGST,092750.000,1.0,0.4,0.3,90.0,0.30,0.40,0.80*70\n",
            nmea_now);
    const doggy::v1::Gps nmea_fix = nmea.publish(nmea_now);
    expect(nmea_fix.ok()
                    && nmea_fix.fix_unix_ms() == 1790846870000LL
                    && near(nmea_fix.latitude_deg(), 53.3613366667, 1e-6)
                    && near(nmea_fix.longitude_deg(), 6.50562, 1e-6)
                    && near(nmea_fix.altitude_amsl_m(), 10.5, 1e-6)
                    && nmea_fix.satellites() == 12
                    && nmea_fix.fix_type() == doggy::v1::GPS_FIX_RTK_FIXED
                    && near(nmea_fix.accuracy_m(), 0.5, 1e-6),
           "NMEA RMC date plus GGA and GST publish an RTK fix");

    GpsDecoder bad;
    feed_text(
            &bad,
            "$GNGGA,092750.000,5321.6802,N,00630.3372,E,1,08,0.9,10.0,M,0.0,M,,*00\n",
            nmea_now);
    const doggy::v1::Gps rejected = bad.publish(nmea_now);
    expect(rejected.ok() == false && rejected.fix_type() == doggy::v1::GPS_FIX_NO_GPS
                    && rejected.has_latitude_deg() == false,
           "a bad NMEA checksum does not publish a fix");

    const doggy::v1::Gps stale = nmea.publish(nmea_now + 6000);
    expect(stale.ok() == false && stale.fix_type() == doggy::v1::GPS_FIX_NO_FIX
                    && stale.has_latitude_deg() == false,
           "a fix older than five seconds is cleared");

    GpsDecoder ublox_decoder;
    ublox_decoder.set_kind(GpsKind::ublox);
    const int64_t ubx_now = 2000000;
    feed_bytes(&ublox_decoder, kNavPvt, ubx_now);
    const doggy::v1::Gps ubx_fix = ublox_decoder.publish(ubx_now);
    expect(ubx_fix.ok()
                    && ubx_fix.fix_unix_ms() == 1790875800250LL
                    && near(ubx_fix.latitude_deg(), 53.3613367, 1e-6)
                    && near(ubx_fix.longitude_deg(), 6.50562, 1e-6)
                    && near(ubx_fix.altitude_amsl_m(), 10.25, 1e-6)
                    && ubx_fix.satellites() == 17
                    && ubx_fix.fix_type() == doggy::v1::GPS_FIX_RTK_FIXED
                    && near(ubx_fix.accuracy_m(), 0.35, 1e-6),
           "UBX NAV-PVT publishes UTC time, RTK, and accuracy");

    feed_bytes(&ublox_decoder, kNavStatus, ubx_now + 10);
    feed_bytes(&ublox_decoder, kMonRf, ubx_now + 20);
    const doggy::v1::Gps ubx_health = ublox_decoder.publish(ubx_now + 20);
    expect(ubx_health.ok()
                    && ubx_health.spoofing() == doggy::v1::GPS_SPOOF_INDICATED
                    && ubx_health.jamming() == doggy::v1::GPS_JAM_WARNING
                    && near(ubx_health.latitude_deg(), 53.3613367, 1e-6)
                    && ubx_health.fix_type() == doggy::v1::GPS_FIX_RTK_FIXED,
           "UBX spoof and jam update without clearing the position");

    GpsDecoder sbf;
    sbf.set_kind(GpsKind::septentrio);
    const int64_t sbf_now = 3000000;
    feed_bytes(&sbf, kSbfPvt, sbf_now);
    const doggy::v1::Gps sbf_fix = sbf.publish(sbf_now);
    expect(sbf_fix.ok()
                    && sbf_fix.fix_unix_ms() == sbf_now
                    && sbf_fix.fix_type() == doggy::v1::GPS_FIX_RTK_FLOAT
                    && near(sbf_fix.altitude_amsl_m(), 10.25, 1e-4)
                    && near(sbf_fix.accuracy_m(), 0.35, 1e-6)
                    && sbf_fix.satellites() == 14
                    && near(sbf_fix.latitude_deg(), 53.3613366667, 1e-5),
           "SBF PVTGeodetic uses host time and ellipsoid minus undulation");

    GpsDecoder novatel;
    novatel.set_kind(GpsKind::novatel);
    const int64_t novatel_now = 4000000;
    feed_bytes(&novatel, kBestPos, novatel_now);
    const doggy::v1::Gps novatel_fix = novatel.publish(novatel_now);
    expect(novatel_fix.ok()
                    && novatel_fix.fix_unix_ms() == novatel_now
                    && novatel_fix.fix_type() == doggy::v1::GPS_FIX_RTK_FIXED
                    && near(novatel_fix.latitude_deg(), 53.3613367, 1e-6)
                    && near(novatel_fix.longitude_deg(), 6.50562, 1e-6)
                    && near(novatel_fix.altitude_amsl_m(), 10.25, 1e-6)
                    && near(novatel_fix.accuracy_m(), 0.0360555, 1e-4)
                    && novatel_fix.satellites() == 18,
           "NovAtel BESTPOS publishes a fixed solution from host time");

    return failures == 0 ? 0 : 1;
}
