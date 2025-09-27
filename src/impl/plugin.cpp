#include <CGameRules.h>
#include <CBaseEntity.h>
#include <entitysystem.h>
#include <utils.hpp>

#include <chrono>
#include <algorithm>
#include <regex>

#include "../plugin.h"
#include "../shared/players_controller.h"
#include "../shared/chat_controller.h"

ChatFilterPlugin g_Plugin;
PLUGIN_EXPOSE( ChatFilterPlugin, g_Plugin );

CEntitySystem* g_pEntitySystem = nullptr;

SH_DECL_HOOK3_void( IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool );
SH_DECL_HOOK6( IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char*, uint64, const char*, bool, CBufferString* );
SH_DECL_HOOK5_void( IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char * );
SH_DECL_HOOK4_void( IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const*, int, uint64 );

CConVar< int > sv_chat_filter_mode( "sv_chat_filter_mode", FCVAR_SPONLY | FCVAR_PROTECTED, "", 2, true, 0, true, 2 );

char* __imp_strncpy( char* dest, const char* src, size_t size )
{
	return strncpy( dest, src, size );
}

CGameEntitySystem* GameEntitySystem( )
{
	return *reinterpret_cast< CGameEntitySystem** >( reinterpret_cast< uintptr_t >( g_pGameResourceServiceServer ) + 0x58 );;
}

CEntityIdentity* CEntitySystem::GetEntityIdentity( const CEntityHandle& hEnt )
{
	return g_pCtx->m_Addresses.m_pGetBaseEntity.Call< CBaseEntity* >( this, hEnt.GetEntryIndex( ) )->m_pEntity;
}

CEntityIdentity* CEntitySystem::GetEntityIdentity( CEntityIndex iEnt )
{
	return g_pCtx->m_Addresses.m_pGetBaseEntity.Call< CBaseEntity* >( this, iEnt.Get( ) )->m_pEntity;
}

bool ChatFilterPlugin::Load( PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late )
{
	PLUGIN_SAVEVARS( );

	std::this_thread::sleep_for( std::chrono::seconds( 5 ) );

	if ( !g_pCtx->OnInit( ismm, error, maxlen ) )
		return false;

	if ( late )
		g_pEntitySystem = GameEntitySystem( );

	SH_ADD_HOOK( IServerGameDLL, GameFrame, g_pCtx->m_Interfaces.m_pServerDll, SH_MEMBER( this, &ChatFilterPlugin::Hook_GameFrame ), true );
	SH_ADD_HOOK( IServerGameClients, ClientConnect, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientConnect ), false );
	SH_ADD_HOOK( IServerGameClients, ClientDisconnect, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientDisconnect ), true );
	SH_ADD_HOOK( IServerGameClients, ClientPutInServer, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientPutInServer ), true );

	g_pCtx->m_Hooks.m_SayHook.Init( g_pCtx->m_Addresses.m_pSay.Get( ), ChatFilterPlugin::Hook_Say ).Hook( );
	g_pCtx->m_Hooks.m_TeamSayHook.Init( g_pCtx->m_Addresses.m_pTeamSay.Get( ), ChatFilterPlugin::Hook_TeamSay ).Hook( );
	g_pCtx->m_Hooks.m_UTIL_SayText2Filter.Init( g_pCtx->m_Addresses.UTIL_SayText2Filter.Get( ), ChatFilterPlugin::Hook_UTIL_SayText2FilterTeamSay ).Hook( );
	g_pCtx->m_Hooks.m_UTIL_SayTextFilter.Init( g_pCtx->m_Addresses.UTIL_SayTextFilter.Get( ), ChatFilterPlugin::Hook_UTIL_SayTextFilterTeamSay ).Hook( );

	g_SMAPI->AddListener( this, this );

	g_pCVar = g_pCtx->m_Interfaces.m_pCvar;

	META_CONVAR_REGISTER( FCVAR_RELEASE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL );

	META_CONPRINTF( "Starting plugin.\n" );

	return true;
}

bool ChatFilterPlugin::Unload( char* error, size_t maxlen )
{
	SH_REMOVE_HOOK( IServerGameDLL, GameFrame, g_pCtx->m_Interfaces.m_pServerDll, SH_MEMBER( this, &ChatFilterPlugin::Hook_GameFrame ), true );
	SH_REMOVE_HOOK( IServerGameClients, ClientDisconnect, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientDisconnect ), true );
	SH_REMOVE_HOOK( IServerGameClients, ClientConnect, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientConnect ), false );
	SH_REMOVE_HOOK( IServerGameClients, ClientPutInServer, g_pCtx->m_Interfaces.m_pGameClients, SH_MEMBER( this, &ChatFilterPlugin::Hook_ClientPutInServer ), true );

	g_pCtx->m_Hooks.m_SayHook.UnHook( );
	g_pCtx->m_Hooks.m_TeamSayHook.UnHook( );
	g_pCtx->m_Hooks.m_UTIL_SayText2Filter.UnHook( );
	g_pCtx->m_Hooks.m_UTIL_SayTextFilter.UnHook( );

	ConVar_Unregister( );

	return true;
}

