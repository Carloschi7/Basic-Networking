#include <iostream>
#include <string>

#include "net_common.h"
#include "message.h"
#include "message_queue.h"
#include "server.h"
#include "client.h"
#include "connection.h"
#include "peer.h"
#include "standalone_peer.h"

uint32_t GUI_select_host(standalone_peer& peer)
{
	auto listend = peer.remote_endpoints();
	if (listend.size() == 0)
	{
		std::cout << "No peers connected at the moment :-(" << std::endl;
		return -1;
	}

	std::cout << "Choose a peer to message" << std::endl;
	for (uint32_t i = 0; i < listend.size(); i++)
		std::cout << i << ": " << listend[i] << std::endl;

	uint32_t peer_num = 0;
	std::cin >> peer_num;
	if (peer_num >= listend.size())
		return -1;

	return peer_num;
}

//Implementing the standalone peer with a raw console GUI
int main()
{
	uint16_t listening_port;
	std::cout << "------------------Standalone peer Console GUI------------------\nSet the listening port:";
	std::cin >> listening_port;
	standalone_peer peer(listening_port);
	
	while (1)
	{
		uint32_t answer = 0;
		std::cout << "Choose an option:\n";
		std::cout << "1) Connect\n2) Message\n3) Ping\n4) Disconnect\n";
		std::cin >> answer;

		if (answer == 1)
		{
			std::string address;
			uint16_t port;
			std::cout << "Insert address and port:" << std::endl;
			std::cin >> address >> port;
			peer.connect(address, port);
		}
		if (answer == 2)
		{
			uint32_t peer_num = GUI_select_host(peer);
			if (peer_num == -1)
				continue;

			std::cout << "Chat opened, type <menu> to go back" << std::endl;

			bool bReadingThreadRunning = true;
			std::thread rt([&]()
				{
					while (bReadingThreadRunning)
					{
						if (peer.queue_size() > 0)
						{
							peer.print_queue(&peer_num);
							peer.clear_queue(&peer_num);
						}
					}
				});

			while (1)
			{
				message<uint32_t> msg;
				std::string str;
				std::getline(std::cin, str);
				msg << str;

				if (!str.empty())
				{
					if (str == "<menu>")
					{
						bReadingThreadRunning = false;
						rt.join();
						break;
					}

					peer.write_some(peer_num, msg);
				}
			}
		}
		if (answer == 3)
		{
			uint32_t peer_num = GUI_select_host(peer);
			if (peer_num == -1)
				continue;

			std::cout << (double)peer.ping(peer_num).count() / 1e9 << "s" << std::endl;
		}
		if (answer == 4)
		{
			uint32_t peer_num = GUI_select_host(peer);
			if (peer_num == -1)
				continue;

			peer.disconnect(peer_num);
			std::cout << "disconnected succesfully!" << std::endl;
		}
	}

	return 0;
}