// clang++ -std=c++17 -Wall -Wextra -O2 -g -pthread iobench.cpp -o iobench -lzmq -lboost_regex

#include "azmq/socket.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cassert>
#include <chrono>
#include <inttypes.h>
#include <signal.h>
#include <stdexcept>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <future>

namespace {

//static constexpr char ADDR[] = "tcp://127.0.0.1:12345";
//static constexpr char ADDR[] = "ipc://iobench_ipc_socket";
static constexpr char ADDR[] = "inproc://iobench_inproc_socket";

struct stats {
	uint64_t m_packets = 0;
	uint64_t m_bytes = 0;
	std::chrono::steady_clock::time_point m_starttime;

	stats() {
		reset();
	}

	void reset(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
		m_packets = 0;
		m_bytes = 0;
		m_starttime = now;
	}

	void add_packet(size_t packet_size) {
		m_packets++;
		m_bytes += packet_size;
	}

	static std::string human_readable_byte_amount(double bytes) {
		uint8_t exponent = 0;
		double value = bytes;
		while (value >= 1024) {
			value /= 1024;
			exponent++;
		}
		const char *suffix;
		switch (exponent) {
		case 1: suffix = " KiB"; break;
		case 2: suffix = " MiB"; break;
		case 3: suffix = " GiB"; break;
		case 4: suffix = " TiB"; break;
		default:
			suffix = " bytes";
			value = bytes;
			break;
		}
		return std::to_string(value) + suffix;
	}

	void print_report(const std::string &prefix, std::chrono::steady_clock::time_point now) const {
		const auto duration = now - m_starttime;
		const auto duration_msec = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
		const double msec = duration_msec.count();
#if 0
		const double packetspersec = ((msec == 0) ? m_packets : (m_packets * 1000 / msec));
		const double bytespersec = ((msec == 0) ? m_bytes : (m_bytes * 1000 / msec));
		printf("%s: %" PRIu64 " packets or %" PRIu64 " bytes in %f msec (%f packets/sec, %f bytes/sec)\n",
			prefix.c_str(), m_packets, m_bytes, msec, packetspersec, bytespersec);
#else
		const uint64_t packetspersec = ((msec == 0) ? m_packets : (m_packets * 1000 / msec));
		const double bytespersec = ((msec == 0) ? m_bytes : (m_bytes * 1000 / msec));
		printf("%s: %" PRIu64 " packets/sec, %s/sec\n",
			prefix.c_str(), packetspersec, human_readable_byte_amount(bytespersec).c_str());
#endif
	}

	void print_report_and_reset(const std::string &prefix) {
		const auto now = std::chrono::steady_clock::now();
		print_report(prefix, now);
		reset(now);
	}
};

class context {
	bool m_is_recv = true;
	boost::asio::io_context m_iocontext;
	boost::asio::signal_set m_signalset;
	azmq::socket m_socket;
	std::vector<uint8_t> m_buffer;
	boost::asio::steady_timer m_timer;
	stats m_partial_stats;
	stats m_total_stats;

	void start_send() {
		assert(!m_is_recv);
		assert(m_buffer.size() >= 8);
		const uint64_t num = m_total_stats.m_packets;
		m_buffer[0] = num & 0xFF;
		m_buffer[1] = (num >> 8) & 0xFF;
		m_buffer[2] = (num >> 16) & 0xFF;
		m_buffer[3] = (num >> 24) & 0xFF;
		m_buffer[4] = (num >> 32) & 0xFF;
		m_buffer[5] = (num >> 40) & 0xFF;
		m_buffer[6] = (num >> 48) & 0xFF;
		m_buffer[7] = (num >> 56) & 0xFF;

		//printf("async_send with m_buffer.size()=%zu\n", m_buffer.size());
		m_socket.async_send(boost::asio::buffer(m_buffer), [this](boost::system::error_code const &ec, size_t bytes_transferred) {
			if (ec) {
				throw std::runtime_error("async_send() failed, " + ec.message());
			}

			if (bytes_transferred != m_buffer.size()) {
				throw std::runtime_error("async_send() failed to send whole buffer, sent " +
					std::to_string(bytes_transferred) + " of " + std::to_string(m_buffer.size()) + " bytes");
			}

			m_partial_stats.add_packet(bytes_transferred);
			m_total_stats.add_packet(bytes_transferred);

			start_send();
		});
	}

