#include <spdlog/sinks/basic_file_sink.h>

#include "Classifier.h"
#include "InjuryConfig.h"
#include "InjuryEngine.h"
#include "Overlay.h"

bool Register_Engine_Functions(RE::BSScript::IVirtualMachine* a_vm);
void Install_Hooks();

namespace
{
    constexpr std::uint32_t kCoSaveId = 'SNWH';

    void InitLog()
    {
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            return;
        }
        auto path = *dir / "SkyrimNet_Whipped.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto logger = std::make_shared<spdlog::logger>("log", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
        spdlog::set_pattern("[%H:%M:%S] [%l] %v");
    }

    void CoSave_OnSave(SKSE::SerializationInterface* intfc)
    {
        InjuryEngine::Save(intfc);
    }

    void CoSave_OnLoad(SKSE::SerializationInterface* intfc)
    {
        std::uint32_t type = 0, version = 0, length = 0;
        while (intfc->GetNextRecordInfo(type, version, length)) {
            if (type == InjuryEngine::kRecord) {
                InjuryEngine::Load(intfc, version, length);
            }
        }
    }

    void CoSave_OnRevert(SKSE::SerializationInterface*)
    {
        InjuryEngine::Revert();
    }

    void OnMessage(SKSE::MessagingInterface::Message* msg)
    {
        switch (msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            InjuryConfig::Load();
            Classifier::Init();
            Overlay::Init();
            break;
        case SKSE::MessagingInterface::kNewGame:
        case SKSE::MessagingInterface::kPostLoadGame:
            Overlay::Reset();
            break;
        default:
            break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    InitLog();
    SKSE::log::info("SkyrimNet_Whipped loading");

    if (auto* ser = SKSE::GetSerializationInterface()) {
        ser->SetUniqueID(kCoSaveId);
        ser->SetSaveCallback(CoSave_OnSave);
        ser->SetLoadCallback(CoSave_OnLoad);
        ser->SetRevertCallback(CoSave_OnRevert);
    }

    Install_Hooks();

    if (auto* messaging = SKSE::GetMessagingInterface(); !messaging || !messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("Failed to register SKSE message listener");
    }

    if (auto* papyrus = SKSE::GetPapyrusInterface(); !papyrus || !papyrus->Register(Register_Engine_Functions)) {
        SKSE::log::error("Failed to register Papyrus functions");
    } else {
        SKSE::log::info("Papyrus functions registered");
    }
    return true;
}
