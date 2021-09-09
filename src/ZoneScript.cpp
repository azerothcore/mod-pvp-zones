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
    bool enabled = true;

    std::unordered_map<uint32 /* zone */, /* areas */ std::vector<uint32>> ids = {{10, {93, 536}}};

    /* non .conf stuff stuff */
    uint32 current_zone;
    uint32 current_area;

    bool active = false;

    std::vector<Player*> area_players;
    std::vector<Player*> zone_players;

    std::map<Player*, uint32> points;
};

Config config;

class ZoneConfig : public WorldScript
{
public:
    ZoneConfig() : WorldScript("pvp_zones_Config") {}

    void OnStartup() override
    {
        config.enabled = sConfigMgr->GetOption<bool>("pvp_zones.Enable", false);
        // config.zone_ids = sConfigMgr->GetOption<std::vector<uint32>>("pvp_zones.Zones", {});
        //    config.area_ids = sConfigMgr->GetOption<std::vector<uint32>>("pvp_zones.Areas", {});
    }
};

class ZoneCommands : public CommandScript
{
public:
    ZoneCommands() : CommandScript("pvp_zones_Commands") {}

    static bool HandleOnCommand(ChatHandler* handler, char const* args)
    {
        config.enabled = true;
        handler->PSendSysMessage("PvP Zones Enabled");

        return true;
    }
    static bool HandleOffCommand(ChatHandler* handler, char const* args)
    {
        config.enabled = false;
        handler->PSendSysMessage("PvP Zones Disabled");

        return true;
    }

    static bool HandleCreateCommand(ChatHandler* handler, char const* args)
    {
        /* TODO: finish */

        auto map_it = std::begin(config.ids);
        std::advance(map_it, rand() % config.ids.size());

        auto area_it = std::begin(map_it->second);
        std::advance(area_it, rand() % map_it->second.size());

        uint32 zone = map_it->first;
        uint32 area = *area_it;

        std::string zone_name, area_name;

        if (AreaTableEntry const* entry = sAreaTableStore.LookupEntry(area))
        {
            area_name = entry->area_name[handler->GetSessionDbcLocale()];

            if (AreaTableEntry const* z_entry = sAreaTableStore.LookupEntry(zone))
            {
                zone_name = z_entry->area_name[handler->GetSessionDbcLocale()];
            }
        }

        handler->SendGlobalSysMessage(("[pvp_zones] A new zone has been declared: " + zone_name + " - " + area_name).c_str());

        return true;
    }

    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> commandTable = {{"pvp_zones_on", SEC_GAMEMASTER, false, &HandleOnCommand, ""}, {"pvp_zones_off", SEC_GAMEMASTER, false, &HandleOffCommand, ""}, {"pvp_zones_create", SEC_GAMEMASTER, false, &HandleCreateCommand, ""}};

        return commandTable;
    }
};

class ZonePlayerScript : public PlayerScript
{
public:
    ZonePlayerScript() : PlayerScript("pvp_zones_PlayerScript") {}

    /* adding/removing players that are currently in the area */
    void OnUpdateArea(Player* player, uint32, uint32 area) override
    {
        if (config.current_area == player->GetAreaId())
        {
            config.area_players.push_back(player);
        }
        else
        {
            config.area_players.erase(std::remove(config.area_players.begin(), config.area_players.end(), player), config.area_players.end());
        }
    }

    void OnUpdateZone(Player* player, uint32 newZone, uint32 /* new area */) override
    {
        /* un/flagging player as pvp.
           create some kind of pvp hook when pvp changes inside the zone
        */

        if (config.current_zone == newZone)
        {
            config.zone_players.push_back(player);
            player->SetPvP(true);
        }
    }
};

// Add all scripts in one
void Addpvp_zonesScripts()
{
    new ZoneConfig();
    new ZoneCommands();
}