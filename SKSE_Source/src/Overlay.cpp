#include "Overlay.h"

#include "InjuryEngine.h"
#include "PrismaUI_API.h"

#include <atomic>
#include <mutex>
#include <string>

namespace Overlay
{
    namespace
    {
        PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
        PrismaView g_view = 0;
        std::atomic<bool> g_domReady = false;
        std::atomic<bool> g_enabled = false;
        bool g_shown = false;  // game thread only

        std::mutex g_lock;
        std::string g_lastPush;  // last OverlayJson sent to the page
        // One pending push and at most one queued task: the task sends whatever is newest when it runs. Each push
        // carries a growing sequence number and the page ignores an older one, since separate PrismaUI invokes may be
        // applied out of order. A reset rides on the same call, so it can't overtake (or be overtaken by) an update.
        std::string g_pendingJson;
        bool g_pendingReset = false;
        bool g_taskQueued = false;
        std::uint64_t g_seq = 0;

        bool Ready()
        {
            return g_prisma && g_domReady.load() && g_prisma->IsValid(g_view);
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
            if (show) {
                g_prisma->Show(g_view);
            } else {
                g_prisma->Hide(g_view);
            }
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
                    g_prisma->Invoke(g_view, script.c_str());
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
            g_prisma = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
                PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
            if (!g_prisma) {
                SKSE::log::warn("Overlay: PrismaUI not installed, overlay disabled");
                return;
            }
            // Resolves under Data/PrismaUI/views/.
            g_view = g_prisma->CreateView("Injury_Engine/overlay.html", [](PrismaView view) {
                g_view = view;
                g_domReady = true;
                SKSE::log::info("Overlay: DomReady");
                if (auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([]() {
                        UpdateVisibility();
                        Notify();
                    });
                }
            });
            if (!g_prisma->IsValid(g_view)) {
                SKSE::log::error("Overlay: CreateView failed (missing PrismaUI/views/Injury_Engine/overlay.html?)");
                g_prisma = nullptr;
                return;
            }
            g_prisma->Hide(g_view);
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
