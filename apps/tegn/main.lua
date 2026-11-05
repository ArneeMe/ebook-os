-- Tegn: draw with a finger or a touch pen. The drawing is kept in
-- storage ("tegning.txt", one stroke per line) and comes back next time.

local W, H = screen.width(), screen.height()
local TOOLBAR = 76
local top = 0            -- set by the header in draw()
local bottom = H - TOOLBAR

local strokes = {}       -- {size=, white=, points={x1, y1, x2, y2, ...}}
local current = nil      -- the stroke being drawn
local pending = {}       -- segments to draw before the next refresh: {x1, y1, x2, y2, size, white}
local full = true        -- next draw() repaints everything
local pen = 4            -- 4 thin, 10 thick
local eraser = false
local sure = false       -- "Tøm" tapped once, waiting for the second tap

local buttons = {}       -- {x=, w=, label=, action=}

local function save()
  local lines = {}
  for _, s in ipairs(strokes) do
    lines[#lines + 1] = s.size .. " " .. (s.white and 1 or 0) .. " " .. table.concat(s.points, " ")
  end
  storage.write("tegning.txt", table.concat(lines, "\n"))
end

local function load()
  local text = storage.read("tegning.txt")
  if not text then return end
  for line in text:gmatch("[^\n]+") do
    local nums = {}
    for n in line:gmatch("%-?%d+") do nums[#nums + 1] = tonumber(n) end
    -- A damaged line (an odd number of coordinates) is skipped, not drawn.
    if #nums >= 4 and #nums % 2 == 0 then
      local s = {size = nums[1], white = nums[2] == 1, points = {}}
      for i = 3, #nums do s.points[#s.points + 1] = nums[i] end
      strokes[#strokes + 1] = s
    end
  end
end

local function segment(x1, y1, x2, y2, size, white)
  local opts = {width = size, white = white}
  screen.line(x1, y1, x2, y2, opts)
  screen.dot(x2, y2, size, opts)
end

local function drawStroke(s)
  local p = s.points
  if #p == 2 then
    screen.dot(p[1], p[2], s.size, {white = s.white})
    return
  end
  for i = 3, #p, 2 do segment(p[i - 2], p[i - 1], p[i], p[i + 1], s.size, s.white) end
end

local function drawToolbar()
  screen.rect(0, bottom, W, TOOLBAR, {fill = true, white = true})
  screen.line(0, bottom, W - 1, bottom, {width = 2})
  buttons = {
    {label = "Tynn", on = not eraser and pen == 4, action = function() pen, eraser = 4, false end},
    {label = "Tykk", on = not eraser and pen == 10, action = function() pen, eraser = 10, false end},
    {label = "Visk", on = eraser, action = function() eraser = true end},
    {label = sure and "Sikker?" or "Tøm", on = sure, action = function()
      if sure then
        strokes, sure = {}, false
        save()
      else
        sure = true
      end
    end},
  }
  local gap = 10
  local bw = math.floor((W - 2 * 16 - (#buttons - 1) * gap) / #buttons)
  for i, b in ipairs(buttons) do
    b.x, b.w = 16 + (i - 1) * (bw + gap), bw
    local y, h = bottom + 12, TOOLBAR - 24
    screen.rect(b.x, y, b.w, h, {radius = 10, fill = b.on, width = 2})
    local tw = screen.textWidth(b.label, {bold = true})
    screen.text(b.x + math.floor((b.w - tw) / 2), y + 12, b.label, {bold = true, white = b.on})
  end
end

function start()
  load()
end

function draw()
  if full then
    screen.clear()
    top = screen.header("Tegn")
    for _, s in ipairs(strokes) do drawStroke(s) end
    drawToolbar()
    full = false
    pending = {}
    return
  end
  for _, p in ipairs(pending) do segment(p[1], p[2], p[3], p[4], p[5], p[6]) end
  pending = {}
end

local function inside(y) return y > top + 4 and y < bottom - 4 end

local function size() return eraser and 28 or pen end

function press(x, y)
  if not inside(y) then return end
  sure = false
  current = {size = size(), white = eraser, points = {x, y}}
  strokes[#strokes + 1] = current
  pending[#pending + 1] = {x, y, x, y, current.size, current.white}
  app.redraw("fast")
end

function move(x, y)
  if not current then return end
  y = math.max(top + 4, math.min(bottom - 4, y))
  local p = current.points
  local lx, ly = p[#p - 1], p[#p]
  -- Skip points closer than 2 px: less to store, no visible difference.
  if math.abs(x - lx) + math.abs(y - ly) < 3 then return end
  p[#p + 1], p[#p + 2] = x, y
  pending[#pending + 1] = {lx, ly, x, y, current.size, current.white}
  app.redraw("fast")
end

function release()
  if not current then return end
  current = nil
  save()
end

function tap(x, y)
  if y < bottom then return end
  for _, b in ipairs(buttons) do
    if x >= b.x and x < b.x + b.w then
      local wasSure = sure
      b.action()
      if wasSure and b.label ~= "Sikker?" then sure = false end
      full = true
      app.redraw(b.label == "Sikker?" and "half" or "fast")
      return
    end
  end
end

function button(name)
  if name == "ok" then
    full = true
    app.redraw("half")
  end
end
