#include "Chat.h"
#include "Common.h"
#include "Config.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Define.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSessionMgr.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <time.h>
#include <vector>

// hardcoded until i can get a vector using GEtOption
struct Config
{
    bool   enabled     = true;
    uint32 kill_goal   = 0;
    uint32 kill_points = 0;

    std::unordered_map<uint32 /* zone */, std::vector<uint32> /* areas */> ids = {{10, {93, 536}}};

    /* non .conf stuff stuff */
    uint32 current_zone;
    uint32 current_area;

    std::string current_zone_name;
    std::string current_area_name;

    bool active = false;

    std::vector<Player*> area_players;
    std::vector<Player*> zone_players;

    std::map<Player*, uint32> points;

    float last_announcement = GameTime::GetGameTime().count();
    float announcement_delay;

    float last_event = 0;
    float event_delay;
    float event_lasts;
};

Config config;

class ZoneConfig : public WorldScript
{
public:
    ZoneConfig() : WorldScript("pvp_zones_Config", {
        WORLDHOOK_ON_STARTUP
    }) {}

    void OnStartup() override
    {
        config.enabled            = sConfigMgr->GetOption<bool>("pvp_zones.Enable", false);
        config.kill_goal          = sConfigMgr->GetOption<uint32>("pvp_zones.KillGoal", false);
        config.announcement_delay = sConfigMgr->GetOption<uint32>("pvp_zones.AnnouncementDelay", false);
        config.kill_points        = sConfigMgr->GetOption<uint32>("pvp_zones.KillPoints", false);
        config.event_delay        = sConfigMgr->GetOption<uint32>("pvp_zones.EventDelay", false);
        config.event_lasts        = sConfigMgr->GetOption<uint32>("pvp_zones.EventLasts", false);
        // config.zone_ids = sConfigMgr->GetOption<std::vector<uint32>>("pvp_zones.Zones", {});
        //    config.area_ids = sConfigMgr->GetOption<std::vector<uint32>>("pvp_zones.Areas", {});
    }
};

/* TODO: change class name */
class ZoneLogicScript : public PlayerScript
{
public:
    ZoneLogicScript() : PlayerScript("pvp_zones_PlayerScript", {
        PLAYERHOOK_ON_UPDATE_AREA,
        PLAYERHOOK_ON_PLAYER_PVP_FLAG_CHANGE,
        PLAYERHOOK_ON_UPDATE_ZONE,
        PLAYERHOOK_ON_PVP_KILL
    }) {}

    /* adding/removing players that are currently in the area */
    void OnPlayerUpdateArea(Player* player, uint32 /* oldArea*/, uint32 newArea) override
    {
        if (!config.enabled || !config.active)
            return;

        if (config.current_area == newArea)
        {
            ChatHandler((player->GetSession())).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
            config.area_players.push_back(player);
        }
        else
            config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
    }

    static bool isPlayerInZone(Player* player)
    {
        return std::find(config.zone_players.begin(), config.zone_players.end(), player) != config.zone_players.end();
    }

    void OnPlayerPVPFlagChange(Player* player, bool state) override
    {
        if (!config.enabled || !config.active)
            return;

        if (isPlayerInZone(player) && !state)
            player->SetPvP(true);
    }

    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /* new area */) override
    {
        if (!config.enabled || !config.active)
            return;

        /* un/flagging player as pvp.
           create some kind of pvp hook when pvp changes inside the zone
        */

        if (config.current_zone == newZone)
        {
            if (isPlayerInZone(player))
                return;
            ChatHandler((player->GetSession())).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.push_back(player);
            player->UpdatePvP(true, true);
        }
        else if (isPlayerInZone(player))
        {
            ChatHandler((player->GetSession())).SendSysMessage("You have left the Oceanic War cffFFFFFFblood zone!");
            config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
        }
    }

    void PostLeaderBoard(ChatHandler* handler)
    {
        handler->SendGlobalSysMessage("PvP Zones Leaderboard:");

        /* this should never happen */
        if (config.points.empty())
            return;

        for (auto& player : config.points)
            handler->PSendSysMessage("{}: {}", player.first->GetName(), player.second);
    }

    static void PostAnnouncement(ChatHandler* handler)
    {
        if (!config.active)
            return;
        if (config.last_announcement + config.announcement_delay < GameTime::GetGameTime().count())
        {
            handler->SendGlobalSysMessage(
                ("[pvp_zones] Is currently active in: " + config.current_zone_name + " - " + config.current_area_name).c_str());
            config.last_announcement = GameTime::GetGameTime().count();
        }
    }

    static void CreateEvent(ChatHandler* handler)
    {
        if (config.active)
        {
            handler->PSendSysMessage("[pvp_zones] Event already active");
            return;
        }

        config.active     = true;
        config.last_event = GameTime::GetGameTime().count();

        auto map_it = std::begin(config.ids);
        std::advance(map_it, rand() % config.ids.size());

        auto area_it = std::begin(map_it->second);
        std::advance(area_it, rand() % map_it->second.size());

        config.current_zone = map_it->first;
        config.current_area = *area_it;

        if (AreaTableEntry const* entry = sAreaTableStore.LookupEntry(config.current_area))
        {
            config.current_area_name = entry->area_name[handler->GetSessionDbcLocale()];

            if (AreaTableEntry const* z_entry = sAreaTableStore.LookupEntry(config.current_zone))
                config.current_zone_name = z_entry->area_name[handler->GetSessionDbcLocale()];
        }

        handler->SendGlobalSysMessage(("[pvp_zones] A new zone has been declared: " + config.current_zone_name + " - " + config.current_area_name).c_str());

        auto players = ObjectAccessor::GetPlayers();

        for (auto& player : players)
        {
            if (player.second->GetZoneId() == config.current_zone)
            {
                player.second->SetPvP(true);
                ChatHandler(player.second->GetSession()).SendSysMessage("You have entered the Oceanic War cffFFFFFFblood zone!");
                config.zone_players.push_back(player.second);
            }

            if (player.second->GetAreaId() == config.current_area)
                config.area_players.push_back(player.second);
        }
    }

    static void EndEvent(ChatHandler* handler)
    {
        config.active = false;
        config.last_event = GameTime::GetGameTime().count();
        handler->SendGlobalSysMessage("[pvp_zones] The event has ended.");

        /* reset */
        config.points.clear();
        config.area_players.clear();
        config.zone_players.clear();
    }

    void OnPlayerPVPKill(Player* winner /*killer*/, Player* loser /*killed*/) override
    {
        if (!config.enabled || !config.active)
            return;

        /* point calculation / checks */
        if (winner->GetAreaId() == config.current_area)
            config.kill_points *= 2;
        if (winner->GetZoneId() == config.current_zone)
        {
            /* populate config.points */
            if (config.points.find(winner) == config.points.end())
                config.points.insert(std::make_pair(winner, config.kill_points));

            if (config.points.find(loser) == config.points.end())
                config.points.insert(std::make_pair(loser, 0));

            config.kill_goal--;

            ChatHandler winner_handle = ChatHandler(winner->GetSession());

            config.points.find(winner)->second += config.kill_points;

            if (config.points.find(loser)->second > 0)
                config.points.find(loser)->second -= config.kill_points;

            if (config.kill_goal <= 0)
            {
                ChatHandler(winner->GetSession()).SendGlobalSysMessage("[pvp_zones] The goal has been reached!");
                EndEvent(&winner_handle);
            }

            ChatHandler(winner->GetSession()).SendSysMessage(("[pvp_zones] You have gained " + std::to_string(config.kill_points) + " PvP point(s)").c_str());
            ChatHandler(loser->GetSession()).SendSysMessage(("[pvp_zones] You have lost " + std::to_string(config.kill_points) + " PvP point(s)").c_str());

            if (config.kill_goal % /* TODO: put this higher, temporary test value */ 1 == 0)
                PostLeaderBoard(&winner_handle);
        }
    }
};

