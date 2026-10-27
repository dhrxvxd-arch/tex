
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;

#define COLOR(hex) \
  _parse_hex_pair(hex, 0), \
  _parse_hex_pair(hex, 2), \
  _parse_hex_pair(hex, 4)

static const char* version  = "Tex version: v0.1.0\n";
static const char* help     = "Tex:\n\t"
                              "--help|-h\t\t\tHelp\n\t"
                              "--version|-v\t\t\tVersion\n\t"
                              "[filename]\t\t\tOpen file\n";

#define BG 1
#define FG 2
static const char* colors[] = {
  [BG] = "#1d2021",
  [FG] = "#e0def4"
};
static const float opacity   = 0.9f;
static const int   font_size = 18;
static const int   padding_x = 10;
static const int   padding_y = 10;
static const int   tabstop   = 4;

static inline u8 _parse_hex_pair(const char* str, int offset) {
  if (str == NULL)
    return 0;

  if (str[0] == '#')
    offset++;

  char hex[3] = { str[offset], str[offset + 1], '\0' };
  return (u8)strtol(hex, NULL, 16);
}

static inline const char* _parse_cmd_args(int argc, const char** argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      fprintf(stderr, "%s", help);
      exit(EXIT_SUCCESS);
    } else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v")) {
      fprintf(stderr, "%s", version);
      exit(EXIT_SUCCESS);
    } else {
      return argv[i];
    }
  }

  return NULL;
}

struct str8 {
  u8* data;
  size_t size;
  size_t capacity;
};

enum editor_mode { MODE_NORMAL, MODE_INSERT, MODE_COMMAND };

struct editor_core_T {
  enum editor_mode mode;
  size_t count;
  bool pending_delete;
  bool suppress_text_input;
  bool modified;
  size_t cur_pos;
  size_t preferred_col;
  bool preferred_col_valid;
  char message[256];
  struct str8 command;
  struct str8 buffer;
};

struct state_T {
  int argc;
  const char** argv;
  bool has_file;
  const char* filename;
  char save_name[4096];
  bool running;
  size_t row_offset;
  size_t col_offset;
  SDL_Window* window;
  SDL_Renderer* renderer;
  TTF_Font* font;
  struct editor_core_T* core;
};

static inline u8 _parse_hex_pair(const char* str, int offset);
static inline const char* _parse_cmd_args(int argc, const char** argv);

static void string8_init(struct str8* string);
static bool string8_reserve(struct str8* string, size_t needed);
static bool string8_insert(struct str8* string, u8 val, size_t pos);
static bool string8_insert_bytes(struct str8* string, const u8* data, size_t len, size_t pos);
static void string8_dest(struct str8* string);

static size_t utf8_next(const struct str8* string, size_t pos);
static size_t utf8_prev(const struct str8* string, size_t pos);
static size_t utf8_count(const struct str8* string, size_t start, size_t end);
static size_t utf8_advance(const struct str8* string, size_t pos, size_t count);

static void editor_core_init(struct editor_core_T* core);
static void editor_core_dest(struct editor_core_T* core);

static void tex_file_draw(struct state_T* state);
static bool tex_file_save(struct state_T* state, const char* filename);
static void tex_window_init(struct state_T* state);
static size_t tex_cursor_row(struct state_T* state);
static size_t tex_visible_rows(struct state_T* state);
static size_t tex_total_rows(const struct str8* buffer);
static int tex_cursor_x(struct state_T* state);
static void tex_scroll_horizontal(struct state_T* state);
static void tex_scroll(struct state_T* state);
static void tex_cursor_draw(struct state_T* state);
static void tex_command_cursor_draw(struct state_T* state);
static void tex_text_draw(struct state_T* state, const char* text, int x, int y, SDL_Color color);
static void tex_status_draw(struct state_T* state);
static char* tex_command_line(const struct editor_core_T* core);
static bool tex_command_write(struct state_T* state, const char* arg);
static void tex_command_execute(struct state_T* state);
static void tex_command_input(struct state_T* state, SDL_Keycode key);
static void tex_input_handle(struct state_T* state, const SDL_Event* event);
static void tex_render(struct state_T* state);
static void tex_window_loop(struct state_T* state);
static void tex_window_dest(struct state_T* state);

static size_t tex_line_start(const struct str8* buffer, size_t pos);
static size_t tex_line_end(const struct str8* buffer, size_t pos);
static void tex_move_vertical(struct editor_core_T* core, int direction);

