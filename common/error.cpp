#include "common/error.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#endif

std::errc errc_from_socket_error(int err) {
#ifndef _WIN32
    return std::errc(err);
#else
    std::errc errc;
    switch (err) {
    // WSA common error codes
    case WSA_INVALID_HANDLE:
        errc = std::errc::bad_file_descriptor;
        break;
    case WSA_NOT_ENOUGH_MEMORY:
        errc = std::errc::not_enough_memory;
        break;
    case WSA_INVALID_PARAMETER:
        errc = std::errc::invalid_argument;
        break;

    // WSA "errno-like" error codes
    case WSAEINTR:
        errc = std::errc::interrupted;
        break;
    case WSAEBADF:
        errc = std::errc::bad_file_descriptor;
        break;
    case WSAEACCES:
        errc = std::errc::permission_denied;
        break;
    case WSAEFAULT:
        errc = std::errc::bad_address;
        break;
    case WSAEINVAL:
        errc = std::errc::invalid_argument;
        break;
    case WSAEMFILE:
        errc = std::errc::too_many_files_open;
        break;
    case WSAEWOULDBLOCK:
        errc = std::errc::operation_would_block;
        break;
    case WSAEINPROGRESS:
        errc = std::errc::operation_in_progress;
        break;
    case WSAEALREADY:
        errc = std::errc::connection_already_in_progress;
        break;
    case WSAENOTSOCK:
        errc = std::errc::not_a_socket;
        break;
    case WSAEDESTADDRREQ:
        errc = std::errc::destination_address_required;
        break;
    case WSAEMSGSIZE:
        errc = std::errc::message_size;
        break;
    case WSAEPROTOTYPE:
        errc = std::errc::wrong_protocol_type;
        break;
    case WSAENOPROTOOPT:
        errc = std::errc::no_protocol_option;
        break;
    case WSAEPROTONOSUPPORT:
        errc = std::errc::protocol_not_supported;
        break;
    case WSAESOCKTNOSUPPORT:
        errc = std::errc::not_supported;
        break;
    case WSAEOPNOTSUPP:
        errc = std::errc::operation_not_supported;
        break;
    case WSAEPFNOSUPPORT:
        errc = std::errc::address_family_not_supported;
        break;
    case WSAEAFNOSUPPORT:
        errc = std::errc::address_family_not_supported;
        break;
    case WSAEADDRINUSE:
        errc = std::errc::address_in_use;
        break;
    case WSAEADDRNOTAVAIL:
        errc = std::errc::address_not_available;
        break;
    case WSAENETDOWN:
        errc = std::errc::network_down;
        break;
    case WSAENETUNREACH:
        errc = std::errc::network_unreachable;
        break;
    case WSAENETRESET:
        errc = std::errc::network_reset;
        break;
    case WSAECONNABORTED:
        errc = std::errc::connection_aborted;
        break;
    case WSAECONNRESET:
        errc = std::errc::connection_reset;
        break;
    case WSAENOBUFS:
        errc = std::errc::no_buffer_space;
        break;
    case WSAEISCONN:
        errc = std::errc::already_connected;
        break;
    case WSAENOTCONN:
        errc = std::errc::not_connected;
        break;
    case WSAETOOMANYREFS:
        errc = std::errc::bad_message;
        break;
    case WSAETIMEDOUT:
        errc = std::errc::timed_out;
        break;
    case WSAECONNREFUSED:
        errc = std::errc::connection_refused;
        break;

    // Filename issues (Why they are in Winsock?)
    case WSAELOOP:
        errc = std::errc::too_many_symbolic_link_levels;
        break;
    case WSAENAMETOOLONG:
        errc = std::errc::filename_too_long;
        break;
    case WSAENOTEMPTY:
        errc = std::errc::directory_not_empty;
        break;

    // Host unreachable
    case WSAEHOSTDOWN:
        errc = std::errc::host_unreachable;
        break;
    case WSAEHOSTUNREACH:
        errc = std::errc::host_unreachable;
        break;

    // Some system errors
    case WSAEPROCLIM:
        errc = std::errc::too_many_files_open_in_system;
        break;
    case WSAEUSERS:
        errc = std::errc::too_many_files_open_in_system;
        break;
    case WSAEDQUOT:
        errc = std::errc::too_many_files_open_in_system;
        break;
    case WSAESTALE:
        errc = std::errc::bad_file_descriptor;
        break;
    case WSAEREMOTE:
        errc = std::errc::bad_address;
        break;

    // Unices raise "broken pipe" when operating on shut down socket
    // Locally shut down
    case WSAESHUTDOWN:
        errc = std::errc::broken_pipe;
        break;
    // Remotely shut down
    case WSAEDISCON:
        errc = std::errc::broken_pipe;
        break;

    // WSAStartup issues
    case WSASYSNOTREADY:
        errc = std::errc::state_not_recoverable;
        break;
    case WSAVERNOTSUPPORTED:
        errc = std::errc::not_supported;
        break;
    case WSANOTINITIALISED:
        errc = std::errc::inappropriate_io_control_operation;
        break;
    }
    return errc;
#endif
}

std::errc ag::errc_from_errno(int err) {
    return std::errc(err);
}
