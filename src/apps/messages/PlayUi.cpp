#include "PlayUi.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "MeshUi.h"
#include "MessagesActivity.h"
#include "core/MappedInputManager.h"
#include "core/fontIds.h"
#include "services/mesh/MeshConfig.h"
#include "services/mesh/MeshNode.h"

using mesh::play::Entry;
using mesh::play::GameState;
using mesh::play::Kind;
using mesh::play::Play;
using mesh::play::Ref;

namespace {

constexpr int PAD = 16;
constexpr int OPTION_H = 64;
constexpr int OPTION_GAP = 8;
constexpr int MINI = 22;  // a cell of the board on the chat card

mesh::Node& node() { return mesh::Node::get(); }
uint8_t me() { return node().network().me; }

std::string nameOf(const uint8_t person) { return person == me() ? "Du" : node().network().name(person); }

// "Kari og Ola", "Kari, Ola og Per".
std::string names(const mesh::Members who) {
  std::vector<std::string> list;
  for (uint8_t n = 1; n <= mesh::MAX_MEMBERS; n++) {
    if (who & mesh::memberBit(n)) list.push_back(nameOf(n));
  }
  std::string out;
  for (size_t i = 0; i < list.size(); i++) {
    if (i) out += i + 1 == list.size() ? " og " : ", ";
    out += list[i];
  }
  return out;
}

// A piece: player 1 is black, player 2 a ring.
void drawPiece(const GfxRenderer& renderer, const int x, const int y, const int size, const int player) {
  if (player == 1) {
    renderer.fillRoundedRect(x, y, size, size, size / 2, Color::Black);
  } else if (player == 2) {
    renderer.drawRoundedRect(x, y, size, size, std::max(2, size / 7), size / 2, true);
  } else {
    renderer.drawRoundedRect(x, y, size, size, 1, size / 2, true);
  }
}

void drawBoard(const GfxRenderer& renderer, const mesh::play::FourInRow& board, const int x, const int y,
               const int cell) {
  using B = mesh::play::FourInRow;
  renderer.drawRoundedRect(x - 4, y - 4, B::COLS * cell + 8, B::ROWS * cell + 8, 2, 8, true);
  const int gap = std::max(2, cell / 10);
  for (int r = 0; r < B::ROWS; r++) {
    for (int c = 0; c < B::COLS; c++) {
      const int px = x + c * cell + gap, py = y + (B::ROWS - 1 - r) * cell + gap;
      drawPiece(renderer, px, py, cell - 2 * gap, board.cell[r][c]);
    }
  }
  // A dot on the newest piece.
  if (board.lastCol >= 0) {
    int r = B::ROWS - 1;
    while (r > 0 && !board.cell[r][board.lastCol]) r--;
    const int dot = std::max(4, cell / 5);
    const int cx = x + board.lastCol * cell + cell / 2, cy = y + (B::ROWS - 1 - r) * cell + cell / 2;
    const bool onBlack = board.cell[r][board.lastCol] == 1;
    renderer.fillRoundedRect(cx - dot / 2, cy - dot / 2, dot, dot, dot / 2, onBlack ? Color::White : Color::Black);
  }
}

int pollHeight(const GfxRenderer& renderer, const Play& p, const int width) {
  const int lines = int(renderer.wrappedText(UI_12_FONT_ID, p.question.c_str(), width - 2 * PAD, 3, EpdFontFamily::BOLD).size());
  return PAD + lines * renderer.getLineHeight(UI_12_FONT_ID) + 8 + renderer.getLineHeight(SMALL_FONT_ID) + 12 +
         int(p.options.size()) * (OPTION_H + OPTION_GAP) + PAD - OPTION_GAP;
}

int gameHeight() { return PAD + mesh::play::FourInRow::ROWS * MINI + 8 + PAD; }

// Sjakk pieces as Norwegian letters in a circle: white pieces open, black filled.
const char* letterOf(const char piece) {
  switch (piece | 0x20) {
    case 'k': return "K";
    case 'q': return "D";
    case 'r': return "T";
    case 'b': return "L";
    case 'n': return "S";
    default: return "B";
  }
}

// A sea of Senk skipet: a1 at the bottom left. ships (optional) shades my own
// ships; shots are drawn as a dot (miss), a cross (hit), filled (sunk) or a
// ring (not answered yet).
void drawSea(const GfxRenderer& renderer, const int x, const int y, const int cell,
             const std::vector<mesh::play::Ships::Shot>& shots, const mesh::play::ShipLayout* ships = nullptr,
             const int selected = -1) {
  constexpr int N = mesh::play::Ships::SIZE;
  renderer.drawRect(x - 2, y - 2, N * cell + 4, N * cell + 4, 2, true);
  for (int sq = 0; sq < N * N; sq++) {
    const int sx = x + (sq % N) * cell, sy = y + (N - 1 - sq / N) * cell;
    renderer.drawRect(sx, sy, cell, cell, 1, true);
    if (ships && ships->cell[sq]) renderer.fillRectDither(sx + 1, sy + 1, cell - 2, cell - 2, Color::LightGray);
    if (sq == selected) renderer.drawRect(sx + 1, sy + 1, cell - 2, cell - 2, std::max(3, cell / 12), true);
  }
  for (const auto& s : shots) {
    const int sx = x + (s.square % N) * cell, sy = y + (N - 1 - s.square / N) * cell;
    const int m = cell / 4;
    // On my own sea the answer is my layout; on theirs, what they answered.
    const char result = ships ? (ships->cell[s.square] ? 'h' : 'm') : s.result;
    if (result == 'm') {
      const int dot = std::max(4, cell / 6);
      renderer.fillRoundedRect(sx + (cell - dot) / 2, sy + (cell - dot) / 2, dot, dot, dot / 2, Color::Black);
    } else if (result == 'h' || (ships && result == 's')) {
      renderer.drawLine(sx + m, sy + m, sx + cell - m, sy + cell - m, std::max(2, cell / 10), true);
      renderer.drawLine(sx + cell - m, sy + m, sx + m, sy + cell - m, std::max(2, cell / 10), true);
    } else if (result == 's') {
      renderer.fillRect(sx + 2, sy + 2, cell - 4, cell - 4, true);
    } else {
      renderer.drawRoundedRect(sx + m, sy + m, cell - 2 * m, cell - 2 * m, 2, (cell - 2 * m) / 2, true);
    }
  }
}

// The chess board with white at the bottom, or black when flipped.
void drawChess(const GfxRenderer& renderer, const mesh::play::Chess& c, const int x, const int y, const int cell,
               const bool flipped, const int selected = -1, const std::vector<int>& marks = {}, const int chosen = -1) {
  renderer.drawRect(x - 2, y - 2, 8 * cell + 4, 8 * cell + 4, 2, true);
  const bool letters = cell >= 30;
  for (int sq = 0; sq < 64; sq++) {
    const int file = sq % 8, rank = sq / 8;
    const int col = flipped ? 7 - file : file, row = flipped ? rank : 7 - rank;
    const int sx = x + col * cell, sy = y + row * cell;
    if ((file + rank) % 2 == 0) renderer.fillRectDither(sx, sy, cell, cell, Color::LightGray);
    if (sq == c.lastFrom || sq == c.lastTo) renderer.drawRect(sx + 1, sy + 1, cell - 2, cell - 2, 2, true);
    if (sq == selected || sq == chosen) renderer.drawRect(sx + 1, sy + 1, cell - 2, cell - 2, std::max(3, cell / 12), true);
    const char p = c.board[sq];
    const int pad = std::max(1, cell / 8), size = cell - 2 * pad;
    if (p != '.') {
      const bool white = p >= 'A' && p <= 'Z';
      if (white) {
        renderer.fillRoundedRect(sx + pad, sy + pad, size, size, size / 2, Color::White);
        renderer.drawRoundedRect(sx + pad, sy + pad, size, size, std::max(1, cell / 20), size / 2, true);
      } else {
        renderer.fillRoundedRect(sx + pad, sy + pad, size, size, size / 2, Color::Black);
      }
      if (letters) {
        const char* l = letterOf(p);
        const int lw = renderer.getTextWidth(UI_12_FONT_ID, l, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, sx + (cell - lw) / 2, sy + (cell - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                          l, white, EpdFontFamily::BOLD);
      }
    }
    if (std::find(marks.begin(), marks.end(), sq) != marks.end()) {
      const int dot = std::max(6, cell / 5);
      renderer.fillRoundedRect(sx + (cell - dot) / 2, sy + (cell - dot) / 2, dot, dot, dot / 2,
                               p != '.' && p >= 'a' ? Color::White : Color::Black);
    }
  }
}

}  // namespace

namespace playui {

std::vector<Entry> entries(const std::vector<mesh::ChatMessage>& plays) {
  std::vector<Entry> out;
  out.reserve(plays.size());
  for (const auto& m : plays) out.push_back({m.sender, m.counter, &m.text});
  return out;
}

int cardHeight(const GfxRenderer& renderer, const mesh::ChatMessage& m, const int width) {
  Play p;
  if (!mesh::play::read(m.text, p)) return 0;
  if (p.kind == Kind::Poll) return pollHeight(renderer, p, width);
  if (p.kind == Kind::Game) return gameHeight();
  return 0;
}

std::string gameStatus(const GameState& s) { return mesh::play::status(s, me(), nameOf); }

void drawCard(const GfxRenderer& renderer, const mesh::ChatMessage& m, const std::vector<Entry>& plays, const int x,
              const int y, const int width, std::vector<Hit>& hits) {
  Play p;
  if (!mesh::play::read(m.text, p)) return;
  const Ref ref{m.sender, m.counter};
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  if (p.kind == Kind::Poll) {
    renderer.drawRoundedRect(x, y, width, pollHeight(renderer, p, width), 2, 16, true);
    int ty = y + PAD;
    for (const auto& l : renderer.wrappedText(UI_12_FONT_ID, p.question.c_str(), width - 2 * PAD, 3, EpdFontFamily::BOLD)) {
      renderer.drawText(UI_12_FONT_ID, x + PAD, ty, l.c_str(), true, EpdFontFamily::BOLD);
      ty += lineH;
    }
    const auto t = mesh::play::tally(ref, plays);
    mesh::Members voted = 0;
    for (size_t i = 0; i < p.options.size(); i++) voted |= t.votes[i];
    const int count = mesh::memberCount(voted);
    char head[48];
    if (count) {
      snprintf(head, sizeof(head), "Avstemning · %d har stemt", count);
    } else {
      snprintf(head, sizeof(head), "Avstemning · trykk for å stemme");
    }
    renderer.drawText(SMALL_FONT_ID, x + PAD, ty + 8, head);
    ty += 8 + renderer.getLineHeight(SMALL_FONT_ID) + 12;
    for (size_t i = 0; i < p.options.size(); i++) {
      const bool mine = t.votes[i] & mesh::memberBit(me());
      const int ox = x + PAD, ow = width - 2 * PAD;
      if (mine) {
        renderer.fillRoundedRect(ox, ty, ow, OPTION_H, 12, Color::Black);
      } else {
        renderer.drawRoundedRect(ox, ty, ow, OPTION_H, 2, 12, true);
      }
      char n[8];
      snprintf(n, sizeof(n), "%d", mesh::memberCount(t.votes[i]));
      const int nw = renderer.getTextWidth(UI_12_FONT_ID, n, EpdFontFamily::BOLD);
      const std::string who = names(t.votes[i]);
      const int textY = who.empty() ? ty + (OPTION_H - lineH) / 2 : ty + 6;
      renderer.drawText(UI_12_FONT_ID, ox + 14, textY,
                        renderer.truncatedText(UI_12_FONT_ID, p.options[i].c_str(), ow - 40 - nw).c_str(), !mine);
      renderer.drawText(UI_12_FONT_ID, ox + ow - 14 - nw, ty + (OPTION_H - lineH) / 2, n, !mine, EpdFontFamily::BOLD);
      if (!who.empty()) {
        renderer.drawText(SMALL_FONT_ID, ox + 14, ty + 6 + lineH,
                          renderer.truncatedText(SMALL_FONT_ID, who.c_str(), ow - 40 - nw).c_str(), !mine);
      }
      hits.push_back({ox, ty, ow, OPTION_H, ref, int(i)});
      ty += OPTION_H + OPTION_GAP;
    }
    return;
  }
  if (p.kind == Kind::Game) {
    GameState s;
    mesh::play::replay(ref, plays, s);
    const int h = gameHeight();
    const bool chess = s.game == mesh::play::Game::Chess;
    renderer.drawRoundedRect(x, y, width, h, 2, 16, true);
    const bool ships = s.game == mesh::play::Game::Ships;
    if (chess) {
      drawChess(renderer, s.chess, x + PAD + 4, y + PAD + 4, mesh::play::FourInRow::ROWS * MINI / 8,
                s.playerOf(me()) == 2);
    } else if (ships) {
      // My shots at the other's sea (or player 1's, for someone watching).
      const int player = s.playerOf(me()) ? s.playerOf(me()) : 1;
      drawSea(renderer, x + PAD + 4, y + PAD + 4, mesh::play::FourInRow::ROWS * MINI / 8, s.ships.shots[player - 1]);
    } else {
      drawBoard(renderer, s.board, x + PAD + 4, y + PAD + 4, MINI);
    }
    const int tx = x + PAD + mesh::play::FourInRow::COLS * MINI + 28;
    const int tw = x + width - PAD - tx;
    renderer.drawText(UI_12_FONT_ID, tx, y + PAD, chess ? "Sjakk" : ships ? "Senk skipet" : "Fire på rad", true,
                      EpdFontFamily::BOLD);
    const std::string vs = s.opponent ? nameOf(s.opponent) + " mot " + nameOf(s.challenger)
                                      : nameOf(s.challenger) + " utfordrer";
    renderer.drawText(UI_10_FONT_ID, tx, y + PAD + lineH + 4, renderer.truncatedText(UI_10_FONT_ID, vs.c_str(), tw).c_str());
    int ty = y + PAD + 2 * lineH + 16;
    for (const auto& l : renderer.wrappedText(UI_12_FONT_ID, gameStatus(s).c_str(), tw, 2, EpdFontFamily::BOLD)) {
      renderer.drawText(UI_12_FONT_ID, tx, ty, l.c_str(), true, EpdFontFamily::BOLD);
      ty += lineH;
    }
    hits.push_back({x, y, width, h, ref, -1, s.game});
  }
}

std::unique_ptr<Activity> openGame(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint8_t conversation,
                                   const Hit& hit) {
  if (hit.game == mesh::play::Game::Chess) {
    return std::make_unique<ChessActivity>(renderer, mappedInput, conversation, hit.ref);
  }
  if (hit.game == mesh::play::Game::Ships) {
    return std::make_unique<ShipsActivity>(renderer, mappedInput, conversation, hit.ref);
  }
  return std::make_unique<FourInRowActivity>(renderer, mappedInput, conversation, hit.ref);
}

}  // namespace playui

// --- Fire på rad ---------------------------------------------------------------

FourInRowActivity::FourInRowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const uint8_t conversation, const Ref game)
    : Activity("FourInRow", renderer, mappedInput), conversation(conversation), game(game) {}

