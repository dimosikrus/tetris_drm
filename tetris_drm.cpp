#include <dirent.h>
#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std;
constexpr int BOARD_W = 10;
constexpr int BOARD_H = 20;
constexpr int CELL_PX = 24;
int screen_w = 0, screen_h = 0;
int DISPLAY_X = 0, DISPLAY_Y = 0, NEXT_X = 0, NEXT_Y = 0;
volatile sig_atomic_t stop_flag = 0;
void sig_handler(int) { stop_flag = 1; }
int board_arr[BOARD_H][BOARD_W];

struct Color {
  uint8_t r, g, b;
};

vector<Color> palette = {
  {0, 0, 0},
  {255, 85, 85},
  {85, 255, 85},
  {85, 85, 255},
  {255, 255, 85},
  {85, 255, 255},
  {255, 85, 255},
  {200, 200, 200}
};

struct Piece {
  int type;
  int rot;
  int x, y;
};
Piece current_piece, next_piece;
int PIECES[7][4][4] = {
    {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {0, 2, 2, 0}, {0, 2, 2, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {0, 3, 0, 0}, {3, 3, 3, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {0, 4, 4, 0}, {4, 4, 0, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {5, 5, 0, 0}, {0, 5, 5, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {6, 0, 0, 0}, {6, 6, 6, 0}, {0, 0, 0, 0}},
    {{0, 0, 0, 0}, {0, 0, 7, 0}, {7, 7, 7, 0}, {0, 0, 0, 0}}};
inline int piece_cell(int type, int rot, int x, int y) {
  int rx, ry;
  switch (rot & 3) {
    case 0:
      rx = x;
      ry = y;
      break;
    case 1:
      rx = 3 - y;
      ry = x;
      break;
    case 2:
      rx = 3 - x;
      ry = 3 - y;
      break;
    default:
      rx = y;
      ry = 3 - x;
      break;
  }
  return PIECES[type][ry][rx];
}
static inline uint32_t color_u32(const Color &c) {
  return (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.b);
}

struct DUMB_Buffer {
  uint32_t handle = 0;
  uint32_t fb_id = 0;
  void *map = nullptr;
};

struct DRMState {
  int fd = -1;
  uint32_t pitch = 0;
  uint64_t buffer_sz = 0;
  DUMB_Buffer buffers[2];
  uint32_t front_buffer = 0;
  drmModeCrtc *old_crtc = nullptr;
  drmModeConnector *connector = nullptr;
  drmModeRes *resources = nullptr;
  uint32_t crtc_id = 0;
} drm_state;

struct InputDev {
  int fd;
  string path;
  bool grabbed;
};
vector<InputDev> input_devs;

void draw_rect_pixels(void *buffer_map, int x, int y, int w, int h, const Color &c) {
  if (!buffer_map) return;
  uint32_t col = color_u32(c);
  for (int py = y; py < y + h; ++py) {
    if (py < 0 || py >= screen_h) continue;
    uint8_t *row = (uint8_t *)buffer_map + (size_t)py * drm_state.pitch;
    for (int px = x; px < x + w; ++px) {
      if (px < 0 || px >= screen_w) continue;
      uint32_t *p = (uint32_t *)(row + px * 4);
      *p = col;
    }
  }
}

void draw_cell(void *buffer_map, int bx, int by, const Color &c, bool is_next_piece) {
  int base_x = is_next_piece ? NEXT_X : DISPLAY_X;
  int base_y = is_next_piece ? NEXT_Y : DISPLAY_Y;
  int sx = base_x + bx * CELL_PX;
  int sy = base_y + by * CELL_PX;
  draw_rect_pixels(buffer_map, sx, sy, CELL_PX, CELL_PX, c);
  if (CELL_PX > 4)
    draw_rect_pixels(buffer_map, sx + 1, sy + 1, CELL_PX - 2, CELL_PX - 2,
                     {0, 0, 0});
  if (CELL_PX > 8)
    draw_rect_pixels(buffer_map, sx + 2, sy + 2, CELL_PX - 4, CELL_PX - 4, c);
}

void render_all(uint32_t buffer_idx) {
  void *current_map = drm_state.buffers[buffer_idx].map;
  if (!current_map) return;
  memset(current_map, 0, (size_t)drm_state.buffer_sz);
  draw_rect_pixels(current_map, DISPLAY_X - 4, DISPLAY_Y - 4,
                   BOARD_W * CELL_PX + 8, BOARD_H * CELL_PX + 8, {50, 50, 50});
  for (int y = 0; y < BOARD_H; ++y)
    for (int x = 0; x < BOARD_W; ++x) {
      int v = board_arr[y][x];
      draw_cell(current_map, x, y, palette[(v > 0 && v < (int)palette.size()) ? v : 0], false);
    }
  for (int py = 0; py < 4; ++py)
    for (int px = 0; px < 4; ++px) {
      int v = piece_cell(current_piece.type, current_piece.rot, px, py);
      if (!v) continue;
      int bx = current_piece.x + px;
      int by = current_piece.y + py;
      if (by >= 0) draw_cell(current_map, bx, by, palette[v], false);
    }
  draw_rect_pixels(current_map, NEXT_X - 4, NEXT_Y - 4, CELL_PX * 4 + 8, CELL_PX * 4 + 8, {50, 50, 50});
  for (int py = 0; py < 4; ++py)
    for (int px = 0; px < 4; ++px) {
      int v = piece_cell(next_piece.type, next_piece.rot, px, py);
      draw_cell(current_map, px, py, palette[v], true);
    }
}

bool collision_at(const Piece &p) {
  for (int py = 0; py < 4; ++py)
    for (int px = 0; px < 4; ++px) {
      int v = piece_cell(p.type, p.rot, px, py);
      if (!v) continue;
      int bx = p.x + px;
      int by = p.y + py;
      if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) return true;
      if (by >= 0 && board_arr[by][bx]) return true;
    }
  return false;
}
void place_piece(const Piece &p) {
  for (int py = 0; py < 4; ++py)
    for (int px = 0; px < 4; ++px) {
      int v = piece_cell(p.type, p.rot, px, py);
      if (!v) continue;
      int bx = p.x + px;
      int by = p.y + py;
      if (by >= 0 && bx >= 0 && bx < BOARD_W && by < BOARD_H)
        board_arr[by][bx] = v;
    }
}
void clear_lines() {
  for (int y = BOARD_H - 1; y >= 0; --y) {
    bool full = true;
    for (int x = 0; x < BOARD_W; ++x)
      if (!board_arr[y][x]) {
        full = false;
        break;
      }
    if (full) {
      for (int r = y; r > 0; --r)
        memcpy(board_arr[r], board_arr[r - 1], sizeof(board_arr[0]));
      memset(board_arr[0], 0, sizeof(board_arr[0]));
      ++y;
    }
  }
}
Piece rand_piece() {
  Piece p;
  p.type = rand() % 7;
  p.rot = 0;
  p.x = BOARD_W / 2 - 2;
  p.y = 0;
  return p;
}
void init_input() {
  DIR *d = opendir("/dev/input");
  if (!d) return;
  struct dirent *de;
  while ((de = readdir(d)) != nullptr) {
    if (strncmp(de->d_name, "event", 5) == 0) {
      string path = string("/dev/input/") + de->d_name;
      int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
      if (fd >= 0) {
        bool grabbed = (ioctl(fd, EVIOCGRAB, (void *)1) == 0);
        input_devs.push_back({fd, path, grabbed});
      }
    }
  }
  closedir(d);
}
vector<int> poll_input(int t) {
  vector<int> k;
  if (input_devs.empty()) return k;
  vector<pollfd> p;
  p.reserve(input_devs.size());
  for (auto &i : input_devs) p.push_back({i.fd, POLLIN, 0});
  if (poll(p.data(), p.size(), t) <= 0) return k;
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i].revents & POLLIN) {
      input_event e;
      while (read(p[i].fd, &e, sizeof(e)) == (ssize_t)sizeof(e)) {
        if (e.type == EV_KEY && e.value > 0) k.push_back(e.code);
      }
    }
  }
  return k;
}
void cleanup_input() {
  for (auto &id : input_devs) {
    if (id.grabbed) ioctl(id.fd, EVIOCGRAB, (void *)0);
    close(id.fd);
  }
  input_devs.clear();
}

