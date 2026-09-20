/*
 * NetworkDefines.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NetworkInterface.h"

VCMI_LIB_NAMESPACE_BEGIN
using NetworkSocket = boost::asio::ip::tcp::socket;
using NetworkAcceptor = boost::asio::ip::tcp::acceptor;
using NetworkBuffer = boost::asio::streambuf;
using NetworkTimer = boost::asio::steady_timer;

/// Returns a stable ASCII-only description instead of the operating system's
/// localized message, which may not match the console code page on Windows.
inline std::string networkErrorMessage(const boost::system::error_code & code)
{
	if(code == boost::asio::error::eof)
		return "End of file";
	if(code == boost::asio::error::operation_aborted)
		return "Operation canceled";
	if(code == boost::asio::error::connection_reset)
		return "Connection reset by peer";
	if(code == boost::asio::error::connection_aborted)
		return "Connection aborted";
	if(code == boost::asio::error::connection_refused)
		return "Connection refused";
	if(code == boost::asio::error::broken_pipe)
		return "Broken pipe";
	if(code == boost::asio::error::timed_out)
		return "Connection timed out";
	if(code == boost::asio::error::host_unreachable)
		return "Host unreachable";
	if(code == boost::asio::error::network_unreachable)
		return "Network unreachable";
	return "Network error (category=" + std::string(code.category().name()) + ", code=" + std::to_string(code.value()) + ")";
}

VCMI_LIB_NAMESPACE_END
