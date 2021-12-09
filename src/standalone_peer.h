#pragma once
#include "net_common.h"
#include "message_queue.h"

#define LOG_ERROR(msg)\
std::cout << msg << std::endl;

#define LOG_ERROR_FUNC(func, msg)\
std::cout << "error found in " << func << ": " << msg << std::endl

//Async peer architecture for multiple connections
class standalone_peer
{
private:
	struct internal_socket;
public:
	//The port number comes into play when another peer tries to connect to this
	//peer. In that case, the defined port will be both this peer's local endpoint and the other
	//peer's remote endpoint
	//The port is not used when this peer tries to connect to another one
	standalone_peer(uint16_t port) :
		m_Acceptor(m_Context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
		m_RunningThread([&]() { m_Context.run(); })
	{
		m_SocketVec.reserve(m_MaxConcurrentConnections);
		m_TimeoutVec.resize(m_MaxConcurrentConnections);
		accept();
	}

	~standalone_peer()
	{
		m_RunningThread.detach();
	}

	void accept()
	{
		m_Acceptor.async_accept([&](asio::error_code ec, asio::ip::tcp::socket socket)
			{
				//Accept in this scenario used as a sort of "continue"
				if (m_ActiveConnections >= m_MaxConcurrentConnections)
					accept();

				if (ec)
				{
					LOG_ERROR_FUNC("accept", ec.message());
					return;
				}


				std::cout << "A new connection has been established:" << socket.remote_endpoint() << std::endl;

				_Add_Socket(std::move(socket));
				accept();
			}
		);
	}

	void connect(const std::string& address, uint16_t port)
	{
		connect(asio::ip::tcp::endpoint(asio::ip::make_address(address), port));
	}

	void connect(const asio::ip::tcp::endpoint& ep)
	{
		//Make sure we don't exceed the maximum amount of connections
		if (m_ActiveConnections >= m_MaxConcurrentConnections)
			return;

		asio::ip::tcp::socket socket(m_Context);
		asio::error_code ec;
		socket.connect(ep, ec);
		
		if (ec)
		{
			std::cout << ec.message() << std::endl;
			return;
		}

		std::cout << "connected succesfully to: " << socket.remote_endpoint() << std::endl;

		_Add_Socket(std::move(socket));
	}

	//Disconnects the socket bound to the given endpoint
	void disconnect(const asio::ip::tcp::endpoint& ep)
	{
		asio::ip::tcp::socket* pEndPeer = nullptr;

		for (auto& is : m_SocketVec)
			if (is.socket.remote_endpoint() == ep)
				pEndPeer = &is.socket;

		if (!pEndPeer)
		{
			LOG_ERROR("no socket was bound to the given endpoint");
			return;
		}

		pEndPeer->close();
		m_ActiveConnections--;
	}

	void disconnect(uint32_t host_number)
	{
		if (host_number >= m_SocketVec.size() || !m_SocketVec[host_number].socket.is_open())
		{
			LOG_ERROR("invalid peer number");
			return;
		}
		disconnect(m_SocketVec[host_number].socket.remote_endpoint());
	}

	//Endpoint used by the acceptor to establish new connections
	asio::ip::tcp::endpoint local_endpoint() const
	{
		return m_Acceptor.local_endpoint();
	}

	//Enpoints of open connections
	std::vector<asio::ip::tcp::endpoint> remote_endpoints() const
	{
		std::vector<asio::ip::tcp::endpoint> ret;

		for (auto& is : m_SocketVec)
			if(is.socket.is_open())
				ret.push_back(is.socket.remote_endpoint());

		return ret;
	}

	void read_some(internal_socket& is)
	{
		static message<uint32_t> msg;
		is.socket.async_read_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t) 
			{
				//Disconnection was issued
				if (!is.socket.is_open())
					return;

				if (ec)
				{
					bool bErased = false;
					uint32_t index = 0;
					for (auto iter = m_SocketVec.begin(); iter != m_SocketVec.end(); ++iter, ++index)
					{
						if (is.unique_id == iter->unique_id)
						{
							auto& tp = m_TimeoutVec[index];
							if (tp.time_since_epoch() == std::chrono::duration<float>(0.0f))
							{
								LOG_ERROR_FUNC("read_some", ec.message());
								tp = std::chrono::steady_clock::now();
								break;
							}

							//Wait 3 seconds
							if ((std::chrono::steady_clock::now() - tp).count() > 3e9)
							{
								iter->socket.close();
								m_ActiveConnections--;
								m_TimeoutVec[index] = std::chrono::steady_clock::time_point();
								LOG_ERROR("A socket has been removed because invisible for several seconds");
								bErased = true;
								break;
							}
						}
					}

					if(!bErased)
						read_some(is);

					return;
				}

				//If no errors are found, we delete the timestamp
				uint32_t index = 0;
				for (auto iter = m_SocketVec.begin(); iter != m_SocketVec.end(); ++iter, ++index)
				{
					if (is.unique_id == iter->unique_id)
					{
						m_TimeoutVec[index] = std::chrono::steady_clock::time_point();
					}
				}

				//Ping request
				if (msg.header.id == 1)
				{
					message<uint32_t> msg;
					msg.header.id = 2;
					is.socket.write_some(asio::buffer(&msg, sizeof(msg)));
				}
				//Ping answer
				else if (msg.header.id == 2)
				{
					m_bPing = true;
				}
				else
				{
					identified_message<uint32_t> id = msg;
					id.endpoint = is.socket.remote_endpoint();
					m_Queue.push_back(id);
				}

				read_some(is);
			});
	}

	void write_some(const asio::ip::tcp::endpoint& ep, const message<uint32_t>& msg)
	{
		//Searching the socket which matches with the given endpoint
		asio::ip::tcp::socket* pEndPeer = nullptr;

		for (auto& is : m_SocketVec)
		{
			if (is.socket.remote_endpoint() == ep)
				pEndPeer = &is.socket;
		}

		//Socket not found
		if (!pEndPeer)
			return;

		pEndPeer->async_write_some(asio::buffer(&msg, sizeof(msg)),
			[](asio::error_code ec, size_t)
			{
				if (ec)
					LOG_ERROR_FUNC("write_some", ec.message());
			});
	}

	void write_some(uint32_t host_number, const message<uint32_t>& msg)
	{
		if (host_number >= m_SocketVec.size())
			return;
		
		if (!m_SocketVec[host_number].socket.is_open())
		{
			LOG_ERROR("Cannot write to a closed socket");
			return;
		}

		asio::error_code ec;
		m_SocketVec[host_number].socket.write_some(asio::buffer(&msg, sizeof(msg)), ec);

		if (ec)
			LOG_ERROR_FUNC("write_some", ec.message());
	}

	//Broadcast to every currently connected sokcet
	void broadcast(const message<uint32_t> msg)
	{
		for (uint32_t i = 0; i < m_SocketVec.size(); i++)
			if (m_SocketVec[i].socket.is_open())
				write_some(i, msg);
	}

	template<typename _duration = std::chrono::steady_clock::duration>
	_duration ping(uint32_t host_number)
	{
		if (!(host_number < m_SocketVec.size() && m_SocketVec[host_number].socket.is_open()))
			return _duration();


		message<uint32_t> msg;
		//Ping request
		msg.header.id = 1;
		auto tp1 = std::chrono::steady_clock::now();
		write_some(host_number, msg);

		while (!m_bPing) {}
		auto tp2 = std::chrono::steady_clock::now();
		m_bPing = false;
		return _duration(tp2 - tp1);
	}

	//SYNC FUNCTION! Invoking this function improperly can stall the program
	//Waits until the number of connections increases after the function call
	void wait_for_connection() const
	{
		uint32_t init_size = m_SocketVec.size();
		while (init_size >= m_SocketVec.size()) {}
	}

	inline uint32_t queue_size() const { return m_Queue.size(); }
	
	//If peer number is defined, only messages from that peer will be printed
	void print_queue(uint32_t* peer_number = nullptr) const 
	{
		if (!peer_number)
		{
			for (const auto& elem : m_Queue.get_list())
				std::cout << elem << std::endl;
		}
		else
		{
			if (*peer_number >= m_SocketVec.size())
				return;

			for (const auto& elem : m_Queue.get_list())
				if(elem.endpoint == m_SocketVec[*peer_number].socket.remote_endpoint())
					std::cout << elem << std::endl;
		}
	}

	//If peer_number is defined, deletes only the msg from that peer
	void clear_queue(uint32_t* peer_number = nullptr)
	{ 
		if (m_Queue.size() == 0)
			return;

		if(!peer_number)
			m_Queue.clear();
		else
		{
			auto& lst = m_Queue.get_list();
			lst.remove_if([&](const identified_message<uint32_t>& msg)
				{
					bool ret = msg.endpoint == m_SocketVec[*peer_number].socket.remote_endpoint(); 
					return ret;
				}
			);
		}
	}