void drm_cleanup() {
  if (drm_state.old_crtc) {
    drmModeSetCrtc(drm_state.fd, drm_state.old_crtc->crtc_id,
                   drm_state.old_crtc->buffer_id, drm_state.old_crtc->x,
                   drm_state.old_crtc->y, &drm_state.connector->connector_id, 1,
                   &drm_state.old_crtc->mode);
    drmModeFreeCrtc(drm_state.old_crtc);
  }
  for (int i = 0; i < 2; ++i) {
    if (drm_state.buffers[i].map)
      munmap(drm_state.buffers[i].map, (size_t)drm_state.buffer_sz);
    if (drm_state.buffers[i].fb_id)
      drmModeRmFB(drm_state.fd, drm_state.buffers[i].fb_id);
    if (drm_state.buffers[i].handle) {
      struct drm_mode_destroy_dumb d = {drm_state.buffers[i].handle};
      ioctl(drm_state.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
  }
  if (drm_state.connector) drmModeFreeConnector(drm_state.connector);
  if (drm_state.resources) drmModeFreeResources(drm_state.resources);
  if (drm_state.fd >= 0) close(drm_state.fd);
}

int init_drm_card(const char *cardpath = "/dev/dri/card0") {
  drm_state.fd = open(cardpath, O_RDWR | O_CLOEXEC);
  if (drm_state.fd < 0) {
    perror("open drm card");
    return -1;
  }
  drm_state.resources = drmModeGetResources(drm_state.fd);
  if (!drm_state.resources) {
    perror("drmModeGetResources");
    drm_cleanup();
    return -1;
  }

  for (int i = 0; i < drm_state.resources->count_connectors; i++) {
    drm_state.connector =
        drmModeGetConnector(drm_state.fd, drm_state.resources->connectors[i]);
    if (drm_state.connector &&
        drm_state.connector->connection == DRM_MODE_CONNECTED &&
        drm_state.connector->count_modes > 0)
      break;
    drmModeFreeConnector(drm_state.connector);
    drm_state.connector = nullptr;
  }
  if (!drm_state.connector) {
    cerr << "No connected connector\n";
    drm_cleanup();
    return -1;
  }

  drmModeEncoder *enc = nullptr;
  if (drm_state.connector->encoder_id)
    enc = drmModeGetEncoder(drm_state.fd, drm_state.connector->encoder_id);
  if (enc && enc->crtc_id)
    drm_state.old_crtc = drmModeGetCrtc(drm_state.fd, enc->crtc_id);
  if (enc) drmModeFreeEncoder(enc);

  drm_state.crtc_id = (drm_state.old_crtc) ? drm_state.old_crtc->crtc_id
                                           : drm_state.resources->crtcs[0];

  screen_w = (int)drm_state.connector->modes[0].hdisplay;
  screen_h = (int)drm_state.connector->modes[0].vdisplay;

  DISPLAY_X = (screen_w - BOARD_W * CELL_PX) / 2;
  DISPLAY_Y = (screen_h - BOARD_H * CELL_PX) / 2;
  NEXT_X = DISPLAY_X + BOARD_W * CELL_PX + 24;
  NEXT_Y = DISPLAY_Y;

  for (int i = 0; i < 2; ++i) {
    struct drm_mode_create_dumb create = {};
    create.width = screen_w;
    create.height = screen_h;
    create.bpp = 32;
    if (ioctl(drm_state.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
      perror("CREATE_DUMB");
      drm_cleanup();
      return -1;
    }
    drm_state.buffers[i].handle = create.handle;
    if (i == 0) {
      drm_state.pitch = create.pitch;
      drm_state.buffer_sz = create.size;
    }

    uint32_t handles[4] = {drm_state.buffers[i].handle};
    uint32_t pitches[4] = {drm_state.pitch};
    uint32_t offsets[4] = {0};
    if (drmModeAddFB2(drm_state.fd, screen_w, screen_h, DRM_FORMAT_XRGB8888,
                      handles, pitches, offsets, &drm_state.buffers[i].fb_id,
                      0) != 0) {
      perror("AddFB2");
      drm_cleanup();
      return -1;
    }

    struct drm_mode_map_dumb map = {};
    map.handle = drm_state.buffers[i].handle;
    if (ioctl(drm_state.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
      perror("MAP_DUMB");
      drm_cleanup();
      return -1;
    }

    drm_state.buffers[i].map =
        mmap(0, (size_t)drm_state.buffer_sz, PROT_READ | PROT_WRITE, MAP_SHARED,
             drm_state.fd, map.offset);
    if (drm_state.buffers[i].map == MAP_FAILED) {
      perror("mmap");
      drm_cleanup();
      return -1;
    }
  }
  render_all(0);
  if (drmModeSetCrtc(drm_state.fd, drm_state.crtc_id,
                     drm_state.buffers[0].fb_id, 0, 0,
                     &drm_state.connector->connector_id, 1,
                     &drm_state.connector->modes[0]) != 0) {
    perror("SetCrtc");
    drm_cleanup();
    return -1;
  }
  drm_state.front_buffer = 0;

  return 0;
}

int main() {
  signal(SIGINT, sig_handler);
  srand(time(nullptr));
  memset(board_arr, 0, sizeof(board_arr));

  if (init_drm_card() != 0) {
    cerr << "Failed to init DRM\n";
    return 1;
  }
  init_input();

  current_piece = rand_piece();
  next_piece = rand_piece();

  const int base_tick_ms = 400;
  int acc = 0;
  auto last = chrono::steady_clock::now();

  // --- NEW: This is the page flip event handler data structure ---
  drmEventContext ev = {};
  ev.version = DRM_EVENT_CONTEXT_VERSION;
  ev.page_flip_handler = [](int, unsigned int, unsigned int, unsigned int,
                            void *data) {
    // This function is called by DRM when the page flip is complete.
    // We just use it to signal the main loop.
    *(bool *)data = false;
  };

  bool running = true;
  while (running && !stop_flag) {
    // --- NEW: Main loop logic with page flipping ---
    uint32_t back_buffer = 1 - drm_state.front_buffer;

    // Step 1: Draw everything to the back buffer
    render_all(back_buffer);

    // Step 2: Request a page flip. This is asynchronous.
    // It asks the kernel to flip to the back buffer as soon as possible (at the
    // next VBLANK).
    bool waiting_for_flip = true;
    if (drmModePageFlip(drm_state.fd, drm_state.crtc_id,
                        drm_state.buffers[back_buffer].fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip) < 0) {
      perror("PageFlip failed");
      break;
    }

    // Step 3: Wait for the flip to actually happen
    struct pollfd p = {drm_state.fd, POLLIN, 0};
    while (waiting_for_flip) {
      if (poll(&p, 1, -1) < 0) break;  // Wait indefinitely for an event
      drmHandleEvent(drm_state.fd, &ev);
    }

    // Step 4: The back buffer is now the front buffer
    drm_state.front_buffer = back_buffer;

    // --- OLD: The game logic itself is mostly unchanged ---
    vector<int> keys = poll_input(5);
    bool soft_drop = false;
    for (int code : keys) {
      Piece moved = current_piece;
      bool piece_moved = false;
      if (code == KEY_LEFT) {
        moved.x -= 1;
        piece_moved = true;
      } else if (code == KEY_RIGHT) {
        moved.x += 1;
        piece_moved = true;
      } else if (code == KEY_DOWN) {
        soft_drop = true;
      } else if (code == KEY_UP || code == KEY_Z) {
        moved.rot = (moved.rot + 1) % 4;
        piece_moved = true;
      } else if (code == KEY_SPACE) {
        while (!collision_at(moved)) {
          current_piece = moved;
          moved.y++;
        }
        acc = base_tick_ms;
      } else if (code == KEY_Q || code == KEY_ESC) {
        running = false;
      }
      if (piece_moved && !collision_at(moved)) current_piece = moved;
    }

    auto now = chrono::steady_clock::now();
    acc += (int)chrono::duration_cast<chrono::milliseconds>(now - last).count();
    last = now;

    int tick_ms = soft_drop ? 50 : base_tick_ms;
    if (acc >= tick_ms) {
      acc = 0;
      Piece moved = current_piece;
      moved.y += 1;
      if (!collision_at(moved)) {
        current_piece = moved;
      } else {
        place_piece(current_piece);
        clear_lines();
        current_piece = next_piece;
        next_piece = rand_piece();
        if (collision_at(current_piece)) {
          // Game over screen
          uint32_t bb = 1 - drm_state.front_buffer;
          draw_rect_pixels(drm_state.buffers[bb].map, 0, 0, screen_w, screen_h,
                           {80, 0, 0});
          drmModePageFlip(drm_state.fd, drm_state.crtc_id,
                          drm_state.buffers[bb].fb_id, 0, nullptr);
          this_thread::sleep_for(chrono::seconds(2));
          running = false;
        }
      }
    }
  }

  cleanup_input();
  drm_cleanup();
  return 0;
}