void FourInRowActivity::reload() {
  const auto plays = node().store().load(conversation, 400, mesh::MessageStore::Only::Plays);
  GameState loaded;
  const bool found = mesh::play::replay(game, playui::entries(plays), loaded);
  RenderLock lock(*this);
  state = loaded;
  known = found;
}

void FourInRowActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

bool FourInRowActivity::myTurn() const { return known && mesh::play::canMove(state, me()); }

void FourInRowActivity::drop() {
  if (!myTurn() || !state.board.canDrop(column)) return;
  const auto text = mesh::play::writeMove(game, uint16_t(state.board.moves + 1), std::string(1, char('0' + column)));
  if (node().sendText(conversation, text)) reload();
  requestUpdate();
}

void FourInRowActivity::loop() {
  using Button = MappedInputManager::Button;
  using B = mesh::play::FourInRow;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture()) {
    activityManager.replaceActivity(std::make_unique<ChatActivity>(renderer, mappedInput, conversation));
    return;
  }
  if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up)) {
    column = (column + B::COLS - 1) % B::COLS;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down)) {
    column = (column + 1) % B::COLS;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    drop();
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    // A tap on the board (or just above it) picks the column; the button drops.
    if (dropH && y >= dropY && y < dropY + dropH) {
      drop();
      return;
    }
    if (y >= boardY - cell && y < boardY + B::ROWS * cell && x >= boardX && x < boardX + B::COLS * cell) {
      column = (x - boardX) / cell;
      requestUpdate();
    }
  }
  if (node().changes() != shownChanges) {
    shownChanges = node().changes();
    reload();
    requestUpdate();
  }
}

