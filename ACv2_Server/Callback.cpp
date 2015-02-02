#include "Callback.h"
#include "Network/Network.h"
#include "Utility.h"
#include "GDK/sampgdk.h"
#include "CAntiCheat.h"
#include "../Shared/Network/CRPC.h"
#include "CServerUpdater.h"
#include "CAntiCheatHandler.h"
#include "PacketPriority.h"

#include <stdio.h>
#include <stdlib.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

namespace Callback
{
	static AMX* amx_allowed = NULL;
	static std::list<AMX*> amxPointers;

	std::list<AMX*>& GetAMXList()
	{
		return amxPointers;
	}

	cell Execute(const char* szFunction, const char* szFormat, ...)
	{
		cell iReturnValue = 1;
		cell* pReturnValues = new cell[amxPointers.size()];
		std::fill_n(pReturnValues, amxPointers.size(), 1);
		size_t idx = 0;
		va_list argPtr;

		for (std::list<AMX*>::iterator it = amxPointers.begin(); it != amxPointers.end(); ++it, ++idx)
		{
			AMX* pAmx = *it;
			int iFuncIdx;

			if (amx_FindPublic(pAmx, szFunction, &iFuncIdx) != 0)
				continue;

			cell addresses[16];
			unsigned int addr_idx = 0;

			if (szFormat)
			{
				va_start(argPtr, szFormat);

				for (unsigned int i = 0; i < strlen(szFormat); ++i)
				{
					switch (szFormat[i])
					{
					case 'i':
					{
						amx_Push(pAmx, va_arg(argPtr, int));

						break;
					}
					case 's':
					{
						amx_PushString(pAmx, &addresses[addr_idx++], NULL, va_arg(argPtr, char*), false, false);

						break;
					}
					case 'a':
					{
						cell iAmxAddr, *pPhysAddr;

						PAWNArray array = va_arg(argPtr, PAWNArray);
						amx_Allot(pAmx, array.length, &iAmxAddr, &pPhysAddr);
						memcpy(pPhysAddr, array.address, array.length*sizeof(cell));
						amx_Push(pAmx, iAmxAddr);
						break;
					}
					}
				}

				va_end(argPtr);
			}

			amx_Exec(pAmx, &pReturnValues[idx], iFuncIdx);

			for (unsigned int i = 0; i < addr_idx; ++i)
				amx_Release(pAmx, addresses[i]);

		}

		for (size_t i = 0; i < idx; ++i)
			if (!pReturnValues[i])
				iReturnValue = 0;

		delete[] pReturnValues;

		return iReturnValue;
	}

	void SAMPGDK_CALL KickPlayer(int timerid, void *params)
	{
		// Make sure the player is connected.
		if (IsPlayerConnected((int)params))
		{
			// Kick the player from the server.
			Kick((int)params);
		}
	}

	void SAMPGDK_CALL CheckAC(int timerid, void *params)
	{
		// Convert the params into a playerid.
		int playerid = (int)params;

		// Make sure the player is still connected to the server.
		if (!IsPlayerConnected(playerid))
		{
			// Return
			return;
		}
	}
	
