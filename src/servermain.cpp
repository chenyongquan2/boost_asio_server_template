
#include "asio/io_context.hpp"
#include "server.h"
#include "stdafx.h"

#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <asio.hpp>
#include "server.h"

asio::awaitable<void> LegacyCompletionHandler(asio::io_context& io)
{
    SPDLOG_INFO("Begin LegacyCompletionHandler");
    co_await asio::post(io, asio::use_awaitable);
    SPDLOG_INFO("End LegacyCompletionHandler");
}

int main()
{
    // asio::io_context io;
    // //io.post(LegacyCompletionHandler &&handler)
    // co_spawn(io, LegacyCompletionHandler(io), asio::detached);
    // SPDLOG_INFO("Begin io.run()");
    // io.run();
    // SPDLOG_INFO("End io.run()");
    try
    {
        size_t port = 8080;
        asio::io_context io_context;
        Server server(io_context, port);
        io_context.run();
        SPDLOG_INFO("Process Exiting");
    }
    catch(std::exception& e)
    {
        SPDLOG_ERROR("Exception: {}", e.what());
    }
    

    return 0;    
}