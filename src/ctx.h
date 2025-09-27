#ifndef _PLUGIN_CTX
#define _PLUGIN_CTX

#ifdef _WIN64 || _WIN32
	#define PLAT_WINDOWS 1
	#define PLAY_LINUX 0
#else
	#define PLAY_LINUX 1
	#define PLAT_WINDOWS 0
#endif

#include <edict.h>
#include <ISmmPlugin.h>
#include <entitysystem.h>
#include <igameeventsystem.h>
#include <CGameRules.h>
#include <iserver.h>
#include <module.h>
#include <function.h>
#include <funchook.h>

#include <memory>
#include <string_view>

PLUGIN_GLOBALVARS( );

#define INIT_MODULE( var, module_name_str ) DynLibUtils::CModule var{ module_name_str };
#define INIT_SERVER_MODULE( var ) DynLibUtils::CModule var{ "server", true };

#define INIT_ADDRESS( mod, var, pat ) var = mod.FindBytesPattern( string_view{ pat }; if ( !var ) return false;
#define INIT_ADDRESS_RELATIVE( mod, var, pat, pre, post ) var = mod.FindBytesPattern( string_view{ pat } ).Relative( iPreOffset, iPostOffset ); if ( !var ) return false;
#define INIT_FUNCTION( mod, var, pat ) var = DynLibUtils::CFunction( mod, pat ); if ( !var ) return false;
#define INIT_FUNCTION_RELATIVE( mod, var, pat, pre, post ) var = DynLibUtils::CFunction( mod, pat, pre, post ); if ( !var ) return false;

#define TICK_NEVER_THINK static_cast< int >( -1 )
#define INVALID_SLOT static_cast< CPlayerSlot >( -1 )
#define INTERVAL_PER_TICK 0.015625f
#define TIME_TO_TICKS( TIME ) ( static_cast< int >( 0.5f + static_cast< float >( TIME ) / INTERVAL_PER_TICK ) )
#define TICKS_TO_TIME( TICKS ) ( INTERVAL_PER_TICK * static_cast< float >( TICKS ) )
#define ROUND_TO_TICKS( TIME ) ( INTERVAL_PER_TICK * TIME_TO_TICKS( TIME ) )
#define STOP_EPSILON 0.1f

class CBasePlayerController;

namespace Ruby
{
	namespace Constans
	{
		constexpr auto isWindows = PLAT_WINDOWS ? true : false;
		constexpr auto isLinux   = PLAY_LINUX ? true : false;
	}

	template< typename T = void* >
	struct CHook
	{
		using Base_t = CHook< T >;

		funchook* m_pHook;
		T         m_pOriginal;
		void*     m_pHookFn;
		bool      m_bIsInit;
		bool	  m_bIsHooked;

		CHook( )
			:	m_pHook		{ nullptr }, 
				m_pOriginal	{ nullptr }, 
				m_pHookFn	{ nullptr }, 
				m_bIsHooked	{ false }, 
				m_bIsInit	{ false }
		{ }

		Base_t& Init( void* pOrig, T pHook )
		{
			m_pOriginal = reinterpret_cast< T >( pOrig );
			m_pHookFn   = pHook;

			if ( m_bIsInit )
				return *this;

			if ( !m_pHook )
				m_pHook = funchook_create( );

			m_bIsInit = true;

			return *this;
		}

		Base_t& Hook( )
		{
			if ( !m_bIsInit || m_bIsHooked || !m_pHook || !m_pOriginal || !m_pHookFn )
				return *this;

			funchook_prepare( m_pHook, ( void** )&m_pOriginal, ( void* )m_pHookFn );
			funchook_install( m_pHook, 0 );

			m_bIsHooked = true;

			return *this;
		}

		Base_t& UnHook( )
		{
			if ( !m_bIsInit || !m_bIsHooked || !m_pHook )
				return *this;

			funchook_destroy( m_pHook );

			return *this;
		}
	};

	class CCtx
	{
	public:

		CCtx( ) = default;

		bool OnInit( ISmmAPI* ismm, char* error, size_t maxlen );

		void OnLevelInit( )
		{
			m_bIsMapLoaded = true;
		}

		void OnLevelShutdown( )
		{
			m_bIsMapLoaded = false;
			m_bSkipTalker  = false;
			m_bSkipOthers  = false;
		}

