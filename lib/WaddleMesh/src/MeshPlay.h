#pragma once

// Polls and games in a conversation (protocol/PROTOCOL.md, "Polls and games").
// They travel as ordinary messages whose text starts with byte 1, so keys,
// acknowledgements, syncs and the phone work as for any other message. A poll
// or a challenge is the message itself; a vote or a move points at it by its
// sender and counter. What a poll counts and where a game stands come from
// replaying the conversation's play messages, so nothing else has to be synced.
// Plain C++, like MeshPacket.h, so the host tests build it too.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "MeshPacket.h"

namespace mesh::play {

constexpr char MARK = '\x01';
constexpr size_t MAX_OPTIONS = 4;
constexpr size_t MAX_QUESTION = 200;
constexpr size_t MAX_OPTION = 60;

enum class Kind : char { Poll = 'A', Vote = 'a', Game = 'S', Move = 'm' };
enum class Game : char { FourInRow = 'F', Chess = 'C', Ships = 'B' };

// A poll or a game: the device that sent it and its counter.
struct Ref {
  uint8_t sender = 0;
  uint32_t counter = 0;
  bool operator==(const Ref& o) const { return sender == o.sender && counter == o.counter; }
};

struct Play {
  Kind kind = Kind::Poll;
  std::string question;              // Poll
  std::vector<std::string> options;  // Poll: 2 to MAX_OPTIONS
  Game game = Game::FourInRow;       // Game
  Ref of;                            // Vote, Move: the poll or game
  uint8_t option = 0;                // Vote: index into the poll's options
  uint16_t number = 0;               // Move: 1 for the first move
  std::string move;                  // Move: "3" (Fire på rad: column), "e2e4" (chess), "x" (gives up)
};

inline bool isPlay(const std::string& text) { return !text.empty() && text[0] == MARK; }
// A vote or a move: shown on its poll or game, not as a message of its own.
bool isReply(const std::string& text);
// False for anything that is not a whole play message of a known kind (a game
// from a newer version, for one).
bool read(const std::string& text, Play& out);

// Empty when the poll does not fit the rules above.
std::string writePoll(const std::string& question, const std::vector<std::string>& options);
std::string writeVote(Ref poll, uint8_t option);
std::string writeGame(Game game);
std::string writeMove(Ref game, uint16_t number, const std::string& move);

// What the conversation list shows for a play message.
std::string preview(const std::string& text);

// A play message as the conversation has it, oldest first.
struct Entry {
  uint8_t sender = 0;  // device
  uint32_t counter = 0;
  const std::string* text = nullptr;
};

// Each person's newest vote counts (their unit and phone are one person).
struct Tally {
  Members votes[MAX_OPTIONS] = {};
};
Tally tally(Ref poll, const std::vector<Entry>& entries);

// Fire på rad: 7 columns, 6 rows, four in a line wins.
struct FourInRow {
  static constexpr int COLS = 7;
  static constexpr int ROWS = 6;
  uint8_t cell[ROWS][COLS] = {};  // row 0 is the bottom; 0 empty, else the player (1 or 2)
  int moves = 0;
  int lastCol = -1;
  int winner = 0;  // 0 playing, 1 or 2, 3 a draw

  // The player whose move it is: 1 makes the odd moves.
  int turn() const { return moves % 2 + 1; }
  bool canDrop(int col) const;
  bool drop(int col);
};

// Sjakk: the usual rules, moves as "e2e4" ("e7e8q" to promote; a pawn that
// reaches the end without a letter becomes a queen). White is player 1.
// No draw by repetition or the 50-move rule: either can give up.
struct Chess {
  // Square = rank * 8 + file, a1 = 0, h8 = 63. "PNBRQK" white, "pnbrqk" black, '.' empty.
  char board[64];
  bool castle[4] = {true, true, true, true};  // white king side, white queen side, black ...
  int enPassant = -1;  // the square a pawn skipped last move
  int moves = 0;
  int winner = 0;      // 0 playing, 1 white, 2 black, 3 stalemate
  int lastFrom = -1, lastTo = -1;

  Chess();
  bool whiteToMove() const { return moves % 2 == 0; }
  // The squares the piece on from may go to, by the rules (own king not left in check).
  std::vector<int> targets(int from) const;
  bool inCheck(bool white) const;
  // "e2e4" style; false when it is not a legal move now.
  bool play(const std::string& move);

  static std::string squareName(int square);  // 12 -> "e2"
};

// Senk skipet: 8 × 8, ships of 4, 3, 2 and 2. Each player's ships come from a
// secret both of their devices have (shipLayout), so they never leave them.
// A move answers the other's last shot and fires one: "h" hit, "m" miss,
// "s" sunk, then the square ("-d4" for the first move, with nothing to
// answer); "w" alone: that was my last ship, I lost. Results cannot be checked
// by the shooter: friends do not cheat.
struct Ships {
  static constexpr int SIZE = 8;
  static constexpr int SHIPS[4] = {4, 3, 2, 2};
  struct Shot {
    int square = 0;   // row * 8 + column, a1 = 0
    char result = '?';  // 'h', 'm', 's', or '?' before the answer comes
  };
  std::vector<Shot> shots[2];  // by player 1 and 2
  int moves = 0;
  int winner = 0;

  // A move by `player` (1 or 2); false when it does not fit.
  bool play(int player, const std::string& move);
};

// Where my ships are in this game: ship number (1-4) per square, 0 for water.
struct ShipLayout {
  uint8_t cell[64] = {};
};
ShipLayout shipLayout(const uint8_t secret[32], Ref game);
// My next move: the answer to the other's last shot (from my layout), then mine.
std::string shipsMove(const Ships& ships, int myPlayer, const ShipLayout& mine, int shot);

// A game as the moves so far leave it. The one who takes the challenge makes
// the first move and is player 1 (white in chess); the challenger is player 2.
// In a group, whoever moves first takes it.
struct GameState {
  Game game = Game::FourInRow;
  uint8_t challenger = 0;  // person
  uint8_t opponent = 0;    // person; 0 until someone has moved
  FourInRow board;
  Chess chess;
  Ships ships;
  int gaveUp = 0;  // the player who gave up, 0 for none

  int moves() const { return game == Game::Chess ? chess.moves : game == Game::Ships ? ships.moves : board.moves; }
  // 0 playing, 1 or 2 won, 3 a draw.
  int winner() const;
  int turn() const { return moves() % 2 + 1; }

  // The person whose move it is, or 0 when anyone but the challenger may take it.
  uint8_t toMove() const;
  // Player 1 or 2 for a person, 0 for someone outside the game.
  int playerOf(uint8_t person) const;
};
// False when `game` is not a challenge in entries.
bool replay(Ref game, const std::vector<Entry>& entries, GameState& out);
// Whether `who` (a person) may make the next move.
bool canMove(const GameState& state, uint8_t who);
// "Din tur", "Venter på Kari", "Kari vant!" ... as `me` sees it.
std::string status(const GameState& state, uint8_t me, const std::function<std::string(uint8_t)>& nameOf);

}  // namespace mesh::play
