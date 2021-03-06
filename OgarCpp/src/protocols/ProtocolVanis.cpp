#include "ProtocolVanis.h"

#include "../primitives/Reader.h"
#include "../primitives/Writer.h"
#include "../sockets/Connection.h"
#include "../sockets/Listener.h"
#include "../worlds/Player.h"
#include "../cells/Cell.h"
#include "../ServerHandle.h"

const static char PONG_CHAR = 3;
const static string_view PONG = string_view(&PONG_CHAR, 1);

void ProtocolVanis::onSocketMessage(Reader& reader) {
	if (!reader.length()) return;

	unsigned char opCode = reader.readUInt8();
	switch (opCode) {
		// join
		case 1:
			connection->spawningName = reader.readStringUTF8();
			connection->spawningSkin = reader.readStringUTF8();
			connection->spawningTag  = reader.readStringUTF8();
			connection->requestSpawning = true;
			break;
		// spectate
		case 2:
			connection->requestingSpectate = true;
			if (reader.length() == 3) 
				connection->spectatePID = reader.readInt16();
			break;
		// ping
		case 3:
			send(PONG, true);
			break;
		// toggle linelock
		case 15:
			connection->linelocked = !connection->linelocked;
			break;
		// mouse
		case 16:
			connection->mouseX = reader.readInt32();
			connection->mouseY = reader.readInt32();
			break;
		// split
		case 17:
			connection->splitAttempts = reader.readUInt8();
			break;
		// feed
		case 21:
			if (reader.length() == 1) {
				connection->ejectAttempts++;
			} else {
				connection->ejectAttempts = 0;
				unsigned char macro = reader.readUInt8();
				// if (connection->ejectMacro != macro) printf("Toggling macro: %i\n", (int) macro);
				connection->ejectMacro = macro > 0;
			}
			break;
		// chat
		case 99:
			connection->onChatMessage(reader.buffer());
			break;
	}
};

void ProtocolVanis::onChatMessage(ChatSource& source, string_view message) {
	Writer writer;
	writer.writeUInt8(0xd);
	writer.writeUInt16(source.pid);
	writer.writeBuffer(message);
	writer.writeUInt16(0);
	send(writer.finalize());
};

void ProtocolVanis::onPlayerSpawned(Player* player) {
	if (player == connection->player) {

		player->lastVisibleCellData.clear();
		player->lastVisibleCells.clear();
		player->visibleCellData.clear();
		player->visibleCells.clear();

		Writer writer;
		writer.writeUInt8(0x12);
		send(writer.finalize());
	}
	Writer writer;
	writer.writeUInt8(15);
	writer.writeUInt16(player->id);
	writer.writeStringUTF8(player->chatName.c_str());
	writer.writeStringUTF8(player->cellSkin.c_str());
	send(writer.finalize());
};

void ProtocolVanis::onNewOwnedCell(PlayerCell* cell) {};

void ProtocolVanis::onNewWorldBounds(Rect* border, bool includeServerInfo) {
	Writer writer;
	writer.writeUInt8(1);
	writer.writeUInt8(2);
	writer.writeUInt8(connection->listener->handle->gamemode->getType());
	writer.writeUInt16(42069);
	writer.writeUInt16(connection->player->id);
	writer.writeUInt32(border->w * 2);
	writer.writeUInt32(border->h * 2);
	send(writer.finalize());
	if (!connection->hasPlayer || !connection->player->hasWorld) return;

	for (auto player : connection->player->world->players)
		onPlayerSpawned(player);
};

void ProtocolVanis::onWorldReset() {
};

void ProtocolVanis::onDead() {
	Writer writer;
	writer.writeUInt8(0x14);
	writer.writeUInt16((connection->listener->handle->tick -
		connection->player->joinTick) * connection->listener->handle->tickDelay / 1000);
	writer.writeUInt16(connection->player->killCount);
	writer.writeUInt32(connection->player->maxScore);
	send(writer.finalize());
}

void ProtocolVanis::onLeaderboardUpdate(LBType type, vector<LBEntry*>& entries, LBEntry* selfEntry) {
	if (type == LBType::FFA) {
		Writer writer;
		writer.writeUInt8(0xb);
		unsigned char count = 0;
		for (auto entry : entries) {
			count++;
			if (count > 10) break;
			writer.writeUInt16(((FFAEntry*)entry)->pid);
		}
		writer.writeUInt16(0);
		send(writer.finalize());
	}
};

void ProtocolVanis::onSpectatePosition(ViewArea* area) {
	Writer writer;
	writer.writeUInt8(0x11);
	writer.writeInt32(area->getX());
	writer.writeInt32(area->getY());
	send(writer.finalize());
};

void ProtocolVanis::onMinimapUpdate() {
	auto world = connection->player->world;
	if (!world) return;

	Writer writer;
	writer.writeUInt8(0xc);
	for (auto player : world->players) {
		if (player->state != PlayerState::ALIVE) continue;
		writer.writeUInt16(player->id);
		float x = 128 * (world->border.w + player->viewArea.getX() - world->border.getX()) / world->border.w;
		x = x < 0 ? 0 : x;
		x = x > 255 ? 255 : x;
		float y = 128 * (world->border.h + player->viewArea.getY() - world->border.getY()) / world->border.h;
		y = y < 0 ? 0 : y;
		y = y > 255 ? 255 : y;
		writer.writeUInt8(x);
		writer.writeUInt8(y);
	}
	if (writer.offset() > 1) {
		writer.writeUInt16(0);
		send(writer.finalize());
	}
};

