/* Coop Test tab — jump to a coop_tests.json preset (see port_coop_test.c).
 * Included into port_imgui_menu.cpp. */
static int sCoopTestSel = 0;

static void DrawRibbonCoopTestTab(void) {
    const int n = Port_CoopTest_PresetCount();
    ImGui::TextDisabled("Presets: %s", Port_CoopTest_PresetsPath());
    ImGui::SameLine();
    if (ImGui::Button("Reload##coop"))
        Port_CoopTest_ReloadPresets();
    if (n <= 0) {
        ImGui::TextUnformatted("No presets loaded (coop_tests.json missing or invalid).");
        return;
    }
    if (sCoopTestSel >= n)
        sCoopTestSel = 0;
    if (ImGui::BeginListBox("##coopPresets", ImVec2(-FLT_MIN, 8 * ImGui::GetTextLineHeightWithSpacing()))) {
        for (int i = 0; i < n; ++i) {
            const PortCoopTestPreset* p = Port_CoopTest_Preset(i);
            char label[128];
            std::snprintf(label, sizeof(label), "%-12s  area 0x%02X room 0x%02X", p->name, p->area, p->room);
            if (ImGui::Selectable(label, sCoopTestSel == i))
                sCoopTestSel = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && Port_CoopTest_Go(i))
                Port_DebugMenu_ToastFromExternal("Coop test: warping");
        }
        ImGui::EndListBox();
    }
    const PortCoopTestPreset* sel = Port_CoopTest_Preset(sCoopTestSel);
    ImGui::TextWrapped("%s", sel->description);
    ImGui::Text("warp %u,%u (%u,%u) layer %u%s%s", sel->area, sel->room, sel->x, sel->y, sel->layer,
                sel->all_items ? "  +all items" : "", sel->mid_game_flags ? "  +mid-game flags" : "");
    if (ImGui::Button("Go##coop")) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), Port_CoopTest_Go(sCoopTestSel) ? "Coop test -> %s" : "Coop test: can't warp now (%s)",
                      sel->name);
        Port_DebugMenu_ToastFromExternal(msg);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("From the title screen this boots a fresh in-memory save; don't save in-game.");
}
