#ifndef ASYNC_DBG_PRINT_H
#define ASYNC_DBG_PRINT_H
#include <memory>
#include <iostream>
#include <string>
#include <cstdio>

namespace yb {
namespace detail {

struct dbg_print_ctx
{
	dbg_print_ctx() : level(0) {}
	dbg_print_ctx(const dbg_print_ctx& old) : level(old.level + 1) {}

	int get_indent() { return level * 4; }
private:
	int level;
};

template<typename ... Args>
std::string dbg_print(const dbg_print_ctx & ctx, const char * fmt, Args ... args)
{
	size_t size = snprintf( nullptr, 0, fmt, args ... ) + 1;
	std::unique_ptr<char[]> buf( new char[ size ] ); 
	snprintf( buf.get(), size, fmt, args ... );
	return std::string( buf.get(), buf.get() + size - 1 );
}

} // namespace detail
} // namespace yb


#endif // ASYNC_DBG_PRINT_H
