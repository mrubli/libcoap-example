#include <mutex>
#include <numeric>
#include <print>
#include <queue>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#include <coap3/coap.h>
#include <fmt/format.h>
#include <fmt/color.h>
#include <fmt/ranges.h>


// Copied from coap_io_internal.h
#define COAP_SOCKET_CAN_READ     0x0100  /**< non blocking socket can now read without blocking */
#define COAP_SOCKET_CAN_ACCEPT   0x0400  /**< non blocking server socket can now accept without blocking */


// MARK: Print helpers

template<>
struct fmt::formatter<std::vector<uint8_t>>
{
	constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

	auto format(const std::vector<uint8_t>& v, auto& ctx) const
	{
		//return fmt::format_to(ctx.out(), "{{ {:#04x} }}", fmt::join(v, ", "));
		auto it = ctx.out();
		it = fmt::format_to(it, "{{");
		for (auto first = true; const auto byte : v)
		{
			if (!first)
			{
				it = fmt::format_to(it, ",");
			}
			first = false;
			if (std::isprint(byte))
				it = fmt::format_to(it, " '{}'", static_cast<char>(byte));
			else
				it = fmt::format_to(it, " {:#04x}", byte);
		}
		it = fmt::format_to(it, " }}");
		return it;
	}
};

namespace
{

template <typename... Args>
void print_styled(fmt::text_style style, const char *prefix, const char *format, Args&& ...args)
{
	fmt::print(style, "[{}]", prefix);
	fmt::print(" ");
	fmt::println(fmt::runtime(format), std::forward<Args>(args)...);
}

template <typename... Args>
void println_server(const char *format, Args&& ...args)
{
	print_styled(bg(fmt::color::red), "server", format, std::forward<Args>(args)...);
}

template <typename... Args>
void println_client(const char *format, Args&& ...args)
{
	print_styled(bg(fmt::color::green), "client", format, std::forward<Args>(args)...);
}

} // anonymous namespace