void FourInRowActivity::render(RenderLock&&) {
  using B = mesh::play::FourInRow;
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  int y = meshui::drawTitle(renderer, "Fire på rad");
  if (!known) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + 60, "Fant ikke spillet");
    renderer.displayBuffer();
    return;
  }

  // Who plays which piece.
  const int legend = 26;
  int lx = 20;
  const auto player = [&](const uint8_t who, const int piece) {
    drawPiece(renderer, lx, y + 20, legend, piece);
    lx += legend + 10;
    const std::string n = who ? nameOf(who) : "?";
    renderer.drawText(UI_12_FONT_ID, lx, y + 20, n.c_str(), true, EpdFontFamily::BOLD);
    lx += renderer.getTextWidth(UI_12_FONT_ID, n.c_str(), EpdFontFamily::BOLD) + 30;
  };
  player(state.opponent, 1);
  player(state.challenger, 2);
  y += 70;
  renderer.drawCenteredText(UI_12_FONT_ID, y, playui::gameStatus(state).c_str(), true, EpdFontFamily::BOLD);
  y += 60;

  cell = (width - 40) / B::COLS;
  boardX = (width - B::COLS * cell) / 2;
  // The piece waiting above the chosen column.
  const bool turn = myTurn();
  if (turn) {
    const int size = cell - 2 * std::max(2, cell / 10);
    drawPiece(renderer, boardX + column * cell + (cell - size) / 2, y, size, state.board.turn());
  }
  boardY = y + cell + 8;
  drawBoard(renderer, state.board, boardX, boardY, cell);

  dropY = boardY + B::ROWS * cell + 30;
  dropH = 0;
  if (turn) {
    dropH = 70;
    const bool can = state.board.canDrop(column);
    const char* label = can ? "Slipp her" : "Kolonnen er full";
    if (can) {
      renderer.fillRoundedRect(20, dropY, width - 40, dropH, 14, Color::Black);
    } else {
      renderer.drawRoundedRect(20, dropY, width - 40, dropH, 2, 14, true);
    }
    const int lw = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (width - lw) / 2, dropY + 20, label, !can, EpdFontFamily::BOLD);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// --- Sjakk ---------------------------------------------------------------------

