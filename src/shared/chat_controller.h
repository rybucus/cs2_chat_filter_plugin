#ifndef _PLUGIN_CHAT_CONTROLLER
#define _PLUGIN_CHAT_CONTROLLER

#include <string_view>
#include <functional>
#include <memory>
#include <unordered_set>
#include <sstream>
#include <vector>
#include <cctype>

#define PCRE2_STATIC
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <utf8.h>
#include <usermessages.pb.h>

#include <../ctx.h>

namespace Ruby
{
	using SayCallback_t = std::function< bool( const CCommandContext&, CCommand&, bool ) >;

	class CPCRE2Wrapper
	{
	public:

		struct CCompiledPattern
		{
			CCompiledPattern( )
				: m_pRegex{ nullptr }, m_pMetaData{ nullptr }
			{ }

			CCompiledPattern( pcre2_code* pRegex, pcre2_match_data* pMetaData )
				: m_pRegex{ pRegex }, m_pMetaData{ pMetaData }
			{ }

			CCompiledPattern( const CCompiledPattern& Other )
			{
				if ( *this != Other )
				{
					m_pRegex = Other.m_pRegex;
					m_pMetaData = Other.m_pMetaData;
				}
			}

			[[nodiscard]] bool operator != ( const CCompiledPattern& Other ) const noexcept
			{
				return this->m_pRegex != Other.m_pRegex
					&& this->m_pMetaData != Other.m_pMetaData;
			}

			[[nodiscard]] bool Present( ) const noexcept
			{
				return *this != CCompiledPattern( );
			}

			operator bool( ) const noexcept
			{
				return Present( );
			}

			pcre2_code*		  m_pRegex;
			pcre2_match_data* m_pMetaData;
		};

		CPCRE2Wrapper( )
			: m_pGCtx{ nullptr }, m_pCCtx{ nullptr }
		{ 
			m_pGCtx = pcre2_general_context_create( nullptr, nullptr, nullptr );
			m_pCCtx = pcre2_compile_context_create( m_pGCtx );
		}

		[[nodiscard]] CCompiledPattern operator ( )( const std::string_view strPattern,
			const int iFlags )
		{
			int iError = 0;
			PCRE2_SIZE iErrorOffset = 0;

			if ( pcre2_code* pRegexCompiled = pcre2_compile
			(
				( PCRE2_SPTR )strPattern.data( ),
				strPattern.size( ),
				iFlags,
				&iError,
				&iErrorOffset,
				m_pCCtx
			); 
				pRegexCompiled )
			{
				if ( auto pMetaData = pcre2_match_data_create_from_pattern( pRegexCompiled, nullptr );
					pMetaData )
				{
					return CCompiledPattern( pRegexCompiled, pMetaData );
				}
				pcre2_code_free( pRegexCompiled );
			}

			return { };
		}

	private:

		pcre2_general_context* m_pGCtx;
		pcre2_compile_context* m_pCCtx;

	};

	class CChatFilter
	{
		struct CPrimitiveRule
		{
			CPrimitiveRule
			( 
				const std::string_view strPattern,
				const int   iFlags,
				const CPCRE2Wrapper::CCompiledPattern& pRegex
			)
				: m_strPattern{ strPattern }, m_iFlags{ iFlags }, m_Data{ pRegex }
			{ }

			~CPrimitiveRule( )
			{
				if ( m_Data.m_pMetaData )
					pcre2_match_data_free( m_Data.m_pMetaData );

				if ( m_Data.m_pRegex )
					pcre2_code_free( m_Data.m_pRegex );
			}

			std::string m_strPattern;
			int			m_iFlags;
			CPCRE2Wrapper::CCompiledPattern m_Data;
		};

	public:

		CChatFilter( )
			: m_pParser{  }, m_Rules{ }, m_bIsLoaded{ }
		{ }

