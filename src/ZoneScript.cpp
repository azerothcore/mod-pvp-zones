#include "Chat.h"
#include "Common.h"
#include "Config.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "Define.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
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

    time_t last_announcement = sWorld->GetGameTime();
    time_t announcement_delay;

    time_t last_event = 0;
    time_t event_delay;
    time_t event_lasts;
};

Config config;

class ZoneConfig : public WorldScript
{
public:
    ZoneConfig() : WorldScript("pvp_zones_Config") {}

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
class ZoneLogicScript : public PlayerScript, WorldScript
{
public:
    ZoneLogicScript() : PlayerScript("pvp_zones_PlayerScript"), WorldScript("pvp_zones_WorldScript") {}

    /* adding/removing players that are currently in the area */
    void OnUpdateArea(Player* player, uint32 /* oldArea*/, uint32 newArea) override
    {
        if (config.current_area == newArea)
        {
            ChatHandler((player->GetSession())).SendSysMessage("You have entered the PvP area");
            config.area_players.push_back(player);
        }
        else
        {
            config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        }
    }

    bool isPlayerInZone(Player* player)
    {
        return std::find(config.zone_players.begin(), config.zone_players.end(), player) != config.zone_players.end();
    }

    void OnPlayerPVPFlagChange(Player* player, bool state) override
    {
        if (isPlayerInZone(player) && !state)
        {
            player->SetPvP(true);
        }
    }

    void OnUpdateZone(Player* player, uint32 newZone, uint32 /* new area */) override
    {
        /* un/flagging player as pvp.
           create some kind of pvp hook when pvp changes inside the zone
        */

        if (config.current_zone == newZone)
        {
            if (isPlayerInZone(player))
            {
                return;
            }
            ChatHandler((player->GetSession())).SendSysMessage("You have entered the PvP zone");
            config.zone_players.push_back(player);
            player->UpdatePvP(true, true);
        }
        else if (isPlayerInZone(player))
        {
            ChatHandler((player->GetSession())).SendSysMessage("You left the PvP zone");
            config.zone_players.erase(std::remove(config.zone_players.begin(), config.zone_players.end(), player), config.zone_players.end());
        }
    }

    void PostLeaderBoard(ChatHandler* handler)
    {
        handler->SendGlobalSysMessage("PvP Zones Leaderboard:");

        /* this should never happen */
        if (config.points.empty())
        {
            return;
        }

        for (auto& player : config.points)
        {
            handler->PSendSysMessage("%s: %u", player.first->GetName().c_str(), player.second);
        }
    }

    static void PostAnnouncement(ChatHandler* handler)
    {
        if (!config.active)
        {
            return;
        }
        if (config.last_announcement + config.announcement_delay < sWorld->GetGameTime())
        {
            handler->PSendSysMessage("[pvp_zones] Is currently active in: %s - %s", config.current_zone_name.c_str(), config.current_area_name.c_str());
            config.last_announcement = sWorld->GetGameTime();
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
        config.last_event = sWorld->GetGameTime();

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
            {
                config.current_zone_name = z_entry->area_name[handler->GetSessionDbcLocale()];
            }
        }

        handler->SendGlobalSysMessage(("[pvp_zones] A new zone has been declared: " + config.current_zone_name + " - " + config.current_area_name).c_str());
    }

    static void EndEvent(ChatHandler* handler)
    {
        config.active = false;
        handler->SendGlobalSysMessage("[pvp_zones] The event has ended.");

        /* reset */
        config.points.clear();
        config.area_players.clear();
        config.zone_players.clear();
    }

