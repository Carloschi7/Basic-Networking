#pragma once
#include "net_common.h"
#include "message.h"

class client_interface
{
public:
	client_interface(asio::io_context& context)
		:m_Context(context),
		m_Socket(context),
		m_ServerEndpoint(asio::ip::tcp::endpoint()),
		m_bIsConnected(false),
		m_bSyncReceive(true),
		m_bPing(false)
	{
		//Used mainly to receive messages asynchronously
		m_ContextThread = std::thread([this]() { m_Context.run(); });
	}

	client_interface(asio::io_context& context, const std::string& local_address, uint16_t port)
		: m_Context(context),
		m_Socket(context),
		m_ServerEndpoint(asio::ip::make_address(local_address), port),
		m_bIsConnected(false),
		m_bSyncReceive(true),
		m_bPing(false)
	{
		//Used mainly to receive messages asynchronously
		m_ContextThread = std::thread([this]() { m_Context.run(); });
		connect_to_endpoint(m_ServerEndpoint);
	}

	virtual ~client_interface()
	{
		if (!m_bIsConnected) return;

		message<uint32_t> msg;
		//Code which tells the server the client is disconnecting
		msg.header.id = -1;
		msg << "Disconnection from host:";
		send(msg);
		m_Socket.close();

		m_ContextThread.join();
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
		

		std::cout << ec.message() << std::endl;
		return ec.value();
	}

	void disconnect()
	{
		if (!m_bIsConnected) return;

		m_Socket.close();
		m_bIsConnected = false;
	}

	void send(const message<uint32_t>& msg)
	{
		if (!m_bIsConnected) 
			throw std::exception("this client is not connected to any server");

		m_Socket.write_some(asio::buffer(&msg, sizeof(msg)));
	}

	void send_string(const std::string& str)
	{
		if (!m_bIsConnected)
			throw std::exception("this client is not connected to any server");
			
		if (str.size() > 256)
			return;

		message<uint32_t> msg;
		msg << str;
		m_Socket.write_some(asio::buffer(&msg, sizeof(msg)));
	}

	void receive()
	{
		m_bSyncReceive = true;
		message<uint32_t> msg;
		m_Socket.read_some(asio::buffer(&msg, sizeof(msg)));
		m_inQueue.push_back(msg);
	}

	//Enables messages to be received in their own thread
	void async_receive()
	{
		if (!m_bIsConnected) 
			throw std::exception("this client is not connected to any server");

		m_bSyncReceive = false;

		static message<uint32_t> msg;
		m_Socket.async_read_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t length) 
			{
				if (ec || m_bSyncReceive)
				{
					std::cout << "error in async_receive: " << ec.message() << std::endl;
					return;
				}
				
				//It is not necessary to save ping messages
				if (msg.header.id == 1)
					m_bPing = true;
				else
					m_inQueue.push_back(msg);
				
				async_receive();
			}
		);
	}

	template<typename _duration = std::chrono::duration<float>>
	_duration ping()
	{
		if (!m_bIsConnected)
			throw std::exception("this client is not connected to any server");

		message<uint32_t> msg;
		msg.header.id = 1; //Ping request
		auto tp1 = std::chrono::steady_clock::now();
		m_Socket.write_some(asio::buffer(&msg, sizeof(msg)));
		//Waiting synchronously for a server answer
		if (m_bSyncReceive)
			m_Socket.read_some(asio::buffer(&msg, sizeof(msg)));
		else
			while (!m_bPing) {} //Waiting for the last ping message(the context thread is going to change the var)
		
		auto tp2 = std::chrono::steady_clock::now();
		if (!m_bSyncReceive)
			m_bPing = false;

		return _duration(tp2 - tp1);
	}

	inline message_queue<message<uint32_t>>& get_queue() { return m_inQueue; }
	inline const message_queue<message<uint32_t>>& get_queue() const { return m_inQueue; }

private:
	//Context provided by the user
	asio::io_context& m_Context;
	//Thread in which the server runs
	std::thread m_ContextThread;
	//Client socket
	asio::ip::tcp::socket m_Socket;
	//Pretty simple to understand...
	asio::ip::tcp::endpoint m_ServerEndpoint;

	//Messages sent to this client
	message_queue<message<uint32_t>> m_inQueue;

	//If the client has no connection with a server, exceptions will be thrown,
	//so be careful about it
	bool m_bIsConnected;
	//Enabled by default
	std::atomic_bool m_bSyncReceive;
	//Ping helper
	std::atomic_bool m_bPing;
};