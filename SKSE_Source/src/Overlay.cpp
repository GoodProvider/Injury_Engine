#include "Overlay.h"

#include "InjuryEngine.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

// Includes <windows.h>, so it comes after CommonLib (PCH) and last here.
#include "MagelightUI_API.h"

namespace Overlay
{
    namespace
    {
        const MAGELIGHT_API::MagelightApi4* g_api = nullptr;
        MAGELIGHT_API::ModId g_mod = 0;
        MAGELIGHT_API::ViewId g_view = 0;
        std::atomic<bool> g_enabled = false;
        bool g_shown = false;  // game thread only

        std::mutex g_lock;
        std::string g_lastPush;  // last OverlayJson sent to the page
        // One pending push and at most one queued task: the task sends whatever is newest when it runs, so the view's
        // capped outbound queue can't fill while the page loads. Each push carries a growing sequence number and the
        // page ignores an older one (InvokeJS is in order, so this is only a guard). A reset rides on the same call,
        // so it can't overtake (or be overtaken by) an update.
        std::string g_pendingJson;
        bool g_pendingReset = false;
        bool g_taskQueued = false;
        std::uint64_t g_seq = 0;

        bool Ready()
        {
            return g_api && g_view && g_api->IsViewValid(g_view);
        }

        // Absolute UTF-8 path: Data\Magelight\Injury_Engine\views\overlay\index.html next to the exe.
        std::string PagePath()
        {
            wchar_t buf[MAX_PATH]{};
            GetModuleFileNameW(nullptr, buf, MAX_PATH);
            const auto path = std::filesystem::path(buf).parent_path() / L"Data" / L"Magelight" / L"Injury_Engine" /
                              L"views" / L"overlay" / L"index.html";
            const auto u = path.u8string();
            return std::string(reinterpret_cast<const char*>(u.data()), u.size());
        }

        // Game thread (ModDesc default). Logging only.
        void OnEvent(const MAGELIGHT_API::EventData* ev, void*)
        {
            using MAGELIGHT_API::Event;
            switch (ev->type) {
            case Event::ViewDomReady:
                SKSE::log::info("Overlay: DomReady");
                break;
            case Event::ViewLoadFailed:
                SKSE::log::error("Overlay: page load failed: {}", ev->detail);
                break;
            case Event::RenderDead:
                SKSE::log::warn("Overlay: Magelight disabled itself after a render fault");
                break;
            case Event::ConsoleMessage:
                if (ev->x == 2) {
                    SKSE::log::error("Overlay: page error: {}", ev->detail);
                }
                break;
            default:
                break;
            }
        }

        bool BlockingMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui) {
                return true;
            }
            return ui->GameIsPaused() || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) ||
                   ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
                   ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
        }

        // Game thread: shown while enabled and no blocking menu is open (the page is transparent when idle).
        void UpdateVisibility()
        {
            if (!Ready()) {
                return;
            }
            const bool show = g_enabled.load() && !BlockingMenuOpen();
            if (show == g_shown) {
                return;
            }
            g_shown = show;
            g_api->ShowView(g_view, show);
        }

        // `json` is an OverlayJson state, or "null" (reset only: the panel stays empty).
        void Push(std::string json, bool reset)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                return;
            }
            {
                std::lock_guard lock(g_lock);
                g_pendingJson = std::move(json);
                g_pendingReset = g_pendingReset || reset;
                if (g_taskQueued) {
                    return;
                }
                g_taskQueued = true;
            }
            tasks->AddTask([]() {
                std::string script;
                {
                    std::lock_guard lock(g_lock);
                    g_taskQueued = false;
                    script = "injuryEngineUpdate(" + g_pendingJson + "," + std::to_string(++g_seq) + "," +
                             (g_pendingReset ? "true" : "false") + ");";
                    g_pendingJson.clear();
                    g_pendingReset = false;
                }
                if (Ready()) {
                    g_api->InvokeJS(g_view, script.c_str());
                }
            });
        }

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent*,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                // Menu state settles after the event; check on the next frame.
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask(UpdateVisibility);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void Init()
    {
        static std::once_flag once;
        std::call_once(once, []() {
            g_api = MAGELIGHT_API::RequestApi4();
            if (!g_api) {
                SKSE::log::warn("Overlay: Magelight UI not installed, overlay disabled");
                return;
            }

            MAGELIGHT_API::ModDesc mod{};
            mod.size = sizeof(mod);
            mod.modId = "Injury_Engine";
            mod.displayName = "Injury Engine";
            mod.minHostVersion = MAGELIGHT_API::PackVersion(1, 7, 1);
            mod.onEvent = OnEvent;
            if (const auto result = g_api->RegisterMod(&mod, &g_mod); result != MAGELIGHT_API::Result::Ok) {
                SKSE::log::warn("Overlay: Magelight UI {} refused the mod (result {}; 1.7.1+ required), overlay disabled",
                    g_api->hostVersion, static_cast<int>(result));
                g_api = nullptr;
                return;
            }

            // No DOM-ready gate: calls made before the page loads are queued and delivered after it.
            const std::string page = PagePath();
            MAGELIGHT_API::ViewDesc view{};
            view.size = sizeof(view);
            view.name = "overlay";
            view.htmlPath = page.c_str();
            view.fullscreen = true;
            view.clickThrough = true;
            view.startVisible = false;
            view.layer = MAGELIGHT_API::Layer::Hud;
            if (g_api->CreateViewEx(g_mod, &view, &g_view) != MAGELIGHT_API::Result::Ok) {
                SKSE::log::error("Overlay: CreateViewEx failed: {}", g_api->GetLastErrorMessage(g_mod));
                g_api = nullptr;
                return;
            }
            g_shown = false;
            if (auto* ui = RE::UI::GetSingleton()) {
                static MenuSink sink;
                ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
            }
            SKSE::log::info("Overlay: view created");
        });
    }

    void SetEnabled(bool enabled)
    {
        if (g_enabled.exchange(enabled) == enabled) {
            return;
        }
        SKSE::log::info("Overlay: {}", enabled ? "enabled" : "disabled");
        if (!enabled) {
            Reset();
        }
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(UpdateVisibility);
        }
        Notify();
    }

    void Notify()
    {
        if (!g_enabled.load() || !Ready()) {
            return;
        }
        std::string json = InjuryEngine::OverlayJson();
        {
            std::lock_guard lock(g_lock);
            if (json == g_lastPush) {
                return;
            }
            g_lastPush = json;
        }
        SKSE::log::debug("Overlay: push {} bytes", json.size());
        Push(std::move(json), false);
    }

    void Reset()
    {
        if (!Ready()) {
            std::lock_guard lock(g_lock);
            g_lastPush.clear();
            return;
        }
        // Enabled: drop the old bars and show the current state in the same call; disabled: leave the panel empty.
        std::string json = g_enabled.load() ? InjuryEngine::OverlayJson() : std::string("null");
        {
            std::lock_guard lock(g_lock);
            g_lastPush = json == "null" ? std::string{} : json;
        }
        Push(std::move(json), true);
    }
}
