#ifndef _PLUGIN_PLAYERS_CONTROLLER
#define _PLUGIN_PLAYERS_CONTROLLER

#include <memory>
#include <functional>
#include <unordered_map>
#include <array>
#include <deque>
#include <type_traits>
#include <CCSPlayerController.h>

#include "../ctx.h"

namespace Ruby
{
    class IPlayersUtilities
    {
    public:

        virtual ~IPlayersUtilities( ) = default;
        virtual void KickPlayer( CPlayerSlot Slot ) = 0;
        virtual void MoveToSpectators( CPlayerSlot Slot ) = 0;

    };

    class CPlayersUtilities : public IPlayersUtilities
    {
    public:

        void KickPlayer( CPlayerSlot Slot ) override
        {
            g_pCtx->m_Interfaces.m_pEngine->DisconnectClient( Slot, NETWORK_DISCONNECT_KICKED );
        }

        void MoveToSpectators( CPlayerSlot Slot ) override
        {
            auto pEntity = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( Slot );
            if ( pEntity )
                pEntity->ChangeTeam( 1 );
        }

    };

    struct CPlayerData
    {
        CPlayerData( )
            :   m_Slot{ INVALID_SLOT }, 
                m_iLastIteractTick{ TICK_NEVER_THINK },
                m_iDisconnectTick{ TICK_NEVER_THINK },
                m_vecLastOrigin{ },
                m_angLastViewAngles{ }
        { }

        CPlayerData( CPlayerSlot Slot, std::uint64_t SteamID )
            :   m_Slot{ Slot },
                m_iLastIteractTick{ g_pCtx->GetGlobalVars( )->tickcount },
                m_iDisconnectTick{ TICK_NEVER_THINK }, 
                m_vecLastOrigin{ },
                m_angLastViewAngles{ }
        { }

        void Invalidate( )
        {
            m_iDisconnectTick   = g_pCtx->GetGlobalVars( )->tickcount;
            m_iLastIteractTick  = TICK_NEVER_THINK;
            m_vecLastOrigin     = Vector{ };
            m_angLastViewAngles = QAngle{ };
            m_Slot              = INVALID_SLOT;
        }

        CPlayerSlot  m_Slot;
        int          m_iLastIteractTick;
        int          m_iDisconnectTick;
        Vector       m_vecLastOrigin;
        QAngle       m_angLastViewAngles;

    };

    struct CConnectedPlayer
    {
        CConnectedPlayer( )
            : m_bIsConnected{ false }, n_bIsInGame{ false }, m_iSlot{ INVALID_SLOT }
        { }

        CConnectedPlayer( CPlayerSlot iSlot )
            : m_bIsConnected{ true }, n_bIsInGame{ false }, m_iSlot{ iSlot }
        { }

        CPlayerSlot m_iSlot;
        bool        m_bIsConnected;
        bool        n_bIsInGame;
    };

    template< typename T >
    class CPlayersController
    {
    public:
        
        static_assert( std::is_base_of_v< IPlayersUtilities, std::remove_cv_t<T> > );

        using Super_t = CPlayersController< T >;
        using UserId_t = std::uint64_t;

    public:

        CPlayersController( )
        {
            m_pUtilities = std::make_unique< CPlayersUtilities >( );
        }

        void RunForPlayers( const std::function< void ( IPlayersUtilities*, ::Ruby::CPlayerData& ) >& Callback )
        {
            for ( auto& PlayerData : m_UniquePlayers )
            {
                Callback( m_pUtilities.get( ), std::get< ::Ruby::CPlayersController::CPlayerData >( PlayerData ) );
            }
        }

        void RunForConnectedPlayers( const std::function< void( ::Ruby::IPlayersUtilities*, ::Ruby::CConnectedPlayer& ) >& Callback )
        {
            for ( auto& Player : m_ConnectedPlayers )
            {
                Callback
                (
                    reinterpret_cast< ::Ruby::IPlayersUtilities* >( m_pUtilities.get( ) ),
                    Player
                );
            }
        }

