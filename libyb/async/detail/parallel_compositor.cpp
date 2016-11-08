#include "parallel_compositor.hpp"
using namespace yb;
using namespace yb::detail;

parallel_compositor::parallel_compositor()
{
}

void parallel_compositor::add_task(task<void> && t)
{
	m_tasks.emplace_back();
	m_tasks.back().t = std::move(t);
}

void parallel_compositor::add_task(task<void> && t, task<void> && u)
{
	parallel_compositor c;
	c.m_tasks.emplace_back();
	c.m_tasks.emplace_back();

	c.m_tasks.front().t = std::move(t);
	c.m_tasks.back().t = std::move(u);

	this->append(std::move(c));
}

void parallel_compositor::append(parallel_compositor && o)
{
	m_tasks.splice(m_tasks.end(), o.m_tasks);
}

bool parallel_compositor::empty() const
{
	return m_tasks.empty();
}

task<void> parallel_compositor::pop()
{
	assert(!this->empty());
	task<void> res = std::move(m_tasks.front().t);
	m_tasks.pop_front();
	return res;
}

void parallel_compositor::prepare_wait(task_wait_preparation_context & ctx, cancel_level cl)
{
	for (std::list<parallel_task>::iterator it = m_tasks.begin(); it != m_tasks.end(); ++it)
	{
		task_wait_memento_builder mb(ctx);
		it->t.prepare_wait(ctx, cl);
		it->m = mb.finish();
	}
}