namespace
{

constexpr auto UseTcp = false;
constexpr auto UsePayload = true;

auto requestQueue = std::queue<std::vector<uint8_t>>{};
auto requestMutex = std::mutex{};

auto responseQueue = std::queue<std::vector<uint8_t>>{};
auto responseMutex = std::mutex{};

coap_context_t *clientCtx = nullptr;
coap_context_t *serverCtx = nullptr;

const auto LargeData = [] {
	auto data = std::vector<uint8_t>{};
	data.resize(1722);	// 150% of the max PDU size (1148)
	std::iota(data.begin(), data.end(), 0);
	return data;
}();


uint32_t calculateCrc32(std::span<const uint8_t> data)
{
	constexpr uint32_t polynomial = 0xedb88320;
	uint32_t crc = 0xffffffff;
	for (uint8_t byte : data)
	{
		crc ^= byte;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
	}
	return ~crc;
}


// MARK: Client

void custom_client_connect(coap_context_t *ctx)
{
	println_client("connect: ctx: {}", (void *)ctx);

	if constexpr (UseTcp)
	{
		coap_io_custom_have_new_data(serverCtx, COAP_SOCKET_CAN_ACCEPT, 1);
	}
}

void custom_client_close(coap_context_t *ctx)
{
	println_client("close: ctx: {}", (void *)ctx);
}

ssize_t custom_client_read(coap_context_t *ctx, uint8_t *data, size_t datalen)
{
	(void)ctx;
	//println_client("read: ctx: {}, data: {}, datalen: {}", (void *)ctx, (void *)data, datalen);

	const auto locky = std::lock_guard{ responseMutex };
	if (responseQueue.empty())
	{
		println_client("read: no response pending");
		return 0;
	}

	const auto response = responseQueue.front();
	responseQueue.pop();

	if (response.size() <= datalen)
	{
		//println_client("read: returning response of size {}: {}", response.size(), response);
		std::copy(response.begin(), response.end(), data);
		return response.size();
	}
	else
	{
		println_client("ERROR: read: buffer too small");
		return 0;
	}
}

ssize_t custom_client_send(coap_context_t *ctx, const uint8_t *data, size_t datalen)
{
	(void)ctx;
	//println_client("write: ctx: {}, data: {}, datalen: {}", (void *)ctx, (void *)data, datalen);

	const auto locky = std::lock_guard{ requestMutex };
	auto request = std::vector<uint8_t>{ data, data + datalen };
	//println_client("write: queuing request of size {}: {}", datalen, request);
	requestQueue.push(std::move(request));

	if constexpr (!UseTcp)
	{
		coap_io_custom_have_new_data(serverCtx, COAP_SOCKET_CAN_READ, 1);
	}

	return datalen;
}

void run_client()
{
	static bool exit = false;

	println_client("starting up");

	assert(serverCtx != nullptr);	// Server needs to be running already

	clientCtx = coap_new_context(nullptr);

	coap_context_set_block_mode(clientCtx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

	const auto custom_callbacks = coap_io_custom_callbacks_t{
		.connect = custom_client_connect,
		.close   = custom_client_close,
		.read    = custom_client_read,
		.send    = custom_client_send,
		.accept  = nullptr,
	};
	coap_io_custom_set_callbacks(clientCtx, &custom_callbacks);

	coap_register_response_handler(clientCtx,
		[] (auto, auto, const coap_pdu_t *received, auto) {
			size_t len;
			const uint8_t *databuf;
			size_t offset;
			size_t total;

			println_client("received response:");
			std::print("  ");
			coap_show_pdu(COAP_LOG_WARN, received);

			if (coap_get_data_large(received, &len, &databuf, &offset, &total))
			{
				const auto response = std::vector<uint8_t>{ databuf, databuf + len };
				println_client("response: {}", response);
				if (response == std::vector<uint8_t>{ 'w', 'o', 'r', 'l', 'd' })
				{
					println_client("yay, response is correct. exit.");
					exit = true;
				}
			}
			return COAP_RESPONSE_OK;
		}
	);

	coap_address_t dst;
	coap_uri_t uri;
	{
		const char *url = UseTcp
		                ? "coap+tcp://localhost:1234/hello?foo=bar"
		                : "coap://localhost:1234/hello?foo=bar";
		coap_split_uri((const unsigned char *)url, strlen(url), &uri);
		coap_addr_info_t *addr_info = coap_resolve_address_info(&uri.host,
			uri.port, uri.port, uri.port, uri.port,
			AF_UNSPEC, COAP_URI_SCHEME_COAP_BIT, COAP_RESOLVE_TYPE_REMOTE);
		dst = addr_info->addr;
		coap_free_address_info(addr_info);
	}

	coap_session_t *session = coap_new_client_session(clientCtx, NULL, &dst, UseTcp ? COAP_PROTO_TCP : COAP_PROTO_UDP);

	// Create a token associated with the session
	uint8_t token[4] = { 0x42, 0x00, 0x00, 0x00 };
	coap_session_init_token(session, 4, token);

	const auto maxPduSize = coap_session_max_pdu_size(session);
	println_client("max PDU size: {}", maxPduSize);
	coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, 0, maxPduSize);

	// Add a token to the PDU (optional)
	{
		size_t tokenLength = 0;
		coap_session_new_token(session, &tokenLength, token);
		assert(tokenLength == sizeof(token));
	}
	coap_add_token(pdu, sizeof(token), token);

	coap_optlist_t *optlist = nullptr;
#if 1
	coap_uri_into_options(&uri, &dst, &optlist, 0, nullptr, 0);
	assert(1 == coap_add_optlist_pdu(pdu, &optlist));
#else
	if (uri.path.length > 0)
	{
		assert(0 < coap_add_option(pdu, COAP_OPTION_URI_PATH, uri.path.length, uri.path.s));
	}
	if (uri.query.length > 0)
	{
		assert(0 < coap_add_option(pdu, COAP_OPTION_URI_QUERY, uri.query.length, uri.query.s));
	}
#endif
	if (UsePayload)
	{
		println_client("crc of data: {:#010x}", calculateCrc32(LargeData));
		assert(1 == coap_add_data_large_request(session, pdu, LargeData.size(), LargeData.data(), nullptr, nullptr));
	}

	println_client("sending PDU:");
	std::print("  ");
	coap_show_pdu(COAP_LOG_DEBUG, pdu);

	coap_send(session, pdu);

	while (!exit)
	{
		//println_client("coap_io_process");
		coap_io_process(clientCtx, COAP_IO_NO_WAIT);
		std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
	}

	println_client("exiting");
	coap_delete_optlist(optlist);
	coap_session_release(session);
	coap_free_context(clientCtx);
	clientCtx = nullptr;
	coap_cleanup();
}


// MARK: Server

void custom_server_connect(coap_context_t *ctx)
{
	println_server("connect: ctx: {}", (void *)ctx);
}

void custom_server_close(coap_context_t *ctx)
{
	println_server("close: ctx: {}", (void *)ctx);
}

ssize_t custom_server_read(coap_context_t *ctx, uint8_t *data, size_t datalen)
{
	(void)ctx;
	//println_server("read: ctx: {}, data: {}, datalen: {}", (void *)ctx, (void *)data, datalen);

	const auto locky = std::lock_guard{ requestMutex };
	if (requestQueue.empty())
	{
		println_server("read: no request pending");
		return 0;
	}

	const auto request = requestQueue.front();
	requestQueue.pop();

	if (request.size() <= datalen)
	{
		//println_server("read: returning request of size {}: {}", request.size(), request);
		std::copy(request.begin(), request.end(), data);
		return request.size();
	}
	else
	{
		println_server("ERROR: read: buffer too small");
		return 0;
	}
}

ssize_t custom_server_send(coap_context_t *ctx, const uint8_t *data, size_t datalen)
{
	(void)ctx;
	//println_server("send: ctx: {}, data: {}, datalen: {}", (void *)ctx, (void *)data, datalen);

	const auto locky = std::lock_guard{ responseMutex };
	auto response = std::vector<uint8_t>{ data, data + datalen };
	//println_server("send: queuing response of size {}: {}", datalen, response);
	responseQueue.push(std::move(response));
	coap_io_custom_have_new_data(clientCtx, COAP_SOCKET_CAN_READ, 1);

	return datalen;
}

void custom_server_accept(coap_context_t *ctx)
{
	println_server("accept: ctx: {}", (void *)ctx);
}


void run_server(bool& exit)
{
	println_server("starting up");
	serverCtx = coap_new_context(nullptr);

	coap_context_set_block_mode(serverCtx, COAP_BLOCK_USE_LIBCOAP | COAP_BLOCK_SINGLE_BODY);

	const auto custom_callbacks = coap_io_custom_callbacks_t{
		.connect = custom_server_connect,
		.close   = custom_server_close,
		.read    = custom_server_read,
		.send    = custom_server_send,
		.accept  = custom_server_accept,
	};
	coap_io_custom_set_callbacks(serverCtx, &custom_callbacks);

	constexpr auto COAP_LISTEN_UCAST_IP = "::";

	coap_str_const_t *listen_address = coap_make_str_const(COAP_LISTEN_UCAST_IP);

	coap_addr_info_t *addr_info = coap_resolve_address_info(listen_address, 0, 0, 0, 0,
		0, coap_get_available_scheme_hint_bits(0, 0, COAP_PROTO_NONE), COAP_RESOLVE_TYPE_LOCAL);
	assert(addr_info != nullptr);

	coap_endpoint_t *ep = coap_new_endpoint(serverCtx, &addr_info->addr, UseTcp ? COAP_PROTO_TCP : COAP_PROTO_UDP);
	assert(ep != nullptr);
	coap_free_address_info(addr_info);

	coap_resource_t *resource = coap_resource_init(coap_make_str_const("hello"), 0);
	coap_register_request_handler(resource, COAP_REQUEST_GET,
		[] (auto /* resource */, auto /* session */, const coap_pdu_t *request, const coap_string_t *query, coap_pdu_t *response) {
			println_server("received request:");
			std::print("  ");
			coap_show_pdu(COAP_LOG_WARN, request);

			{
				auto queryStr = std::string_view{ reinterpret_cast<const char *>(query->s), query->length };
				println_server("query: {}", queryStr);
			}
			{
				size_t dataSize = 0;
				const uint8_t *data = nullptr;
				coap_get_data(request, &dataSize, &data);
				const auto dataSpan = std::span{ data, dataSize };
				assert(UsePayload != dataSpan.empty());
				if (!dataSpan.empty())
				{
					const auto crc = calculateCrc32(dataSpan);
					println_server("crc of data: {:#010x}", crc);
					if (crc == calculateCrc32(LargeData))
					{
						println_server("crc matches");
					}
					else
					{
						println_server("ERROR: crc mismatch");
					}
					println_server("data: {}", dataSpan);
				}
			}

			coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
			coap_add_data(response, 5, (const uint8_t *)"world");

			println_server("sending response:");
			std::print("  ");
			coap_show_pdu(COAP_LOG_WARN, response);
		}
	);
	coap_add_resource(serverCtx, resource);

	while (!exit)
	{
		//println_server("coap_io_process");
		coap_io_process(serverCtx, COAP_IO_NO_WAIT);
		std::this_thread::sleep_for(std::chrono::milliseconds{ 100 });
	}

	println_server("exiting");

	coap_free_context(serverCtx);
	serverCtx = nullptr;
}

} // anonymous namespace


// MARK: main

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	// Initialize libcoap
	coap_startup();
	coap_set_log_level(COAP_LOG_DEBUG);

	// Start the server and wait for it to be running
	auto exitServer = false;
	auto server_thread = std::thread{ [&] { run_server(exitServer); } };
	std::this_thread::sleep_for(std::chrono::milliseconds{ 100 }); // cheap hack but we want serverCtx to be set

	// Start the client and wait for it to finish
	auto client_thread = std::thread{ run_client };
	client_thread.join();

	// Signal the server to exit and wait for it to finish
	exitServer = true;
	server_thread.join();

	// Clean up libcoap
	coap_cleanup();

	return 0;
}
