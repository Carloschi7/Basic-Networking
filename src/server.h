#pragma once
#include "net_common.h"
#include "message.h"

//Basic client-server implentation

//Runs on its own thread
	/*server_interface server(context, 60000);

	client_interface client(context, "127.0.0.1", 60000);
	client.async_receive();

	while (1)
	{
		message<uint32_t> msg;
		std::string str;
		std::getline(std::cin, str);
		msg << str;
		server.write_some(0, msg);

		if (client.get_queue().size() > 0)
		{
			std::cout << client.get_queue() << std::endl;
		}
		client.get_queue().clear();
	}*/

class server_interface
{
public:
	server_interface(asio::io_context& context, uint16_t listening_port)
		: m_Context(context),
		m_Socket(context),
		m_Acceptor(context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), listening_port))
	{
		m_ContextThread = std::thread([this]() { m_Context.run(); });
		std::cout << "Server listening on: " << m_Acceptor.local_endpoint() << std::endl;
		m_ClientSockets.reserve(m_MaxConcurrentClients);
		accept();
	}

	virtual ~server_interface()
	{
		m_ContextThread.detach();
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

				//Getting the socket's ownership
				m_ClientSockets.push_back(std::move(socket));
				read_some(m_ClientSockets.back());

				if(m_ClientSockets.size() < m_MaxConcurrentClients)
					accept();
			}
		);
	}

	void read_some(asio::ip::tcp::socket& client_socket)
	{
		//This item is static because the main thread, by exiting this function, would
		//delete msg before it is actually used by the server io_context
		static message<uint32_t> msg;

		client_socket.async_read_some(asio::buffer(&msg, sizeof(msg)),
			[&](asio::error_code ec, size_t length)
			{
				if (ec)
				{
					//For now if we get here we are just going to assume that the client
					//disconnected
					_RemoveSocket(client_socket);
					std::cout << "error in read_some: " << ec.message() << std::endl;
					return;
				}

				message<uint32_t> ping_ans;
				//Sorting user request
				switch (msg.header.id)
				{
				//Client is disconnecting explicitly
				case -1:
					std::cout << "Host: " << client_socket.remote_endpoint() << " disconnected" << std::endl;
					_RemoveSocket(client_socket);
					return;
				
				//Server ping
				case 1:
					ping_ans << "PING answer";
					ping_ans.header.id = 1;
					write_some(client_socket, ping_ans);
					break;
				//Normal msg
				default:
					identified_message<uint32_t> id_msg;
					id_msg = msg;
					id_msg.endpoint = client_socket.remote_endpoint();
					m_inQueue.push_back(id_msg);
				}

				read_some(client_socket);
			}
		);
	}

	void write_some(asio::ip::tcp::socket& socket, const message<uint32_t>& msg)
	{
		if (m_bWritingThreadRunning && std::this_thread::get_id() != m_WritingThread.get_id())
			throw std::exception("Automated writing enabled, no need for manual call");

		socket.write_some(asio::buffer(&msg, sizeof(msg)));
	}

	//Writes back to the sender of the message
	void write_some(const identified_message<uint32_t>& msg)
	{
		if (m_bWritingThreadRunning && std::this_thread::get_id() != m_WritingThread.get_id())
			throw std::exception("Automated writing enabled, no need for manual call");

		//Find correlated socket
		asio::ip::tcp::socket* curSocket = nullptr;

		for (auto& socket : m_ClientSockets)
			if (msg.endpoint == socket.remote_endpoint())
				curSocket = &socket;

		if (curSocket == nullptr)
			throw std::exception("Client socket not found");

		write_some(*curSocket, msg);
	}

	//The client number var represents the current location of the client
	//socket in the m_ClientSockets vector
	void write_some(uint8_t client_number, const message<uint32_t>& msg)
	{
		if (client_number >= m_ClientSockets.size())
			throw std::exception("the client index exceeds array size");

		write_some(m_ClientSockets[client_number], msg);
	}

	//Issues the server to handle write requests automatically
	//Sends automatically any msg in the m_outQueue list
	void enable_automated_write()
	{
		m_bWritingThreadRunning = true;
		m_WritingThread = std::thread([this]() 
			{
				while (m_bWritingThreadRunning)
				{
					while (m_outQueue.size() > 0)
					{
						//Wait for the server to write the message endpoint
						write_some(m_outQueue.front());
						m_outQueue.pop_front();
					}
				}
			});
	}

	void disable_automated_write()
	{
		m_bWritingThreadRunning = false;
		m_WritingThread.join();
	}

	void broadcast(const message<uint32_t>& msg)
	{
		for (auto& socket : m_ClientSockets)
			write_some(socket, msg);
	}

	//Prints the message queue
	void print_queue() const
	{
		for (auto& elem : m_inQueue.get_list())
		{
			std::cout << elem.endpoint << ": " << elem << std::endl;
		}
	}

	void clear_queue()
	{
		m_inQueue.clear();
	}

	inline uint32_t queue_size() const { return m_inQueue.size(); }

private:
	//Removes the disconnected client socket from the list
	void _RemoveSocket(asio::ip::tcp::socket& client_socket)
	{
		for (auto iter = m_ClientSockets.begin(); iter != m_ClientSockets.end(); ++iter)
		{
			//We only get the address from a vector element because the vector is
			//never going to be resized
			if (&(*iter) == &client_socket)
			{
				m_ClientSockets.erase(iter);
				break;
			}
		}
	}

private:
	//Thread in which the server runs
	std::thread m_ContextThread;
	//Automated writing thread
	std::thread m_WritingThread;
	//Flag which regulates the writing thread
	bool m_bWritingThreadRunning = false;

	//Server working context given by the main program
	asio::io_context& m_Context;
	//Accepts the connections with the clients
	asio::ip::tcp::acceptor m_Acceptor;
	//Server socket
	asio::ip::tcp::socket m_Socket;

	//Message queue from clients
	message_queue<identified_message<uint32_t>> m_inQueue;
	//Message queue to clients (the server pushes back messages which will be send
	// asynchronously)
	message_queue<identified_message<uint32_t>> m_outQueue;
	//Max clients at a time
	static constexpr uint32_t m_MaxConcurrentClients = 256;
	//Vector which stores the connected clients
	std::vector<asio::ip::tcp::socket> m_ClientSockets;
};