	void SAMPGDK_CALL CheckPlayersMemory(int timerid, void *params) 
	{
		// Loop through all players.
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			// Make sure the player is connected to the AC and the server.
			if (IsPlayerConnected(i) && CAntiCheatHandler::IsConnected(i))
			{
				// Verify the players weapon.dat values.
				RakNet::BitStream bsData;

				// Write header
				bsData.Write((unsigned char)PACKET_RPC);
				bsData.Write(MD5_MEMORY_REGION);

				bsData.Write(0xC8C418);
				bsData.Write(0x460);

				// Send RPC.
				Network::PlayerSend(i, &bsData, LOW_PRIORITY, RELIABLE);

				/*// Verify the players handling.cfg values
				RakNet::BitStream bsData2;
				bsData2.Write(0xC2B9DC);
				bsData2.Write(0xAF00);

				Network::PlayerSendRPC(MD5_MEMORY_REGION, playerid, &bsData2);*/
			}
		}
	}

	bool GetACEnabled()
	{
		return ACToggle;
	}

	PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerConnect(int playerid)
	{
		// if the AC is on, let the user know this server is protected.
		if (ACToggle)
		{
			// Tell the player we're using the AC on this server.
			SendClientMessage(playerid, -1, "{FF0000}Warning: {FFFFFF}This server has Anti-Cheat (v2) enabled.");
		}

		// Make sure the new connected user isn't an NPC.
		if (IsPlayerNPC(playerid))
		{
			// Return
			return true;
		}

		if (CAntiCheatHandler::IsConnected(playerid))
		{
			// Find a CAntiCheat class associated with this player (this was created in Network::HandleConnection earlier in this function)
			CAntiCheat* ac = CAntiCheatHandler::GetAntiCheat(playerid);

			std::string hwid = "";

			if (ac != NULL)
			{
				// Get the player's Hardware ID.
				hwid = ac->GetPlayerHardwareID();

				// Send the client the files we need them to return md5's to.
				ac->CheckGTAFiles(playerid);

				// Set defaults
				ac->ToggleUnlimitedSprint(Callback::Default_InfSprint);
				ac->ToggleSprintOnAllSurfaces(Callback::Default_SprintOnAllSurfaces);
				ac->ToggleMacroLimitations(Callback::Default_MacroLimits);
				ac->ToggleSwitchReload(Callback::Default_SwitchReload);
				ac->ToggleCrouchBug(Callback::Default_CrouchBug);
				ac->ToggleLiteFoot(Callback::Default_LiteFoot);
				ac->ToggleVehicleBlips(Callback::Default_VehicleBlips);
			}

			// Check if it's an empty string
			if (hwid.empty())
			{
				if (ACToggle)
				{
					// Create new variables for strings we'll send to the rest of the players telling them what happened.
					char msg[144], name[MAX_PLAYER_NAME];

					// Get the player's name
					GetPlayerName(playerid, name, sizeof(name));

					// Format the main string we'll send to the players on the server.
					snprintf(msg, sizeof(msg), "{FF0000}%s{FFFFFF}'s AC did not respond in time.", name);

					// Send the message to the server
					SendClientMessageToAll(-1, msg);

					// Kick the player from the server.
					SetTimer(1000, 0, Callback::KickPlayer, (void*)playerid);
				}

			} // hwid.empty()
		} // CAntiCheatHandler::IsConnected(playerid)
		else if (ACToggle)
		{
			// Notify them that this isn't allowed.
			SendClientMessage(playerid, -1, "{FF0000}Error: {FFFFFF}You've been kicked from this server for not running Whitetiger's Anti-Cheat (v2)");

			char msg[160], name[MAX_PLAYER_NAME];

			// Get the player name and store it in the name variable.
			GetPlayerName(playerid, name, sizeof(name));
			snprintf(msg, sizeof(msg), "{FF0000}%s{FFFFFF} has been kicked from the server for not running Whitetiger's Anti-Cheat (v2)", name);

			// Send them the formatted message.
			SendClientMessageToAll(-1, msg);

			// Tell them where to get the AC and install it.
			SendClientMessage(playerid, -1, "{FFFFFF}You can download the latest version of Whitetiger's Anti-Cheat at: {FF0000}http://samp-ac.com");

			Utility::Printf("%s has been kicked from the server for not connecting with AC while AC is on.", name);

			// Finally, kick the player.
			SetTimer(1000, 0, Callback::KickPlayer, (void*)playerid);

			return true;
		}

		return true;
	}

	PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerDisconnect(int playerid, int reason)
	{
		// If the player has just disconnected from the server, we need to handle AC disconnect as well.

		// If the player was connected to the AC before disconnecting.
		if (CAntiCheatHandler::IsConnected(playerid))
		{
			CAntiCheatHandler::Remove(playerid);
		}
		return true;
	}

	PLUGIN_EXPORT bool PLUGIN_CALL OnGameModeInit()
	{
		// Check for an update to this plugin version.
		CServerUpdater::CheckForUpdate();

		// Check memory pretty frequently in a new timer.
		SetTimer(60000, 1, CheckPlayersMemory, NULL);

		if (!boost::filesystem::exists("ac_config.ini"))
		{
			Utility::Printf("Warning: ac_config.ini is missing, loading default AC values.");

			ACToggle = true;
			Default_InfSprint = true;
			Default_SprintOnAllSurfaces = true;
			Default_MacroLimits = true;
			Default_LiteFoot = true;
			Default_VehicleBlips = true;

			Default_CrouchBug = 9999;
			Default_FrameLimit = 9999;
		}
		else
		{
			// Load config.
			boost::property_tree::ptree pt;
			boost::property_tree::ini_parser::read_ini("ac_config.ini", pt);

			// Load default values from file.
			ACToggle = pt.get<bool>("defaults.main_ac_checks");
			Default_InfSprint = pt.get<bool>("defaults.inf_sprint");
			Default_SprintOnAllSurfaces = pt.get<bool>("defaults.sprint_all_surfaces");
			Default_MacroLimits = pt.get<bool>("defaults.macro_limits");
			Default_SwitchReload = pt.get<bool>("defaults.switch_reload");
			Default_CrouchBug = pt.get<int>("defaults.crouch_bug");
			Default_LiteFoot = pt.get<bool>("defaults.lite_foot");
			Default_FrameLimit = pt.get<int>("defaults.frame_limit");
			Default_VehicleBlips = pt.get<bool>("defaults.vehicle_blips");
		}

		return true;
	}

	PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerCommandText(int playerid, const char* params)
	{

		if (!strcmp(params, "/acinfo"))
		{
			// This command is here so we can check for abuse, if main_ac_checks is 0 then it can't be proven that the scripter
			// hasn't added some weird condition that allows him to cheat but not anyone else.
			char output[128];
			snprintf(output, sizeof(output), "{d3d3d3}** main_ac_checks: {FFFFFF}%d", ACToggle);
			SendClientMessage(playerid, -1, output);
			return 1;
		}

		// If the user typed /actoggle and they're allowed to run that command.
		else if (!strcmp(params, "/actoggle") && CAntiCheat::CanEnableAC(playerid))
		{ 
			// Set ACToggle to whatever it wasn't previously.
			ACToggle = !ACToggle;

			// Create 2 variables, one to hold the player name who just enabled/disabled AC, and one to send a formatted message
			// telling all the users on the server that the AC is now on and they will be kicked if they're not using it.
			char str[144], name[MAX_PLAYER_NAME];

			// Get the player name
			GetPlayerName(playerid, name, sizeof(name));

			// Format the message telling all the users that AC is now on.
			snprintf(str, sizeof(str), "{FF0000}%s{0000FF} has {FFFFFF}%s{0000FF} SAMP_AC_v2.", name, ACToggle == true ? "Enabled" : "Disabled");

			// Send the message to everyone on the server.
			SendClientMessageToAll(-1, str);

			// If the AC was turned on
			if (ACToggle)
			{
				// Loop through the maximum amount of players that can be connected to the server.
				for (int i = 0; i < MAX_PLAYERS; ++i)
				{
					// If they're connected to the server, and not connected to the AC
					if (IsPlayerConnected(i) && !IsPlayerNPC(i) && !CAntiCheatHandler::IsConnected(i))
					{
						// Kick them from the server!
						SetTimer(1000, 0, Callback::KickPlayer, (void*)i);
					}
				}
			}
			// We know what command they typed, so return true;
			return true;
		}
		return false;
	}

	PLUGIN_EXPORT bool PLUGIN_CALL OnRconLoginAttempt(const char* ip, const char* port, bool success)
	{
		// If the rcon login attempt wasn't successful, we don't really care...
		if (!success) return true;

		// Loop through the maximum amount of players.
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			// If the player is connected.
			if (IsPlayerConnected(i))
			{
				// Create a variable to store the player's IP.
				char IP[MAX_PLAYER_NAME];

				// Get the player's IP and store it in the variable we just created.
				GetPlayerIp(i, IP, sizeof(IP));

				// Compare the IP to the IP that tried to do that rcon login
				if (!strcmp(ip, IP))
				{
					// If it was them, we just found the playerid who logged into rcon, therfore he's allowed to use the /actoggle command.
					CAntiCheat::ToggleCanEnableAC(i, true);
				}
			}
		}
		return true;
	}
}