		inline static CGlobalVars* GetGlobalVars( )
		{
			INetworkGameServer* pGameServer = g_pNetworkServerService->GetIGameServer( );

			if ( !pGameServer )
				return nullptr;

			return g_pNetworkServerService->GetIGameServer( )->GetGlobals( );
		}

	public:

		bool					m_bIsMapLoaded	{ };
		bool					m_bSkipTalker	{ };
		bool					m_bSkipOthers	{ };

		struct
		{
			IServerGameDLL*     m_pServerDll    { };
			IServerGameClients* m_pGameClients  { };
			IVEngineServer2*	m_pEngine		{ };
			CCSGameRules*		m_pGameRules	{ };
			ICvar*				m_pCvar			{ };
			IGameEventSystem*	m_pGameEventSystem { };
		}
		m_Interfaces;

		struct
		{ 
			struct
			{
				DynLibUtils::CModule m_hServerModule{ "server", true };
			}
			m_Modules;

			DynLibUtils::CFunction m_pGetBaseEntityBySlot;
			DynLibUtils::CFunction m_pGetBaseEntity;
			DynLibUtils::CFunction m_pLevelInit;
			DynLibUtils::CFunction m_pSay;
			DynLibUtils::CFunction m_pTeamSay;
			DynLibUtils::CFunction m_pChatPrint;
			DynLibUtils::CFunction m_pPlayerCanHearChatFrom;

			DynLibUtils::CFunction UTIL_SayText2Filter;
			DynLibUtils::CFunction UTIL_SayTextFilter;

			bool Inititalize( )
			{
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pGetBaseEntityBySlot, "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 74 1D" );
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pLevelInit, "48 89 5C 24 18 56 48 83 EC 30 48 8B 05" );
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pGetBaseEntity, "4C 8D 49 10 81 FA ?? ?? ?? ??" );
				// #STR: "say_team", "Display player message to team"
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pTeamSay, "48 89 5C 24 ? 57 48 83 EC 30 48 8B D9 48 8B FA 48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 84 C0 75 18 48 8D 0D ? ? ? ? 48 8B 5C 24 ? 48 83 C4 30 5F 48 FF 25 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B D8 48 85 C0 74 4B" );
				// #STR: "Display player message"
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pSay, "48 89 5C 24 ? 57 48 83 EC 30 48 8B D9 48 8B FA 48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 84 C0 75 18 48 8D 0D ? ? ? ? 48 8B 5C 24 ? 48 83 C4 30 5F 48 FF 25 ? ? ? ? 48 8B CB E8 ? ? ? ? 48 8B D8 48 85 C0 74 56" );
				// #STR: "say_team", "%s %s", "Console", "%s %s @ %s:", "%s %s:", "All Chat"...
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pChatPrint, "44 89 4C 24 ? 44 88 44 24 ?" );
				INIT_FUNCTION( m_Modules.m_hServerModule, m_pPlayerCanHearChatFrom, "40 53 48 83 EC 20 8B 81 ? ? ? ? 48 8B DA 48 85 D2" );

				INIT_FUNCTION_RELATIVE( m_Modules.m_hServerModule, UTIL_SayText2Filter, "E8 ? ? ? ? EB 19 41 B1 01", 0x1, 0x0 );
				INIT_FUNCTION_RELATIVE( m_Modules.m_hServerModule, UTIL_SayTextFilter, "E8 ? ? ? ? 48 8B 7C 24 ? E8 ? ? ? ?", 0x1, 0x0 );

				return true;
			}
		} 
		m_Addresses;

		struct
		{
			::Ruby::CHook< void( * )( const CCommandContext&, CCommand& ) >
				m_SayHook		{ };
			::Ruby::CHook< void( * )( const CCommandContext&, CCommand& ) >
				m_TeamSayHook	{ };
			::Ruby::CHook< bool( * )( CBasePlayerController*, CBasePlayerController* ) >
				m_PlayerCanHearChatFromHook	{ };
			::Ruby::CHook< void( * )( IRecipientFilter*, CBasePlayerController*, bool, void*, const char*, const char*, const char*, const char* ) >
				m_UTIL_SayText2Filter { };
			::Ruby::CHook< void( * )( IRecipientFilter*, void*, CBasePlayerController*, bool ) >
				m_UTIL_SayTextFilter{ };
		}
		m_Hooks;

	};
}

inline auto g_pCtx = std::make_unique< Ruby::CCtx >( );

#endif // !_PLUGIN_CTX

