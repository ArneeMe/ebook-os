#include "MeshPlay.h"

#include <monocypher.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace mesh::play {

namespace {

constexpr char SEP = '\x1f';  // between a poll's question and its options
constexpr size_t REF_SIZE = 10;  // sender (2 hex) + counter (8 hex)

std::string refText(const Ref r) {
  char s[REF_SIZE + 1];
  snprintf(s, sizeof(s), "%02x%08lx", r.sender, static_cast<unsigned long>(r.counter));
  return s;
}

// `digits` lowercase hex digits at text[at]; false for anything else.
bool hex(const std::string& text, const size_t at, const size_t digits, uint32_t& out) {
  if (text.size() < at + digits) return false;
  out = 0;
  for (size_t i = at; i < at + digits; i++) {
    const char c = text[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0) return false;
    out = out << 4 | uint32_t(v);
  }
  return true;
}

bool readRef(const std::string& text, Ref& out) {
  uint32_t sender = 0;
  if (!hex(text, 2, 2, sender) || !hex(text, 4, 8, out.counter)) return false;
  out.sender = uint8_t(sender);
  return isDevice(out.sender);
}

bool validOption(const std::string& s) { return !s.empty() && s.size() <= MAX_OPTION && s.find(SEP) == std::string::npos; }

}  // namespace

bool isReply(const std::string& text) {
  return text.size() >= 2 && text[0] == MARK && (text[1] == char(Kind::Vote) || text[1] == char(Kind::Move));
}

bool read(const std::string& text, Play& out) {
  if (!isPlay(text) || text.size() < 2) return false;
  out = Play{};
  out.kind = Kind(text[1]);
  switch (out.kind) {
    case Kind::Poll: {
      size_t at = 2;
      bool first = true;
      while (true) {
        const size_t end = text.find(SEP, at);
        const std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (first) {
          out.question = part;
          first = false;
        } else {
          out.options.push_back(part);
        }
        if (end == std::string::npos) break;
        at = end + 1;
      }
      if (out.question.empty() || out.question.size() > MAX_QUESTION) return false;
      if (out.options.size() < 2 || out.options.size() > MAX_OPTIONS) return false;
      for (const auto& o : out.options) {
        if (!validOption(o)) return false;
      }
      return true;
    }
    case Kind::Vote: {
      uint32_t option = 0;
      if (text.size() != 2 + REF_SIZE + 1 || !readRef(text, out.of) || !hex(text, 2 + REF_SIZE, 1, option)) return false;
      out.option = uint8_t(option);
      return out.option < MAX_OPTIONS;
    }
    case Kind::Game:
      out.game = Game(text.size() == 3 ? text[2] : 0);
      return out.game == Game::FourInRow || out.game == Game::Chess || out.game == Game::Ships;
    case Kind::Move: {
      uint32_t number = 0;
      if (!readRef(text, out.of) || !hex(text, 2 + REF_SIZE, 4, number) || number == 0) return false;
      out.number = uint16_t(number);
      out.move = text.substr(2 + REF_SIZE + 4);
      return !out.move.empty();
    }
  }
  return false;
}

std::string writePoll(const std::string& question, const std::vector<std::string>& options) {
  if (question.empty() || question.size() > MAX_QUESTION || question.find(SEP) != std::string::npos) return "";
  if (options.size() < 2 || options.size() > MAX_OPTIONS) return "";
  std::string text{MARK, char(Kind::Poll)};
  text += question;
  for (const auto& o : options) {
    if (!validOption(o)) return "";
    text += SEP;
    text += o;
  }
  return text;
}

std::string writeVote(const Ref poll, const uint8_t option) {
  std::string text{MARK, char(Kind::Vote)};
  text += refText(poll);
  text += "0123456789abcdef"[option & 0xF];
  return text;
}

std::string writeGame(const Game game) { return std::string{MARK, char(Kind::Game), char(game)}; }

std::string writeMove(const Ref game, const uint16_t number, const std::string& move) {
  char n[5];
  snprintf(n, sizeof(n), "%04x", number);
  return std::string{MARK, char(Kind::Move)} + refText(game) + n + move;
}

