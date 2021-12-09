#pragma once
#include "net_common.h"
#include "message_queue.h"

//Basic peer-connection implementation
/*connection c(context, 60000);
	while (1) {}*/

	/*peer p1(context);
	p1.connect("127.0.0.1", 60000);
	p1.read_some();

	std::thread print_thread([&]()
		{
			for (;;)
			{
				if (p1.queue_size() > 0)
				{
					p1.print_queue();
					p1.clear_queue();
				}
			}
		});

	while (1)
	{
		std::string str;
		std::getline(std::cin, str);
		p1.write_some_string(str);
	}*/

class connection
{
public:
	connection(asio::io_context& context, uint16_t listening_port)
		:m_Context(context),
		m_Acceptor(context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), listening_port)),
		m_SocketConnection(context, context)
	{
		m_ContextThread = std::thread([this]() { m_Context.run(); });
		enable_automated_write();
		accept();
	}

	void accept()
	{
		m_Acceptor.async_accept([&](asio::error_code ec, asio::ip::tcp::socket socket)
			{
				if (ec)
				{
					std::cout << "error in accept: " << ec.message() << std::endl;
					return;
				}

				std::cout << "A new connection has been established:" << socket.remote_endpoint() << std::endl;


				//The first socket who connects will be first in the pair
				auto& first = m_SocketConnection.first;
				auto& second = m_SocketConnection.second;
				if (!first.is_open())
				{
					first = std::move(socket);
					read_some(first);
				}
				else
				{
					second = std::move(socket);
					read_some(second);
				}

				m_bPeersAreLinked = first.is_open() && second.is_open();

				if (m_bPeersAreLinked) //Send to both peers the connection is ready
				{
					identified_message<uint32_t> id1, id2;
					
					//Code 2 = peers ready
					id1.header.id = 2;
					id2.header.id = 2;
					id1.endpoint = first.remote_endpoint();
					id2.endpoint = second.remote_endpoint();
					if (m_bWritingThreadRunning)
					{
						m_inQueue.push_back(id1);
						m_inQueue.push_back(id2);
					}
					else
					{
						write_some(id1);
						write_some(id2);
					}
					return;
				}

				accept();
			}
		);
	}

	void read_some(asio::ip::tcp::socket& peer_socket)
	{
		static message<uint32_t> msg;
		peer_socket.async_read_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t length)
			{
				//We start reading operations only when both peers are ready
				if (!m_bPeersAreLinked)
					return;

				if (ec)
				{
					//TODO: implementa un modo per riammettere i client nel momento in cui uno si disconnette
					std::cout << "error in read_some: " << ec.message() << std::endl;
					peer_socket.close();
					accept();
					return;
				}

				identified_message<uint32_t> id_msg = msg;
				id_msg.endpoint = peer_socket.remote_endpoint();
				m_inQueue.push_back(id_msg);

				read_some(peer_socket);
			});
	}
	//Sends messages from the out queue to the respective peer
	void enable_automated_write()
	{
		m_bWritingThreadRunning = true;
		m_WritingThread = std::thread([this]()
			{
				while (m_bWritingThreadRunning)
				{
					while (m_inQueue.size() > 0)
					{
						auto msg = m_inQueue.pop_front();
						_exchange_endpoint(msg);
						write_some(msg);
					}
				}
			}
		);
	}

	void disable_automated_write()
	{
		m_bWritingThreadRunning = false;
		m_WritingThread.join();
	}

	void write_some(const identified_message<uint32_t>& msg)
	{
		message<uint32_t> ret = msg;

		if (msg.endpoint == m_SocketConnection.first.remote_endpoint())
			m_SocketConnection.first.write_some(asio::buffer(&ret, sizeof(ret)));
		else
			m_SocketConnection.second.write_some(asio::buffer(&ret, sizeof(ret)));
	}

	inline bool are_peers_linked() const { return m_bPeersAreLinked; }

private:
	//Exchanges the message peer endpoint
	void _exchange_endpoint(identified_message<uint32_t>& msg)
	{
		if (msg.endpoint == m_SocketConnection.first.remote_endpoint())
			msg.endpoint = m_SocketConnection.second.remote_endpoint();
		else
			msg.endpoint = m_SocketConnection.first.remote_endpoint();
	}

private:
	using socket = asio::ip::tcp::socket;
	using acceptor = asio::ip::tcp::acceptor;

	asio::io_context& m_Context;
	std::thread m_ContextThread;

	std::thread m_WritingThread;
	std::atomic_bool m_bWritingThreadRunning = false;

	//Acceptor, used to find the peers
	acceptor m_Acceptor;
	//Binds two peer sockets togheter
	std::pair<socket, socket> m_SocketConnection;
	message_queue<identified_message<uint32_t>> m_inQueue;

	bool m_bPeersAreLinked = false;
};