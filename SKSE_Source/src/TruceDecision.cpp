#include "TruceDecision.h"

// PublicAPI.h defines its function-pointer globals: include it in this .cpp only.
#include <Windows.h>
#include "PublicAPI.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace TruceDecision
{
    namespace
    {
        constexpr const char* kTruceTemplate = "injury_engine/accept_truce";
        constexpr const char* kSurrenderTemplate = "injury_engine/accept_surrender";
        constexpr int kMinApiVersion = 12;
        constexpr int kMaxProbabilities = 2;
        constexpr int kTimelineEvents = 30;  // events asked for; speech is split into more lines
        constexpr int kTimelineLines = 15;   // lines kept, newest

        // Debug CRT (/MTd) must not consume std::string returned by SkyrimNet (/MD).
        bool CrossDllStdStringSafe()
        {
#ifdef _DEBUG
            return false;
#else
            return true;
#endif
        }

        bool g_apiFound = false;  // FindFunctions at kDataLoaded
        // Game thread only:
        struct Waiting
        {
            RE::FormID target = 0;
            double since = 0.0;  // real seconds
        };
        std::unordered_map<RE::FormID, Waiting> g_inFlight;  // speakers waiting for an answer
        // A request older than this no longer blocks its speaker (a player box closed without a callback, a lost
        // decision reply).
        constexpr double kInFlightTimeout = 60.0;
        bool g_noRoute = false;  // no decisions provider: stop asking until the next load
        bool g_noRouteWarned = false;
        bool g_apiWarned = false;

        std::mutex g_answerLock;
        std::vector<Answer> g_answers;

        bool ApiReady()
        {
            return g_apiFound && CrossDllStdStringSafe() && PublicGetVersion && PublicGetVersion() >= kMinApiVersion &&
                   PublicSendCustomDecisionToLLM;
        }

        std::uint64_t UuidOf(RE::FormID id)
        {
            return CrossDllStdStringSafe() && PublicFormIDToUUID ? PublicFormIDToUUID(id) : 0;
        }

        std::string NameOf(RE::Actor* actor)
        {
            const char* name = actor ? actor->GetDisplayFullName() : nullptr;
            return name && *name ? name : "Someone";
        }

        // Skyrim relationship rank between two actors (copied from SkyrimNet_SexLab StrategyDecision.cpp).
        // The leveled-actor case falls back to the template bases. No relationship form: strangers.
        std::string Relationship(RE::Actor* a, RE::Actor* b)
        {
            if (!a || !b) {
                return "strangers";
            }
            RE::BGSRelationship* rel = RE::BGSRelationship::GetRelationship(a->GetActorBase(), b->GetActorBase());
            if (!rel) {
                auto* ta = a->GetTemplateBase();
                auto* tb = b->GetTemplateBase();
                if (ta && tb && (ta != a->GetActorBase() || tb != b->GetActorBase())) {
                    rel = RE::BGSRelationship::GetRelationship(ta, tb);
                }
            }
            if (!rel) {
                return "strangers";
            }
            static constexpr const char* kLabels[] = { "lovers", "allies", "confidants", "friends", "acquaintances",
                "rivals", "foes", "enemies", "archnemeses" };
            const auto level = static_cast<int>(rel->level.get());
            return level >= 0 && level <= 8 ? kLabels[level] : "strangers";
        }

        // Health as words (no numbers reach the LLM).
        std::string HealthWord(RE::Actor* actor)
        {
            auto* av = actor ? actor->AsActorValueOwner() : nullptr;
            const float maxHealth = av ? av->GetPermanentActorValue(RE::ActorValue::kHealth) : 0.0f;
            if (maxHealth <= 0.0f) {
                return "unhurt";
            }
            const float ratio = av->GetActorValue(RE::ActorValue::kHealth) / maxHealth;
            return ratio >= 0.9f ? "unhurt" : ratio >= 0.6f ? "lightly hurt" : ratio >= 0.25f ? "badly hurt" : "near death";
        }

        std::string Trim(const std::string& s)
        {
            const auto b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) {
                return "";
            }
            return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
        }

        // "stop *bob lowers his sword*" by Bob -> "Bob said: stop", "Bob lowers his sword." (reduced copy of
        // SkyrimNet_SexLab SplitSpeech).
        void SplitSpeech(const std::string& speaker, const std::string& text, std::vector<std::string>& out)
        {
            std::size_t pos = 0;
            while (pos < text.size()) {
                const auto open = text.find('*', pos);
                const std::string spoken = Trim(text.substr(pos, open == std::string::npos ? open : open - pos));
                if (!spoken.empty()) {
                    out.push_back((speaker.empty() ? std::string("Someone") : speaker) + " said: " + spoken);
                }
                if (open == std::string::npos) {
                    break;
                }
                const auto close = text.find('*', open + 1);
                std::string narrated = Trim(text.substr(open + 1, close == std::string::npos ? close : close - open - 1));
                if (!narrated.empty()) {
                    narrated[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(narrated[0])));
                    out.push_back(narrated);
                }
                if (close == std::string::npos) {
                    break;
                }
                pos = close + 1;
            }
        }

        // The target's recent events, oldest first, one line each (hits, combat and spells left out).
        std::vector<std::string> BuildTimeline(RE::FormID target)
        {
            std::vector<std::string> out;
            if (!CrossDllStdStringSafe() || !PublicGetRecentEvents) {
                return out;
            }
            nlohmann::json events;
            try {
                events = nlohmann::json::parse(PublicGetRecentEvents(target, kTimelineEvents, ""), nullptr, false);
            } catch (...) {
                return out;
            }
            if (!events.is_array()) {
                return out;
            }
            std::vector<nlohmann::json> list(events.begin(), events.end());
            std::stable_sort(list.begin(), list.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
                const auto time = [](const nlohmann::json& e) {
                    return e.is_object() && e.contains("gameTime") && e["gameTime"].is_number() ? e["gameTime"].get<double>() : 0.0;
                };
                return time(a) < time(b);
            });
            for (const auto& e : list) {
                if (!e.is_object()) {
                    continue;
                }
                const std::string type = e.value("type", std::string{});
                const std::string text = Trim(e.value("text", std::string{}));
                const std::string who = e.value("originatingActorName", std::string{});
                if (text.empty() || type == "spell" || type == "hit" || type == "combat" || type == "npc_thoughts" ||
                    type == "gamemaster_dialogue") {
                    continue;
                }
                if (type.rfind("dialogue", 0) == 0) {
                    SplitSpeech(who, text, out);
                } else {
                    out.push_back(text);
                }
            }
            if (out.size() > static_cast<std::size_t>(kTimelineLines)) {
                out.erase(out.begin(), out.end() - kTimelineLines);
            }
            return out;
        }

        std::string BuildContext(RE::Actor* speaker, RE::Actor* target)
        {
            nlohmann::json ctx;
            ctx["speaker_uuid"] = UuidOf(speaker->GetFormID());
            ctx["speaker_name"] = NameOf(speaker);
            ctx["target_uuid"] = UuidOf(target->GetFormID());
            ctx["target_name"] = NameOf(target);
            ctx["relationship"] = Relationship(target, speaker);
            ctx["speaker_health"] = HealthWord(speaker);
            ctx["target_health"] = HealthWord(target);
            ctx["speaker_is_follower"] = speaker->IsPlayerTeammate();
            ctx["target_is_follower"] = target->IsPlayerTeammate();
            ctx["target_fights_speaker"] = target->IsInCombat() && target->GetActorRuntimeData().currentCombatTarget.get().get() == speaker;
            ctx["timeline"] = BuildTimeline(target->GetFormID());
            return ctx.dump();
        }

        void NotifyPapyrus()
        {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent event{ "Injury_Engine_Truce", "", 0.0f, nullptr };
                source->SendEvent(&event);
            }
        }

        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        void Push(RE::FormID speaker, RE::FormID target, bool accepted, bool yield);

        // Game thread. False when the request can go out (and marks `speaker` as waiting). A speaker still waiting on
        // another target gets an immediate refusal for this one, since Papyrus has already acted on it; a repeat for
        // the same target is ignored (its answer is coming).
        bool Busy(RE::FormID speaker, RE::FormID target, bool yield)
        {
            const double now = NowSeconds();
            if (const auto it = g_inFlight.find(speaker); it != g_inFlight.end()) {
                if (now - it->second.since <= kInFlightTimeout) {
                    if (it->second.target == target) {
                        SKSE::log::info("truce: {:08X} already waiting for an answer from {:08X}; duplicate ignored", speaker, target);
                    } else {
                        SKSE::log::info("truce: {:08X} still waiting on {:08X}; request to {:08X} refused", speaker,
                            it->second.target, target);
                        Push(speaker, target, false, yield);
                    }
                    return true;
                }
                SKSE::log::warn("truce: {:08X} request to {:08X} never answered; dropped", speaker, it->second.target);
            }
            g_inFlight[speaker] = { target, now };
            return false;
        }

        // Game thread. The request from `speaker` to `target` is answered (a newer one to another target stays).
        void Done(RE::FormID speaker, RE::FormID target)
        {
            if (const auto it = g_inFlight.find(speaker); it != g_inFlight.end() && it->second.target == target) {
                g_inFlight.erase(it);
            }
        }

        // Game thread.
        void Push(RE::FormID speaker, RE::FormID target, bool accepted, bool yield)
        {
            {
                std::lock_guard lock(g_answerLock);
                g_answers.push_back({ speaker, target, accepted, yield });
            }
            NotifyPapyrus();
        }

        // Failed request: no-provider latch, else a warning.
        void NoteFailure(RE::FormID speaker, const std::string& json)
        {
            std::string code;
            try {
                code = nlohmann::json::parse(json).value("error", std::string{});
            } catch (...) {
            }
            if (code == "no_decisions_route") {
                g_noRoute = true;
                if (!g_noRouteWarned) {
                    g_noRouteWarned = true;
                    SKSE::log::warn("truce: no decisions provider set up in SkyrimNet; Stop_Fighting offers are refused");
                }
            } else {
                SKSE::log::warn("truce: {:08X} decision failed: {}", speaker, json);
            }
        }

        // Game thread. The answer for `speaker` arrived (or failed).
        void OnAnswer(RE::FormID speaker, RE::FormID target, bool yield, int success, const std::string& answer,
            const std::string& json)
        {
            Done(speaker, target);
            if (!success) {
                NoteFailure(speaker, json);
                Push(speaker, target, false, yield);
                return;
            }
            const bool accepted = answer == "true" || answer == "yes";
            SKSE::log::info("{}: {:08X} -> {:08X} answer {} ({})", yield ? "surrender" : "truce", speaker, target,
                accepted ? "accepted" : "refused", json);
            Push(speaker, target, accepted, yield);
        }

        // The player's answer from the Yes / No box (button 0 = Yes).
        class PlayerChoice : public RE::IMessageBoxCallback
        {
        public:
            PlayerChoice(RE::FormID speaker, RE::FormID target, bool yield) :
                _speaker(speaker), _target(target), _yield(yield) {}

            void Run(std::uint8_t a_button) override
            {
                const RE::FormID speaker = _speaker;
                const RE::FormID target = _target;
                const bool yield = _yield;
                const bool accepted = a_button == 0;
                SKSE::GetTaskInterface()->AddTask([speaker, target, yield, accepted]() {
                    Done(speaker, target);
                    SKSE::log::info("{}: player {} {:08X}", yield ? "surrender" : "truce", accepted ? "accepted" : "refused", speaker);
                    Push(speaker, target, accepted, yield);
                });
            }

        private:
            RE::FormID _speaker;
            RE::FormID _target;
            bool _yield;
        };

        // Vanilla message box "<speaker> wishes to ... Do you accept?" with Yes / No (Esc = No). Queued with
        // MessageBoxData::QueueMessage (SE 51422 / AE 52271, as the SkyMessage plugin does); not on VR.
        bool ShowPlayerBox(RE::Actor* speaker, RE::FormID target, bool yield)
        {
            if (REL::Module::IsVR()) {
                SKSE::log::warn("truce: player message box not supported on VR");
                return false;
            }
            auto* factory = RE::MessageDataFactoryManager::GetSingleton();
            auto* strings = RE::InterfaceStrings::GetSingleton();
            const auto* creator = factory && strings ? factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData) : nullptr;
            auto* box = creator ? creator->Create() : nullptr;
            if (!box) {
                SKSE::log::warn("truce: could not create a message box");
                return false;
            }
            const std::string text = NameOf(speaker) + (yield ? " wishes to yield to you. Do you accept?" : " wishes to stop fighting. Do you accept?");
            box->bodyText = text.c_str();
            box->buttonText.push_back("Yes");
            box->buttonText.push_back("No");
            box->callback = RE::make_smart<PlayerChoice>(speaker->GetFormID(), target, yield);
            box->cancelButtonIndex = 1;
            box->isCancellable = true;
            box->warningType = 25;
            using QueueMessage_t = void(RE::MessageBoxData*);
            static REL::Relocation<QueueMessage_t> queue{ REL::RelocationID(51422, 52271) };
            queue(box);
            return true;
        }
    }

    void Init()
    {
        g_apiFound = FindFunctions();
        const int version = g_apiFound && PublicGetVersion ? PublicGetVersion() : 0;
        SKSE::log::info("truce: SkyrimNet API {} (version {}), decisions {}", g_apiFound ? "found" : "missing", version,
            ApiReady() ? "available" : "unavailable");
    }

    void Reset()
    {
        g_inFlight.clear();
        g_noRoute = false;
        std::lock_guard lock(g_answerLock);
        g_answers.clear();
    }

    void Ask(RE::Actor* speaker, RE::Actor* target, bool yield)
    {
        if (!speaker || !target) {
            return;
        }
        const RE::FormID speakerId = speaker->GetFormID();
        const RE::FormID targetId = target->GetFormID();
        if (target->IsPlayerRef()) {
            // The player answers with a Yes / No box; no decision model involved.
            if (Busy(speakerId, targetId, yield)) {
                return;
            }
            if (!ShowPlayerBox(speaker, targetId, yield)) {
                Done(speakerId, targetId);
                Push(speakerId, targetId, false, yield);
                return;
            }
            SKSE::log::info("{}: asking the player about {:08X}", yield ? "surrender" : "truce", speakerId);
            return;
        }
        if (!ApiReady()) {
            if (!g_apiWarned) {
                g_apiWarned = true;
                SKSE::log::warn("truce: SkyrimNet decision API (v{}+) unavailable; Stop_Fighting offers and surrenders are refused", kMinApiVersion);
            }
            Push(speakerId, targetId, false, yield);
            return;
        }
        if (g_noRoute) {
            Push(speakerId, targetId, false, yield);
            return;
        }
        if (Busy(speakerId, targetId, yield)) {
            return;
        }
        const std::string ctx = BuildContext(speaker, target);
        const bool queued = PublicSendCustomDecisionToLLM(yield ? kSurrenderTemplate : kTruceTemplate, ctx.c_str(),
            kMaxProbabilities, [speakerId, targetId, yield](const char* answer, const char* resultJson, int success) {
                // SkyrimNet ThreadPool: copy, then do the work on the game thread.
                std::string a = answer ? answer : "";
                std::string j = resultJson ? resultJson : "";
                SKSE::GetTaskInterface()->AddTask([speakerId, targetId, yield, success, a = std::move(a), j = std::move(j)]() {
                    OnAnswer(speakerId, targetId, yield, success, a, j);
                });
            });
        if (!queued) {
            Done(speakerId, targetId);
            SKSE::log::warn("truce: {:08X} request not queued; refused", speakerId);
            Push(speakerId, targetId, false, yield);
            return;
        }
        SKSE::log::info("{}: send {:08X} -> {:08X}: {}", yield ? "surrender" : "truce", speakerId, targetId, ctx);
    }

    std::vector<Answer> Take()
    {
        std::lock_guard lock(g_answerLock);
        return std::exchange(g_answers, {});
    }
}