private:
	void _Add_Socket(asio::ip::tcp::socket&& socket)
	{
		uint32_t i = 0;
		for (; i < m_SocketVec.size(); i++)
			if (!m_SocketVec[i].socket.is_open())
				break;

		if (i != m_SocketVec.size())
		{
			m_SocketVec[i] = { std::move(socket), m_SocketEnumeration++ };
			read_some(m_SocketVec[i]);
		}
		else
		{
			//Getting the socket's ownership
			m_SocketVec.push_back({ std::move(socket), m_SocketEnumeration++ });
			read_some(m_SocketVec.back());
		}
		m_ActiveConnections++;
	}

private:
	asio::io_context m_Context;
	std::thread m_RunningThread;

	//Maximum number of connections
	static constexpr uint32_t m_MaxConcurrentConnections = 256;
	//Used to accept new peers
	asio::ip::tcp::acceptor m_Acceptor;
	//List of sockets
	struct internal_socket
	{
		asio::ip::tcp::socket socket;
		uint32_t unique_id;
	};
	std::vector<internal_socket> m_SocketVec;
	uint32_t m_SocketEnumeration = 0;
	//NOTE: this is different from m_SocketVec.size() because there may are some
	//closed sockets waiting to be replaced
	uint32_t m_ActiveConnections = 0;
	//List of timeout which help to remove a socket after
	//it does not respond for a fixed amount of time
	std::vector<std::chrono::steady_clock::time_point> m_TimeoutVec;
	//Incoming messages
	message_queue<identified_message<uint32_t>> m_Queue;
	//For ping answers
	std::atomic_bool m_bPing = false;
};