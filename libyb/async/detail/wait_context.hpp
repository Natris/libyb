#ifndef LIBYB_ASYNC_DETAIL_WAIT_CONTEX_HPP
#define LIBYB_ASYNC_DETAIL_WAIT_CONTEX_HPP

#include "../../utils/noncopyable.hpp"
#include <memory> // unique_ptr

namespace yb {

struct task_wait_poll_item;
struct task_wait_preparation_context_impl;

struct task_wait_checkpoint
{
	size_t poll_item_count;
	size_t timer_item_count;
	size_t finished_task_count;
};

struct task_wait_memento
{
	size_t poll_item_first;
	size_t poll_item_last;

	size_t timer_item_first;
	size_t timer_item_last;

	size_t finished_task_count;

	void clear()
	{
		poll_item_first = 0;
		poll_item_last = 0;

		timer_item_first = 0;
		timer_item_last = 0;

		finished_task_count = 0;
	}
};

class task_wait_preparation_context
{
public:
	task_wait_preparation_context();
	~task_wait_preparation_context();

	void clear();
	void add_poll_item(task_wait_poll_item const & item);
	void set_finished();
	task_wait_preparation_context_impl * get() const;
	task_wait_checkpoint checkpoint() const;

private:
	std::unique_ptr<task_wait_preparation_context_impl> m_pimpl;
};

class task_wait_memento_builder
	: noncopyable
{
public:
	task_wait_memento_builder(task_wait_preparation_context & ctx) throw()
		: m_ctx(ctx), m_checkpoint(ctx.checkpoint())
	{
	}

	task_wait_memento finish() throw()
	{
		task_wait_checkpoint chkp = m_ctx.checkpoint();

		task_wait_memento res;
		res.finished_task_count = chkp.finished_task_count - m_checkpoint.finished_task_count;
		res.poll_item_first = m_checkpoint.poll_item_count;
		res.poll_item_last = chkp.poll_item_count;
		res.timer_item_first = m_checkpoint.timer_item_count;
		res.timer_item_last = chkp.timer_item_count;
		return res;
	}

private:
	task_wait_preparation_context & m_ctx;
	task_wait_checkpoint m_checkpoint;
};

class task_wait_finalization_context
{
public:
	task_wait_preparation_context * prep_ctx;
	size_t finished_tasks;
	size_t selected_poll_item;
	size_t selected_timer_item;

	bool is_poll_item() const { return selected_poll_item != (size_t)-1; }
	bool is_timer_item() const { return selected_timer_item != (size_t)-1; }

	bool contains(task_wait_memento const & m) const
	{
		return (finished_tasks && m.finished_task_count != 0)
			|| (is_poll_item() && m.poll_item_first <= selected_poll_item && selected_poll_item < m.poll_item_last)
			|| (is_timer_item() && m.timer_item_first <= selected_timer_item && selected_timer_item < m.timer_item_last);
	}
};

} // namespace yb

#endif // LIBYB_ASYNC_DETAIL_WAIT_CONTEX_HPP