		void LoadConfigFromFile( const std::string& Path ) noexcept
		{
			if ( m_pParser = new KeyValues( "chat_filter_config" ); m_pParser )
			{
				if ( m_pParser->LoadFromFile( g_pFullFileSystem, Path.c_str( ) ) )
				{
					CPCRE2Wrapper RegexWrapper{ };

					for ( auto pSubDir = m_pParser->GetFirstSubKey( );
						pSubDir; pSubDir = pSubDir->GetNextKey( ) )
					{
						if ( const auto szPattern = pSubDir->GetString( "pattern" ); 
							szPattern )
						{
							if ( const auto szFlags = pSubDir->GetString( "flags" ); 
								szFlags )
							{
								const std::string_view strPattern( szPattern );
								if ( strPattern.size( ) >= 512 )
									continue;

								std::vector< std::string > vecFlagsTokens{ };
								SplitComma( szFlags, vecFlagsTokens );

								const auto iFlags = ResolveFlags( vecFlagsTokens );

								if ( auto pRegexCompiled = RegexWrapper( strPattern, iFlags );
									pRegexCompiled )
									m_Rules.push_back( std::make_unique< CPrimitiveRule >( strPattern, iFlags, pRegexCompiled ) );
								else
									META_CONPRINTF( "Failed to compile REGEX pattern: %s", pSubDir->GetName( ) );
							}
						}
					}
				}
				delete m_pParser;
			}
			m_bIsLoaded = true;
		}

		std::string MakeFilteredString( const std::string& szStr, char bFilterSymbol = '*' ) const noexcept
		{
			if ( m_Rules.empty( ) || szStr.empty( ) )
				return szStr;

			std::string szResult( szStr );

			for ( const auto& pRule : m_Rules )
			{
				const auto& Rule = *pRule;

				if ( !Rule.m_Data )
					continue;

				PCRE2_SIZE iStartOffset = 0;
				std::vector< std::pair< size_t, size_t > > vecRanges{ };

				while ( iStartOffset <= szResult.size( ) )
				{
					PCRE2_SPTR szSubject    = reinterpret_cast< PCRE2_SPTR >( szResult.data( ) );
					PCRE2_SIZE iSubjectLen  = static_cast< PCRE2_SIZE >( szResult.size( ) );

					const auto iRc 
						= pcre2_match( Rule.m_Data.m_pRegex, szSubject, iSubjectLen, iStartOffset, 0, Rule.m_Data.m_pMetaData, nullptr );

					if ( iRc < 0 )
						break;

					PCRE2_SIZE* pOVector     = pcre2_get_ovector_pointer( Rule.m_Data.m_pMetaData );
					PCRE2_SIZE  iMatchStart  = pOVector[ 0 ];
					PCRE2_SIZE  iMatchEnd    = pOVector[ 1 ];

					if ( iMatchEnd <= iMatchStart )
					{
						iStartOffset = iMatchStart + 1;
						continue;
					}

					vecRanges.emplace_back( static_cast< size_t >( iMatchStart ), static_cast< size_t >( iMatchEnd ) );
					iStartOffset = iMatchEnd;
				}

				if ( vecRanges.empty( ) )
					continue;

				auto CountCodepoints = [ & ]( size_t a, size_t b ) -> size_t
				{
					using It_t = std::string::const_iterator;

					utf8::iterator< It_t > itBegin( szResult.begin( ) + static_cast< std::ptrdiff_t >( a ), szResult.begin( ), szResult.end( ) );
					utf8::iterator< It_t > itEnd( szResult.begin( ) + static_cast< std::ptrdiff_t >( b ), szResult.begin( ), szResult.end( ) );

					size_t iCount{ 0 };
					for ( ; itBegin != itEnd; ++itBegin )
						++iCount;

					return iCount ? iCount : ( b - a );
				};

				std::string newRes;
				newRes.reserve( szResult.size( ) );
				size_t iPos{ 0 };

				for ( auto& r : vecRanges )
				{
					const auto iBegin = r.first, 
						iEnd = r.second;

					if ( iBegin > iPos )
						newRes.append( szResult.data( ) + iPos, iBegin - iPos );

					const auto iCodePointsCount = CountCodepoints( iBegin, iEnd );

					newRes.append( iCodePointsCount, bFilterSymbol );

					iPos = iEnd;
				}

				if ( iPos < szResult.size( ) )
					newRes.append( szResult.data( ) + iPos, szResult.size( ) - iPos );

				szResult.swap( newRes );
			}	

			return szResult;
		}

