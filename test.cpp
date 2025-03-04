#include <print>

#include <coap3/coap.h>

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	std::println("Testing ...");

	coap_startup();

	coap_cleanup();

	return 0;
}
