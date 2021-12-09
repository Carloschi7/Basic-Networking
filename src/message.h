#pragma once
#include "net_common.h"

class client_interface;

template<typename data_type>
struct message_header
{
	data_type id{};
	uint32_t length = 0;
};

constexpr uint32_t message_length_max = 1024;
//NOT OPTIMIZED
template<typename data_type>
struct message
{
	using array_type = std::array<uint8_t, message_length_max>;

	message_header<data_type> header;
	array_type m_Data = { 0 };
	uint16_t m_Cursor = 0;
public:
	constexpr message() :
		m_Data{0},
		m_Cursor(0)
	{}
	
	message(const message_header<data_type>& hd, const array_type& msg)
		: header(hd), m_Data(msg)
	{}
	message(const message&) = default;
	~message() {}

	uint32_t size() const
	{
		return m_Cursor;
	}

	void clear()
	{
		std::memset(m_Data.data(), 0, message_length_max);
		m_Cursor = 0;
	}

	template<typename T, std::enable_if_t<std::is_standard_layout_v<T>, int> = 0>
	message& operator<<(const T& data)
	{
		if (m_Cursor + sizeof(T) > message_length_max)
			return *this;

		std::memcpy(m_Data.data() + m_Cursor, &data, sizeof(T));
		m_Cursor += sizeof(T);

		header.length = m_Cursor;
		return *this;
	}

	message& operator<<(const std::string& str)
	{
		if (m_Cursor + str.size() > message_length_max)
			return *this;

		std::memcpy(m_Data.data() + m_Cursor, str.data(), str.size());
		m_Cursor += str.size();

		header.length = m_Cursor;
		return *this;
	}

	template<typename T, std::enable_if_t<std::is_standard_layout_v<T>, int> = 0>
	message& operator>>(T& data)
	{
		if (sizeof(T) > m_Cursor)
			return *this;
		
		m_Cursor -= sizeof(T);
		std::memcpy(&data, m_Data.data() + m_Cursor, sizeof(T));

		header.length = m_Cursor;
		return *this;
	}
};

template<typename data_type>
std::ostream& operator<<(std::ostream& output, const message<data_type>& msg)
{
	output << "message size: " << msg.header.length << std::endl;
	output << "message data: ";
	
	for (auto& ch : msg.m_Data)
		output << ch;

	return output;
}

template<typename data_type>
struct identified_message : public message<data_type>
{
	//Used to send the message back
	asio::ip::tcp::endpoint endpoint;

public:
	identified_message(){}
	identified_message(const identified_message&) = default;
	identified_message(const message<data_type>& msg) { operator=(msg); }

	identified_message& operator=(const message<data_type>& msg)
	{
		this->header = msg.header;
		this->m_Data = msg.m_Data;
		this->m_Cursor = msg.m_Cursor;
		return *this;
	}
};