ChessActivity::ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint8_t conversation,
                             const Ref game)
    : Activity("Chess", renderer, mappedInput), conversation(conversation), game(game) {}

void ChessActivity::reload() {
  const auto plays = node().store().load(conversation, 400, mesh::MessageStore::Only::Plays);
  GameState loaded;
  const bool found = mesh::play::replay(game, playui::entries(plays), loaded);
  RenderLock lock(*this);
  state = loaded;
  known = found;
}

void ChessActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

bool ChessActivity::myTurn() const { return known && mesh::play::canMove(state, me()); }

// The challenger plays black; before anyone has moved, an onlooker sees white.
bool ChessActivity::flipped() const { return state.challenger == me(); }

int ChessActivity::squareAt(const int x, const int y) const {
  if (x < boardX || y < boardY || x >= boardX + 8 * cell || y >= boardY + 8 * cell) return -1;
  const int col = (x - boardX) / cell, row = (y - boardY) / cell;
  const int file = flipped() ? 7 - col : col, rank = flipped() ? row : 7 - row;
  return rank * 8 + file;
}

void ChessActivity::send(const std::string& move) {
  if (node().sendText(conversation, mesh::play::writeMove(game, uint16_t(state.moves() + 1), move))) reload();
  from = to = -1;
  sure = false;
  requestUpdate();
}

void ChessActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture()) {
    activityManager.replaceActivity(std::make_unique<ChatActivity>(renderer, mappedInput, conversation));
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const int width = renderer.getScreenWidth();
    if (y >= buttonY && y < buttonY + 70 && known && !state.winner()) {
      if (x >= width / 2) {
        // "Gi opp": a second tap gives the game away.
        if (sure && state.playerOf(me())) {
          send("x");
        } else {
          sure = true;
          requestUpdate();
        }
      } else if (to >= 0 && myTurn()) {
        send(mesh::play::Chess::squareName(from) + mesh::play::Chess::squareName(to));
      }
      return;
    }
    sure = false;
    const int sq = squareAt(x, y);
    if (sq >= 0 && myTurn()) {
      const auto targets = from >= 0 ? state.chess.targets(from) : std::vector<int>{};
      if (std::find(targets.begin(), targets.end(), sq) != targets.end()) {
        to = sq;
      } else if (!state.chess.targets(sq).empty()) {
        from = sq;
        to = -1;
      } else {
        from = to = -1;
      }
    }
    requestUpdate();
  }
  if (node().changes() != shownChanges) {
    shownChanges = node().changes();
    reload();
    requestUpdate();
  }
}

void ChessActivity::render(RenderLock&&) {
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  int y = meshui::drawTitle(renderer, "Sjakk");
  if (!known) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + 60, "Fant ikke spillet");
    renderer.displayBuffer();
    return;
  }
  const std::string players = "Hvit: " + (state.opponent ? nameOf(state.opponent) : std::string("?")) +
                              "   Svart: " + nameOf(state.challenger);
  renderer.drawCenteredText(UI_10_FONT_ID, y + 10, players.c_str());
  y += 50;
  renderer.drawCenteredText(UI_12_FONT_ID, y, playui::gameStatus(state).c_str(), true, EpdFontFamily::BOLD);
  y += 50;

  cell = (width - 28) / 8;
  boardX = (width - 8 * cell) / 2;
  boardY = y;
  const auto marks = from >= 0 ? state.chess.targets(from) : std::vector<int>{};
  drawChess(renderer, state.chess, boardX, boardY, cell, flipped(), from, marks, to);

  buttonY = boardY + 8 * cell + 24;
  if (!state.winner()) {
    const int bw = (width - 3 * 14) / 2;
    const bool ready = to >= 0 && myTurn();
    const std::string label = ready ? "Flytt " + mesh::play::Chess::squareName(from) + "-" +
                                          mesh::play::Chess::squareName(to)
                                    : !myTurn() ? "Ikke din tur" : from >= 0 ? "Velg hvor" : "Velg en brikke";
    if (ready) {
      renderer.fillRoundedRect(14, buttonY, bw, 70, 14, Color::Black);
    } else {
      renderer.drawRoundedRect(14, buttonY, bw, 70, 2, 14, true);
    }
    int lw = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, 14 + (bw - lw) / 2, buttonY + 20, label.c_str(), !ready, EpdFontFamily::BOLD);
    if (state.playerOf(me())) {
      const char* giveUp = sure ? "Sikker? Trykk igjen" : "Gi opp";
      renderer.drawRoundedRect(28 + bw, buttonY, bw, 70, 2, 14, true);
      lw = renderer.getTextWidth(UI_12_FONT_ID, giveUp, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, 28 + bw + (bw - lw) / 2, buttonY + 20, giveUp, true, EpdFontFamily::BOLD);
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

// --- Senk skipet -------------------------------------------------------------------

ShipsActivity::ShipsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint8_t conversation,
                             const Ref game)
    : Activity("Ships", renderer, mappedInput), conversation(conversation), game(game) {}

