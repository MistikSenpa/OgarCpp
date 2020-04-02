#pragma once

#include <chrono>
#include <uwebsockets/App.h>
#include "Router.h"

class Protocol;

using namespace std::chrono;
using std::string;
using std::string_view;

enum CloseCodes : short {
	CLOSE_NORMAL = 1000,
	CLOSE_GOING_AWAY,
	CLOSE_PROTOCOL_ERROR,
	CLOSE_UNSUPPORTED,
	CLOSED_NO_STATUS = 1005,
	CLOSE_ABNORMAL,
	UNSUPPORTED_PAYLOAD,
	POLICY_VIOLATION,
	CLOSE_TOO_LARGE
};

class Connection : public Router {
public:
	unsigned int ipv4;
	uWS::WebSocket<false, true>* socket;
	time_point<steady_clock> lastChatTime = steady_clock::now();
	Protocol* protocol = nullptr;
	bool socketDisconnected = false;
	int closeCode = 0;
	string closeReason = "";
	// Minions
	bool minionsFrozen = false;
	bool controllingMinions = false;

	Connection(Listener* listener, unsigned int ipv4, uWS::WebSocket<false, true>* socket) :
		Router(listener), ipv4(ipv4), socket(socket) {};
	bool isExternal() override { return true; };
	void close();
	void onSocketClose(int code, string_view reason);
	void onSocketMessage(string_view buffer);
	void createPlayer() override;
	void onChatMessage(string_view message);
	void onQPress();
	bool shouldClose() { return socketDisconnected; };
	void update();
	void onWorldSet();
	void onNewOwnedCell(PlayerCell*);
	void onWorldReset();
	void send(string_view data);
	void closeSocket(int code, string_view reason);
};