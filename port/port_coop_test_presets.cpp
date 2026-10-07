/* coop_tests.json loader for the coop-test mode (port_coop_test.c).
 *
 * {
 *   "presets": [
 *     { "name": "enemies", "description": "...",
 *       "warp": "0x48,0x09,0x88,0xb8,1",      // area,room,x,y,layer (docs/env-vars.md)
 *       "all_items": true, "mid_game_flags": true }
 *   ]
 * }
 */

#include "port_coop_test.h"
#include "port_exe_path.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

static void CopyStr(char* dst, size_t cap, const std::string& s) {
    std::snprintf(dst, cap, "%s", s.c_str());
}

extern "C" int Port_CoopTest_LoadPresets(PortCoopTestPreset* out, int max, char* path_out, int path_len) {
    std::filesystem::path path = "coop_tests.json";
    if (auto dir = port::ExecutableDir()) {
        std::filesystem::path p = *dir / "coop_tests.json";
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
            path = p;
    }
    if (path_out && path_len > 0)
        std::snprintf(path_out, (size_t)path_len, "%s", path.string().c_str());

    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "[coop-test] cannot open %s\n", path.string().c_str());
        return 0;
    }
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false, true);
    if (j.is_discarded() || !j.is_object() || !j.contains("presets") || !j["presets"].is_array()) {
        std::fprintf(stderr, "[coop-test] %s: invalid JSON (expected {\"presets\": [...]})\n",
                     path.string().c_str());
        return 0;
    }

    int n = 0;
    for (const auto& e : j["presets"]) {
        if (n >= max)
            break;
        if (!e.is_object() || !e.contains("name") || !e["name"].is_string() || !e.contains("warp") ||
            !e["warp"].is_string()) {
            std::fprintf(stderr, "[coop-test] skipping preset without name/warp\n");
            continue;
        }
        int a = 0, r = 0, x = 0, y = 0, l = 1;
        const std::string warp = e["warp"].get<std::string>();
        if (std::sscanf(warp.c_str(), "%i,%i,%i,%i,%i", &a, &r, &x, &y, &l) < 4) {
            std::fprintf(stderr, "[coop-test] preset %s: bad warp \"%s\" (want area,room,x,y,layer)\n",
                         e["name"].get<std::string>().c_str(), warp.c_str());
            continue;
        }
        PortCoopTestPreset* p = &out[n++];
        std::memset(p, 0, sizeof(*p));
        CopyStr(p->name, sizeof(p->name), e["name"].get<std::string>());
        CopyStr(p->description, sizeof(p->description), e.value("description", std::string()));
        p->area = (unsigned char)a;
        p->room = (unsigned char)r;
        p->x = (unsigned short)x;
        p->y = (unsigned short)y;
        p->layer = (unsigned char)l;
        p->all_items = e.value("all_items", true) ? 1 : 0;
        p->mid_game_flags = e.value("mid_game_flags", true) ? 1 : 0;
    }
    return n;
}
