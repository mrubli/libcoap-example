#include <print>

#include <coap3/coap.h>


namespace
{

	ssize_t custom_read(coap_session_t *session, uint8_t *data, size_t datalen)
	{
		std::println("custom_read: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
		return 0;
	}

	ssize_t custom_write(coap_session_t *session, const uint8_t *data, size_t datalen)
	{
		std::println("custom_write: session: {}, data: {}, datalen: {}", (void *)session, (void *)data, datalen);
		return 0;
	}

	void custom_establish(coap_session_t *session)
	{
		std::println("custom_establish: session: {}", (void *)session);
	}

	void custom_close(coap_session_t *session)
	{
		std::println("custom_close: session: {}", (void *)session);
	}
}


int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	std::println("Testing ...");

	coap_startup();

	coap_context_t *ctx = coap_new_context(nullptr);

	coap_session_t *session = coap_new_client_session(ctx, NULL, NULL, COAP_PROTO_CUSTOM);
	const auto custom_callbacks = coap_custom_transport_func_t{
		.read = custom_read,
		.write = custom_write,
		.establish = custom_establish,
		.close = custom_close,
	};
	coap_set_custom_transport_func(session, &custom_callbacks);

	coap_pdu_t *pdu = coap_pdu_init(COAP_MESSAGE_CON, COAP_REQUEST_CODE_GET, 0, 16);

	coap_show_pdu(COAP_LOG_WARN, pdu);

	coap_send(session, pdu);

//done:
	coap_session_release(session);
	coap_free_context(ctx);
	coap_cleanup();

	return 0;
}