void ChatFilterPlugin::AllPluginsLoaded( )
{
	if ( !g_pChatFilter->IsRulesLoaded( ) )
		g_pChatFilter->LoadConfigFromFile( "addons/cs2_chat_filter_plugin/config/chat_filter_config.ini" );

	const auto hFirstHandle = g_pChatController->RegisterChatCallback
	( 
		[ ]( const CCommandContext& ctx, CCommand& args, bool bIsSayedToTeam ) -> bool
		{
			if ( !g_pChatFilter->IsRulesLoaded( ) )
				return true;

			if ( const auto pController = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( ctx.GetPlayerSlot( ) );
				pController )
			{
				if ( auto pPawn = pController->GetPawn( );
					pPawn )
				{
					std::string szInputMsg( args[ 1 ] );

					switch ( sv_chat_filter_mode.GetInt( ) )
					{
						case 1: // все видят звездочки
 						{
							const auto szFilteredMsg = g_pChatFilter->MakeFilteredString( szInputMsg );

							if ( !bIsSayedToTeam )
							{
								CCommand ArgsCopy;
								CUtlString strNewCommand( ( std::string( "say " ) + szFilteredMsg ).c_str( ) );
								ArgsCopy.Tokenize( strNewCommand );
								if ( ( pController->m_flLastPlayerTalkTime( ) + 0.25f ) < g_pCtx->GetGlobalVars( )->curtime )
								{
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( ArgsCopy ), 0, 0, 0i64 );
									pController->m_flLastPlayerTalkTime( ) = g_pCtx->GetGlobalVars( )->curtime;
								}
							}
							else
							{
								CCommand ArgsCopy;
								CUtlString strNewCommand( ( std::string( "say_team " ) + szFilteredMsg ).c_str( ) );
								ArgsCopy.Tokenize( strNewCommand );
								if ( ( pController->m_flLastPlayerTalkTime( ) + 0.25f ) < g_pCtx->GetGlobalVars( )->curtime )
								{
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( ArgsCopy ), 1, 0, 0i64 );
									pController->m_flLastPlayerTalkTime( ) = g_pCtx->GetGlobalVars( )->curtime;
								}
							}

							return false;
						}
						case 2: // все кроме отправителя видят звездочки
						{
							const auto szFilteredMsg = g_pChatFilter->MakeFilteredString( szInputMsg );

							if ( ( pController->m_flLastPlayerTalkTime( ) + 0.25f ) < g_pCtx->GetGlobalVars( )->curtime )
							{
								g_pCtx->m_bSkipTalker = true;

								if ( !bIsSayedToTeam )
								{
									CCommand ArgsCopy;
									CUtlString strNewCommand( ( std::string( "say " ) + szFilteredMsg ).c_str( ) );
									ArgsCopy.Tokenize( strNewCommand );
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( ArgsCopy ), 0, 0, 0i64 );
								}
								else
								{
									CCommand ArgsCopy;
									CUtlString strNewCommand( ( std::string( "say_team " ) + szFilteredMsg ).c_str( ) );
									ArgsCopy.Tokenize( strNewCommand );
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( ArgsCopy ), 1, 0, 0i64 );
								}

								g_pCtx->m_bSkipTalker = false;
								g_pCtx->m_bSkipOthers = true;

								if ( !bIsSayedToTeam )
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( args ), 0, 0, 0i64 );
								else
									const auto bResult = g_pCtx->m_Addresses.m_pChatPrint.Call< bool >( pController, std::ref( args ), 1, 0, 0i64 );

								g_pCtx->m_bSkipOthers = false;

								pController->m_flLastPlayerTalkTime( ) = g_pCtx->GetGlobalVars( )->curtime;
							}

							return false;
						}
						default: break; // фильтр отключен
					}
				}
			}
			return true;
		}
	);
}

void ChatFilterPlugin::OnLevelInit( char const* pMapName, char const* pMapEntities, char const* pOldLevel, char const* pLandmarkName, bool loadGame, bool background )
{
	g_pCtx->OnLevelInit( );
}

void ChatFilterPlugin::OnLevelShutdown( )
{
	g_pCtx->OnLevelShutdown( );

	g_pPlayersController->EraseAllPlayers( );
}

