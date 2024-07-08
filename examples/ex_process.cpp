#include <iostream>
#include <boost/asio.hpp>
#include <boost/process.hpp>

namespace bp   = boost::process;
namespace asio = boost::asio;

void async_read_handler(const boost::system::error_code& ec,
                        std::size_t bytes_transferred, asio::streambuf& buffer) {
    if (!ec) {
        std::istream is(&buffer);
        std::string line;
        std::getline(is, line);
        std::cout << "Async read: " << line << std::endl;
    } else {
        std::cerr << "Async read error: " << ec.message() << std::endl;
    }
}

int main() {
    try {
        asio::io_context io_context;
        asio::streambuf buffer;

        // Launch a child process
        bp::async_pipe pipe(io_context);
        bp::child c("echo Hello Boost", bp::std_out > pipe);

        // Asynchronously read from the child process output
        asio::async_read_until(pipe, buffer, '\n',
                               std::bind(async_read_handler, std::placeholders::_1,
                                         std::placeholders::_2, std::ref(buffer)));

        // Run the io_context to start asynchronous operations
        io_context.run();

        // Wait for the child process to finish
        c.wait();
        int exit_code = c.exit_code();
        std::cout << "Child process exited with code " << exit_code << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
