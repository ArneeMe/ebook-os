-- Bokhylle: the books in books/ of the waddleos repo (added with
-- `waddle book --hylle`), fetched to /Books with a tap.

local BASE = "https://raw.githubusercontent.com/ArneeMe/waddleos/waddle/books/"
local W, H = screen.width(), screen.height()
local ROW_H = 84
local FOOTER = 70

local shelf = {}       -- {file=, title=, author=, bytes=}
local state = "load"   -- load, list, failed
local problem = ""
local status = ""      -- the line under the header: "Henter …", "Lagt i Bøker"
local fetching = nil   -- the book to download at the next tick
local page = 1
local focus = 1        -- the chosen book, for the buttons (up/down, ok)
local perPage = 1
local rows = {}        -- {y=, book=} on this page
local top = 0

local function parse(text)
  local list = {}
  for line in text:gmatch("[^\n]+") do
    local file, title, author, bytes = line:match("^([^\t]+)\t([^\t]*)\t([^\t]*)\t(%d+)$")
    if file then list[#list + 1] = {file = file, title = title, author = author, bytes = tonumber(bytes)} end
  end
  return list
end

local function size(bytes)
  if bytes >= 1024 * 1024 then return string.format("%.1f MB", bytes / 1024 / 1024) end
  return math.max(1, math.floor(bytes / 1024)) .. " KB"
end

local function pages() return math.max(1, math.ceil(#shelf / perPage)) end

function start()
  app.every(100)  -- the list is fetched at the first tick, once "Henter" shows
end

function tick()
  if state == "load" then
    app.every(0)
    local text, err = net.get(BASE .. "index.tsv")
    if text then
      shelf, state = parse(text), "list"
    else
      state, problem = "failed", err or ""
    end
    app.redraw("half")
  elseif fetching then
    local book = fetching
    fetching = nil
    app.every(0)
    local ok, err = books.download(BASE .. book.file, book.file)
    status = ok and ("Lagt i Bøker: " .. book.title) or ("Fikk ikke hentet: " .. (err or ""))
    app.redraw("fast")
  end
end

function draw()
  screen.clear()
  top = screen.header("Bokhylle")
  if state == "load" then
    screen.text(20, top + 40, "Henter listen ...", {bold = true})
    return
  end
  if state == "failed" then
    screen.text(20, top + 40, "Fikk ikke hentet listen", {bold = true})
    for i, line in ipairs(screen.wrap(problem, W - 40, {size = "small", lines = 4})) do
      screen.text(20, top + 80 + (i - 1) * screen.lineHeight("small"), line, {size = "small"})
    end
    screen.text(20, top + 200, "Trykk for å prøve igjen", {size = "small"})
    return
  end
  if status ~= "" then
    screen.text(20, top, status, {size = "small", bold = true})
  end
  local y0 = top + 36
  if #shelf == 0 then
    screen.text(20, y0 + 20, "Ingen bøker i hylla ennå", {bold = true})
    for i, line in ipairs(screen.wrap("Legg til med waddle book --hylle på datamaskinen.", W - 40, {size = "small"})) do
      screen.text(20, y0 + 60 + (i - 1) * screen.lineHeight("small"), line, {size = "small"})
    end
    return
  end
  perPage = math.max(1, math.floor((H - y0 - FOOTER) / ROW_H))
  page = math.min(page, pages())
  rows = {}
  for i = (page - 1) * perPage + 1, math.min(#shelf, page * perPage) do
    local b = shelf[i]
    local y = y0 + (#rows) * ROW_H
    rows[#rows + 1] = {y = y, book = b}
    if i == focus then screen.rect(0, y, 6, ROW_H - 4, {fill = true}) end
    local have = books.has(b.file)
    local right = have and "Har" or "Hent"
    local rw = screen.textWidth(right, {bold = true})
    local title = screen.wrap(b.title, W - 60 - rw, {bold = true, lines = 1})[1] or ""
    screen.text(20, y + 10, title, {bold = true})
    screen.text(20, y + 44, (b.author ~= "" and b.author .. "  ·  " or "") .. size(b.bytes), {size = "small"})
    screen.text(W - 20 - rw, y + 24, right, {bold = true})
    screen.line(20, y + ROW_H - 2, W - 20, y + ROW_H - 2)
  end
  local footer = "Side " .. page .. " av " .. pages()
  screen.text(math.floor((W - screen.textWidth(footer, {size = "small"})) / 2), H - FOOTER + 24, footer, {size = "small"})
  if page > 1 then screen.text(20, H - FOOTER + 20, "< Forrige", {bold = true}) end
  if page < pages() then
    screen.text(W - 20 - screen.textWidth("Neste >", {bold = true}), H - FOOTER + 20, "Neste >", {bold = true})
  end
end

local function turn(step)
  local next = math.max(1, math.min(pages(), page + step))
  if next ~= page then
    page = next
    focus = (page - 1) * perPage + 1
    app.redraw("fast")
  end
end

local function fetch(book)
  if fetching or books.has(book.file) then return end
  fetching = book
  status = "Henter " .. book.title .. " ..."
  app.redraw("fast")
  app.every(100)
end

local function retry()
  state = "load"
  app.every(100)
  app.redraw("fast")
end

function tap(x, y)
  if state == "failed" then
    retry()
    return
  end
  if state ~= "list" or fetching then return end
  if y >= H - FOOTER then
    turn(x < W / 2 and -1 or 1)
    return
  end
  for _, r in ipairs(rows) do
    if y >= r.y and y < r.y + ROW_H then
      fetch(r.book)
      return
    end
  end
end

-- Without touch: up/down choose a book, left/right turn the page, ok fetches.
function button(name)
  if state == "failed" then
    if name == "ok" then retry() end
    return
  end
  if state ~= "list" or #shelf == 0 then return end
  if name == "ok" then
    fetch(shelf[focus])
  elseif name == "down" or name == "up" then
    focus = math.max(1, math.min(#shelf, focus + (name == "down" and 1 or -1)))
    page = math.floor((focus - 1) / perPage) + 1
    app.redraw("fast")
  elseif name == "next" or name == "right" then
    turn(1)
  elseif name == "prev" or name == "left" then
    turn(-1)
  end
end