void ShipsActivity::reload() {
  const auto plays = node().store().load(conversation, 400, mesh::MessageStore::Only::Plays);
  GameState loaded;
  const bool found = mesh::play::replay(game, playui::entries(plays), loaded);
  // My ships come from my secret, the same on my unit and my phone.
  const auto layout = mesh::play::shipLayout(mesh::identity().secret, game);
  RenderLock lock(*this);
  state = loaded;
  known = found;
  mine = layout;
}

void ShipsActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

bool ShipsActivity::myTurn() const { return known && mesh::play::canMove(state, me()); }

void ShipsActivity::send(const std::string& move) {
  if (node().sendText(conversation, mesh::play::writeMove(game, uint16_t(state.moves() + 1), move))) reload();
  target = -1;
  sure = false;
  requestUpdate();
}

void ShipsActivity::loop() {
  using Button = MappedInputManager::Button;
  constexpr int N = mesh::play::Ships::SIZE;
  if (mappedInput.wasReleased(Button::Back) || mappedInput.wasHomeGesture()) {
    activityManager.replaceActivity(std::make_unique<ChatActivity>(renderer, mappedInput, conversation));
    return;
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    // The buttons beside my own sea: "Skyt" above, "Gi opp" below.
    const int bx = 20 + N * 26 + 24;
    if (x >= bx && y >= buttonY && y < buttonY + 160 && known && !state.winner()) {
      const int player = state.moves() == 0 ? 1 : state.playerOf(me());
      if (y >= buttonY + 90) {
        if (sure && state.playerOf(me())) {
          send("x");
        } else {
          sure = true;
          requestUpdate();
        }
      } else if (target >= 0 && myTurn()) {
        send(mesh::play::shipsMove(state.ships, player, mine, target));
      }
      return;
    }
    sure = false;
    if (myTurn() && x >= boardX && y >= boardY && x < boardX + N * cell && y < boardY + N * cell) {
      const int sq = (N - 1 - (y - boardY) / cell) * N + (x - boardX) / cell;
      const int player = state.moves() == 0 ? 1 : state.playerOf(me());
      bool fired = false;
      for (const auto& s : state.ships.shots[player - 1]) fired = fired || s.square == sq;
      target = fired ? -1 : sq;
    }
    requestUpdate();
  }
  if (node().changes() != shownChanges) {
    shownChanges = node().changes();
    reload();
    requestUpdate();
  }
}