std::string preview(const std::string& text) {
  Play p;
  if (!read(text, p)) return "(trenger nyere versjon)";
  switch (p.kind) {
    case Kind::Poll:
      return "Avstemning: " + p.question;
    case Kind::Vote:
      return "stemte";
    case Kind::Game:
      return p.game == Game::Chess   ? "utfordrer til sjakk"
             : p.game == Game::Ships ? "utfordrer til senk skipet"
                                     : "utfordrer til fire på rad";
    case Kind::Move:
      return "gjorde et trekk";
  }
  return "";
}

Tally tally(const Ref poll, const std::vector<Entry>& entries) {
  Tally t;
  for (const auto& e : entries) {
    Play p;
    if (!e.text || !read(*e.text, p) || p.kind != Kind::Vote || !(p.of == poll)) continue;
    const Members who = memberBit(person(e.sender));
    for (auto& v : t.votes) v &= ~who;
    t.votes[p.option] |= who;
  }
  return t;
}

// --- Fire på rad ---

bool FourInRow::canDrop(const int col) const {
  return winner == 0 && col >= 0 && col < COLS && cell[ROWS - 1][col] == 0;
}

bool FourInRow::drop(const int col) {
  if (!canDrop(col)) return false;
  int row = 0;
  while (cell[row][col]) row++;
  const int player = turn();
  cell[row][col] = uint8_t(player);
  moves++;
  lastCol = col;
  // Four in a line through the new piece, in any of the four directions.
  static constexpr int DIRS[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
  for (const auto& d : DIRS) {
    int run = 1;
    for (const int sign : {1, -1}) {
      int r = row + sign * d[0], c = col + sign * d[1];
      while (r >= 0 && r < ROWS && c >= 0 && c < COLS && cell[r][c] == player) {
        run++;
        r += sign * d[0];
        c += sign * d[1];
      }
    }
    if (run >= 4) winner = player;
  }
  if (!winner && moves == ROWS * COLS) winner = 3;
  return true;
}

// --- Sjakk ---

namespace {

bool isWhite(const char p) { return p >= 'A' && p <= 'Z'; }
char lower(const char p) { return isWhite(p) ? char(p - 'A' + 'a') : p; }
int fileOf(const int sq) { return sq % 8; }
int rankOf(const int sq) { return sq / 8; }
bool onBoard(const int f, const int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

constexpr int KNIGHT[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int ROOK[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BISHOP[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// Whether a piece of that colour attacks sq.
bool attacked(const char* b, const int sq, const bool byWhite) {
  const int f = fileOf(sq), r = rankOf(sq);
  auto is = [&](const int ff, const int rr, const char piece) {
    if (!onBoard(ff, rr)) return false;
    const char p = b[rr * 8 + ff];
    return p != '.' && isWhite(p) == byWhite && lower(p) == piece;
  };
  const int pawnRank = byWhite ? r - 1 : r + 1;
  if (is(f - 1, pawnRank, 'p') || is(f + 1, pawnRank, 'p')) return true;
  for (const auto& d : KNIGHT) {
    if (is(f + d[0], r + d[1], 'n')) return true;
  }
  for (const auto& d : KING) {
    if (is(f + d[0], r + d[1], 'k')) return true;
  }
  for (int line = 0; line < 2; line++) {
    for (const auto& d : line ? BISHOP : ROOK) {
      for (int ff = f + d[0], rr = r + d[1]; onBoard(ff, rr); ff += d[0], rr += d[1]) {
        const char p = b[rr * 8 + ff];
        if (p == '.') continue;
        const char want = line ? 'b' : 'r';
        if (isWhite(p) == byWhite && (lower(p) == want || lower(p) == 'q')) return true;
        break;
      }
    }
  }
  return false;
}

}  // namespace

Chess::Chess() { memcpy(board, "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr", 64); }

std::string Chess::squareName(const int sq) {
  return std::string{char('a' + fileOf(sq)), char('1' + rankOf(sq))};
}

bool Chess::inCheck(const bool white) const {
  const char king = white ? 'K' : 'k';
  for (int sq = 0; sq < 64; sq++) {
    if (board[sq] == king) return attacked(board, sq, !white);
  }
  return false;
}

std::vector<int> Chess::targets(const int from) const {
  std::vector<int> out;
  if (from < 0 || from >= 64 || winner) return out;
  const char p = board[from];
  const bool white = whiteToMove();
  if (p == '.' || isWhite(p) != white) return out;
  const int f = fileOf(from), r = rankOf(from);
  auto free = [&](const int sq) { return board[sq] == '.'; };
  auto enemy = [&](const int sq) { return board[sq] != '.' && isWhite(board[sq]) != white; };
  std::vector<int> pseudo;
  const char kind = lower(p);
  if (kind == 'p') {
    const int dir = white ? 1 : -1;
    if (onBoard(f, r + dir) && free((r + dir) * 8 + f)) {
      pseudo.push_back((r + dir) * 8 + f);
      const bool home = white ? r == 1 : r == 6;
      if (home && free((r + 2 * dir) * 8 + f)) pseudo.push_back((r + 2 * dir) * 8 + f);
    }
    for (const int df : {-1, 1}) {
      if (!onBoard(f + df, r + dir)) continue;
      const int to = (r + dir) * 8 + f + df;
      if (enemy(to) || to == enPassant) pseudo.push_back(to);
    }
  } else if (kind == 'n' || kind == 'k') {
    for (const auto& d : kind == 'n' ? KNIGHT : KING) {
      if (!onBoard(f + d[0], r + d[1])) continue;
      const int to = (r + d[1]) * 8 + f + d[0];
      if (free(to) || enemy(to)) pseudo.push_back(to);
    }
    // Castling: the king and rook unmoved, the squares between free, and the
    // king not in, through or into check.
    if (kind == 'k' && from == (white ? 4 : 60) && !attacked(board, from, !white)) {
      const int base = white ? 0 : 56;
      const int side = white ? 0 : 2;
      if (castle[side] && free(base + 5) && free(base + 6) && !attacked(board, base + 5, !white)) {
        pseudo.push_back(base + 6);
      }
      if (castle[side + 1] && free(base + 3) && free(base + 2) && free(base + 1) && !attacked(board, base + 3, !white)) {
        pseudo.push_back(base + 2);
      }
    }
  } else {
    for (int line = 0; line < 2; line++) {
      if ((line == 0 && kind == 'b') || (line == 1 && kind == 'r')) continue;
      for (const auto& d : line ? BISHOP : ROOK) {
        for (int ff = f + d[0], rr = r + d[1]; onBoard(ff, rr); ff += d[0], rr += d[1]) {
          const int to = rr * 8 + ff;
          if (free(to)) {
            pseudo.push_back(to);
            continue;
          }
          if (enemy(to)) pseudo.push_back(to);
          break;
        }
      }
    }
  }
  // Only moves that do not leave the own king in check.
  for (const int to : pseudo) {
    Chess after = *this;
    after.board[to] = after.board[from];
    after.board[from] = '.';
    if (kind == 'p' && to == enPassant) after.board[(white ? to - 8 : to + 8)] = '.';
    if (!after.inCheck(white)) out.push_back(to);
  }
  return out;
}

bool Chess::play(const std::string& move) {
  if (move.size() != 4 && move.size() != 5) return false;
  const int from = (move[1] - '1') * 8 + (move[0] - 'a'), to = (move[3] - '1') * 8 + (move[2] - 'a');
  if (move[0] < 'a' || move[0] > 'h' || move[2] < 'a' || move[2] > 'h' || move[1] < '1' || move[1] > '8' ||
      move[3] < '1' || move[3] > '8') {
    return false;
  }
  if (move.size() == 5 && std::string("qrbn").find(move[4]) == std::string::npos) return false;
  const auto legal = targets(from);
  if (std::find(legal.begin(), legal.end(), to) == legal.end()) return false;
  const bool white = whiteToMove();
  const char p = board[from];
  const char kind = lower(p);
  if (kind == 'p' && to == enPassant) board[white ? to - 8 : to + 8] = '.';
  board[to] = p;
  board[from] = '.';
  if (kind == 'p' && (rankOf(to) == 7 || rankOf(to) == 0)) {
    const char promo = move.size() == 5 ? move[4] : 'q';
    board[to] = white ? char(promo - 'a' + 'A') : promo;
  }
  if (kind == 'k' && std::abs(to - from) == 2) {  // castling: the rook too
    const bool kingSide = to > from;
    const int rookFrom = kingSide ? from + 3 : from - 4, rookTo = kingSide ? from + 1 : from - 1;
    board[rookTo] = board[rookFrom];
    board[rookFrom] = '.';
  }
  // A king or rook that moves, or a rook taken, ends that castling.
  for (const int sq : {from, to}) {
    if (sq == 4) castle[0] = castle[1] = false;
    if (sq == 60) castle[2] = castle[3] = false;
    if (sq == 7) castle[0] = false;
    if (sq == 0) castle[1] = false;
    if (sq == 63) castle[2] = false;
    if (sq == 56) castle[3] = false;
  }
  enPassant = kind == 'p' && std::abs(to - from) == 16 ? (from + to) / 2 : -1;
  lastFrom = from;
  lastTo = to;
  moves++;
  // The other side has no legal move: checkmate, or stalemate.
  bool any = false;
  for (int sq = 0; sq < 64 && !any; sq++) any = !targets(sq).empty();
  if (!any) winner = inCheck(!white) ? (white ? 1 : 2) : 3;
  return true;
}

// --- Senk skipet ---

constexpr int Ships::SHIPS[4];

namespace {
int squareOf(const std::string& s, const size_t at) {
  if (s.size() < at + 2 || s[at] < 'a' || s[at] >= 'a' + Ships::SIZE || s[at + 1] < '1' || s[at + 1] >= '1' + Ships::SIZE) {
    return -1;
  }
  return (s[at + 1] - '1') * Ships::SIZE + (s[at] - 'a');
}
}  // namespace

bool Ships::play(const int player, const std::string& move) {
  if (winner || (player != 1 && player != 2) || move.empty()) return false;
  auto& theirs = shots[2 - player];  // the other player's shots, the last one answered now
  const char result = move[0];
  if (moves == 0 ? result != '-' : result != 'h' && result != 'm' && result != 's' && result != 'w') return false;
  if (result == 'w') {
    if (move.size() != 1 || theirs.empty()) return false;
    theirs.back().result = 's';
    winner = 3 - player;
    moves++;
    return true;
  }
  const int square = move.size() == 3 ? squareOf(move, 1) : -1;
  if (square < 0) return false;
  for (const auto& s : shots[player - 1]) {
    if (s.square == square) return false;  // fired there before
  }
  if (moves > 0) {
    if (theirs.empty()) return false;
    theirs.back().result = result;
  }
  shots[player - 1].push_back({square, '?'});
  moves++;
  return true;
}

ShipLayout shipLayout(const uint8_t secret[32], const Ref game) {
  // A random-looking but fixed layout: BLAKE2b of the secret and the game.
  uint8_t input[32 + 5], seed[64];
  memcpy(input, secret, 32);
  input[32] = game.sender;
  memcpy(input + 33, &game.counter, 4);
  crypto_blake2b(seed, sizeof(seed), input, sizeof(input));
  size_t at = 0;
  auto next = [&](const int n) {
    if (at + 2 > sizeof(seed)) {
      crypto_blake2b(seed, sizeof(seed), seed, sizeof(seed));
      at = 0;
    }
    const int v = (seed[at] << 8 | seed[at + 1]) % n;
    at += 2;
    return v;
  };
  ShipLayout out;
  for (int ship = 0; ship < 4; ship++) {
    const int len = Ships::SHIPS[ship];
    for (int tries = 0; tries < 200; tries++) {
      const bool across = next(2);
      const int col = next(Ships::SIZE - (across ? len - 1 : 0)), row = next(Ships::SIZE - (across ? 0 : len - 1));
      bool free = true;
      // Not on or next to another ship.
      for (int i = 0; i < len && free; i++) {
        const int c = col + (across ? i : 0), r = row + (across ? 0 : i);
        for (int dr = -1; dr <= 1; dr++) {
          for (int dc = -1; dc <= 1; dc++) {
            const int rr = r + dr, cc = c + dc;
            if (rr >= 0 && rr < Ships::SIZE && cc >= 0 && cc < Ships::SIZE && out.cell[rr * Ships::SIZE + cc]) free = false;
          }
        }
      }
      if (!free) continue;
      for (int i = 0; i < len; i++) out.cell[(row + (across ? 0 : i)) * Ships::SIZE + col + (across ? i : 0)] = uint8_t(ship + 1);
      break;
    }
  }
  return out;
}

std::string shipsMove(const Ships& ships, const int myPlayer, const ShipLayout& mine, const int shot) {
  const std::string square{char('a' + shot % Ships::SIZE), char('1' + shot / Ships::SIZE)};
  const auto& theirs = ships.shots[2 - myPlayer];
  if (ships.moves == 0 || theirs.empty()) return "-" + square;
  const int hit = mine.cell[theirs.back().square];
  if (!hit) return "m" + square;
  // Sunk when every square of that ship has been fired at; all sunk: I lost.
  bool shipGone = true, allGone = true;
  for (int sq = 0; sq < 64; sq++) {
    if (!mine.cell[sq]) continue;
    bool fired = false;
    for (const auto& s : theirs) fired = fired || s.square == sq;
    if (!fired && mine.cell[sq] == hit) shipGone = false;
    if (!fired) allGone = false;
  }
  if (allGone) return "w";
  return (shipGone ? "s" : "h") + square;
}

int GameState::winner() const {
  if (gaveUp) return gaveUp == 1 ? 2 : 1;
  if (game == Game::Ships) return ships.winner;
  return game == Game::Chess ? chess.winner : board.winner;
}

uint8_t GameState::toMove() const { return turn() == 1 ? opponent : challenger; }

int GameState::playerOf(const uint8_t who) const {
  if (who == 0) return 0;
  if (who == opponent) return 1;
  if (who == challenger) return 2;
  return 0;
}

bool replay(const Ref game, const std::vector<Entry>& entries, GameState& out) {
  out = GameState{};
  bool found = false;
  std::vector<Play> moves;
  std::vector<uint8_t> movers;
  for (const auto& e : entries) {
    Play p;
    if (!e.text || !read(*e.text, p)) continue;
    if (p.kind == Kind::Game && e.sender == game.sender && e.counter == game.counter) {
      found = true;
      out.game = p.game;
      out.challenger = person(e.sender);
    } else if (p.kind == Kind::Move && p.of == game) {
      moves.push_back(p);
      movers.push_back(person(e.sender));
    }
  }
  if (!found) return false;
  // Moves can arrive out of order: take move n + 1 wherever it is, until none
  // is left. The first valid one wins (both of a person's devices may answer).
  // "x" gives up, from either player at any time (numbered as the next move).
  for (bool progress = true; progress && !out.winner();) {
    progress = false;
    for (size_t i = 0; i < moves.size(); i++) {
      if (moves[i].number != out.moves() + 1) continue;
      const uint8_t who = movers[i];
      if (moves[i].move == "x") {
        const int player = out.playerOf(who);
        if (!player) continue;
        out.gaveUp = player;
        progress = true;
        break;
      }
      if (out.moves() == 0) {
        if (who == out.challenger) continue;
      } else if (who != out.toMove()) {
        continue;
      }
      bool ok = false;
      if (out.game == Game::Chess) {
        ok = out.chess.play(moves[i].move);
      } else if (out.game == Game::Ships) {
        ok = out.ships.play(out.moves() == 0 ? 1 : out.playerOf(who), moves[i].move);
      } else {
        ok = moves[i].move.size() == 1 && out.board.drop(moves[i].move[0] - '0');
      }
      if (!ok) continue;
      if (out.moves() == 1) out.opponent = who;
      progress = true;
      break;
    }
  }
  return true;
}

bool canMove(const GameState& s, const uint8_t who) {
  if (s.winner() || !who) return false;
  const uint8_t next = s.toMove();
  return next == who || (next == 0 && s.challenger != who);
}

std::string status(const GameState& s, const uint8_t me, const std::function<std::string(uint8_t)>& nameOf) {
  const int winner = s.winner();
  if (winner == 3) return s.game == Game::Chess ? "Patt: uavgjort" : "Uavgjort";
  if (winner) {
    const uint8_t who = winner == 1 ? s.opponent : s.challenger;
    const char* how = s.gaveUp ? " (den andre ga opp)" : s.game == Game::Chess ? " (sjakkmatt)" : "";
    return (who == me ? std::string("Du vant!") : nameOf(who) + " vant!") + how;
  }
  if (s.game == Game::Chess && s.chess.inCheck(s.chess.whiteToMove())) {
    const uint8_t next = s.toMove();
    return next == me ? "Sjakk! Din tur" : "Sjakk! Venter på " + nameOf(next);
  }
  const uint8_t next = s.toMove();
  if (next == 0) return s.challenger == me ? "Venter på at noen tar utfordringen" : "Trykk for å ta utfordringen";
  return next == me ? "Din tur" : "Venter på " + nameOf(next);
}

}  // namespace mesh::play