using namespace Acore::ChatCommands;
class ZoneCommands : public CommandScript
{
public:
    ZoneCommands() : CommandScript("pvp_zones_Commands") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTable =
        {
            { "pvp_zones_on",     HandleOnCommand,     SEC_GAMEMASTER, Console::No },
            { "pvp_zones_off",    HandleOffCommand,    SEC_GAMEMASTER, Console::No },
            { "pvp_zones_create", HandleCreateCommand, SEC_GAMEMASTER, Console::No },
            { "pvp_zones_end",    HandleEndCommand,    SEC_GAMEMASTER, Console::No },
            { "pvp_zones_debug",  HandleDebugCommand,  SEC_GAMEMASTER, Console::No }
        };

        return commandTable;
    }

    static bool HandleOnCommand(ChatHandler* handler)
    {
        config.enabled = true;
        handler->PSendSysMessage("PvP Zones Enabled");

        return true;
    }
    static bool HandleOffCommand(ChatHandler* handler)
    {
        config.enabled = false;
        handler->PSendSysMessage("PvP Zones Disabled");

        return true;
    }

    static bool HandleCreateCommand(ChatHandler* handler)
    {
        ZoneLogicScript::CreateEvent(handler);

        return true;
    }

    static bool HandleEndCommand(ChatHandler* handler)
    {
        ZoneLogicScript::EndEvent(handler);

        return true;
    }

    static bool HandleDebugCommand(ChatHandler* /* handler */)
    {
        LOG_INFO("module", "[pvp_zones] Debug: active: {}, arena_name: {}, zone_name: {}, last_announcement: {}, last_event: {}, next_announcement: {}s", config.active, config.current_area_name, config.current_zone_name, config.last_announcement, config.last_event, (config.last_announcement + config.announcement_delay) - GameTime::GetGameTime().count());
        return true;
    }
};

class ZoneWorld : public WorldScript
{
public:
    ZoneWorld() : WorldScript("pvp_zones_World", {
        WORLDHOOK_ON_UPDATE
    }) {}

    void OnUpdate(uint32 /* p_time */) override
    {
        if (!config.enabled)
            return;

        WorldSessionMgr::SessionMap m_sessions = sWorldSessionMgr->GetAllSessions();
        Player* player = nullptr;

        for (WorldSessionMgr::SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        {
            if (!itr->second || !itr->second->GetPlayer() || !itr->second->GetPlayer()->IsInWorld())
                continue;

            player = itr->second->GetPlayer();
        }

        if (!player)
            return;

        ChatHandler handler(player->GetSession());

        if (config.active && config.last_event + config.event_lasts < GameTime::GetGameTime().count())
            ZoneLogicScript::EndEvent(&handler);

        if (!config.active && config.last_event + config.event_delay < GameTime::GetGameTime().count())
            ZoneLogicScript::CreateEvent(&handler);

        if (config.active && config.last_announcement + config.announcement_delay <= GameTime::GetGameTime().count())
            ZoneLogicScript::PostAnnouncement(&handler);
    }
};

void Addpvp_zonesScripts()
{
    new ZoneWorld();
    new ZoneConfig();
    new ZoneLogicScript();
    new ZoneCommands();
}
