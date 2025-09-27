#ifndef DYNLIBUTILS_FUNCTION_H
#define DYNLIBUTILS_FUNCTION_H

#ifdef _WIN32
#pragma once
#endif

#include <module.h>

namespace DynLibUtils
{
	class CFunction
	{
	public:

		CFunction( )
			: m_hPtr{ }
		{ }

		~CFunction( ) = default;
		CFunction( const CFunction& ) = default;
		CFunction& operator= ( const CFunction& ) = default;

		CFunction( const DynLibUtils::CModule& hModule, const std::ptrdiff_t iOffset )
		{
			m_hPtr = hModule.GetModuleBase( ).Offset( iOffset );
		}

		CFunction( const DynLibUtils::CModule& hModule, const char* szPattern )
		{
			m_hPtr = hModule.FindBytesPattern( szPattern );
		}

		CFunction( const DynLibUtils::CModule& hModule, const char* szPattern, const std::ptrdiff_t iPreOffset, const std::ptrdiff_t iPostOffset = 0x0 )
		{
			m_hPtr = hModule.FindBytesPattern( szPattern ).Relative( iPreOffset, iPostOffset );
		}

		[[nodiscard]] __forceinline bool IsPresent( ) const noexcept
		{
			return m_hPtr != 0;
		}

		[[nodiscard]] __forceinline DynLibUtils::CMemory Get( ) const noexcept
		{
			return m_hPtr;
		}

		explicit __forceinline operator bool( ) const noexcept
		{
			return IsPresent( );
		}

		template <typename Ty, typename... Args>
		[[nodiscard]] __forceinline Ty Call( Args... args ) const noexcept
		{
			if ( m_hPtr == 0ull )
			{
				if constexpr ( std::is_void_v<Ty> )
					return;
				else if constexpr ( std::is_pointer_v<Ty> )
					return nullptr;
				else
					return Ty{ };
			}
			return reinterpret_cast< Ty( __fastcall* )( Args... ) >( m_hPtr.GetPtr( ) )( args... );
		}

	private:

		DynLibUtils::CMemory m_hPtr;

	};
}

#endif