#ifndef ASYNC_DBG_PRINT_H
#define ASYNC_DBG_PRINT_H
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>

namespace yb {
namespace detail {

typedef int dbg_print_ctx;


inline std::string dbg_print(dbg_print_ctx , const char * , ...)
{
	return std::string();
}
#if 0
template<typename ... Args>
std::string dbg_print(dbg_print_ctx ctx, const char * fmt, Args ... args)
{
	/*
	size_t size = snprintf( nullptr, 0, fmt, args ... ) + 1 + ctx * 4 + 1;
	std::unique_ptr<char[]> buf( new char[ size ] ); 
	buf.get()[0] = '\n';
	memset(buf.get() + 1, ' ', ctx * 4);
	snprintf( buf.get() + ctx * 4 + 1, size - (ctx * 4 + 1), fmt, args ... );
	return std::string( buf.get(), buf.get() + size - 1 );*/
	for (int i = 0; i < ctx; ++i) {
		printf("    ");
	}
	printf(fmt, args...);
	printf("\n");
	return std::string();
}
#endif // 0

} // namespace detail
} // namespace yb


#endif // ASYNC_DBG_PRINT_H
