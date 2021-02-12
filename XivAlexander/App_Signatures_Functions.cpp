#include "pch.h"
#include "App_Signatures.h"
#include "App_Signatures_Functions.h"

namespace App::Signatures::Functions {

	namespace Socket {
		FunctionSignature<SOCKET, int, int, int> socket("socket::socket", ::socket, 
			[](int af, int type, int protocol) { return socket.Thunked(af, type, protocol); });
		FunctionSignature<int, SOCKET, const sockaddr*, int> connect("socket::connect", ::connect, 
			[](SOCKET s, const sockaddr* name, int namelen) { return connect.Thunked(s, name, namelen); });
		FunctionSignature<int, int, fd_set*, fd_set*, fd_set*, const timeval*> select("socket::select", ::select, 
			[](int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const timeval* timeout) { return select.Thunked(nfds, readfds, writefds, exceptfds, timeout); });
		FunctionSignature<int, SOCKET, char*, int, int> recv("socket::recv", ::recv, 
			[](SOCKET s, char* buf, int len, int flags) { return recv.Thunked(s, buf, len, flags); });
		FunctionSignature<int, SOCKET, const char*, int, int> send("socket::send", ::send, 
			[](SOCKET s, const char* buf, int len, int flags) { return send.Thunked(s, buf, len, flags); });
		FunctionSignature<int, SOCKET> closesocket("socket::closesocket", ::closesocket,
			[](SOCKET s) { return closesocket.Thunked(s); });
	}

	namespace WinApi {
		// The game client's internal debugging code often trips when this function returns true,
		// so we return false instead for the ease of debugging.
		FunctionSignature<BOOL> IsDebuggerPresent("WinApi::IsDebuggerPresent", ::IsDebuggerPresent,
			[]() -> BOOL { return FALSE; }
		);
	}
}