static void tex_file_draw(struct state_T* state) {
  if (!state->has_file)
    return;

  FILE* fp = fopen(state->filename, "rb");
  if (fp == NULL) {
    fprintf(stderr, "tex: cannot open file '%s': ", state->filename);
    perror(NULL);
    exit(EXIT_FAILURE);
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fprintf(stderr, "tex: cannot seek file '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  long end = ftell(fp);
  if (end < 0) {
    fprintf(stderr, "tex: cannot determine size of '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  if (fseek(fp, 0, SEEK_SET) != 0) {
    fprintf(stderr, "tex: cannot rewind file '%s': ", state->filename);
    perror(NULL);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  size_t file_size = (size_t)end;
  if (file_size > SIZE_MAX - 1 ||
      !string8_reserve(&state->core->buffer, file_size + 1)) {
    fprintf(stderr, "tex: cannot allocate buffer for '%s'\n", state->filename);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  struct str8* buffer = &state->core->buffer;
  size_t bytes_read = fread(buffer->data, 1, file_size, fp);
  if (bytes_read != file_size) {
    fprintf(stderr, "tex: error reading file '%s'\n", state->filename);
    fclose(fp);
    exit(EXIT_FAILURE);
  }

  if (fclose(fp) != 0) {
    fprintf(stderr, "tex: error closing file '%s': ", state->filename);
    perror(NULL);
    exit(EXIT_FAILURE);
  }

  buffer->size = bytes_read;
  buffer->data[buffer->size] = '\0';
  state->core->cur_pos = 0;
}

static bool tex_file_save(struct state_T* state, const char* filename) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  FILE* fp = fopen(filename, "wb");
  if (fp == NULL) {
    snprintf(core->message, sizeof(core->message),
      "E212: Cannot open '%s' for writing", filename);
    return false;
  }

  bool ok = fwrite(buffer->data, 1, buffer->size, fp) == buffer->size;
  if (fclose(fp) != 0)
    ok = false;

  if (!ok) {
    snprintf(core->message, sizeof(core->message),
      "E514: Write error on '%s'", filename);
    return false;
  }

  snprintf(core->message, sizeof(core->message),
    "\"%s\" %zuB written", filename, buffer->size);
  return true;
}

static void string8_init(struct str8* string) {
  string->data = calloc(1, sizeof(*string->data));

  if (string->data == NULL) {
    fprintf(stderr, "tex: cannot allocate string buffer\n");
    exit(EXIT_FAILURE);
  }

  string->size = 0;
  string->capacity = 1;
}

static bool string8_reserve(struct str8* string, size_t needed) {
  if (needed <= string->capacity)
    return true;

  size_t capacity = string->capacity;

  while (capacity < needed) {
    if (capacity > SIZE_MAX / 2) {
      capacity = needed;
      break;
    }

    capacity *= 2;
  }

  if (capacity > SIZE_MAX / sizeof(*string->data))
    return false;

  u8* data = realloc(string->data, capacity * sizeof(*data));

  if (data == NULL)
    return false;

  string->data = data;
  string->capacity = capacity;
  return true;
}

static bool string8_insert(struct str8* string, u8 val, size_t pos) {
  return string8_insert_bytes(string, &val, 1, pos);
}

static bool string8_insert_bytes(
  struct str8* string,
  const u8* data,
  size_t len,
  size_t pos
) {
  if (string->data == NULL || pos > string->size || (len > 0 && data == NULL))
    return false;

  if (len == 0)
    return true;

  if (len > SIZE_MAX - string->size - 1)
    return false;

  size_t new_size = string->size + len;

  if (!string8_reserve(string, new_size + 1))
    return false;

  memmove(
    string->data + pos + len,
    string->data + pos,
    string->size - pos + 1
  );

  memcpy(string->data + pos, data, len);
  string->size = new_size;

  return true;
}

static void string8_dest(struct str8* string) {
  free(string->data);
  *string = (struct str8){0};
}

static size_t utf8_next(const struct str8* string, size_t pos) {
  if (pos >= string->size)
    return string->size;

  u8 c = string->data[pos];
  size_t len;

  if (c < 0x80)
    len = 1;
  else if ((c & 0xE0) == 0xC0)
    len = 2;
  else if ((c & 0xF0) == 0xE0)
    len = 3;
  else if ((c & 0xF8) == 0xF0)
    len = 4;
  else
    len = 1;

  if (len > string->size - pos)
    return pos + 1;

  for (size_t i = 1; i < len; i++) {
    if ((string->data[pos + i] & 0xC0) != 0x80)
      return pos + 1;
  }

  return pos + len;
}

static size_t utf8_prev(const struct str8* string, size_t pos) {
  if (pos == 0)
    return 0;

  if (pos > string->size)
    pos = string->size;

  pos--;

  while (pos > 0 && (string->data[pos] & 0xC0) == 0x80)
    pos--;

  return pos;
}

static size_t utf8_count(
  const struct str8* string,
  size_t start,
  size_t end
) {
  size_t count = 0;

  if (start > string->size)
    start = string->size;
  if (end > string->size)
    end = string->size;

  while (start < end) {
    size_t next = utf8_next(string, start);

    if (next > end)
      break;

    start = next;
    count++;
  }

  return count;
}

static size_t utf8_advance(
  const struct str8* string,
  size_t pos,
  size_t count
) {
  while (count-- > 0 && pos < string->size)
    pos = utf8_next(string, pos);

  return pos;
}

static void editor_core_init(struct editor_core_T* core) {
  core->mode = MODE_NORMAL;
  core->count = 0;
  core->pending_delete = false;
  core->suppress_text_input = false;
  core->modified = false;
  core->cur_pos = 0;
  core->preferred_col = 0;
  core->preferred_col_valid = false;
  core->message[0] = '\0';

  string8_init(&core->buffer);
  string8_init(&core->command);
}

static void editor_core_dest(struct editor_core_T* core) {
  string8_dest(&core->command);
  string8_dest(&core->buffer);

  core->cur_pos = 0;
  core->preferred_col = 0;
  core->preferred_col_valid = false;
}

static size_t tex_line_start(const struct str8* buffer, size_t pos) {
  if (pos > buffer->size)
    pos = buffer->size;

  while (pos > 0 && buffer->data[pos - 1] != '\n')
    pos--;

  return pos;
}

static size_t tex_line_end(const struct str8* buffer, size_t pos) {
  if (pos > buffer->size)
    pos = buffer->size;

  while (pos < buffer->size && buffer->data[pos] != '\n')
    pos++;

  return pos;
}

static void tex_move_vertical(struct editor_core_T* core, int direction) {
  struct str8* buffer = &core->buffer;

  size_t start = tex_line_start(buffer, core->cur_pos);
  size_t column = utf8_count(buffer, start, core->cur_pos);

  if (!core->preferred_col_valid) {
    core->preferred_col = column;
    core->preferred_col_valid = true;
  }

  size_t target_start;
  size_t target_end;

  if (direction < 0) {
    if (start == 0)
      return;

    target_end = start - 1;
    target_start = tex_line_start(buffer, target_end);
  } else {
    size_t current_end = tex_line_end(buffer, core->cur_pos);

    if (current_end >= buffer->size)
      return;

    target_start = current_end + 1;
    target_end = tex_line_end(buffer, target_start);
  }

  size_t target_length = utf8_count(buffer, target_start, target_end);
  size_t target_column = core->preferred_col;

  if (target_column > target_length)
    target_column = target_length;

  core->cur_pos = utf8_advance(buffer, target_start, target_column);
}

static size_t tex_cursor_row(struct state_T* state) {
  size_t row = 0;

  for (size_t i = 0; i < state->core->cur_pos; i++) {
    if (state->core->buffer.data[i] == '\n')
      row++;
  }

  return row;
}

static size_t tex_visible_rows(struct state_T* state) {
  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int line_height = TTF_FontLineSkip(state->font);
  int available = height - 2 * padding_y - line_height;

  if (line_height <= 0 || available <= 0)
    return 1;

  size_t rows = (size_t)(available / line_height);
  return rows > 0 ? rows : 1;
}

static size_t tex_total_rows(const struct str8* buffer) {
  size_t rows = 1;

  for (size_t i = 0; i < buffer->size; i++) {
    if (buffer->data[i] == '\n')
      rows++;
  }

  return rows;
}

static int tex_cursor_x(struct state_T* state) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  size_t start = tex_line_start(buffer, core->cur_pos);
  size_t len = core->cur_pos - start;

  if (len == 0)
    return 0;

  char* prefix = malloc(len + 1);

  if (prefix == NULL) {
    fprintf(stderr, "tex: cannot allocate cursor prefix\n");
    exit(EXIT_FAILURE);
  }

  memcpy(prefix, buffer->data + start, len);
  prefix[len] = '\0';

  int width = 0;

  if (TTF_SizeUTF8(state->font, prefix, &width, NULL) < 0)
    width = 0;

  free(prefix);
  return width;
}

static void tex_scroll_horizontal(struct state_T* state) {
  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int available_width = width - 2 * padding_x;
  if (available_width <= 0)
    return;

  int cursor_x = tex_cursor_x(state);

  if ((size_t)cursor_x < state->col_offset) {
    state->col_offset = (size_t)cursor_x;
  } else if ((size_t)cursor_x >= state->col_offset + (size_t)available_width) {
    state->col_offset = (size_t)cursor_x - (size_t)available_width + 1;
  }
}

static void tex_scroll(struct state_T* state) {
  size_t cursor_row = tex_cursor_row(state);
  size_t visible_rows = tex_visible_rows(state);

  if (cursor_row < state->row_offset) {
    state->row_offset = cursor_row;
  } else if (cursor_row >= state->row_offset + visible_rows) {
    state->row_offset = cursor_row - visible_rows + 1;
  }
}

static void tex_cursor_draw(struct state_T* state) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  if (core->mode == MODE_COMMAND) {
    tex_command_cursor_draw(state);
    return;
  }

  size_t pos = core->cur_pos;
  size_t row = tex_cursor_row(state);
  size_t visible = tex_visible_rows(state);

  if (row < state->row_offset ||
      row - state->row_offset >= visible)
    return;

  int x = padding_x + tex_cursor_x(state) - (int)state->col_offset;
  int y = padding_y +
    (int)(row - state->row_offset) * TTF_FontLineSkip(state->font);

  int window_w, window_h;
  SDL_GetRendererOutputSize(state->renderer, &window_w, &window_h);

  if (x < padding_x || x >= window_w - padding_x ||
      y >= window_h - padding_y)
    return;

  int h = TTF_FontLineSkip(state->font);
  if (h <= 0)
    h = TTF_FontHeight(state->font);

  SDL_SetRenderDrawColor(state->renderer, 196, 167, 231, 255);

  if (core->mode == MODE_INSERT) {
    SDL_RenderDrawLine(state->renderer, x, y, x, y + h - 1);
    SDL_RenderDrawLine(state->renderer, x + 1, y, x + 1, y + h - 1);
    return;
  }

  size_t line_end = tex_line_end(buffer, pos);
  size_t char_end = pos < line_end ? utf8_next(buffer, pos) : pos;

  char* character = NULL;
  int w = 0;

  if (char_end > pos) {
    size_t len = char_end - pos;
    character = malloc(len + 1);

    if (character == NULL) {
      fprintf(stderr, "tex: cannot allocate cursor character\n");
      exit(EXIT_FAILURE);
    }

    memcpy(character, buffer->data + pos, len);
    character[len] = '\0';

    if (TTF_SizeUTF8(state->font, character, &w, NULL) < 0)
      w = 0;
  } else if (TTF_SizeUTF8(state->font, " ", &w, NULL) < 0) {
    w = font_size / 2;
  }

  if (w < 1)
    w = font_size / 2;

  if (x + w > window_w - padding_x)
    w = window_w - padding_x - x;

  if (w <= 0) {
    free(character);
    return;
  }

  SDL_Rect block = {
    .x = x,
    .y = y,
    .w = w,
    .h = h
  };

  SDL_RenderFillRect(state->renderer, &block);

  if (character != NULL) {
    SDL_Color inverse = { 29, 32, 33, 255 };

    SDL_Surface* surface =
      TTF_RenderUTF8_Blended(state->font, character, inverse);

    if (surface != NULL) {
      SDL_Texture* texture =
        SDL_CreateTextureFromSurface(state->renderer, surface);

      if (texture != NULL) {
        SDL_Rect dest = {
          .x = x,
          .y = y,
          .w = surface->w,
          .h = surface->h
        };

        SDL_RenderCopy(state->renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
      }

      SDL_FreeSurface(surface);
    }
  }

  free(character);
}

static void tex_window_init(struct state_T* state) {
  state->filename = _parse_cmd_args(state->argc, state->argv);
  state->has_file = state->filename != NULL;
  tex_file_draw(state);

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "tex: cannot initialize SDL: %s\n", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  state->window = SDL_CreateWindow(
    "Tex",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    640,
    480,
    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
  );

  if (state->window == NULL) {
    fprintf(stderr, "tex: cannot create window: %s\n", SDL_GetError());
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  if (SDL_SetWindowOpacity(state->window, opacity) < 0)
    fprintf(stderr, "tex: cannot set window opacity: %s\n", SDL_GetError());

  if (TTF_Init() < 0) {
    fprintf(stderr, "tex: cannot initialize SDL_ttf: %s\n", TTF_GetError());
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->font = TTF_OpenFont(
    "/home/user/dev/tex/IosevkaNerdFont-Regular.ttf",
    font_size
  );

  if (state->font == NULL) {
    fprintf(stderr, "tex: cannot load font: %s\n", TTF_GetError());
    TTF_Quit();
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->renderer = SDL_CreateRenderer(
    state->window,
    -1,
    SDL_RENDERER_ACCELERATED
  );

  if (state->renderer == NULL) {
    fprintf(stderr, "tex: cannot create renderer: %s\n", SDL_GetError());
    TTF_CloseFont(state->font);
    TTF_Quit();
    SDL_DestroyWindow(state->window);
    SDL_Quit();
    exit(EXIT_FAILURE);
  }

  state->running = true;
}

static bool tex_is_space(const struct str8* buffer, size_t pos) {
  return pos >= buffer->size || buffer->data[pos] == '\n' ||
    buffer->data[pos] == '\t' || buffer->data[pos] == ' ' ||
    buffer->data[pos] == '\r';
}

static bool tex_is_word(const struct str8* buffer, size_t pos) {
  if (pos >= buffer->size)
    return false;
  u8 c = buffer->data[pos];
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
    (c >= '0' && c <= '9') || c == '_';
}

static size_t tex_normal_limit(const struct str8* buffer, size_t pos) {
  size_t end = tex_line_end(buffer, pos);
  if (end > tex_line_start(buffer, pos) && pos >= end)
    return utf8_prev(buffer, end);
  return end;
}

static void tex_normal_clamp(struct editor_core_T* core) {
  struct str8* buffer = &core->buffer;
  if (core->cur_pos > buffer->size)
    core->cur_pos = buffer->size;
  size_t start = tex_line_start(buffer, core->cur_pos);
  size_t end = tex_line_end(buffer, core->cur_pos);
  if (core->cur_pos >= end && end > start)
    core->cur_pos = utf8_prev(buffer, end);
  else if (core->cur_pos > end)
    core->cur_pos = end;
  core->preferred_col_valid = false;
}

static void tex_move_word_forward(struct editor_core_T* core) {
  struct str8* b = &core->buffer;
  if (core->cur_pos >= b->size) return;
  bool was_word = tex_is_word(b, core->cur_pos);
  bool was_space = tex_is_space(b, core->cur_pos);
  size_t p = core->cur_pos;
  if (was_space) {
    while (p < b->size && tex_is_space(b, p)) p = utf8_next(b, p);
  } else {
    while (p < b->size && !tex_is_space(b, p) && tex_is_word(b, p) == was_word)
      p = utf8_next(b, p);
    while (p < b->size && tex_is_space(b, p)) p = utf8_next(b, p);
  }
  core->cur_pos = p;
  tex_normal_clamp(core);
}

static void tex_move_word_backward(struct editor_core_T* core) {
  struct str8* b = &core->buffer;
  size_t p = core->cur_pos;
  if (p == 0) return;
  p = utf8_prev(b, p);
  while (p > 0 && tex_is_space(b, p)) p = utf8_prev(b, p);
  bool word = tex_is_word(b, p);
  while (p > 0) {
    size_t prev = utf8_prev(b, p);
    if (tex_is_space(b, prev) || tex_is_word(b, prev) != word) break;
    p = prev;
  }
  core->cur_pos = p;
  tex_normal_clamp(core);
}

static void tex_delete_range(struct editor_core_T* core, size_t start, size_t end) {
  struct str8* b = &core->buffer;
  if (start > end) { size_t t = start; start = end; end = t; }
  if (end > b->size) end = b->size;
  if (start > b->size) start = b->size;
  if (end > start) {
    memmove(b->data + start, b->data + end, b->size - end + 1);
    b->size -= end - start;
    core->modified = true;
  }
  core->cur_pos = start;
  tex_normal_clamp(core);
}

static void tex_open_line(struct editor_core_T* core, bool below) {
  struct str8* b = &core->buffer;
  size_t start = tex_line_start(b, core->cur_pos);
  size_t end = tex_line_end(b, core->cur_pos);
  size_t pos = below ? end : start;
  if (!string8_insert(b, '\n', pos)) return;
  core->modified = true;
  core->cur_pos = below ? pos + 1 : start;
  core->mode = MODE_INSERT;
  core->preferred_col_valid = false;
}

static char* tex_command_line(const struct editor_core_T* core) {
  size_t len = core->command.size;
  char* line = malloc(len + 2);

  if (line == NULL) {
    fprintf(stderr, "tex: cannot allocate command line\n");
    exit(EXIT_FAILURE);
  }

  line[0] = ':';
  memcpy(line + 1, core->command.data, len);
  line[len + 1] = '\0';

  return line;
}

static void tex_text_draw(
  struct state_T* state,
  const char* text,
  int x,
  int y,
  SDL_Color color
) {
  SDL_Surface* surface = TTF_RenderUTF8_Blended(state->font, text, color);

  if (surface == NULL)
    return;

  SDL_Texture* texture = SDL_CreateTextureFromSurface(state->renderer, surface);

  if (texture != NULL) {
    SDL_Rect dest = {
      .x = x,
      .y = y,
      .w = surface->w,
      .h = surface->h
    };

    SDL_RenderCopy(state->renderer, texture, NULL, &dest);
    SDL_DestroyTexture(texture);
  }

  SDL_FreeSurface(surface);
}

static void tex_status_draw(struct state_T* state) {
  struct editor_core_T* core = state->core;
  SDL_Color fg = { COLOR(colors[FG]), 255 };

  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int y = height - padding_y - TTF_FontLineSkip(state->font);
  char* line = NULL;
  const char* text = core->message;

  if (core->mode == MODE_COMMAND) {
    line = tex_command_line(core);
    text = line;
  } else if (core->message[0] == '\0' && core->mode == MODE_INSERT) {
    text = "-- INSERT --";
  }

  if (text[0] != '\0')
    tex_text_draw(state, text, padding_x, y, fg);

  free(line);
}

static void tex_command_cursor_draw(struct state_T* state) {
  char* line = tex_command_line(state->core);
  int w = 0;

  if (TTF_SizeUTF8(state->font, line, &w, NULL) < 0)
    w = 0;

  free(line);

  int width, height;
  SDL_GetRendererOutputSize(state->renderer, &width, &height);

  int h = TTF_FontLineSkip(state->font);
  int x = padding_x + w;
  int y = height - padding_y - h;

  if (x >= width - padding_x)
    return;

  SDL_SetRenderDrawColor(state->renderer, 196, 167, 231, 255);
  SDL_RenderDrawLine(state->renderer, x, y, x, y + h - 1);
  SDL_RenderDrawLine(state->renderer, x + 1, y, x + 1, y + h - 1);
}

static bool tex_command_write(struct state_T* state, const char* arg) {
  struct editor_core_T* core = state->core;
  const char* target = arg[0] != '\0' ? arg : state->filename;

  if (target == NULL) {
    snprintf(core->message, sizeof(core->message), "E32: No file name");
    return false;
  }

  if (!tex_file_save(state, target))
    return false;

  if (!state->has_file) {
    snprintf(state->save_name, sizeof(state->save_name), "%s", target);
    state->filename = state->save_name;
    state->has_file = true;
  }

  if (!strcmp(target, state->filename))
    core->modified = false;

  return true;
}

static void tex_command_execute(struct state_T* state) {
  struct editor_core_T* core = state->core;
  char* cmd = (char*)core->command.data;

  while (*cmd == ' ' || *cmd == '\t' || *cmd == ':')
    cmd++;

  char* arg = cmd;
  while (*arg != '\0' && *arg != ' ' && *arg != '\t')
    arg++;

  if (*arg != '\0') {
    *arg++ = '\0';
    while (*arg == ' ' || *arg == '\t')
      arg++;
  }

  if (cmd[0] == '\0')
    return;

  char* end = NULL;
  unsigned long line = strtoul(cmd, &end, 10);

  if (end != cmd && *end == '\0') {
    size_t pos = 0;

    for (unsigned long i = 1; i < line; i++) {
      size_t line_end = tex_line_end(&core->buffer, pos);
      if (line_end >= core->buffer.size)
        break;
      pos = line_end + 1;
    }

    core->cur_pos = pos;
  } else if (!strcmp(cmd, "w") || !strcmp(cmd, "w!") || !strcmp(cmd, "write")) {
    tex_command_write(state, arg);
  } else if (!strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
    if (tex_command_write(state, arg))
      state->running = false;
  } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
    if (core->modified)
      snprintf(core->message, sizeof(core->message),
        "E37: No write since last change (add ! to override)");
    else
      state->running = false;
  } else if (!strcmp(cmd, "q!")) {
    state->running = false;
  } else {
    snprintf(core->message, sizeof(core->message),
      "E492: Not an editor command: %s", cmd);
  }
}

static void tex_command_input(struct state_T* state, SDL_Keycode key) {
  struct editor_core_T* core = state->core;
  struct str8* command = &core->command;

  switch (key) {
    case SDLK_ESCAPE:
      core->mode = MODE_NORMAL;
      command->size = 0;
      command->data[0] = '\0';
      break;
    case SDLK_BACKSPACE:
      if (command->size == 0) {
        core->mode = MODE_NORMAL;
      } else {
        command->size = utf8_prev(command, command->size);
        command->data[command->size] = '\0';
      }
      break;
    case SDLK_RETURN: case SDLK_KP_ENTER:
      core->mode = MODE_NORMAL;
      tex_command_execute(state);
      command->size = 0;
      command->data[0] = '\0';
      break;
    default: break;
  }
}

static void tex_input_handle(struct state_T* state, const SDL_Event* event) {
  struct editor_core_T* core = state->core;
  struct str8* buffer = &core->buffer;

  if (event->type == SDL_TEXTINPUT) {
    if (core->suppress_text_input) { core->suppress_text_input = false; return; }
    if (core->mode == MODE_COMMAND) {
      const char* text = event->text.text;
      if (!string8_insert_bytes(&core->command, (const u8*)text, strlen(text), core->command.size))
        fprintf(stderr, "tex: failed to insert command text\n");
      return;
    }
    if (core->mode != MODE_INSERT || core->pending_delete || core->count)
      return;
    const char* text = event->text.text;
    size_t len = strlen(text);
    if (string8_insert_bytes(buffer, (const u8*)text, len, core->cur_pos)) {
      core->cur_pos += len;
      core->modified = true;
    } else {
      fprintf(stderr, "tex: failed to insert text\n");
    }
    core->preferred_col_valid = false;
    tex_scroll(state);
    tex_scroll_horizontal(state);
    return;
  }

  if (event->type != SDL_KEYDOWN)
    return;

  SDL_Keycode key = event->key.keysym.sym;

  if (core->mode != MODE_COMMAND)
    core->message[0] = '\0';

  if (core->mode == MODE_INSERT) {
    switch (key) {
      case SDLK_ESCAPE:
        core->mode = MODE_NORMAL;
        if (core->cur_pos > 0) core->cur_pos = utf8_prev(buffer, core->cur_pos);
        tex_normal_clamp(core);
        break;
      case SDLK_BACKSPACE:
        if (core->cur_pos > 0) {
          size_t prev = utf8_prev(buffer, core->cur_pos);
          tex_delete_range(core, prev, core->cur_pos);
          core->mode = MODE_INSERT;
          core->cur_pos = prev;
        }
        break;
      case SDLK_DELETE:
        if (core->cur_pos < buffer->size) {
          size_t next = utf8_next(buffer, core->cur_pos);
          tex_delete_range(core, core->cur_pos, next);
          core->mode = MODE_INSERT;
        }
        break;
      case SDLK_LEFT: core->cur_pos = utf8_prev(buffer, core->cur_pos); break;
      case SDLK_RIGHT: core->cur_pos = utf8_next(buffer, core->cur_pos); break;
      case SDLK_UP: tex_move_vertical(core, -1); break;
      case SDLK_DOWN: tex_move_vertical(core, 1); break;
      case SDLK_HOME: core->cur_pos = tex_line_start(buffer, core->cur_pos); break;
      case SDLK_END: core->cur_pos = tex_line_end(buffer, core->cur_pos); break;
      case SDLK_RETURN: case SDLK_KP_ENTER:
        if (string8_insert(buffer, '\n', core->cur_pos)) {
          core->cur_pos++;
          core->modified = true;
        }
        break;
      case SDLK_TAB:
        for (int i = 0; i < tabstop; i++) {
          if (!string8_insert(buffer, ' ', core->cur_pos)) break;
          core->cur_pos++;
          core->modified = true;
        }
        break;
      default: break;
    }
  } else if (core->mode == MODE_COMMAND) {
    tex_command_input(state, key);
  } else {
    if (key >= SDLK_0 && key <= SDLK_9 && !(event->key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT))) {
      int digit = (int)(key - SDLK_0);
      if (digit == 0 && core->count == 0 && !core->pending_delete) {
        core->cur_pos = tex_line_start(buffer, core->cur_pos);
      } else if (digit > 0 || core->count > 0) {
        if (core->count < 100000) core->count = core->count * 10 + (size_t)digit;
      }
    } else {
      size_t count = core->count ? core->count : 1;
      core->count = 0;
      if (core->pending_delete) {
        core->pending_delete = false;
        size_t start = core->cur_pos, end = start;
        if (key == SDLK_d) {
          start = tex_line_start(buffer, core->cur_pos);
          end = start;
          for (size_t i = 0; i < count && end < buffer->size; i++) {
            end = tex_line_end(buffer, end);
            if (end < buffer->size) end++;
          }
          if (end == start) end = tex_line_end(buffer, start);
        } else if (key == SDLK_w) {
          for (size_t i = 0; i < count; i++) tex_move_word_forward(core);
          end = core->cur_pos;
          core->cur_pos = start;
        } else if (key == SDLK_4 && (event->key.keysym.mod & KMOD_SHIFT)) {
          end = tex_line_end(buffer, core->cur_pos);
        } else if (key == SDLK_x) {
          end = utf8_advance(buffer, start, count);
        } else {
          goto input_done;
        }
        tex_delete_range(core, start, end);
      } else {
        switch (key) {
          case SDLK_ESCAPE: core->count = 0; break;
          case SDLK_h: case SDLK_LEFT:
            while (count-- && core->cur_pos > tex_line_start(buffer, core->cur_pos))
              core->cur_pos = utf8_prev(buffer, core->cur_pos);
            break;
          case SDLK_l: case SDLK_RIGHT:
            while (count--) {
              size_t end = tex_line_end(buffer, core->cur_pos);
              if (core->cur_pos >= end || utf8_next(buffer, core->cur_pos) >= end) break;
              core->cur_pos = utf8_next(buffer, core->cur_pos);
            }
            break;
          case SDLK_j: case SDLK_DOWN:
            while (count--) tex_move_vertical(core, 1);
            break;
          case SDLK_k: case SDLK_UP:
            while (count--) tex_move_vertical(core, -1);
            break;
          case SDLK_w: while (count--) tex_move_word_forward(core); break;
          case SDLK_b: while (count--) tex_move_word_backward(core); break;
          case SDLK_e:
            while (count--) {
              size_t p = core->cur_pos;
              if (p < buffer->size && tex_is_space(buffer, p)) {
                while (p < buffer->size && tex_is_space(buffer, p)) p = utf8_next(buffer, p);
              } else if (p < buffer->size) {
                bool word = tex_is_word(buffer, p);
                do { size_t n = utf8_next(buffer, p); if (n >= buffer->size || tex_is_space(buffer, n) || tex_is_word(buffer, n) != word) break; p = n; } while (p < buffer->size);
              }
              core->cur_pos = p;
            }
            break;
          case SDLK_0: core->cur_pos = tex_line_start(buffer, core->cur_pos); break;
          case SDLK_6: if (event->key.keysym.mod & KMOD_SHIFT) {
            size_t p = tex_line_start(buffer, core->cur_pos);
            size_t end = tex_line_end(buffer, p);
            while (p < end && (buffer->data[p] == ' ' || buffer->data[p] == '\t')) p++;
            core->cur_pos = p < end ? p : tex_normal_limit(buffer, p);
          } break;
          case SDLK_4: if (event->key.keysym.mod & KMOD_SHIFT) core->cur_pos = tex_normal_limit(buffer, core->cur_pos); break;
          case SDLK_COLON: case SDLK_SEMICOLON:
            if (key == SDLK_COLON || (event->key.keysym.mod & KMOD_SHIFT)) {
              core->mode = MODE_COMMAND;
              core->command.size = 0;
              core->command.data[0] = '\0';
              core->suppress_text_input = true;
            }
            break;
          case SDLK_i:
            if (event->key.keysym.mod & KMOD_SHIFT) {
              core->cur_pos = tex_line_start(buffer, core->cur_pos);
              while (core->cur_pos < tex_line_end(buffer, core->cur_pos) &&
                     (buffer->data[core->cur_pos] == ' ' || buffer->data[core->cur_pos] == '\t'))
                core->cur_pos++;
            }
            core->mode = MODE_INSERT;
            core->suppress_text_input = true;
            break;
          case SDLK_a:
            if (event->key.keysym.mod & KMOD_SHIFT) {
              core->cur_pos = tex_line_end(buffer, core->cur_pos);
            } else if (core->cur_pos < buffer->size && buffer->data[core->cur_pos] != '\n') {
              core->cur_pos = utf8_next(buffer, core->cur_pos);
            }
            core->mode = MODE_INSERT;
            core->suppress_text_input = true;
            break;
          case SDLK_o: tex_open_line(core, !(event->key.keysym.mod & KMOD_SHIFT)); core->suppress_text_input = true; break;
          case SDLK_x:
            if (event->key.keysym.mod & KMOD_SHIFT) {
              size_t p = core->cur_pos;
              size_t line_start = tex_line_start(buffer, p);
              while (count-- && p > line_start) p = utf8_prev(buffer, p);
              tex_delete_range(core, p, core->cur_pos);
            } else {
              size_t end = utf8_advance(buffer, core->cur_pos, count);
              size_t line_end = tex_line_end(buffer, core->cur_pos);
              if (end > line_end) end = line_end;
              tex_delete_range(core, core->cur_pos, end);
            }
            break;
          case SDLK_d: core->pending_delete = true; core->count = count; break;
          case SDLK_u: break;
          default: break;
        }
      }
    }
  }

input_done:
  if (core->mode == MODE_NORMAL) tex_normal_clamp(core);
  core->preferred_col_valid = false;
  tex_scroll(state);
  tex_scroll_horizontal(state);
}

static void tex_render(struct state_T* state) {
  struct str8* buffer = &state->core->buffer;

  SDL_Color fg = { COLOR(colors[FG]), 255 };

  int line_y = padding_y;
  int line_height = TTF_FontLineSkip(state->font);
  size_t start = 0;
  size_t row = 0;
  size_t visible_rows = tex_visible_rows(state);

  SDL_SetRenderDrawColor(
    state->renderer, COLOR(colors[BG]), 255
  );
  SDL_RenderClear(state->renderer);

  while (start < buffer->size && row < state->row_offset) {
    if (buffer->data[start++] == '\n')
      row++;
  }

  size_t end = start;

  while (row < state->row_offset + visible_rows && end <= buffer->size) {
    if (end < buffer->size && buffer->data[end] != '\n') {
      end++;
      continue;
    }

    size_t len = end - start;

    if (len > 0) {
      char* line = malloc(len + 1);

      if (line == NULL) {
        fprintf(stderr, "tex: cannot allocate line buffer\n");
        exit(EXIT_FAILURE);
      }

      memcpy(line, buffer->data + start, len);
      line[len] = '\0';

      SDL_Surface* surface =
        TTF_RenderUTF8_Blended(state->font, line, fg);

      free(line);

      if (surface != NULL) {
        SDL_Texture* texture =
          SDL_CreateTextureFromSurface(state->renderer, surface);

        if (texture != NULL) {
          SDL_Rect dest = {
            .x = padding_x - (int)state->col_offset,
            .y = line_y,
            .w = surface->w,
            .h = surface->h
          };

          SDL_RenderCopy(state->renderer, texture, NULL, &dest);
          SDL_DestroyTexture(texture);
        }

        SDL_FreeSurface(surface);
      }
    }

    line_y += line_height;
    row++;

    if (end == buffer->size)
      break;

    start = end + 1;
    end = start;
  }

  tex_status_draw(state);
  tex_cursor_draw(state);
  SDL_RenderPresent(state->renderer);
}

static void tex_window_loop(struct state_T* state) {
  SDL_Event event;
  SDL_StartTextInput();

  while (state->running) {
    if (SDL_WaitEventTimeout(&event, 16)) {
      do {
        switch (event.type) {
          case SDL_QUIT: {
            state->running = false;
          } break;

          case SDL_KEYDOWN:
          case SDL_TEXTINPUT: {
            tex_input_handle(state, &event);
          } break;

          case SDL_MOUSEWHEEL: {
            int direction = event.wheel.y;

            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
              direction = -direction;

            size_t visible_rows = tex_visible_rows(state);
            size_t total_rows = tex_total_rows(&state->core->buffer);
            size_t max_offset = total_rows > visible_rows
              ? total_rows - visible_rows
              : 0;

            if (state->row_offset > max_offset)
              state->row_offset = max_offset;

            size_t amount =
              (size_t)(direction < 0 ? -direction : direction) * 3;

            if (direction > 0) {
              state->row_offset = amount > state->row_offset
                ? 0
                : state->row_offset - amount;
            } else if (direction < 0) {
              state->row_offset = amount > max_offset - state->row_offset
                ? max_offset
                : state->row_offset + amount;
            }
          } break;

          default:
            break;
        }
      } while (SDL_PollEvent(&event));
    }

    tex_render(state);
  }

  SDL_StopTextInput();
}

static void tex_window_dest(struct state_T* state) {
  if (state->renderer != NULL)
    SDL_DestroyRenderer(state->renderer);

  if (state->window != NULL)
    SDL_DestroyWindow(state->window);

  if (state->font != NULL)
    TTF_CloseFont(state->font);

  TTF_Quit();
  SDL_Quit();
}

int main(int argc, const char** argv) {
  struct editor_core_T core;
  editor_core_init(&core);

  struct state_T state = {
    .argc = argc,
    .argv = argv,
    .running = false,
    .row_offset = 0,
    .col_offset = 0,
    .window = NULL,
    .renderer = NULL,
    .font = NULL,
    .core = &core,
  };

  tex_window_init(&state);
  tex_window_loop(&state);
  tex_window_dest(&state);
  editor_core_dest(&core);

  return EXIT_SUCCESS;
}