		bool IsRulesLoaded( ) const noexcept
		{
			return m_bIsLoaded;
		}

	private:

		static __forceinline
			int ResolveFlags( const std::vector< std::string >& vecTokens )
		{
			auto iRetFlags = 0;
			for ( auto& szToken : vecTokens )
			{
				auto iId = 0;

				try { iId = std::stoi( szToken ); }
				catch ( ... ) { continue; }

				switch ( iId )
				{
					case 0:  break;
					case 1:  iRetFlags |= PCRE2_CASELESS; break;
					case 2:  iRetFlags |= PCRE2_MULTILINE; break;
					case 3:  iRetFlags |= PCRE2_DOTALL; break;
					case 4:  iRetFlags |= PCRE2_EXTENDED; break;
					case 5:  iRetFlags |= PCRE2_ANCHORED; break;
					case 6:  iRetFlags |= PCRE2_DOLLAR_ENDONLY; break;
					case 7:  iRetFlags |= PCRE2_UNGREEDY; break;
					case 8:  iRetFlags |= PCRE2_NOTEMPTY; break;
					case 9:  iRetFlags |= PCRE2_UTF; break;
					case 10: iRetFlags |= PCRE2_NO_UTF_CHECK; break;
					case 11: iRetFlags |= PCRE2_UCP; break;
					default: break;
				}
			}
			return iRetFlags;
		}

		static __forceinline 
			void SplitComma( const std::string_view s, std::vector<std::string>& out )
		{
			size_t i = 0, n = s.size( );
			while ( i < n )
			{
				size_t j = i;
				while ( j < n && s[ j ] != ',' ) ++j;

				size_t a = i;
				while ( a < j && std::isspace( static_cast< unsigned char >( s[ a ] ) ) ) 
					++a;

				size_t b = j;
				while ( b > a && std::isspace( static_cast< unsigned char >( s[ b - 1 ] ) ) ) 
					--b;

				if ( b > a )
					out.emplace_back( s.substr( a, b - a ) );

				i = ( j < n ) ? j + 1 : j;
			}
		}

	private:

		KeyValues* m_pParser;
		std::vector< std::unique_ptr< CPrimitiveRule > > m_Rules;
		bool m_bIsLoaded;

	};

	class CChatController
	{
	private:

		static std::uint64_t NextHandle( ) noexcept
		{
			static std::uint64_t hBaseHandle = static_cast< std::uint64_t >( -1 );
			return ++hBaseHandle;
		}

	public:

		CChatController( )
		{ }

		[[nodiscard]] std::uint64_t RegisterChatCallback( const SayCallback_t& C )
		{
			const auto hOurHandle = NextHandle( );
			m_Callbacks.insert_or_assign( hOurHandle, C );
			return hOurHandle;
		}

		void RemoveCallback( const std::uint64_t hHandle )
		{
			m_Callbacks.erase( hHandle );
		}

		void RemoveAllCallbacks( )
		{
			m_Callbacks.clear( );
		}

		bool OnSay( const CCommandContext& ctx, CCommand& args, bool bIsSayedToTeam = false ) const noexcept
		{
			bool bIsShouldSkipOriginalCall = false;
			for ( const auto& it : m_Callbacks )
			{
				const auto& C = std::get< SayCallback_t >( it );
				if ( C( ctx, args, bIsSayedToTeam ) )
					bIsShouldSkipOriginalCall = true;
			}
			return bIsShouldSkipOriginalCall;
		}

	private:

		std::unordered_map< std::uint64_t, SayCallback_t > m_Callbacks		{ };

	};
}

inline auto g_pChatController = std::make_unique< Ruby::CChatController >( );

inline auto g_pChatFilter = std::make_unique< Ruby::CChatFilter >( );

#endif // !_PLUGIN_CTX
