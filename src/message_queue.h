#pragma once
#include "net_common.h"
#include "message.h"

//Thread safe message queue
template<class msg_type>
class message_queue
{
	using msg = msg_type;
public:
	constexpr message_queue() = default;
	message_queue(const message_queue&) = delete;

	std::list<msg>& queue() { return m_Queue; }
	const std::list<msg>& queue() const { return m_Queue; }
	
	void push_front(const msg& m)
	{
		std::scoped_lock lk(m_AccessMutex);
		m_Queue.push_front(m);
	}

	void push_back(const msg& m)
	{
		std::scoped_lock lk(m_AccessMutex);
		m_Queue.push_back(m);
	}

	msg& front() 
	{ 
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue.front();
	}
	const msg& front() const 
	{ 
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue.front();
	}
	msg& back() 
	{
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue.back();
	}
	const msg& back() const 
	{ 
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue.back();
	}

	msg pop_back()
	{
		std::scoped_lock lk(m_AccessMutex);
		auto elem = m_Queue.back();
		m_Queue.pop_back();
		return elem;
	}

	msg pop_front()
	{
		std::scoped_lock lk(m_AccessMutex);
		auto elem = m_Queue.front();
		m_Queue.pop_front();
		return elem;
	}

	void clear()
	{
		std::scoped_lock lk(m_AccessMutex);
		m_Queue.clear();
	}

	std::list<msg>& get_list()
	{
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue;
	}

	const std::list<msg>& get_list() const 
	{
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue;
	}
	uint32_t size() const
	{ 
		std::scoped_lock lk(m_AccessMutex);
		return m_Queue.size();
	}

private:
	std::list<msg> m_Queue;
	mutable std::mutex m_AccessMutex;
};

template<typename msg_type>
std::ostream& operator<<(std::ostream& output, const message_queue<msg_type>& queue)
{
	for (const auto& item : queue.get_list())
		std::cout << item << std::endl;

	return output;
}