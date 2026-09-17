#include "Config.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ScriptMgr.h"

namespace {

    class OnNewCharFirstLogin : public PlayerScript 
	{

    public:
        OnNewCharFirstLogin() : PlayerScript("OnNewCharFirstLogin")
        {
        }

        // run at first login of a new character
        void OnLogin(Player *player, bool firstLogin) override
        {
            if (firstLogin)
            {
                ChatHandler(player->GetSession()).SendSysMessage("|cffFF0000LegionCore |r");
            }
        }
    };
}

class OnWorldserverLoaded : public WorldScript
{
public:
	OnWorldserverLoaded() : WorldScript("OnWorldserverLoaded") {}

	// run always when worldserver has loaded
	void OnStartup() override
	{
        // LegionBotAI per-character settings (level sync + autogear modes).
        // Created here so it exists before any player can use the bots.
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS `character_legionbot_settings` ("
            "`guid` INT UNSIGNED NOT NULL,"
            "`level_mode` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "`fixed_level` TINYINT UNSIGNED NOT NULL DEFAULT 1,"
            "`player_tank` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "PRIMARY KEY (`guid`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
        // Upgrade installs created before player_tank existed
        CharacterDatabase.DirectExecute(
            "ALTER TABLE `character_legionbot_settings` ADD COLUMN IF NOT EXISTS "
            "`player_tank` TINYINT UNSIGNED NOT NULL DEFAULT 0");
	}
};

void AddSC_CustomStartups() {
    new OnNewCharFirstLogin();
    new OnWorldserverLoaded();
}
