#include <print>
#include <thread>

#include <coap3/coap.h>


namespace
{

ssize_t custom_client_read(coap_session_t *session, uint8_t *data, size_t datalen)
{
	std::println("[client] read: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
	return 0;
}

ssize_t custom_client_write(coap_session_t *session, const uint8_t *data, size_t datalen)
{
	std::println("[client] write: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
	return datalen;
}

void custom_client_establish(coap_session_t *session)
{
	std::println("[client] establish: session: {}", (void *)session);
}

void custom_client_close(coap_session_t *session)
{
	std::println("[client] close: session: {}", (void *)session);
}


ssize_t custom_server_read(coap_session_t *session, uint8_t *data, size_t datalen)
{
	std::println("[server] read: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
	return 0;
}

ssize_t custom_server_write(coap_session_t *session, const uint8_t *data, size_t datalen)
{
	std::println("[server] write: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
	return datalen;
}

void custom_server_establish(coap_session_t *session)
{
	std::println("[server] establish: session: {}", (void *)session);
}

void custom_server_close(coap_session_t *session)
{
	std::println("[server] close: session: {}", (void *)session);
}


int run_client()
{
	std::println("[client] starting up");

	coap_context_t *ctx = coap_new_context(nullptr);

	const auto custom_callbacks = coap_custom_transport_func_t{
		.read =      custom_client_read,
		.write =     custom_client_write,
		.establish = custom_client_establish,
		.close =     custom_client_close,
	};
	coap_set_custom_transport_func(ctx, &custom_callbacks);

	coap_session_t *session = coap_new_client_session(ctx, NULL, NULL, COAP_PROTO_CUSTOM);

	coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, 0, 16);

	coap_show_pdu(COAP_LOG_DEBUG, pdu);

	coap_send(session, pdu);

	while (true)
	{
		std::println("[client] coap_io_process");
		coap_io_process(ctx, COAP_IO_WAIT);
	}

//done:
	coap_session_release(session);
	coap_free_context(ctx);
	coap_cleanup();
}

int run_server()
{
	std::println("[server] starting up");

	coap_context_t *ctx = coap_new_context(nullptr);

	const auto custom_callbacks = coap_custom_transport_func_t{
		.read =      custom_server_read,
		.write =     custom_server_write,
		.establish = custom_server_establish,
		.close =     custom_server_close,
	};
	coap_set_custom_transport_func(ctx, &custom_callbacks);

	coap_endpoint_t *ep = coap_new_endpoint(ctx, nullptr, COAP_PROTO_CUSTOM);

	coap_resource_t *resource = coap_resource_init(coap_make_str_const("hello"), 0);
	coap_register_request_handler(resource, COAP_REQUEST_GET,
		[] (auto, auto, const coap_pdu_t *request, auto, coap_pdu_t *response) {
			coap_show_pdu(COAP_LOG_WARN, request);
			coap_pdu_set_code(response, COAP_RESPONSE_CODE_CONTENT);
			coap_add_data(response, 5, (const uint8_t *)"world");
			coap_show_pdu(COAP_LOG_WARN, response);
		})
	;
	coap_add_resource(ctx, resource);

	while (true)
	{
		std::println("[server] coap_io_process");
		coap_io_process(ctx, COAP_IO_WAIT);
	}
}

} // anonymous namespace


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	coap_startup();
	coap_set_log_level(COAP_LOG_DEBUG);

	auto server_thread = std::thread{ run_server };
	std::this_thread::sleep_for(std::chrono::seconds{ 1 });
	auto client_thread = std::thread{ run_client };

	client_thread.join();
	server_thread.join();

	return 0;
}