void writeAddOrUpdate(Writer& writer, vector<Cell*>& cells) {
	for (auto cell : cells) {
		unsigned char type = cell->getType();
		switch (type) {
			case PLAYER:
				type = cell->owner ? 1 : 5;
				break;
			case VIRUS:
				type = 2;
				break;
			case EJECTED_CELL:
				type = 3;
				break;
			case PELLET:
				type = 4;
				break;
		}
		writer.writeUInt8(type);
		if (type == 1)
			writer.writeUInt16(cell->owner->id);
		writer.writeUInt32(cell->id);
		int x = static_cast<int>(cell->getX());
		int y = static_cast<int>(cell->getY());
		short size = static_cast<short>(cell->getSize());
		x = (x >> 2) << 2;
		y = (y >> 2) << 2;
		size = (size >> 2) << 2;
		writer.writeInt32(x);
		writer.writeInt32(y);
		writer.writeInt16(size);
	}
}

void ProtocolVanis::onVisibleCellUpdate(vector<Cell*>& add, vector<Cell*>& upd, vector<Cell*>& eat, vector<Cell*>& del) {
	Writer writer;
	writer.writeUInt8(10);
	writeAddOrUpdate(writer, add);
	writeAddOrUpdate(writer, upd);
	writer.writeUInt8(0);
	for (auto cell : del)
		writer.writeUInt32(cell->id);
	writer.writeUInt32(0);
	for (auto cell : eat) {
		writer.writeUInt32(cell->id);
		writer.writeUInt32(cell->eatenBy->id);
	}
	writer.writeUInt32(0);
	send(writer.finalize());

	auto player = connection->player;
	if (!player) return;
	for (auto router : player->router->spectators) {

		if (router->type == RouterType::PLAYER
			&& router->hasPlayer
			&& router->player->state == PlayerState::SPEC
			&& router->spectateTarget == player->router)
			((Connection*) router)->protocol->send(writer.finalize());
		// printf("Sending buffer from player#%u to player#%u\n", player->id, router->player->id);
	}
};

void ProtocolVanis::onVisibleCellThreadedUpdate() {
	/*
	auto player = connection->player;
	if (!player) return;
	Writer writer;
	writer.writeUInt8(10);
	// add and upd
	for (auto [id, data] : player->visibleCellData) {
		if (player->lastVisibleCellData.find(id) == player->lastVisibleCellData.cend()) {
			unsigned char type = data->type;
			switch (type) {
				case PLAYER:
					type = data->dead ? 5 : 1;
					break;
				case VIRUS:
					type = 2;
					break;
				case EJECTED_CELL:
					type = 3;
					break;
				case PELLET:
					type = 4;
					break;
			}
			writer.writeUInt8(type);
			if (type == 1)
				writer.writeUInt16(data->pid);
			writer.writeUInt32(data->id);
			writer.writeInt32(data->getX());
			writer.writeInt32(data->getY());
			writer.writeInt16(data->size);
		} else if (data->type != CellType::PELLET) {
			unsigned char type = data->type;
			switch (type) {
				case PLAYER:
					type = data->dead ? 5 : 1;
					break;
				case VIRUS:
					type = 2;
					break;
				case EJECTED_CELL:
					type = 3;
					break;
			}
			writer.writeUInt8(type);
			if (type == 1)
				writer.writeUInt16(data->pid);
			writer.writeUInt32(data->id);
			writer.writeInt32(data->getX());
			writer.writeInt32(data->getY());
			writer.writeInt16(data->size);
		}
	}
	writer.writeUInt8(0);
	for (auto [id, cell] : player->lastVisibleCellData)
		if (!cell->eatenById && player->visibleCellData.find(id) == player->lastVisibleCellData.cend())
			writer.writeUInt32(id);
	writer.writeUInt32(0);
	for (auto [id, cell] : player->lastVisibleCellData)
		if (cell->eatenById && player->visibleCellData.find(id) == player->lastVisibleCellData.cend()) {
			writer.writeUInt32(id);
			writer.writeUInt32(cell->eatenById);
		}
	writer.writeUInt32(0);

	auto buffer = writer.finalize();
	send(buffer);

	for (auto router : player->router->spectators) {

		if (router->type != RouterType::PLAYER 
			|| !router->hasPlayer
			|| router->player->state != PlayerState::SPEC 
			|| router->spectateTarget != player->router) continue;
		printf("Sending buffer from player#%u to player#%u\n", player->id, router->player->id);
		((Connection*)router)->send(buffer);
	} */
}

void ProtocolVanis::onStatsRequest() {
};

void ProtocolVanis::onTimingMatrix() {
	Writer writer;
	writer.writeUInt8(0x20);
	writer.writeBuffer(string_view((char*) &connection->listener->handle->timing, sizeof(TimingMatrix)));
	send(writer.finalize());
}