    void OnPVPKill(Player* winner /*killer*/, Player* loser /*killed*/) override
    {

        /* point calculation / checks */
        if (winner->GetAreaId() == config.current_area)
        {
            config.kill_points *= 2;
        }
        if (winner->GetZoneId() == config.current_zone)
        {
            /* populate config.points */
            if (config.points.find(winner) == config.points.end())
            {
                config.points.insert(std::make_pair(winner, config.kill_points));
            }

            if (config.points.find(loser) == config.points.end())
            {
                config.points.insert(std::make_pair(loser, 0));
            }

            config.kill_goal--;

            ChatHandler winner_handle = ChatHandler(winner->GetSession());

            config.points.find(winner)->second += config.kill_points;

            if (config.points.find(loser)->second > 0)
            {
                config.points.find(loser)->second -= config.kill_points;
            }

            if (config.kill_goal <= 0)
            {
                ChatHandler(winner->GetSession()).SendGlobalSysMessage("[pvp_zones] The goal has been reached!");
                EndEvent(&winner_handle);
            }

            ChatHandler(winner->GetSession()).SendSysMessage(("[pvp_zones] You have gained " + std::to_string(config.kill_points) + " PvP point(s)").c_str());
            ChatHandler(loser->GetSession()).SendSysMessage(("[pvp_zones] You have lost " + std::to_string(config.kill_points) + " PvP point(s)").c_str());

            if (config.kill_goal % /* TODO: put this higher, temporary test value */ 1 == 0)
            {
                PostLeaderBoard(&winner_handle);
            }
        }
    }
};

class ZoneCommands : public CommandScript
{
public:
    ZoneCommands() : CommandScript("pvp_zones_Commands") {}

    static bool HandleOnCommand(ChatHandler* handler, char const*)
    {
        config.enabled = true;
        handler->PSendSysMessage("PvP Zones Enabled");

        return true;
    }
    static bool HandleOffCommand(ChatHandler* handler, char const*)
    {
        config.enabled = false;
        handler->PSendSysMessage("PvP Zones Disabled");

        return true;
    }

    static bool HandleCreateCommand(ChatHandler* handler, char const*)
    {
        ZoneLogicScript::CreateEvent(handler);

        return true;
    }

    static bool HandleEndCommand(ChatHandler* handler, char const*)
    {
        ZoneLogicScript::EndEvent(handler);

        return true;
    }

    static bool HandleDebugCommand(ChatHandler* handler, char const*)
    {
        sLog->outString("[pvp_zones] Debug: active: %i, arena_name: %s, zone_name: %s, last_announcement: %i, last_event: %i, next_announcement: %is", config.active, config.current_area_name, config.current_zone_name, config.last_announcement, config.last_event, (config.last_announcement + config.announcement_delay) - sWorld->GetGameTime());
        return true;
    }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable = {{"pvp_zones_on", SEC_GAMEMASTER, false, &HandleOnCommand, ""}, {"pvp_zones_off", SEC_GAMEMASTER, false, &HandleOffCommand, ""}, {"pvp_zones_create", SEC_GAMEMASTER, false, &HandleCreateCommand, ""}, {"pvp_zones_end", SEC_GAMEMASTER, false, &HandleEndCommand, ""}};

        return commandTable;
    }
};

class ZoneWorld : public WorldScript
{
public:
    ZoneWorld() : WorldScript("pvp_zones_World") {}

    void OnUpdate(uint32 p_time) override
    {
        if (!config.enabled)
        {
            return;
        }

        SessionMap   m_sessions = sWorld->GetAllSessions();
        Player*      player;
        ChatHandler* handle;

        for (SessionMap::iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        {
            if (!itr->second || !itr->second->GetPlayer() || !itr->second->GetPlayer()->IsInWorld())
                continue;

            player = itr->second->GetPlayer();
        }

        if (!player)
        {
            return;
        }

        handle = new ChatHandler(player->GetSession());

        /* create event every x seconds based on config */
        if (config.last_event + config.event_delay < sWorld->GetGameTime())
        {
            ZoneLogicScript::CreateEvent(handle);
        }

        /* ends event if event is already running x seconds */
        if (config.last_event + config.event_lasts < sWorld->GetGameTime())
        {
            ZoneLogicScript::EndEvent(handle);
        }

        /* announcement stuff */
        if (config.last_announcement + config.announcement_delay < sWorld->GetGameTime())
        {
            ZoneLogicScript::PostAnnouncement(handle);
        }
    }
};

void Addpvp_zonesScripts()
{
    new ZoneConfig();
    new ZoneLogicScript();
    new ZoneCommands();
}