	void start_recv() {
		assert(m_is_recv);
		//printf("async_receive with m_buffer.size()=%zu\n", m_buffer.size());
		m_socket.async_receive(boost::asio::buffer(m_buffer.data(), m_buffer.size()), [this](boost::system::error_code const &ec, size_t bytes_transferred) {
			if (ec) {
				throw std::runtime_error("async_receive() failed, " + ec.message());
			}

			if (bytes_transferred != m_buffer.size()) {
				throw std::runtime_error("async_receive() did not fill the whole buffer, received only " +
					std::to_string(bytes_transferred) + " of " + std::to_string(m_buffer.size()) + " bytes");
			}

			m_partial_stats.add_packet(bytes_transferred);
			m_total_stats.add_packet(bytes_transferred);

			start_recv();
		});
	}

	void start_timer() {
		m_timer.expires_from_now(std::chrono::seconds(1));
		m_timer.async_wait([this](boost::system::error_code const &ec) {
			if (!ec) {
				print_partial_stats();
				start_timer();
			}
		});
	}

	void print_partial_stats() {
		m_partial_stats.print_report_and_reset(get_report_prefix());
	}

	std::string get_report_prefix() const {
		return m_is_recv ? "recv" : "send";
	}

public:
	context(bool is_recv, size_t buffersize)
		: m_is_recv(is_recv)
		, m_signalset(m_iocontext, SIGINT, SIGTERM)
		, m_socket(m_iocontext, is_recv ? ZMQ_PULL : ZMQ_PUSH)
		, m_timer(m_iocontext)
	{
		m_buffer.resize(buffersize);
		if (is_recv) {
			m_socket.bind(ADDR);
			start_recv();
		} else {
			m_socket.connect(ADDR);
			start_send();
		}
		start_timer();
		m_signalset.async_wait([this](const boost::system::error_code &ec, int signal_number) {
			if (ec) {
				throw std::runtime_error("signal_set::async_wait() failed, " + ec.message());
			}
			printf("exiting due to signal %i\n", signal_number);
			m_iocontext.stop();
		});
	}

	void run() {
		m_iocontext.run();
	}

	void print_total_stats() {
		m_total_stats.print_report_and_reset(get_report_prefix() + " total");
	}
};

}

int main(int argc, char **argv) {
	bool is_recv = false;
	bool is_both = true;
	try {
		size_t buffersize = 1024 * 1024;

		if (argc < 2) {
			throw std::runtime_error("missing command line argument: send/recv");
		}
		std::string action(argv[1]);
		if (action == "send") {
			is_recv = false;
			is_both = false;
		} else if (action == "recv") {
			is_recv = true;
			is_both = false;
		} else if (action == "both") {
			is_recv = false;
			is_both = true;
		} else {
			throw std::runtime_error("unknown command line option '" + action + "'");
		}

		if (argc >= 3) {
			buffersize = std::stoull(argv[2]);
		}

		if (argc >= 4) {
			throw std::runtime_error("too many command line arguments");
		}

		printf("buffersize=%zu\n", buffersize);
		if (is_both) {
			context receiver(true, buffersize);
			context sender(false, buffersize);

			std::future<void> future = std::async(std::launch::async, [&sender]() {
				sender.run();
			});

			receiver.run();

			future.get();

			receiver.print_total_stats();
			sender.print_total_stats();
		} else {
			context ctx(is_recv, buffersize);
			ctx.run();
			ctx.print_total_stats();
		}
	} catch (const std::exception &e) {
		fprintf(stderr, "%s: error: %s\n", (is_recv ? "recv" : "send"), e.what());
		return 1;
	}
	return 0;
}