void ChatFilterPlugin::Hook_GameFrame( bool bSimulating, bool bFirstTick, bool bLastTick )
{
	if ( !g_pEntitySystem )
		g_pEntitySystem = GameEntitySystem( );

	if ( !g_pCtx->m_Interfaces.m_pGameRules
		&& g_pEntitySystem )
	{
		if ( const auto pGameRulesProxy = static_cast< CCSGameRulesProxy* >( UTIL_FindEntityByClassname( "cs_gamerules" ) );
			pGameRulesProxy )
		{
			g_pCtx->m_Interfaces.m_pGameRules = pGameRulesProxy->m_pGameRules( );
		}
	}

	if ( g_pCtx->GetGlobalVars( )
		&& g_pCtx->m_Interfaces.m_pGameRules )
	{
		g_pPlayersController->QueueConnectedPlayers( );
	}
}

bool ChatFilterPlugin::Hook_ClientConnect( CPlayerSlot Slot, const char* szName, uint64 llUid, const char* szNetworkID, bool bUnk, CBufferString* pRejectReason )
{
	g_pPlayersController->RegisterPlayer( Slot );

	RETURN_META_VALUE( MRES_IGNORED, true );
}

void ChatFilterPlugin::Hook_ClientDisconnect( CPlayerSlot Slot, ENetworkDisconnectionReason eReason, const char* szName, uint64 llUid, const char* szNetworkID )
{
	if ( g_pCtx->GetGlobalVars( ) )
		g_pPlayersController->PlayerDisconnect( Slot );
}

void ChatFilterPlugin::Hook_ClientPutInServer( CPlayerSlot iSlot, char const* szName, int iType, uint64 llUid )
{
	g_pPlayersController->ServerPutPlayer( iSlot );
}

void ChatFilterPlugin::Hook_Say( const CCommandContext& ctx, CCommand& args )
{
	if ( args.ArgC( ) > 1 && args[ 1 ][ 0 ] )
		if ( !g_pChatController->OnSay( ctx, args, false ) )
			return;

	g_pCtx->m_Hooks.m_SayHook.m_pOriginal( ctx, args );
}

void ChatFilterPlugin::Hook_TeamSay( const CCommandContext& ctx, CCommand& args )
{
	if ( args.ArgC( ) > 1 && args[ 1 ][ 0 ] )
		if ( !g_pChatController->OnSay( ctx, args, true ) )
			return;

	g_pCtx->m_Hooks.m_TeamSayHook.m_pOriginal( ctx, args );
}

bool ChatFilterPlugin::Hook_PlayerCanHearChatFrom( CBasePlayerController* pThis, CBasePlayerController* pPlayer )
{
	return g_pCtx->m_Hooks.m_PlayerCanHearChatFromHook.m_pOriginal( pThis, pPlayer );
}

void ChatFilterPlugin::Hook_UTIL_SayText2FilterTeamSay( IRecipientFilter* pUnk, CBasePlayerController* pController, bool bIsTeam, void* pUnk2,
	const char* pszFormat, const char* pszPlayerName, const char* p, const char* pszLocation )
{
	if ( sv_chat_filter_mode.GetInt( ) == 2  && pUnk )
	{
		if ( g_pCtx->m_bSkipTalker 
			&& pController )
		{
			if ( pUnk->GetRecipients( ).Get( pController->GetPlayerSlot( ) ) != 0 )
				pUnk->GetRecipients( ).ClearAll( );
		}

		if ( g_pCtx->m_bSkipOthers 
			&& pController )
		{
			if ( pUnk->GetRecipients( ).Get( pController->GetPlayerSlot( ) ) == 0 )
				pUnk->GetRecipients( ).ClearAll( );
		}
	}
	return g_pCtx->m_Hooks.m_UTIL_SayText2Filter.m_pOriginal( pUnk, pController, bIsTeam, pUnk2, pszFormat, pszPlayerName, p, pszLocation );
}

void ChatFilterPlugin::Hook_UTIL_SayTextFilterTeamSay( IRecipientFilter* pUnk, void* pUnk2, CBasePlayerController* pController, bool bIsTeam )
{
	if ( sv_chat_filter_mode.GetInt( ) == 2 && pUnk )
	{
		if ( g_pCtx->m_bSkipTalker
			&& pController )
		{
			if ( pUnk->GetRecipients( ).Get( pController->GetPlayerSlot( ) ) != 0 )
				pUnk->GetRecipients( ).ClearAll( );
		}

		if ( g_pCtx->m_bSkipOthers
			&& pController )
		{
			if ( pUnk->GetRecipients( ).Get( pController->GetPlayerSlot( ) ) == 0 )
				pUnk->GetRecipients( ).ClearAll( );
		}
	}
	return g_pCtx->m_Hooks.m_UTIL_SayTextFilter.m_pOriginal( pUnk, pUnk2, pController, bIsTeam );
}