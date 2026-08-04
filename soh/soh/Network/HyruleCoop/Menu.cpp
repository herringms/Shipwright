#include "HyruleCoop.h"

#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"
#include "soh/util.h"

namespace {

void DrawDirectCoopMenu(WidgetInfo&) {
    HyruleCoop::Manager* manager = HyruleCoop::Manager::Instance;
    std::string name = CVarGetString(CVAR_REMOTE_HYRULE_COOP("Name"), "Link");
    std::string address = CVarGetString(CVAR_REMOTE_HYRULE_COOP("Address"), "127.0.0.1");
    uint16_t port = CVarGetInteger(CVAR_REMOTE_HYRULE_COOP("Port"), 43390);

    ImGui::SeparatorText("Hyrule Co-op Proof of Concept 2");
    ImGui::TextWrapped(
        "Host creates the shared world. Join connects directly to the host address using the same TCP and UDP port.");
    ImGui::Spacing();

    ImGui::BeginDisabled(manager->IsActive());
    ImGui::Text("Player Name");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##HyruleCoopName", &name, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_HYRULE_COOP("Name"), name.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::Text("Host Address (Join Game only)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##HyruleCoopAddress", &address, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_HYRULE_COOP("Address"), address.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::Text("TCP + UDP Port");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    UIWidgets::PushStyleInput(THEME_COLOR);
    if (ImGui::InputScalar("##HyruleCoopPort", ImGuiDataType_U16, &port)) {
        CVarSetInteger(CVAR_REMOTE_HYRULE_COOP("Port"), port);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    UIWidgets::PopStyleInput();
    ImGui::TextDisabled("Default: 43390. Host and guest must use the same port.");

    const bool validName = !SohUtils::IsStringEmpty(name);
    const bool validPort = port > 1024;
    ImGui::BeginDisabled(!validName || !validPort);
    const float connectionButtonWidth =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    if (UIWidgets::Button("Host Game", UIWidgets::ButtonOptions()
                                          .Size(ImVec2(connectionButtonWidth, 0))
                                          .Color(UIWidgets::Colors::Green))) {
        manager->Host(port, name);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(SohUtils::IsStringEmpty(address));
    if (UIWidgets::Button("Join Game", UIWidgets::ButtonOptions()
                                          .Size(ImVec2(connectionButtonWidth, 0))
                                          .Color(UIWidgets::Colors::Blue))) {
        manager->Join(address, port, name);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (manager->IsActive()) {
        if (UIWidgets::Button("Disconnect", UIWidgets::ButtonOptions()
                                                .Size(ImVec2(ImGui::GetContentRegionAvail().x, 0))
                                                .Color(UIWidgets::Colors::Red))) {
            manager->Disconnect();
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Status");
    ImGui::TextWrapped("%s", manager->GetStatusText().c_str());
    if (manager->IsReady()) {
        ImGui::TextWrapped("Proof active: the host owns the clock, durable scene flags, and Deku Baba state.");
    }
}

void RegisterDirectCoopMenu() {
    WidgetPath path = { "Network", "Direct Co-op", SECTION_COLUMN_1 };
    SohGui::GetSohMenu()->AddWidget(path, "DirectCoopMainMenu", WIDGET_CUSTOM)
        .CustomFunction(DrawDirectCoopMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc menuInitFunc(RegisterDirectCoopMenu);

} // namespace
