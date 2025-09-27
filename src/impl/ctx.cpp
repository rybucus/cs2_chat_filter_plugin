#include "../ctx.h"

#include <windows.h>
#include <tlhelp32.h>
#include <utils.hpp>

namespace Ruby
{
	bool CCtx::OnInit( ISmmAPI* ismm, char* error, size_t maxlen )
	{
		META_CONPRINTF( "m_hServerModule are: %p\n", m_Addresses.m_Modules.m_hServerModule.GetModuleBase( ).GetPtr( ) );

		auto result = m_Addresses.m_Modules.m_hServerModule.GetModuleBase( ) != 0;

		if ( !result )
		{
			META_CONPRINTF( "m_hServerModule nullptr!\n" );
			return result;
		}

		META_CONPRINTF( "Parsing Patterns...\n");

		result &= m_Addresses.Inititalize( );

		if ( !result )
		{
			META_CONPRINTF( "Failed to parse patterns!\n" );
			return result;
		}

		META_CONPRINTF( "Patterns good. Now parsing interfaces...\n" );

		GET_V_IFACE_ANY( GetServerFactory, m_Interfaces.m_pServerDll, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL );
		GET_V_IFACE_ANY( GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION );
		GET_V_IFACE_ANY( GetServerFactory, m_Interfaces.m_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS );
		GET_V_IFACE_ANY( GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION );
		GET_V_IFACE_CURRENT( GetEngineFactory, m_Interfaces.m_pEngine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION );
		GET_V_IFACE_CURRENT( GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION );
		GET_V_IFACE_CURRENT( GetEngineFactory, m_Interfaces.m_pCvar, ICvar, CVAR_INTERFACE_VERSION );
		GET_V_IFACE_CURRENT( GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION );
		GET_V_IFACE_ANY( GetEngineFactory, m_Interfaces.m_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION );
		GET_V_IFACE_ANY( GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION );

		return result;
	}
}