void ShipsActivity::render(RenderLock&&) {
  constexpr int N = mesh::play::Ships::SIZE;
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  int y = meshui::drawTitle(renderer, "Senk skipet");
  if (!known) {
    renderer.drawCenteredText(UI_12_FONT_ID, y + 60, "Fant ikke spillet");
    renderer.displayBuffer();
    return;
  }
  // Before the first move, the one who takes it will be player 1.
  const int player = state.playerOf(me()) ? state.playerOf(me()) : 1;
  const auto& myShots = state.ships.shots[player - 1];
  const auto& theirShots = state.ships.shots[2 - player];
  renderer.drawCenteredText(UI_12_FONT_ID, y + 4, playui::gameStatus(state).c_str(), true, EpdFontFamily::BOLD);
  std::string last;
  if (!myShots.empty() && myShots.back().result != '?') {
    const char r = myShots.back().result;
    last = std::string("Ditt siste skudd: ") + (r == 'm' ? "bom" : r == 'h' ? "treff!" : "senket et skip!");
  }
  if (!last.empty()) renderer.drawCenteredText(UI_10_FONT_ID, y + 44, last.c_str());
  y += 80;

  cell = (width - 76) / N;
  boardX = (width - N * cell) / 2;
  boardY = y;
  drawSea(renderer, boardX, boardY, cell, myShots, nullptr, target);

  // My own sea, small, with their shots; the buttons beside it.
  const int small = 26;
  const int seaY = boardY + N * cell + 40;
  renderer.drawText(SMALL_FONT_ID, 20, seaY - 26, "DITT HAV", true, EpdFontFamily::BOLD);
  drawSea(renderer, 20, seaY, small, theirShots, &mine);
  const int bx = 20 + N * small + 24, bw = width - bx - 20;
  buttonY = seaY;
  if (!state.winner()) {
    const bool ready = target >= 0 && myTurn();
    const std::string label = ready ? "Skyt " + std::string{char('a' + target % N), char('1' + target / N)}
                                    : myTurn() ? "Velg en rute" : "Ikke din tur";
    if (ready) {
      renderer.fillRoundedRect(bx, buttonY, bw, 70, 14, Color::Black);
    } else {
      renderer.drawRoundedRect(bx, buttonY, bw, 70, 2, 14, true);
    }
    int lw = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, bx + (bw - lw) / 2, buttonY + 20, label.c_str(), !ready, EpdFontFamily::BOLD);
    if (state.playerOf(me())) {
      const char* giveUp = sure ? "Sikker?" : "Gi opp";
      renderer.drawRoundedRect(bx, buttonY + 90, bw, 70, 2, 14, true);
      lw = renderer.getTextWidth(UI_12_FONT_ID, giveUp, EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, bx + (bw - lw) / 2, buttonY + 110, giveUp, true, EpdFontFamily::BOLD);
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
