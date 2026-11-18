// NuPhy WH80 keyboard, over the 2.4 GHz dongle (19F5:8F01) or the USB cable (19F5:A011).
// Protocol from NuPhyIO's web app (https://drive.nuphy.io/static/js/main.b3fb29e0.js): createCommand()
// builds the 64-byte payload [55][05][00][sum][01][20][00][00][data...]: byte 3 is the checksum,
// byte 4 the data length, bytes 5-6 the address, byte 7 zero, data from byte 8. getBatteryState()
// uses GetFunc = 5 with the level at DATA_INDEX = 8, bit 7 = charging. Verified live on the dongle:
// 86 %, not charging.
// Read-only: this driver only ever sends that one battery request.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x19F5, kDongle = 0x8F01, kCable = 0xA011;

bool Supported(USHORT pid) { return pid == kDongle || pid == kCable; }

BYTE Sum(const std::vector<BYTE>& data, size_t from) {
    unsigned sum = 0;
    for (size_t i = from; i < data.size(); ++i) sum += data[i];
    return (BYTE)(sum & 0xFF);
}

}  // namespace

void PollNuPhy(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(kVid)) {
        if (!Supported(pid)) continue;
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0x0001 && c.usage() == 0x0000 && c.inLen() == 65 && c.outLen() == 65;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            d.Flush();
            std::vector<BYTE> req(65, 0);  // [0] is the report id; the frame starts at [1]
            req[1] = 0x55;
            req[2] = 0x05;
            req[5] = 0x01;
            req[6] = 0x20;
            req[4] = Sum(req, 5);  // 0x21
            if (!d.Write(req)) continue;
            std::vector<BYTE> r;
            if (d.Read(r, 600) < 9) continue;
            if (r[0] != 0xAA || r[1] != 0x05 || r[4] != 0x01 || r[5] != 0x20 || r[6] != 0x00) continue;
            if (r[3] != Sum(r, 4)) continue;
            if ((r[8] & 0x7F) > 100) continue;
            int pct = r[8] & 0x7F;
            bool charging = (r[8] & 0x80) != 0;
            // The cable firmware reports 0xE4 (100 % + charging) at every mode address while
            // charging. Leave the level unknown so the UI keeps the last real percentage and
            // still shows the charging state.
            if (pid == kCable && charging && pct == 100) pct = -1;
            DeviceStatus s;
            s.key = L"nuphy:wh80";
            s.name = L"NuPhy WH80";
            s.kind = DeviceKind::Keyboard;
            s.via = pid == kDongle ? L"2.4 GHz receiver" : L"USB cable";
            s.percent = pct;
            s.charging = charging;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
