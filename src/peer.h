#pragma once
#include "net_common.h"
#include "message_queue.h"

class peer
{
public:
	peer(asio::io_context& context)
		:m_Context(context),
		m_Socket(context)
	{
		m_ContextThread = std::thread([this]() { m_Context.run(); });
	}

	~peer()
	{
		disconnect();
	}

	int32_t connect(const std::string& address, uint16_t port)
	{
		asio::ip::tcp::endpoint ep(asio::ip::make_address(address), port);
		return connect_to_endpoint(ep);
	}

	int32_t connect_to_endpoint(const asio::ip::tcp::endpoint& ep)
	{
		asio::error_code ec;
		m_Socket.connect(ep, ec);
		m_bIsConnected = ec.value() == 0;

		if(ec)
			std::cout << ec.message() << std::endl;
		
		return ec.value();
	}

	void disconnect()
	{
		m_Socket.close();
		m_bIsConnected = false;
	}
	
	void write_some(const message<uint32_t>& msg) 
	{
		if (!m_bIsConnected)
			return;

		m_Socket.async_write_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t)
			{
				if (ec)
				{
					std::cout << "error in write_some: " << ec.message() << std::endl;
					return;
				}
			}
		);
	}
	void write_some_string(const std::string& str)
	{
		message<uint32_t> msg;
		msg << str;
		write_some(msg);
	}
	void read_some()
	{
		if (!m_bIsConnected)
			return;

		static message<uint32_t> msg;
		
		m_Socket.async_read_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t)
			{
				if (ec)
				{
					std::cout << "error in write_some: " << ec.message() << std::endl;
					return;
				}

				m_inQueue.push_back(msg);
				read_some();
			}
		);
	}

	bool is_connection_established()
	{
		if (m_inQueue.size() > 0 && m_inQueue.front().header.id == 2)
		{
			m_inQueue.pop_front();
			return true;
		}

		return false;
	}

	inline uint32_t queue_size() const { return m_inQueue.size(); }
	inline void clear_queue() { m_inQueue.clear(); }

	void print_queue() const
	{
		for (const auto& elem : m_inQueue.get_list())
		{
			std::cout << elem << std::endl;
		}
	}

private:
	asio::io_context& m_Context;
	std::thread m_ContextThread;
	asio::ip::tcp::socket m_Socket;
	message_queue<message<uint32_t>> m_inQueue;
	bool m_bIsConnected;
};