        void RunForConnectedPlayer( CPlayerSlot Slot, 
            const std::function< void( IPlayersUtilities*, ::Ruby::CConnectedPlayer& ) >& Callback )
        {
            if ( Slot.Get( ) < 0 || Slot.Get( ) > 64 )
                return;

            Callback( m_pUtilities.get( ), m_ConnectedPlayers[ Slot.Get( ) ] );
        }

        std::size_t GetConnectedPlayersCount( )
        {
            std::size_t iCount = 0;
            RunForConnectedPlayers
            (
                [ & ]( IPlayersUtilities*, ::Ruby::CConnectedPlayer& Player )
                {
                    auto pEntity = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( Player.m_iSlot );

                    if ( pEntity && !pEntity->IsBot( ) && Player.n_bIsInGame && Player.m_bIsConnected )
                        iCount++;
                }
            );
            return iCount;
        }

        void RegisterFrameCmd( const std::function< void( ) >& C )
        {
            m_FrameCmdsStack.push_back( C );
        }

        void RegisterPlayer( CPlayerSlot Slot )
        {
            m_ConnectedPlayers[ Slot.Get( ) ] = ::Ruby::CConnectedPlayer{ Slot };
        }

        void QueueConnectedPlayers( )
        {
            for ( auto& ConnectedPlayer : m_ConnectedPlayers )
            {
                if ( !ConnectedPlayer.m_bIsConnected || !ConnectedPlayer.n_bIsInGame )
                    continue;

                const auto pEntity = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( ConnectedPlayer.m_iSlot );

                if ( pEntity && !pEntity->IsBot( ) )
                {
                    auto it = m_UniquePlayers.find( pEntity->m_steamID( ) );

                    if ( it == m_UniquePlayers.end( ) )
                        m_UniquePlayers.insert_or_assign( pEntity->m_steamID( ), CPlayerData( ConnectedPlayer.m_iSlot, pEntity->m_steamID( ) ) );
                    else if ( auto& PlayerData = std::get<::Ruby::CPlayerData>( *it );
                        PlayerData.m_iDisconnectTick != TICK_NEVER_THINK )
                    {
                        PlayerData.m_iDisconnectTick = TICK_NEVER_THINK;
                        PlayerData.m_Slot = ConnectedPlayer.m_iSlot;
                        PlayerData.m_iLastIteractTick = g_pCtx->GetGlobalVars( )->tickcount;
                    }
                }
            }

            for ( const auto& Cmd : m_FrameCmdsStack )
            {
                Cmd( );
            }

            m_FrameCmdsStack.clear( );
        }

        void ServerPutPlayer( CPlayerSlot Slot )
        {
            m_ConnectedPlayers[ Slot.Get( ) ].n_bIsInGame = true;
        }

        void PlayerDisconnect( CPlayerSlot Slot )
        {
            m_ConnectedPlayers[ Slot.Get( ) ] = ::Ruby::CConnectedPlayer{ };

            const auto pEntity = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( Slot );

            if ( pEntity )
            {
                auto it = m_UniquePlayers.find( pEntity->m_steamID( ) );

                if ( it != m_UniquePlayers.end( ) )
                    std::get< ::Ruby::CPlayerData >( *it ).Invalidate( );
            }
        }

        void ErasePlayerCache( CPlayerSlot Slot )
        {
            const auto pEntity = g_pCtx->m_Addresses.m_pGetBaseEntityBySlot.Call< CCSPlayerController* >( Slot );

            if ( pEntity )
                m_UniquePlayers.erase( pEntity->m_steamID( ) );
        }

        void EraseAllPlayers( )
        {
            m_UniquePlayers.clear( );
        }

    private:

        std::vector< std::function< void( ) > >                                 m_FrameCmdsStack    { };
        std::array< ::Ruby::CConnectedPlayer, 65 >                              m_ConnectedPlayers  { };
        std::unordered_map< UserId_t, ::Ruby::CPlayerData >                     m_UniquePlayers     { };
        std::unique_ptr< IPlayersUtilities >                                    m_pUtilities        { };

    };

    using CBasePlayersController = ::Ruby::CPlayersController< CPlayersUtilities >;
}

inline auto g_pPlayersController = std::make_unique< ::Ruby::CBasePlayersController >( );

#endif // !_PLUGIN_PLAYERS_CONTROLLER
